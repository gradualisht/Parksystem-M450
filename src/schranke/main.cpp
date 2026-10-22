#include <Arduino.h>
#include <HTTPClient.h>
#include <Servo.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include "ParkLogic.h"
#include "ParkProtocol.h"
#include "config.h"
#include "pages.h"

// Schranken-Pico (Pico 2): steuert die Einfahrts- und die Ausfahrtsschranke (zwei SG90),
// simuliert die Kennzeichenerkennung und fragt die Kasse per HTTP ab.
//   Einfahrt (US1): Kennzeichen -> Kasse registriert -> Schranke oeffnet innerhalb von 3 s
//   Anzeige  (US2): freie Plaetze, "Besetzt" bei Vollbelegung (Schranke bleibt zu)
//   Ausfahrt (US6): Kasse prueft Zahlstatus -> Schranke oeffnet nur bei gueltiger Zahlung
//   Ruftaste (US7): kein Kennzeichen erkannt -> Anfrage an die Kasse, Personal gibt frei
// Bedienung: Weboberflaeche http://192.168.4.100/ oder serielle Kommandos (USB, 115200):
//   enter <id> | exit <id> | call in|out | servo in|out open|close | status

static WebServer server(80);
static Servo entryServo;
static Servo exitServo;

struct Barrier {
  const char *name;
  Servo *servo;
  uint8_t pin;
  bool isOpen;
  uint32_t closeAtMs;
  std::string text;  // Anzeigetext am Tor
};

static Barrier entryGate = {"einfahrt", &entryServo, PIN_SERVO_EINFAHRT, false, 0, "Bereit."};
static Barrier exitGate = {"ausfahrt", &exitServo, PIN_SERVO_AUSFAHRT, false, 0, "Bereit."};

struct Outcome {
  std::string result;   // z. B. OK, FULL, UNPAID, NOT_RECOGNIZED, KASSE_OFFLINE
  std::string message;  // Anzeigetext fuer den Gast
  bool opened;
  uint32_t latencyMs;   // Zeit von der Erkennung bis zum Servo-Befehl (NFR1)
};

static int lastFree = -1;
static int lastCapacity = 0;
static bool kasseOnline = false;
static int32_t lastLatencyMs = -1;
static uint32_t lastFreePollMs = 0;
static uint32_t lastWifiTryMs = 0;
static bool wifiWasUp = false;

static bool callActive = false;
static uint32_t callNo = 0;
static bool callOutbound = false;
static uint32_t callStartMs = 0;
static uint32_t lastCallPollMs = 0;

static std::vector<std::string> eventLog;
static char inputBuffer[64];
static uint8_t inputLength = 0;

static std::string num(uint32_t value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", (unsigned)value);
  return buf;
}

static std::string quoted(const std::string &text) { return "\"" + jsonEscape(text) + "\""; }

static void logEvent(const char *fmt, ...) {
  char msg[200];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  std::string line = "t=" + num(millis() / 1000) + "s " + msg;
  Serial.println(line.c_str());
  eventLog.push_back(line);
  if (eventLog.size() > 20) {
    eventLog.erase(eventLog.begin());
  }
}

// ---- Schranken (US1, US6) ---------------------------------------------------

static void barrierOpen(Barrier &b) {
  b.servo->write(SERVO_ANGLE_OPEN);
  b.isOpen = true;
  b.closeAtMs = millis() + BARRIER_OPEN_MS;
}

static void barrierClose(Barrier &b) {
  b.servo->write(SERVO_ANGLE_CLOSED);
  b.isOpen = false;
}

// Schliesst die Schranke nach BARRIER_OPEN_MS; Vergleich ist bei millis()-Ueberlauf korrekt.
static void barrierUpdate(Barrier &b) {
  if (b.isOpen && (int32_t)(millis() - b.closeAtMs) >= 0) {
    barrierClose(b);
    logEvent("BARRIER gate=%s state=closed", b.name);
  }
}

// ---- Kasse abfragen --------------------------------------------------------

// Liefert den HTTP-Code, bei einem Verbindungsfehler einen Wert <= 0.
static int kasseRequest(bool post, const std::string &path, std::string &out) {
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(2000);
  if (!http.begin(String(KASSE_BASE_URL) + path.c_str())) {
    kasseOnline = false;
    return -1;
  }
  int code = post ? http.POST("") : http.GET();
  if (code > 0) {
    out = http.getString().c_str();
  }
  http.end();
  kasseOnline = code > 0;
  return code;
}

static void updateFree(const std::string &body) {
  long freeSpots = jsonGetInt(body, "free", -1);
  if (freeSpots >= 0) {
    lastFree = (int)freeSpots;
    lastCapacity = (int)jsonGetInt(body, "capacity", lastCapacity);
  }
}

static void pollFree() {
  std::string body;
  if (kasseRequest(false, "/api/free", body) == 200) {
    updateFree(body);
  } else {
    lastFree = -1;
  }
}

// ---- Vorgaenge (US1, US6, US7) ---------------------------------------------

static const char MSG_NOT_RECOGNIZED[] = "Kennzeichen nicht erkannt. Bitte die Ruftaste drücken.";
static const char MSG_KASSE_OFFLINE[] = "Kasse nicht erreichbar. Schranke bleibt geschlossen.";

static Outcome finish(Outcome o, uint32_t startMs) {
  o.latencyMs = millis() - startMs;
  return o;
}

static Outcome processEntry(const std::string &raw) {
  uint32_t t0 = millis();
  Outcome o = {"", "", false, 0};
  std::string id = normalizeId(raw);
  if (id.empty()) {
    o.result = "NOT_RECOGNIZED";
    o.message = MSG_NOT_RECOGNIZED;
    return finish(o, t0);
  }
  std::string body;
  if (kasseRequest(true, "/api/entry?id=" + urlEncode(id), body) != 200) {
    o.result = "KASSE_OFFLINE";
    o.message = MSG_KASSE_OFFLINE;
    return finish(o, t0);
  }
  updateFree(body);
  o.result = jsonGetString(body, "result", "ERROR");
  if (o.result == "OK") {
    barrierOpen(entryGate);  // sofort oeffnen, danach erst die Zeit stoppen (NFR1)
    o.opened = true;
    o.message = "Willkommen, die Schranke öffnet. Freie Plätze: " + num((uint32_t)lastFree) + ".";
  } else if (o.result == "FULL") {
    o.message = "Besetzt. Die Einfahrt ist gesperrt.";
  } else if (o.result == "DUPLICATE") {
    o.message = "Dieses Fahrzeug ist bereits im Parkhaus.";
  } else {
    o.message = MSG_NOT_RECOGNIZED;  // ungueltiges Kennzeichen: wie nicht erkannt behandeln
  }
  return finish(o, t0);
}

static Outcome processExit(const std::string &raw) {
  uint32_t t0 = millis();
  Outcome o = {"", "", false, 0};
  std::string id = normalizeId(raw);
  if (id.empty()) {
    o.result = "NOT_RECOGNIZED";
    o.message = MSG_NOT_RECOGNIZED;
    return finish(o, t0);
  }
  std::string body;
  if (kasseRequest(true, "/api/exit?id=" + urlEncode(id), body) != 200) {
    o.result = "KASSE_OFFLINE";
    o.message = MSG_KASSE_OFFLINE;
    return finish(o, t0);
  }
  updateFree(body);
  o.result = jsonGetString(body, "result", "ERROR");
  std::string due = jsonGetString(body, "due", "0.00");
  if (o.result == "OPEN") {
    barrierOpen(exitGate);
    o.opened = true;
    o.message = "Gute Fahrt, die Schranke öffnet.";
  } else if (o.result == "UNPAID") {
    o.message = "Bitte zuerst an der Kasse bezahlen. Offener Betrag: CHF " + due + ".";
  } else if (o.result == "EXPIRED") {
    o.message = "Die Zahlung ist älter als 15 Minuten. Bitte an der Kasse bezahlen. Offener Betrag: CHF " + due + ".";
  } else {
    o.message = "Fahrzeug nicht registriert. Bitte die Ruftaste drücken.";
  }
  return finish(o, t0);
}

// Zeigt das Ergebnis am Tor an und protokolliert es (Latenz fuer NFR1).
static void report(Barrier &gate, const std::string &id, const Outcome &o) {
  gate.text = o.message;
  if (o.opened) {
    lastLatencyMs = (int32_t)o.latencyMs;
  }
  logEvent("RESULT gate=%s id=%s result=%s opened=%d latency_ms=%u free=%d%s", gate.name, id.c_str(),
           o.result.c_str(), o.opened ? 1 : 0, (unsigned)o.latencyMs, lastFree,
           o.opened && o.latencyMs > MAX_REACTION_MS ? " SLOW" : "");
}

static void doEntry(const std::string &raw) { report(entryGate, normalizeId(raw), processEntry(raw)); }

static void doExit(const std::string &raw) { report(exitGate, normalizeId(raw), processExit(raw)); }

static void doCall(bool outbound) {
  Barrier &gate = outbound ? exitGate : entryGate;
  if (callActive) {
    gate.text = "Es läuft bereits eine Anfrage an die Servicezentrale. Bitte warten.";
    return;
  }
  std::string body;
  if (kasseRequest(true, std::string("/api/call?dir=") + (outbound ? "out" : "in"), body) != 200) {
    gate.text = MSG_KASSE_OFFLINE;
    logEvent("CALL gate=%s result=KASSE_OFFLINE", gate.name);
    return;
  }
  callNo = (uint32_t)jsonGetInt(body, "call", 0);
  callOutbound = outbound;
  callActive = true;
  callStartMs = millis();
  lastCallPollMs = callStartMs;
  gate.text = "Die Servicezentrale wurde gerufen. Bitte warten.";
  logEvent("CALL gate=%s no=%u state=PENDING", gate.name, (unsigned)callNo);
}

// Fragt bei offener Ruftaste-Anfrage den Stand ab (US7).
static void pollCall() {
  if (!callActive || millis() - lastCallPollMs < 1000) {
    return;
  }
  lastCallPollMs = millis();
  Barrier &gate = callOutbound ? exitGate : entryGate;
  std::string body;
  if (kasseRequest(false, "/api/callstatus?call=" + num(callNo), body) != 200) {
    return;
  }
  std::string state = jsonGetString(body, "state", "pending");
  uint32_t waitedMs = millis() - callStartMs;
  if (state == "approved") {
    callActive = false;
    barrierOpen(gate);
    std::string ticket = jsonGetString(body, "ticket", "");
    gate.text = callOutbound ? "Freigabe durch die Servicezentrale, die Schranke öffnet."
                             : "Ticket " + ticket + " ausgegeben, die Schranke öffnet.";
    logEvent("CALL gate=%s no=%u state=APPROVED ticket=%s waited_ms=%u", gate.name, (unsigned)callNo,
             ticket.c_str(), (unsigned)waitedMs);
  } else if (state == "rejected") {
    callActive = false;
    gate.text = "Die Servicezentrale hat die Anfrage abgelehnt.";
    logEvent("CALL gate=%s no=%u state=REJECTED waited_ms=%u", gate.name, (unsigned)callNo, (unsigned)waitedMs);
  } else if (waitedMs > SERVICE_CALL_TARGET_MS) {
    gate.text = "Die Servicezentrale antwortet noch nicht. Bitte weiter warten.";
  }
}

// ---- Weboberflaeche --------------------------------------------------------

static void sendJson(int code, const std::string &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", String(body.c_str()));
}

static std::string gateJson(const Barrier &b) {
  return std::string("{\"open\":") + (b.isOpen ? "true" : "false") + ",\"text\":" + quoted(b.text) + "}";
}

static void handleStatus() {
  std::string json = std::string("{\"online\":") + (kasseOnline ? "true" : "false") + ",\"free\":" +
                     std::to_string(lastFree) + ",\"capacity\":" + std::to_string(lastCapacity) +
                     ",\"entry\":" + gateJson(entryGate) + ",\"exit\":" + gateJson(exitGate) +
                     ",\"latencyMs\":" + std::to_string(lastLatencyMs) + ",\"log\":[";
  for (size_t i = 0; i < eventLog.size(); i++) {
    json += std::string(i ? "," : "") + quoted(eventLog[eventLog.size() - 1 - i]);
  }
  json += "]}";
  sendJson(200, json);
}

static void handleEnter() {
  doEntry(server.arg("id").c_str());
  sendJson(200, "{\"result\":\"DONE\"}");
}

static void handleExitRequest() {
  doExit(server.arg("id").c_str());
  sendJson(200, "{\"result\":\"DONE\"}");
}

static void handleCallRequest() {
  doCall(server.arg("dir") == "out");
  sendJson(200, "{\"result\":\"DONE\"}");
}

// ---- Serielle Kommandos ----------------------------------------------------

static void printStatus() {
  Serial.printf("STATUS wifi=%d kasse=%d free=%d capacity=%d entry=%s exit=%s call=%d ip=%s\n",
                WiFi.status() == WL_CONNECTED ? 1 : 0, kasseOnline ? 1 : 0, lastFree, lastCapacity,
                entryGate.isOpen ? "open" : "closed", exitGate.isOpen ? "open" : "closed", callActive ? 1 : 0,
                WiFi.localIP().toString().c_str());
}

static void handleCommand(char *line) {
  char *space = strchr(line, ' ');
  std::string arg;
  if (space != NULL) {
    *space = '\0';
    arg = space + 1;
  }
  const char *cmd = line;
  if (strcmp(cmd, "status") == 0) {
    printStatus();
  } else if (strcmp(cmd, "enter") == 0) {
    doEntry(arg);
  } else if (strcmp(cmd, "exit") == 0) {
    doExit(arg);
  } else if (strcmp(cmd, "call") == 0) {
    doCall(arg == "out");
  } else if (strcmp(cmd, "servo") == 0) {
    // Verdrahtungstest: servo in|out open|close
    Barrier &gate = arg.compare(0, 3, "out") == 0 ? exitGate : entryGate;
    if (arg.find("open") != std::string::npos) {
      barrierOpen(gate);
    } else {
      barrierClose(gate);
    }
    Serial.printf("SERVO gate=%s pin=%u state=%s\n", gate.name, (unsigned)gate.pin, gate.isOpen ? "open" : "closed");
  } else {
    Serial.println("UNKNOWN_COMMAND (enter <id> | exit <id> | call in|out | servo in|out open|close | status)");
  }
}

static void readSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputLength > 0) {
        inputBuffer[inputLength] = '\0';
        handleCommand(inputBuffer);
        inputLength = 0;
      }
    } else if (inputLength < sizeof(inputBuffer) - 1) {
      inputBuffer[inputLength++] = c;
    }
  }
}

// ---- WLAN, Start und Hauptschleife -------------------------------------------

static void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.config(IPAddress(SCHRANKE_IP_OCTETS), IPAddress(KASSE_IP_OCTETS), IPAddress(KASSE_IP_OCTETS),
              IPAddress(NETMASK_OCTETS));
  WiFi.noLowPowerMode();  // niedrige Antwortzeit wichtiger als Stromsparen (NFR1)
  WiFi.beginNoBlock(WIFI_SSID, WIFI_PASSWORD);
  lastWifiTryMs = millis();
}

// Verbindet bei Verbindungsverlust neu, ohne die Schrankensteuerung zu blockieren.
static void keepWifi() {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wifiWasUp) {
    logEvent("WIFI connected ssid=%s ip=%s", WIFI_SSID, WiFi.localIP().toString().c_str());
  } else if (!up && wifiWasUp) {
    logEvent("WIFI lost");
    kasseOnline = false;
  }
  wifiWasUp = up;
  if (!up && millis() - lastWifiTryMs >= 8000) {
    lastWifiTryMs = millis();
    WiFi.disconnect();
    WiFi.beginNoBlock(WIFI_SSID, WIFI_PASSWORD);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  entryServo.attach(PIN_SERVO_EINFAHRT, 500, 2400);
  exitServo.attach(PIN_SERVO_AUSFAHRT, 500, 2400);
  barrierClose(entryGate);
  barrierClose(exitGate);

  startWifi();

  server.on("/", HTTP_GET, []() { server.send(200, "text/html; charset=utf-8", String(SCHRANKE_HTML)); });
  server.on("/style.css", HTTP_GET, []() { server.send(200, "text/css; charset=utf-8", String(STYLE_CSS)); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/enter", HTTP_POST, handleEnter);
  server.on("/api/exit", HTTP_POST, handleExitRequest);
  server.on("/api/call", HTTP_POST, handleCallRequest);
  server.begin();

  Serial.printf("READY schranke einfahrt=GP%u ausfahrt=GP%u\n", (unsigned)PIN_SERVO_EINFAHRT, (unsigned)PIN_SERVO_AUSFAHRT);
}

void loop() {
  keepWifi();
  server.handleClient();
  readSerial();
  barrierUpdate(entryGate);
  barrierUpdate(exitGate);
  pollCall();

  // Anzeige der freien Plaetze regelmaessig aktualisieren (US2: spaetestens nach 5 s).
  if (WiFi.status() == WL_CONNECTED && millis() - lastFreePollMs >= FREE_POLL_MS) {
    lastFreePollMs = millis();
    pollFree();
  }
  digitalWrite(LED_BUILTIN, kasseOnline ? HIGH : LOW);
}

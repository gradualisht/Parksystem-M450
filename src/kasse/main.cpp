#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include "ParkLogic.h"
#include "ParkProtocol.h"
#include "config.h"
#include "pages.h"

// Kassen-Pico (Pico 1): WLAN-Zugangspunkt, Weboberflaeche fuer Gebuehr und Bezahlung (US4, US5),
// zentrale Datenhaltung (US3) und Schnittstelle fuer den Schranken-Pico.
//
// Geraete-Schnittstelle (vom Schranken-Pico genutzt, Antworten sind flaches JSON):
//   GET  /api/free                    freie Plaetze (US2)
//   POST /api/entry?id=ZH123456       Einfahrt registrieren (US1, US3)
//   POST /api/exit?id=ZH123456        Ausfahrt pruefen und vollziehen (US6)
//   POST /api/call?dir=in|out         Ruftaste (US7)
//   GET  /api/callstatus?call=N       Stand der Ruftaste-Anfrage (US7)
// Weboberflaeche: GET /, GET /api/state, GET /api/quote?id=..., POST /api/pay
// Personal (HTTP Basic, US8): GET /staff, GET /api/staff/state, POST /api/staff/approve|reject|scale
//
// Serielle Kommandos (USB, 115200): status | list | quote <id> | pay <id> <karte|twint> | scale <n> | reset

static WebServer server(80);
static ParkingDb db(PARK_CAPACITY);
static SimClock simClock(SIM_START_SECONDS, DEFAULT_TIME_SCALE);

enum CallState { CALL_PENDING, CALL_APPROVED, CALL_REJECTED };

struct ServiceCall {
  uint32_t no;
  bool outbound;
  CallState state;
  std::string ticket;
  uint32_t createdMs;
};

static std::vector<ServiceCall> calls;
static uint32_t nextCallNo = 1;
static std::vector<std::string> eventLog;

static char inputBuffer[64];
static uint8_t inputLength = 0;
static uint32_t lastTickMs = 0;

static const char TARIFF_TEXT[] = "CHF 2.00 pro angefangene 30 Minuten";

static uint32_t simNow() { return simClock.nowSeconds(); }

static std::string num(uint32_t value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", (unsigned)value);
  return buf;
}

static std::string quoted(const std::string &text) { return "\"" + jsonEscape(text) + "\""; }

// Ereignis auf Serial ausgeben und fuer die Personal-Seite merken.
static void logEvent(const char *fmt, ...) {
  char msg[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  std::string line = formatSimTime(simNow()) + " " + msg;
  Serial.println(line.c_str());
  eventLog.push_back(line);
  if (eventLog.size() > 30) {
    eventLog.erase(eventLog.begin());
  }
}

static void sendJson(int code, const std::string &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", String(body.c_str()));
}

static std::string freeJson() {
  return "\"free\":" + num(db.freeSpots()) + ",\"capacity\":" + num(db.capacity()) +
         ",\"occupied\":" + num(db.parkedCount());
}

// ---- Gast-Seiten ----------------------------------------------------------

static void handleIndex() { server.send(200, "text/html; charset=utf-8", String(INDEX_HTML)); }

static void handleStyle() { server.send(200, "text/css; charset=utf-8", String(STYLE_CSS)); }

static void handleState() {
  sendJson(200, "{" + freeJson() + ",\"simTime\":" + quoted(formatSimTime(simNow())) +
                    ",\"scale\":" + num(simClock.scale()) + ",\"tariff\":" + quoted(TARIFF_TEXT) +
                    ",\"validMinutes\":" + num(PAID_VALID_SECONDS / 60) + "}");
}

static std::string quoteJson(const std::string &id) {
  Quote q = db.quote(id, simNow());
  if (!q.found) {
    return "{\"found\":false}";
  }
  return "{\"found\":true,\"id\":" + quoted(id) + ",\"entry\":" + quoted(formatSimTime(q.entrySec)) +
         ",\"duration\":" + quoted(formatDuration(q.durationSec)) + ",\"tariff\":" + quoted(TARIFF_TEXT) +
         ",\"fee\":" + quoted(formatChf(q.feeRappen)) + ",\"paid\":" + quoted(formatChf(q.paidRappen)) +
         ",\"due\":" + quoted(formatChf(q.dueRappen)) + ",\"payValid\":" + (q.payValid ? "true" : "false") +
         ",\"validLeftMin\":" + num((q.validLeftSec + 59) / 60) + "}";
}

static void handleQuote() { sendJson(200, quoteJson(normalizeId(server.arg("id").c_str()))); }

static std::string payOutcome(const std::string &id, const std::string &method, bool decline,
                              std::string &message) {
  uint32_t paidNow = 0;
  PayResult r = db.pay(id, method, decline, simNow(), paidNow);
  switch (r) {
    case PAY_OK:
      message = "Zahlung von CHF " + formatChf(paidNow) + " erfolgreich. Die Ausfahrt ist " +
                num(PAID_VALID_SECONDS / 60) + " Minuten gültig.";
      logEvent("PAY id=%s method=%s amount=%s result=OK", id.c_str(), method.c_str(),
               formatChf(paidNow).c_str());
      return "OK";
    case PAY_DECLINED:
      message = method == "twint"
                    ? "Zahlung fehlgeschlagen: Twint hat die Zahlung abgelehnt (simuliert). Der Betrag bleibt offen."
                    : "Zahlung fehlgeschlagen: Die Karte wurde abgelehnt (simuliert). Der Betrag bleibt offen.";
      logEvent("PAY id=%s method=%s result=DECLINED", id.c_str(), method.c_str());
      return "DECLINED";
    case PAY_BAD_METHOD:
      message = "Zahlungsmittel nicht unterstützt. Bitte Karte oder Twint wählen.";
      logEvent("PAY id=%s method=%s result=BAD_METHOD", id.c_str(), method.c_str());
      return "BAD_METHOD";
    default:
      message = "Kein Fahrzeug mit diesem Kennzeichen im Parkhaus gefunden.";
      logEvent("PAY id=%s result=UNKNOWN", id.c_str());
      return "UNKNOWN";
  }
}

static void handlePay() {
  std::string id = normalizeId(server.arg("id").c_str());
  std::string method = server.arg("method").c_str();
  std::string message;
  std::string result = payOutcome(id, method, server.arg("decline") == "1", message);
  sendJson(200, "{\"result\":" + quoted(result) + ",\"message\":" + quoted(message) + "}");
}

// ---- Geraete-Schnittstelle fuer den Schranken-Pico -------------------------

static void handleFree() { sendJson(200, "{" + freeJson() + "}"); }

static void handleEntry() {
  std::string id = normalizeId(server.arg("id").c_str());
  EntryResult r = db.enter(id, simNow());
  const char *name = r == ENTRY_OK        ? "OK"
                     : r == ENTRY_FULL    ? "FULL"
                     : r == ENTRY_INVALID ? "INVALID"
                                          : "DUPLICATE";
  logEvent("ENTRY id=%s result=%s free=%u", id.c_str(), name, (unsigned)db.freeSpots());
  sendJson(200, "{\"result\":\"" + std::string(name) + "\",\"id\":" + quoted(id) + "," + freeJson() + "}");
}

static void handleExit() {
  std::string id = normalizeId(server.arg("id").c_str());
  ExitDecision d = db.tryExit(id, simNow());
  const char *name = d.result == EXIT_OPEN      ? "OPEN"
                     : d.result == EXIT_UNPAID  ? "UNPAID"
                     : d.result == EXIT_EXPIRED ? "EXPIRED"
                                                : "UNKNOWN";
  logEvent("EXIT id=%s result=%s due=%s free=%u", id.c_str(), name, formatChf(d.dueRappen).c_str(),
           (unsigned)db.freeSpots());
  sendJson(200, "{\"result\":\"" + std::string(name) + "\",\"id\":" + quoted(id) + ",\"due\":" +
                    quoted(formatChf(d.dueRappen)) + "," + freeJson() + "}");
}

static ServiceCall *findCall(uint32_t no) {
  for (size_t i = 0; i < calls.size(); i++) {
    if (calls[i].no == no) {
      return &calls[i];
    }
  }
  return NULL;
}

static void handleCall() {
  ServiceCall call;
  call.no = nextCallNo++;
  call.outbound = server.arg("dir") == "out";
  call.state = CALL_PENDING;
  call.createdMs = millis();
  calls.push_back(call);
  if (calls.size() > 10) {
    calls.erase(calls.begin());
  }
  logEvent("CALL no=%u dir=%s state=PENDING", (unsigned)call.no, call.outbound ? "out" : "in");
  sendJson(200, "{\"result\":\"PENDING\",\"call\":" + num(call.no) + "}");
}

static void handleCallStatus() {
  ServiceCall *call = findCall((uint32_t)server.arg("call").toInt());
  if (call == NULL) {
    sendJson(404, "{\"state\":\"unknown\"}");
    return;
  }
  const char *state = call->state == CALL_PENDING ? "pending" : call->state == CALL_APPROVED ? "approved" : "rejected";
  sendJson(200, "{\"state\":\"" + std::string(state) + "\",\"ticket\":" + quoted(call->ticket) + "}");
}

// ---- Personal (US7, US8) ---------------------------------------------------

static bool requireStaff() {
  if (!server.authenticate(STAFF_USER, STAFF_PASSWORD)) {
    server.requestAuthentication(BASIC_AUTH, "Parkhaus Personal");
    return false;
  }
  return true;
}

static void handleStaffPage() {
  if (requireStaff()) {
    server.send(200, "text/html; charset=utf-8", String(STAFF_HTML));
  }
}

static void handleStaffState() {
  if (!requireStaff()) {
    return;
  }
  uint32_t now = simNow();
  std::string json = "{\"simTime\":" + quoted(formatSimTime(now)) + ",\"scale\":" + num(simClock.scale()) + "," +
                     freeJson() + ",\"calls\":[";
  for (size_t i = 0; i < calls.size(); i++) {
    const ServiceCall &c = calls[i];
    const char *state = c.state == CALL_PENDING ? "pending" : c.state == CALL_APPROVED ? "approved" : "rejected";
    json += std::string(i ? "," : "") + "{\"no\":" + num(c.no) + ",\"outbound\":" + (c.outbound ? "true" : "false") +
            ",\"state\":\"" + state + "\",\"waitSec\":" + num((millis() - c.createdMs) / 1000) + "}";
  }
  json += "],\"records\":[";
  const std::vector<ParkRecord> &records = db.records();
  for (size_t i = 0; i < records.size(); i++) {
    const ParkRecord &r = records[i];
    json += std::string(i ? "," : "") + "{\"no\":" + num(r.no) + ",\"id\":" + quoted(r.id) +
            ",\"ticket\":" + (r.isTicket ? "true" : "false") + ",\"entry\":" + quoted(formatSimTime(r.entrySec)) +
            ",\"exited\":" + (r.exited ? "true" : "false") + ",\"exit\":" + quoted(r.exited ? formatSimTime(r.exitSec) : "") +
            ",\"paid\":" + quoted(r.hasPaid ? formatChf(r.paidRappen) : "") + "}";
  }
  json += "],\"log\":[";
  for (size_t i = 0; i < eventLog.size(); i++) {
    json += std::string(i ? "," : "") + quoted(eventLog[eventLog.size() - 1 - i]);
  }
  json += "]}";
  sendJson(200, json);
}

static void handleStaffApprove() {
  if (!requireStaff()) {
    return;
  }
  ServiceCall *call = findCall((uint32_t)server.arg("call").toInt());
  if (call == NULL || call->state != CALL_PENDING) {
    sendJson(200, "{\"result\":\"ERROR\",\"message\":\"Anfrage nicht gefunden oder bereits bearbeitet.\"}");
    return;
  }
  if (!call->outbound) {
    // Einfahrt ohne erkanntes Kennzeichen: manuelles Ticket mit Einfahrtszeit (US7).
    std::string code;
    if (db.issueTicket(simNow(), code) == ENTRY_FULL) {
      sendJson(200, "{\"result\":\"ERROR\",\"message\":\"Parkhaus besetzt, kein Ticket möglich.\"}");
      return;
    }
    call->state = CALL_APPROVED;
    call->ticket = code;
    logEvent("CALL no=%u dir=in state=APPROVED ticket=%s", (unsigned)call->no, code.c_str());
    sendJson(200, "{\"result\":\"OK\",\"message\":\"Ticket " + code + " ausgegeben, Schranke öffnet.\"}");
    return;
  }
  // Ausfahrt ohne erkanntes Kennzeichen: Freigabe nur mit gueltiger Zahlung (US6).
  std::string id = normalizeId(server.arg("id").c_str());
  ExitDecision d = db.tryExit(id, simNow());
  if (d.result != EXIT_OPEN) {
    std::string why = d.result == EXIT_UNKNOWN ? "Fahrzeug nicht registriert."
                                               : "Nicht freigegeben: Gebühr offen (CHF " + formatChf(d.dueRappen) + ").";
    sendJson(200, "{\"result\":\"ERROR\",\"message\":" + quoted(why) + "}");
    return;
  }
  call->state = CALL_APPROVED;
  logEvent("CALL no=%u dir=out state=APPROVED id=%s", (unsigned)call->no, id.c_str());
  sendJson(200, "{\"result\":\"OK\",\"message\":\"Ausfahrt freigegeben, Schranke öffnet.\"}");
}

static void handleStaffReject() {
  if (!requireStaff()) {
    return;
  }
  ServiceCall *call = findCall((uint32_t)server.arg("call").toInt());
  if (call != NULL && call->state == CALL_PENDING) {
    call->state = CALL_REJECTED;
    logEvent("CALL no=%u state=REJECTED", (unsigned)call->no);
  }
  sendJson(200, "{\"result\":\"OK\"}");
}

static void handleStaffScale() {
  if (!requireStaff()) {
    return;
  }
  simClock.update(millis());
  uint32_t value = (uint32_t)server.arg("value").toInt();
  bool ok = simClock.setScale(value);
  if (ok) {
    logEvent("SCALE value=%u", (unsigned)value);
  }
  sendJson(200, std::string("{\"result\":\"") + (ok ? "OK" : "ERROR") + "\",\"scale\":" + num(simClock.scale()) + "}");
}

// ---- Serielle Kommandos ----------------------------------------------------

static void printStatus() {
  Serial.print("STATUS time=");
  Serial.print(formatSimTime(simNow()).c_str());
  Serial.print(" scale=");
  Serial.print(simClock.scale());
  Serial.print(" free=");
  Serial.print(db.freeSpots());
  Serial.print(" capacity=");
  Serial.print(db.capacity());
  Serial.print(" records=");
  Serial.print((unsigned)db.records().size());
  Serial.print(" clients=");
  Serial.println(WiFi.softAPgetStationNum());
}

static void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  char *arg1 = strtok(NULL, " ");
  char *arg2 = strtok(NULL, " ");
  if (cmd == NULL) {
    return;
  }
  if (strcmp(cmd, "status") == 0) {
    printStatus();
  } else if (strcmp(cmd, "list") == 0) {
    const std::vector<ParkRecord> &records = db.records();
    for (size_t i = 0; i < records.size(); i++) {
      const ParkRecord &r = records[i];
      Serial.printf("RECORD no=%u id=%s entry=%s exited=%d paid=%s\n", (unsigned)r.no, r.id.c_str(),
                    formatSimTime(r.entrySec).c_str(), r.exited ? 1 : 0, formatChf(r.paidRappen).c_str());
    }
    Serial.println("LIST_END");
  } else if (strcmp(cmd, "quote") == 0 && arg1 != NULL) {
    Serial.print("QUOTE ");
    Serial.println(quoteJson(normalizeId(arg1)).c_str());
  } else if (strcmp(cmd, "pay") == 0 && arg1 != NULL) {
    std::string message;
    std::string result = payOutcome(normalizeId(arg1), arg2 != NULL ? arg2 : "karte", false, message);
    Serial.print("PAY_RESULT ");
    Serial.print(result.c_str());
    Serial.print(" ");
    Serial.println(message.c_str());
  } else if (strcmp(cmd, "reset") == 0) {
    db.clear();
    calls.clear();
    nextCallNo = 1;
    logEvent("RESET records cleared");
  } else if (strcmp(cmd, "scale") == 0 && arg1 != NULL) {
    simClock.update(millis());
    bool ok = simClock.setScale((uint32_t)atoi(arg1));
    Serial.printf("SCALE %s value=%u\n", ok ? "OK" : "REJECTED", (unsigned)simClock.scale());
  } else {
    Serial.println("UNKNOWN_COMMAND (status | list | quote <id> | pay <id> <karte|twint> | scale <n> | reset)");
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

// ---- Start und Hauptschleife -----------------------------------------------

static void startAccessPoint() {
  IPAddress ip(KASSE_IP_OCTETS);
  IPAddress mask(NETMASK_OCTETS);
  WiFi.softAPConfig(ip, ip, mask);
  bool ok = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("AP %s ssid=%s ip=%s\n", ok ? "UP" : "FAILED", WIFI_SSID, WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  startAccessPoint();

  server.on("/", HTTP_GET, handleIndex);
  server.on("/style.css", HTTP_GET, handleStyle);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/quote", HTTP_GET, handleQuote);
  server.on("/api/pay", HTTP_POST, handlePay);
  server.on("/api/free", HTTP_GET, handleFree);
  server.on("/api/entry", HTTP_POST, handleEntry);
  server.on("/api/exit", HTTP_POST, handleExit);
  server.on("/api/call", HTTP_POST, handleCall);
  server.on("/api/callstatus", HTTP_GET, handleCallStatus);
  server.on("/staff", HTTP_GET, handleStaffPage);
  server.on("/api/staff/state", HTTP_GET, handleStaffState);
  server.on("/api/staff/approve", HTTP_POST, handleStaffApprove);
  server.on("/api/staff/reject", HTTP_POST, handleStaffReject);
  server.on("/api/staff/scale", HTTP_POST, handleStaffScale);
  server.begin();

  simClock.update(millis());
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.printf("READY kasse capacity=%u scale=%u\n", (unsigned)PARK_CAPACITY, (unsigned)simClock.scale());
}

void loop() {
  simClock.update(millis());
  server.handleClient();
  readSerial();

  // Einmal pro Sekunde: Loeschfrist pruefen (US8).
  uint32_t nowMs = millis();
  if (nowMs - lastTickMs >= 1000) {
    lastTickMs = nowMs;
    uint32_t removed = db.purge(simNow());
    if (removed > 0) {
      logEvent("PURGE removed=%u retention=24h", (unsigned)removed);
    }
  }
}

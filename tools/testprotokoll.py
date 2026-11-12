#!/usr/bin/env python3
"""Testprotokoll-Lauf: fuehrt die Testfaelle TC01 bis TC22 des Testkonzepts auf der Hardware aus.

Voraussetzung: beide Picos laufen, der Mac ist mit dem WLAN "Parkhaus" verbunden und beide Picos sind per USB
angeschlossen (die Kasse wird per Seriell zurueckgesetzt). Bedient wird, wie im Browser, nur ueber die
HTTP-Schnittstellen der Weboberflaechen. Zeitraffer: 1 s echt = 20 s Simulation (siehe Spalte "Zeitraffer").

Aufruf:  python3 tools/testprotokoll.py [ergebnis.json]      (benoetigt pyserial, z. B. aus PlatformIO)
Exit-Code 0 = alle Testfaelle bestanden, 1 = mindestens ein Testfall fehlgeschlagen.
"""
import base64
import glob
import json
import math
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import serial

KASSE = "http://192.168.4.1"
SCHRANKE = "http://192.168.4.100"
STAFF = "Basic " + base64.b64encode(b"personal:park2026").decode()
SCALE = 20  # Zeitraffer im Lauf: 1 s echt = 20 s Simulation
MAX_REACTION_S = 3.0
results = []
current = None  # zuletzt gestarteter Testfall (fuer die Fehlerbehandlung in main)


# ---- Zugriff ----------------------------------------------------------------

def http(base, path, method="GET", params=None, auth=None, timeout=8):
    """Liefert (HTTP-Code, Antworttext, Dauer in Sekunden)."""
    data = urllib.parse.urlencode(params).encode() if method == "POST" and params is not None else None
    url = base + path + ("?" + urllib.parse.urlencode(params) if params and method == "GET" else "")
    req = urllib.request.Request(url, data=data, method=method)
    if auth:
        req.add_header("Authorization", auth)
    t0 = time.time()
    for attempt in (1, 2):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return resp.status, resp.read().decode(), time.time() - t0
        except urllib.error.HTTPError as err:
            return err.code, err.read().decode(), time.time() - t0
        except (urllib.error.URLError, TimeoutError, ConnectionError) as err:
            if attempt == 2:
                raise
            print("  (HTTP-Wiederholung nach %s: %s)" % (type(err).__name__, err))
            time.sleep(1.0)


def jget(base, path, method="GET", params=None, auth=None):
    code, body, _ = http(base, path, method, params, auth)
    return json.loads(body) if body.strip().startswith(("{", "[")) else {"raw": body, "code": code}


def sim_sec(text):
    m = re.match(r"T(\d+) (\d+):(\d+):(\d+)", text)
    return (int(m[1]) - 1) * 86400 + int(m[2]) * 3600 + int(m[3]) * 60 + int(m[4])


def hms(text):
    h, m, s = (int(x) for x in text.split(":"))
    return h * 3600 + m * 60 + s


def state():
    return jget(KASSE, "/api/state")


def staff():
    return jget(KASSE, "/api/staff/state", auth=STAFF)


def record(plate):
    return next((r for r in staff()["records"] if r["id"] == plate), None)


def quote(plate):
    return jget(KASSE, "/api/quote", params={"id": plate})


def pay(plate, method, decline=False):
    params = {"id": plate, "method": method}
    if decline:
        params["decline"] = "1"
    return jget(KASSE, "/api/pay", "POST", params)


def set_scale(value):
    jget(KASSE, "/api/staff/scale", "POST", {"value": str(value)}, STAFF)


def sstatus():
    return jget(SCHRANKE, "/api/status")


def last_result(gate):
    """Neueste RESULT-Zeile des Tors aus dem Ereignisprotokoll der Schranke."""
    st = sstatus()
    for line in st["log"]:
        if "RESULT gate=" + gate + " " in line:
            return dict(re.findall(r"(\w+)=(\S*)", line)), st
    return {}, st


def gate_action(path, gate, plate):
    t0 = time.time()
    http(SCHRANKE, path, "POST", {"id": plate})
    elapsed = time.time() - t0
    res, st = last_result(gate)
    return elapsed, res, st[("entry" if gate == "einfahrt" else "exit")]


def do_enter(plate):
    return gate_action("/api/enter", "einfahrt", plate)


def do_exit(plate):
    return gate_action("/api/exit", "ausfahrt", plate)


def wait_free(expected, timeout=8.0):
    """Wartet, bis die Anzeige der Schranke den erwarteten Wert zeigt. Liefert (Sekunden, Wert)."""
    t0 = time.time()
    value = None
    while time.time() - t0 < timeout:
        value = sstatus()["free"]
        if value == expected:
            break
        time.sleep(0.4)
    return time.time() - t0, value


def kasse_reset():
    """Setzt die Kasse ueber die serielle Konsole zurueck (kein HTTP-Reset vorhanden)."""
    for port in sorted(glob.glob("/dev/cu.usbmodem*")):
        dev = serial.Serial(port, 115200, timeout=0.3)
        dev.reset_input_buffer()
        dev.write(b"status\n")
        time.sleep(1.0)
        if "STATUS time=" in dev.read(2000).decode(errors="replace"):
            dev.write(b"reset\n")
            time.sleep(1.0)
            ok = "RESET" in dev.read(2000).decode(errors="replace")
            dev.close()
            return ok
        dev.close()
    return False


# ---- Protokollierung ---------------------------------------------------------

class TC:
    def __init__(self, tc_id, title, soll, scale=SCALE):
        self.data = {"id": tc_id, "title": title, "soll": soll, "scale": scale, "checks": [],
                     "time": time.strftime("%H:%M:%S")}
        global current
        current = self
        print("\n== %s: %s" % (tc_id, title))

    def check(self, what, ok, ist=""):
        self.data["checks"].append({"what": what, "ok": bool(ok), "ist": str(ist)})
        print("  %s - %s%s" % ("PASS" if ok else "FAIL", what, (" | " + str(ist)) if ist != "" else ""))

    def note(self, text):
        self.data["note"] = text

    def done(self):
        self.data["status"] = "PASS" if all(c["ok"] for c in self.data["checks"]) else "FAIL"
        results.append(self.data)
        print("  => %s" % self.data["status"])


def run():
    code, _, _ = http(KASSE, "/")
    if code != 200 or http(SCHRANKE, "/")[0] != 200:
        print("FEHLER: Kasse oder Schranke nicht erreichbar (WLAN Parkhaus?)")
        return 2
    if not kasse_reset():
        print("FEHLER: Kasse konnte seriell nicht zurueckgesetzt werden")
        return 2
    set_scale(SCALE)
    time.sleep(3)  # Schranke holt den neuen Stand der freien Plaetze
    cap = state()["capacity"]
    print("Ausgangslage:", state(), "| Schranke:", sstatus()["free"], "freie Plaetze")

    # ---------------- Testblock 1: Einfahrt (US1 bis US4) ----------------
    t = TC("TC01", "Einfahrt mit gueltigem Kennzeichen oeffnet die Einfahrtsschranke rechtzeitig",
           "Schranke oeffnet innerhalb von 3 Sekunden nach Eingabe")
    sim_before = sim_sec(state()["simTime"])
    rt, res, gate = do_enter("ZH 123 456")
    sim_after = sim_sec(state()["simTime"])
    t.check("Ergebnis OK, Schranke geoeffnet", res.get("result") == "OK" and res.get("opened") == "1", res)
    t.check("Reaktionszeit unter 3 s (HTTP-Gesamtdauer)", rt < MAX_REACTION_S,
            "%.0f ms gesamt, Schranke intern %s ms" % (rt * 1000, res.get("latency_ms")))
    t.check("Anzeige am Tor", gate["open"] is True, gate["text"])
    t.done()

    t = TC("TC02", "Einfahrtsdaten werden korrekt gespeichert",
           "Datensatz mit Kennzeichen ZH123456 sowie korrektem Einfahrtsdatum und -uhrzeit")
    r = record("ZH123456")
    t.check("Datensatz vorhanden", r is not None, r)
    if r:
        e = sim_sec(r["entry"])
        t.check("Einfahrtszeit liegt zwischen Eingabe und Abruf", sim_before <= e <= sim_after,
                "%s (Simulationszeit vor/nach Eingabe: %d/%d s)" % (r["entry"], sim_before, sim_after))
        t.check("Fahrzeug ist als eingefahren (nicht ausgefahren) gefuehrt", r["exited"] is False)
    t.done()

    t = TC("TC03", "Ungueltiges oder leeres Kennzeichen loest den Fallback aus",
           "Schranke bleibt geschlossen, Ruftaste wird angeboten, kein Datensatz entsteht")
    n_before = len(staff()["records"])
    for label, value in (("leeres Feld", ""), ("Wert '??-???'", "??-???")):
        rt, res, gate = do_enter(value)
        t.check("%s: Schranke bleibt zu" % label, res.get("opened") == "0", res.get("result"))
        t.check("%s: Anzeige bietet die Ruftaste an" % label, "Ruftaste" in gate["text"], gate["text"])
    t.check("Kein neuer Datensatz", len(staff()["records"]) == n_before)
    t.done()

    t = TC("TC04", "Anzeige freier Plaetze aktualisiert sich nach einer Einfahrt",
           "Anzeige zeigt spaetestens 5 s nach der Einfahrt einen um 1 reduzierten Wert")
    free_before = sstatus()["free"]
    rt, res, _ = do_enter("AG 987 654")
    secs, value = wait_free(free_before - 1)
    t.check("Einfahrt OK", res.get("result") == "OK", res.get("result"))
    t.check("Anzeige um 1 reduziert innerhalb von 5 s", value == free_before - 1 and secs <= 5.0,
            "%d -> %s nach %.1f s" % (free_before, value, secs))
    t.done()

    t = TC("TC08", "Datensaetze zweier gleichzeitig anwesender Fahrzeuge bleiben eindeutig zugeordnet",
           "Beide Datensaetze bleiben dem richtigen Kennzeichen zugeordnet, keine Vermischung")
    a0, b0 = record("ZH123456"), record("AG987654")
    quote("ZH123456")  # Bezahlvorgang fuer ZH 123 456 starten (Gebuehr abrufen)
    a1, b1 = record("ZH123456"), record("AG987654")
    t.check("Zwei getrennte Datensaetze vorhanden", a0 and b0 and a0["no"] != b0["no"], "Nr. %s und %s" % (a0["no"], b0["no"]))
    t.check("Datensatz AG987654 unveraendert", b0 == b1, b1)
    t.check("Datensatz ZH123456 behaelt Einfahrtszeit", a0["entry"] == a1["entry"], a1["entry"])
    t.done()

    t = TC("TC09", "Korrekte Gebuehrenberechnung bei kurzer Parkdauer",
           "Parkdauer entspricht der Standzeit, Betrag = eine angefangene 30-Minuten-Einheit zu CHF 2.00")
    q = quote("ZH123456")
    dur = hms(q["duration"])
    r = record("ZH123456")
    real_dur = sim_sec(state()["simTime"]) - sim_sec(r["entry"])
    t.check("Parkdauer entspricht der Standzeit (+/- 60 s Simulation)", abs(dur - real_dur) <= 60,
            "%s angezeigt, %d s aus Einfahrtszeit" % (q["duration"], real_dur))
    t.check("Parkdauer hoechstens 30 Minuten", 0 < dur <= 1800, q["duration"])
    t.check("Betrag CHF 2.00", q["fee"] == "2.00", "Gebuehr %s, offen %s" % (q["fee"], q["due"]))
    t.done()

    t = TC("TC11", "Gebuehr ist vor dem Ausloesen der Zahlung einsehbar",
           "Betrag und Parkdauer sind sichtbar, ohne dass eine Zahlung ausgeloest wurde")
    q = quote("ZH123456")
    r = record("ZH123456")
    t.check("Betrag und Parkdauer werden angezeigt", q["found"] and q["fee"] != "" and q["duration"] != "", q)
    t.check("Noch keine Zahlung erfasst", q["paid"] == "0.00" and not q["payValid"] and r["paid"] == "", r["paid"] or "leer")
    t.done()

    t = TC("TC10", "Korrekte Gebuehrenberechnung bei laengerer Parkdauer ueber mehrere Tarifeinheiten",
           "Betrag = Anzahl angefangener 30-Minuten-Einheiten x CHF 2.00", scale=600)
    rt, res, _ = do_enter("ZH 777 222")
    t.check("Einfahrt OK", res.get("result") == "OK", res.get("result"))
    set_scale(600)  # Zeitraffer erhoehen statt Einfahrtszeit zurueckzudatieren
    t0 = time.time()
    q = quote("ZH777222")
    while hms(q["duration"]) < 3700 and time.time() - t0 < 30:
        time.sleep(0.5)
        q = quote("ZH777222")
    set_scale(SCALE)
    q = quote("ZH777222")
    dur = hms(q["duration"])
    units = math.ceil(dur / 1800)
    t.check("Parkdauer ueber 1 Stunde", dur > 3600, q["duration"])
    t.check("Betrag = %d angefangene Einheiten x CHF 2.00 = CHF %d.00" % (units, units * 2),
            q["fee"] == "%d.00" % (units * 2), "Dauer %s, Betrag CHF %s" % (q["duration"], q["fee"]))
    t.done()

    # ---------------- Testblock 2: Bezahlung und Ausfahrt (US5, US6) ----------------
    rt, res, _ = do_enter("AG 555 333")
    pay_17 = pay("AG555333", "karte")  # TC17: Zahlung zu Beginn des Blocks, Ausfahrt erst am Ende
    t17_start = time.time()
    print("\n(TC17-Vorbereitung) Einfahrt:", res.get("result"), "| Zahlung:", pay_17.get("result"))

    t = TC("TC12", "Erfolgreiche Zahlung mit erstem Zahlungsmittel (Karte) markiert den Datensatz als bezahlt",
           "Datensatz wechselt auf bezahlt, gueltig fuer die Ausfahrt")
    p = pay("ZH123456", "karte")
    q = quote("ZH123456")
    t.check("Zahlung erfolgreich", p["result"] == "OK", p["message"])
    t.check("Status bezahlt, gueltig fuer die Ausfahrt", q["payValid"] and q["due"] == "0.00",
            "offen %s, noch gueltig %s min" % (q["due"], q["validLeftMin"]))
    t.check("Datensatz traegt den bezahlten Betrag", record("ZH123456")["paid"] == q["fee"], record("ZH123456")["paid"])
    t.done()

    t = TC("TC13", "Erfolgreiche Zahlung mit zweitem Zahlungsmittel (Twint) markiert den Datensatz als bezahlt",
           "Datensatz wechselt auf bezahlt, gueltig fuer die Ausfahrt")
    p = pay("AG987654", "twint")
    q = quote("AG987654")
    t.check("Zahlung erfolgreich", p["result"] == "OK", p["message"])
    t.check("Status bezahlt, gueltig fuer die Ausfahrt", q["payValid"] and q["due"] == "0.00",
            "offen %s, noch gueltig %s min" % (q["due"], q["validLeftMin"]))
    t.done()

    t = TC("TC14", "Fehlgeschlagene Zahlung hinterlaesst den Datensatz auf offen",
           "Datensatz bleibt offen, Fehlermeldung mit erkennbarem Grund")
    p = pay("ZH777222", "karte", decline=True)
    q = quote("ZH777222")
    t.check("Zahlung schlaegt fehl", p["result"] == "DECLINED", p["result"])
    t.check("Fehlermeldung nennt den Grund", "abgelehnt" in p["message"], p["message"])
    t.check("Datensatz bleibt offen", not q["payValid"] and q["due"] != "0.00" and record("ZH777222")["paid"] == "",
            "offen CHF %s" % q["due"])
    t.done()

    t = TC("TC15", "Ausfahrt mit bezahltem, gueltigem Status oeffnet die Ausfahrtsschranke rechtzeitig",
           "Ausfahrtsschranke oeffnet innerhalb von 3 Sekunden")
    q = quote("ZH123456")
    rt, res, gate = do_exit("ZH 123 456")
    t.check("Zahlung lag weniger als 15 Minuten zurueck", q["payValid"], "noch %s min gueltig" % q["validLeftMin"])
    t.check("Ergebnis OPEN, Schranke geoeffnet", res.get("result") == "OPEN" and res.get("opened") == "1", res)
    t.check("Reaktionszeit unter 3 s", rt < MAX_REACTION_S,
            "%.0f ms gesamt, Schranke intern %s ms" % (rt * 1000, res.get("latency_ms")))
    t.done()

    t = TC("TC16", "Ausfahrt ohne Bezahlung wird verweigert",
           "Ausfahrtsschranke bleibt geschlossen, Anzeige fordert zur Bezahlung an der Kasse auf")
    rt, res, gate = do_exit("ZH 777 222")
    t.check("Schranke bleibt zu", res.get("result") == "UNPAID" and res.get("opened") == "0", res)
    t.check("Anzeige fordert zur Bezahlung an der Kasse auf", "an der Kasse bezahlen" in gate["text"], gate["text"])
    t.check("Fahrzeug gilt weiter als im Parkhaus", record("ZH777222")["exited"] is False)
    t.done()

    t = TC("TC05", "Anzeige freier Plaetze aktualisiert sich nach einer Ausfahrt",
           "Anzeige zeigt spaetestens 5 s nach der Ausfahrt einen um 1 erhoehten Wert")
    time.sleep(2.5)
    free_before = sstatus()["free"]
    rt, res, _ = do_exit("AG 987 654")
    secs, value = wait_free(free_before + 1)
    t.check("Ausfahrt OPEN", res.get("result") == "OPEN", res.get("result"))
    t.check("Anzeige um 1 erhoeht innerhalb von 5 s", value == free_before + 1 and secs <= 5.0,
            "%d -> %s nach %.1f s" % (free_before, value, secs))
    t.done()

    t = TC("TC17", "Ausfahrt mit abgelaufener Zahlungsgueltigkeit wird verweigert",
           "Ausfahrtsschranke bleibt geschlossen, Aufforderung zur erneuten Bezahlung")
    while quote("AG555333")["payValid"] and time.time() - t17_start < 120:
        time.sleep(1)
    waited = time.time() - t17_start
    rt, res, gate = do_exit("AG 555 333")
    t.check("Seit der Zahlung vergingen mindestens 15 Minuten Simulationszeit", waited * SCALE >= 900,
            "%.0f s echt = %.1f min simuliert (Zeitraffer %d)" % (waited, waited * SCALE / 60, SCALE))
    t.check("Schranke bleibt zu", res.get("result") == "EXPIRED" and res.get("opened") == "0", res)
    t.check("Anzeige fordert zur erneuten Bezahlung auf", "aelter" in gate["text"].replace("ä", "ae") and "bezahlen" in gate["text"], gate["text"])
    t.done()

    # ---------------- Testblock 3: Fallback und Datenschutz (US7, US8) ----------------
    t = TC("TC18", "Ruftaste bei nicht erkanntem Kennzeichen loest den Fallback aus",
           "Innerhalb von 60 Sekunden wird ein manuelles Ticket ausgegeben (Servicezentrale meldet sich)")
    rt, res, gate = do_enter("??-???")
    t.check("Vorbedingung: Erkennung fehlgeschlagen, Schranke bleibt zu", res.get("opened") == "0", res.get("result"))
    http(SCHRANKE, "/api/call", "POST", {"dir": "in"})
    t_call = time.time()
    pending = []
    for _ in range(10):
        pending = [c for c in staff()["calls"] if c["state"] == "pending" and not c["outbound"]]
        if pending:
            break
        time.sleep(0.5)
    t.check("Anfrage erscheint beim Personal", len(pending) == 1, pending)
    time.sleep(5)  # Personal reagiert
    appr = jget(KASSE, "/api/staff/approve", "POST", {"call": pending[-1]["no"]}, STAFF) if pending else {}
    reaction = time.time() - t_call
    t.check("Ticket ausgegeben", appr.get("result") == "OK", appr.get("message"))
    t.check("Reaktion innerhalb von 60 s", reaction < 60, "%.0f s nach Druck der Ruftaste" % reaction)
    opened = False
    for _ in range(10):
        gate = sstatus()["entry"]
        if gate["open"]:
            opened = True
            break
        time.sleep(0.5)
    t.check("Einfahrtsschranke oeffnet nach der Ticketausgabe", opened and "Ticket" in gate["text"], gate["text"])
    ticket = (re.search(r"TK\d{4}", appr.get("message", "")) or [None])[0]
    t.done()

    t = TC("TC19", "Mit manuellem Ticket erfasste Einfahrtszeit wird fuer die Gebuehrenberechnung verwendet",
           "Parkdauer basiert auf der Einfahrtszeit des Tickets, Betrag wird korrekt berechnet")
    r = record(ticket) if ticket else None
    q = quote(ticket) if ticket else {"found": False}
    t.check("Ticket %s ist als Datensatz mit Einfahrtszeit erfasst" % ticket, r is not None and r["ticket"] is True, r)
    if r and q.get("found"):
        dur = hms(q["duration"])
        t.check("Einfahrtszeit der Kasse entspricht der des Tickets", q["entry"] == r["entry"], q["entry"])
        t.check("Parkdauer = jetzt minus Einfahrtszeit des Tickets (+/- 60 s)",
                abs(dur - (sim_sec(state()["simTime"]) - sim_sec(r["entry"]))) <= 60, q["duration"])
        t.check("Betrag korrekt", q["fee"] == "%d.00" % (2 * max(1, math.ceil(dur / 1800))),
                "Dauer %s, Betrag CHF %s" % (q["duration"], q["fee"]))
    t.done()

    plates = ["ZH123456", "AG987654", "ZH777222", "AG555333"]
    t = TC("TC21", "Zugriff auf Kennzeichendaten ist auf berechtigtes Betriebspersonal beschraenkt",
           "Zugriff ohne Betriebspersonal-Berechtigung wird verweigert, Kennzeichen sind nicht einsehbar")
    t.check("Personalseite ohne Anmeldung gesperrt", http(KASSE, "/staff")[0] == 401, "HTTP %d" % http(KASSE, "/staff")[0])
    t.check("Fahrzeugliste ohne Anmeldung gesperrt", http(KASSE, "/api/staff/state")[0] == 401)
    wrong = "Basic " + base64.b64encode(b"personal:falsch").decode()
    t.check("Falsches Passwort wird abgewiesen", http(KASSE, "/api/staff/state", auth=wrong)[0] == 401)
    t.check("Mit Personal-Zugang ist die Liste einsehbar", staff()["records"] != [])
    leaks = {}
    for base, path in ((KASSE, "/"), (KASSE, "/api/state"), (KASSE, "/api/free"), (SCHRANKE, "/"), (SCHRANKE, "/api/status")):
        body = http(base, path)[1]
        hit = [p for p in plates if p in body]
        if hit:
            leaks[base.split("//")[1] + path] = hit
    t.check("Oeffentliche Seiten und Schnittstellen enthalten keine Kennzeichen", not leaks,
            leaks if leaks else "keine Kennzeichen in den freien Antworten")
    t.done()

    t = TC("TC20", "Kennzeichendaten werden spaetestens 24 Stunden nach bezahlter Ausfahrt geloescht oder anonymisiert",
           "Kennzeichen ZH123456 ist geloescht oder so anonymisiert, dass kein Rueckschluss mehr moeglich ist", scale=3600)
    t.check("Vorbedingung: Fahrzeug ist ausgefahren", record("ZH123456")["exited"] is True)
    set_scale(3600)  # Testausloeser: 1 s = 1 h, 24 h dauern 24 s
    time.sleep(27)
    set_scale(SCALE)
    s = staff()
    t.check("Datensatz ZH123456 nicht mehr in der Fahrzeugliste", all(r["id"] != "ZH123456" for r in s["records"]))
    t.check("Datensatz AG987654 (ebenfalls ausgefahren) nicht mehr in der Fahrzeugliste", all(r["id"] != "AG987654" for r in s["records"]))
    t.check("Fahrzeuge im Haus bleiben erhalten", any(r["id"] == "ZH777222" for r in s["records"]) and any(r["id"] == "AG555333" for r in s["records"]))
    t.check("Kassen-Ereignisprotokoll enthaelt das Kennzeichen nicht mehr", "ZH123456" not in json.dumps(s["log"]),
            "Treffer im Protokoll der Kasse" if "ZH123456" in json.dumps(s["log"]) else "kein Treffer")
    t.check("Schranken-Ereignisprotokoll enthaelt das Kennzeichen nicht mehr", "ZH123456" not in json.dumps(sstatus()["log"]),
            "Treffer im Protokoll der Schranke" if "ZH123456" in json.dumps(sstatus()["log"]) else "kein Treffer")
    t.done()

    t = TC("TC06", "Einfahrt wird bei Vollbelegung verweigert, auch mit gueltigem Kennzeichen",
           "Anzeige zeigt Besetzt, Einfahrtsschranke oeffnet nicht (Testkapazitaet %d statt 3)" % cap)
    inside = [r for r in staff()["records"] if not r["exited"]]
    for i in range(cap - len(inside)):
        do_enter("ZH 100 %03d" % (i + 1))
    secs, value = wait_free(0)
    t.check("Parkhaus mit %d Fahrzeugen voll belegt, Anzeige zeigt 0" % cap, value == 0, "freie Plaetze: %s" % value)
    rt, res, gate = do_enter("ZH 555 111")
    t.check("Einfahrt mit gueltigem Kennzeichen abgewiesen", res.get("result") == "FULL" and res.get("opened") == "0", res)
    t.check("Anzeige zeigt Besetzt", gate["text"].startswith("Besetzt"), gate["text"])
    t.done()

    kasse_reset()  # Ausgangszustand fuer den Gesamtablauf: Parkhaus leer
    time.sleep(3)

    t = TC("TC07", "Bei jeder Einfahrt wird ein vollstaendiger Datensatz angelegt",
           "Neuer Datensatz mit Kennzeichen, Einfahrtsdatum und -uhrzeit ist vorhanden")
    n_before = len(staff()["records"])
    sim_before = sim_sec(state()["simTime"])
    rt, res, _ = do_enter("ZH 123 456")
    sim_after = sim_sec(state()["simTime"])
    s = staff()
    r = next((r for r in s["records"] if r["id"] == "ZH123456"), None)
    t.check("Einfahrt OK", res.get("result") == "OK", res.get("result"))
    t.check("Genau ein neuer Datensatz", len(s["records"]) == n_before + 1, "%d -> %d" % (n_before, len(s["records"])))
    t.check("Datensatz mit Kennzeichen sowie Datum und Uhrzeit der Einfahrt", r is not None and sim_before <= sim_sec(r["entry"]) <= sim_after, r)
    t.done()

    t = TC("TC22", "Gesamtablauf Einfahrt bis Ausfahrt inklusive Bezahlstatus-Abfrage zwischen Pico 2 und Pico 1",
           "Ausfahrtsschranke oeffnet nur, weil Pico 2 von Pico 1 den Status bezahlt zurueckerhaelt")
    rt, res1, _ = do_exit("ZH 123 456")
    p = pay("ZH123456", "karte")
    rt, res2, _ = do_exit("ZH 123 456")
    log = staff()["log"]  # neueste Eintraege zuerst
    t.check("Vor der Zahlung bleibt die Ausfahrt zu", res1.get("result") == "UNPAID" and res1.get("opened") == "0", res1.get("result"))
    t.check("Zahlung an der Kasse erfolgreich", p["result"] == "OK", p["message"])
    t.check("Nach der Zahlung oeffnet die Ausfahrt", res2.get("result") == "OPEN" and res2.get("opened") == "1", res2.get("result"))
    t.check("Kasse protokolliert beide Abfragen der Schranke (UNPAID, dann OPEN)",
            any("EXIT id=ZH123456 result=OPEN" in l for l in log) and any("EXIT id=ZH123456 result=UNPAID" in l for l in log),
            [l for l in log if "EXIT id=ZH123456" in l])
    t.done()

    # ---------------- Explorative Phase ----------------
    t = TC("E1", "Explorativ: Geraete-Schnittstelle der Kasse ohne Anmeldung",
           "Nur die Schranke darf Ein- und Ausfahrten registrieren; ein anderer WLAN-Teilnehmer nicht")
    code, body, _ = http(KASSE, "/api/entry", "POST", {"id": "ZH 999 002"})
    r = record("ZH999002")
    t.check("POST /api/entry ohne Anmeldung wird abgewiesen", code in (401, 403) and r is None,
            "HTTP %d, Datensatz %s" % (code, "angelegt" if r else "nicht angelegt"))
    calls_before = len(staff()["calls"])
    code_exit = http(KASSE, "/api/exit", "POST", {"id": "ZH 123 456"})[0]
    code_call = http(KASSE, "/api/call", "POST", {"dir": "in"})[0]
    t.check("POST /api/exit ohne Anmeldung wird abgewiesen", code_exit in (401, 403), "HTTP %d" % code_exit)
    t.check("POST /api/call ohne Anmeldung wird abgewiesen, keine Anfrage beim Personal",
            code_call in (401, 403) and len(staff()["calls"]) == calls_before, "HTTP %d" % code_call)
    code_wrong = http(KASSE, "/api/entry", "POST", {"id": "ZH 999 003", "token": "falsch"})[0]
    t.check("Falsches Geraete-Token wird abgewiesen", code_wrong in (401, 403) and record("ZH999003") is None,
            "HTTP %d" % code_wrong)
    rt, res, _ = do_enter("ZH 999 004")  # Die Schranke selbst (mit Token) funktioniert weiterhin
    t.check("Schranke meldet Einfahrt weiterhin erfolgreich an die Kasse", res.get("result") == "OK", res.get("result"))
    t.done()

    set_scale(60)
    kasse_reset()

    failed = [r for r in results if r["status"] != "PASS"]
    print("\n%d von %d Testfaellen bestanden" % (len(results) - len(failed), len(results)))
    for r in failed:
        print("FEHLGESCHLAGEN:", r["id"], r["title"])
    return 1 if failed else 0


def main():
    """Fuehrt alle Testfaelle aus. Ein Fehler bricht den Lauf ab, haelt aber den betroffenen Testfall als ERROR fest,
    schreibt das JSON trotzdem und setzt den Zeitraffer der Kasse zurueck."""
    started = time.strftime("%Y-%m-%d %H:%M:%S")
    code = 2
    try:
        code = run()
    except Exception as err:  # noqa: BLE001 - jeder Abbruch muss im Protokoll landen
        print("\nABBRUCH: %s: %s" % (type(err).__name__, err))
        if current is not None and current.data not in results:
            current.data["status"] = "ERROR"
            current.data["note"] = "Abbruch: %s: %s" % (type(err).__name__, err)
            results.append(current.data)
    finally:
        try:
            set_scale(60)
        except Exception:  # noqa: BLE001
            print("Hinweis: Zeitraffer konnte nicht auf 60 zurueckgesetzt werden")
        if len(sys.argv) > 1:
            with open(sys.argv[1], "w") as f:
                json.dump({"started": started, "ended": time.strftime("%Y-%m-%d %H:%M:%S"), "results": results},
                          f, ensure_ascii=False, indent=1)
    return code


if __name__ == "__main__":
    sys.exit(main())

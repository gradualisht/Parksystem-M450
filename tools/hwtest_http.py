#!/usr/bin/env python3
"""HTTP-Test des Parksystems: Weboberflaeche, Bezahlung, Personal-Freigabe (US7), Datenschutz (US8).

Voraussetzung: beide Picos laufen, der Mac ist mit dem WLAN "Parkhaus" verbunden
(Passwort siehe include/config.h). Nur Python-Standardbibliothek.

HINWEIS: Dieses Skript wurde geschrieben, nachdem die Boards nicht mehr verfuegbar waren.
Es ist gegen die Hardware noch nicht gelaufen.

Aufruf:  python3 tools/hwtest_http.py
"""
import base64
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

KASSE = "http://192.168.4.1"
SCHRANKE = "http://192.168.4.100"
STAFF = "Basic " + base64.b64encode(b"personal:park2026").decode()
results = []


def call(base, path, method="GET", params=None, auth=False, timeout=8):
    """Liefert (HTTP-Code, Antworttext)."""
    data = urllib.parse.urlencode(params).encode() if method == "POST" else None
    url = base + path + ("?" + urllib.parse.urlencode(params) if params and method == "GET" else "")
    req = urllib.request.Request(url, data=data, method=method)
    if auth:
        req.add_header("Authorization", STAFF)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode()
    except urllib.error.HTTPError as err:
        return err.code, err.read().decode()


def jcall(*args, **kwargs):
    code, body = call(*args, **kwargs)
    return code, (json.loads(body) if body.strip().startswith(("{", "[")) else body)


def check(name, condition, detail=""):
    results.append((name, bool(condition)))
    print(("PASS" if condition else "FAIL"), "-", name, ("| " + str(detail)) if detail else "")


def main():
    # Ausgangslage: sauberer Zustand ist nicht erzwingbar (kein HTTP-Reset), daher eindeutiges Kennzeichen
    plate = "ZH%d" % (int(time.time()) % 900000 + 1000)

    code, body = call(KASSE, "/")
    check("Kasse liefert die Gast-Seite", code == 200 and "Parkhaus Kasse" in body)
    code, body = call(SCHRANKE, "/")
    check("Schranke liefert den Simulator", code == 200 and "Schranken-Simulator" in body)

    code, state = jcall(KASSE, "/api/state")
    check("US2: Kasse meldet freie Plaetze", code == 200 and state["free"] >= 0, state)
    free_before = state["free"]

    # US1 / US3: Einfahrt ueber die Weboberflaeche der Schranke
    call(SCHRANKE, "/api/enter", "POST", {"id": plate})
    code, st = jcall(SCHRANKE, "/api/status")
    check("US1: Einfahrtsschranke oeffnet", st["entry"]["open"] is True, st["entry"])
    code, state = jcall(KASSE, "/api/state")
    check("US2: freie Plaetze sinken um 1", state["free"] == free_before - 1, state)

    # US4: Gebuehr einsehen
    time.sleep(2)
    code, q = jcall(KASSE, "/api/quote", params={"id": plate})
    check("US4: Gebuehr und Parkdauer werden angezeigt", q.get("found") and q["due"] != "0.00", q)

    # US6: unbezahlt keine Ausfahrt
    call(SCHRANKE, "/api/exit", "POST", {"id": plate})
    code, st = jcall(SCHRANKE, "/api/status")
    check("US6: unbezahlt bleibt die Ausfahrt zu, Display fordert zur Zahlung auf",
          st["exit"]["open"] is False and "Kasse" in st["exit"]["text"], st["exit"])

    # US5: fehlgeschlagene Zahlung, dann Twint
    code, r = jcall(KASSE, "/api/pay", "POST", {"id": plate, "method": "karte", "decline": "1"})
    check("US5: fehlgeschlagene Zahlung nennt den Grund", r["result"] == "DECLINED" and "abgelehnt" in r["message"], r)
    code, q = jcall(KASSE, "/api/quote", params={"id": plate})
    check("US5: nach fehlgeschlagener Zahlung bleibt der Betrag offen", q["due"] != "0.00" and not q["payValid"], q)
    code, r = jcall(KASSE, "/api/pay", "POST", {"id": plate, "method": "twint"})
    check("US5: Bezahlung mit Twint erfolgreich", r["result"] == "OK", r)

    time.sleep(6)  # Einfahrtsschranke schliesst, Ausfahrt pruefen
    call(SCHRANKE, "/api/exit", "POST", {"id": plate})
    code, st = jcall(SCHRANKE, "/api/status")
    check("US6: bezahlt -> Ausfahrtsschranke oeffnet", st["exit"]["open"] is True, st["exit"])

    # US8: Zugriff auf Kennzeichen nur fuer Personal
    code, _ = call(KASSE, "/api/staff/state")
    check("US8: Personal-Daten ohne Anmeldung gesperrt (401)", code == 401, code)
    code, staff = jcall(KASSE, "/api/staff/state", auth=True)
    check("US8: Personal sieht die Fahrzeugliste", code == 200 and any(r["id"] == plate for r in staff["records"]))

    # US7: Ruftaste, Personal gibt Ticket aus
    time.sleep(6)
    call(SCHRANKE, "/api/call", "POST", {"dir": "in"})
    code, staff = jcall(KASSE, "/api/staff/state", auth=True)
    pending = [c for c in staff["calls"] if c["state"] == "pending" and not c["outbound"]]
    check("US7: Ruftaste erscheint beim Personal", len(pending) >= 1, pending)
    if pending:
        code, r = jcall(KASSE, "/api/staff/approve", "POST", {"call": pending[-1]["no"]}, auth=True)
        check("US7: Personal gibt Ticket aus", r["result"] == "OK" and "TK" in r["message"], r)
        time.sleep(2.5)  # Schranke fragt jede Sekunde nach
        code, st = jcall(SCHRANKE, "/api/status")
        check("US7: Einfahrtsschranke oeffnet nach Freigabe", st["entry"]["open"] is True and "Ticket" in st["entry"]["text"], st["entry"])

    # US8: Loeschfrist (Zeitraffer 1 s = 1 h -> 24 h in 24 s)
    jcall(KASSE, "/api/staff/scale", "POST", {"value": "3600"}, auth=True)
    time.sleep(27)
    jcall(KASSE, "/api/staff/scale", "POST", {"value": "60"}, auth=True)
    code, staff = jcall(KASSE, "/api/staff/state", auth=True)
    check("US8: Datensatz nach 24 h (Simulation) geloescht", not any(r["id"] == plate for r in staff["records"]))

    failed = [n for n, ok in results if not ok]
    print("\n%d von %d Pruefungen bestanden" % (len(results) - len(failed), len(results)))
    for n in failed:
        print("FEHLGESCHLAGEN:", n)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

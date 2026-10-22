#!/usr/bin/env python3
"""Hardware-Test des Parksystems mit beiden Picos (USB-Seriell).

Voraussetzung: Kassen-Pico und Schranken-Pico sind geflasht, per USB angeschlossen und das
Parkhaus-WLAN steht (der Schranken-Pico hat sich mit der Kasse verbunden).

Der Test sendet serielle Kommandos, die Schranke fragt dabei ueber WLAN die Kasse ab. Er prueft
User Stories US1 bis US7 (Einfahrt, Anzeige, Registrierung, Gebuehr, Bezahlung, Ausfahrt, Ruftaste ohne
Personal-Freigabe). Die Personal-Freigabe (US7) und der Datenschutz (US8) prueft hwtest_http.py.

Aufruf:  python3 tools/hwtest.py        (benoetigt pyserial, z. B. aus der PlatformIO-Umgebung)
"""
import glob
import re
import sys
import time

import serial

MAX_REACTION_MS = 3000
results = []


class Device:
    def __init__(self, port):
        self.port = port
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def send(self, command, wait_for=None, timeout=6.0):
        """Sendet ein Kommando und liefert alle Zeilen bis zur gesuchten Antwortzeile."""
        self.ser.reset_input_buffer()
        self.ser.write((command + "\n").encode())
        lines, buf = [], ""
        end = time.time() + timeout
        while time.time() < end:
            buf += self.ser.read(512).decode(errors="replace")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if line:
                    lines.append(line)
            if wait_for and any(re.search(wait_for, ln) for ln in lines):
                break
            if not wait_for and lines and time.time() > end - timeout + 1.0:
                break
        return lines

    def find(self, command, pattern, timeout=6.0):
        for line in self.send(command, pattern, timeout):
            if re.search(pattern, line):
                return line
        return None


def check(name, condition, detail=""):
    results.append((name, bool(condition), detail))
    print(("PASS" if condition else "FAIL"), "-", name, ("| " + detail) if detail else "")


def field(line, key):
    m = re.search(r"\b" + key + r"=(\S+)", line or "")
    return m.group(1) if m else None


def detect_ports():
    kasse = schranke = None
    for port in sorted(glob.glob("/dev/cu.usbmodem*")):
        dev = Device(port)
        dev.ser.write(b"status\n")
        time.sleep(1.0)
        text = dev.ser.read(2000).decode(errors="replace")
        dev.ser.close()
        if "STATUS time=" in text:
            kasse = port
        elif "STATUS wifi=" in text:
            schranke = port
    return kasse, schranke


def main():
    kasse_port, schranke_port = detect_ports()
    print("Kasse:", kasse_port, "| Schranke:", schranke_port)
    if not kasse_port or not schranke_port:
        print("FEHLER: Es wurden nicht beide Geraete erkannt.")
        return 2
    kasse, schranke = Device(kasse_port), Device(schranke_port)

    kasse.send("reset", "RESET")
    kasse.send("scale 60", "SCALE")

    # Schranke muss mit der Kasse verbunden sein (WLAN)
    st = None
    for _ in range(20):
        st = schranke.find("status", r"STATUS wifi=")
        if field(st, "wifi") == "1" and field(st, "kasse") == "1" and field(st, "free") == "5":
            break
        time.sleep(1)
    check("WLAN: Schranke ist mit der Kasse verbunden und kennt die freien Plaetze",
          field(st, "kasse") == "1" and field(st, "free") == "5", st or "")

    # US2: Anzeige freie Plaetze, aktualisiert nach Ein-/Ausfahrt
    # US1 + US3: Einfahrt mit gueltigem Kennzeichen
    r = schranke.find("enter ZH 123456", r"RESULT gate=einfahrt")
    check("US1: gueltiges Kennzeichen -> Schranke oeffnet", field(r, "result") == "OK" and field(r, "opened") == "1", r or "")
    lat = int(field(r, "latency_ms") or 99999)
    check("NFR1: Einfahrtsschranke oeffnet innerhalb von 3 s", lat < MAX_REACTION_MS, f"{lat} ms")
    check("US2: Anzeige zeigt nach Einfahrt 4 freie Plaetze", field(r, "free") == "4", r or "")
    st = schranke.find("status", r"STATUS wifi=")
    check("US1: Einfahrtsschranke ist offen (Servo gefahren)", field(st, "entry") == "open", st or "")
    time.sleep(5.5)
    st = schranke.find("status", r"STATUS wifi=")
    check("US1: Einfahrtsschranke schliesst nach 5 s automatisch", field(st, "entry") == "closed", st or "")

    lst = kasse.send("list", "LIST_END")
    rec = [ln for ln in lst if ln.startswith("RECORD")]
    check("US3: Kasse hat Datensatz mit Kennzeichen und Einfahrtszeit",
          len(rec) == 1 and field(rec[0], "id") == "ZH123456" and re.search(r"entry=T1 \d\d:\d\d:\d\d", rec[0]) is not None,
          rec[0] if rec else "kein Datensatz")

    r = schranke.find("enter XX1", r"RESULT gate=einfahrt")
    check("US1: ungueltiges Kennzeichen -> Schranke bleibt zu", field(r, "result") == "INVALID" and field(r, "opened") == "0", r or "")
    r = schranke.find("enter", r"RESULT gate=einfahrt")
    check("US1/US7: Kennzeichen nicht erkannt -> Schranke bleibt zu", field(r, "result") == "NOT_RECOGNIZED" and field(r, "opened") == "0", r or "")
    r = schranke.find("enter zh-123456", r"RESULT gate=einfahrt")
    check("US3: gleiches Fahrzeug kann nicht doppelt einfahren", field(r, "result") == "DUPLICATE" and field(r, "opened") == "0", r or "")

    # US4: Gebuehr
    time.sleep(2)
    q = kasse.find("quote ZH123456", r"^QUOTE ")
    check("US4: Kasse zeigt Parkdauer, Gebuehr und offenen Betrag",
          q is not None and '"found":true' in q and '"fee":"2.00"' in q and '"due":"2.00"' in q, q or "")

    # US6: ohne Zahlung keine Ausfahrt
    r = schranke.find("exit ZH123456", r"RESULT gate=ausfahrt")
    check("US6: unbezahlt -> Ausfahrtsschranke bleibt zu, Betrag wird angezeigt",
          field(r, "result") == "UNPAID" and field(r, "opened") == "0", r or "")
    st = schranke.find("status", r"STATUS wifi=")
    check("US6: Ausfahrtsschranke ist geschlossen", field(st, "exit") == "closed", st or "")

    # US5: Bezahlung
    p = kasse.find("pay ZH123456 karte", r"^PAY_RESULT ")
    check("US5: Bezahlung mit Karte erfolgreich", p is not None and "PAY_RESULT OK" in p, p or "")
    p = kasse.find("pay ZH123456 bitcoin", r"^PAY_RESULT ")
    check("US5: nicht unterstuetztes Zahlungsmittel wird abgelehnt", p is not None and "BAD_METHOD" in p, p or "")

    # US6: nach Zahlung Ausfahrt
    r = schranke.find("exit ZH123456", r"RESULT gate=ausfahrt")
    lat = int(field(r, "latency_ms") or 99999)
    check("US6: bezahlt -> Ausfahrtsschranke oeffnet", field(r, "result") == "OPEN" and field(r, "opened") == "1", r or "")
    check("NFR1: Ausfahrtsschranke oeffnet innerhalb von 3 s", lat < MAX_REACTION_MS, f"{lat} ms")
    check("US2: Anzeige zeigt nach Ausfahrt wieder 5 freie Plaetze", field(r, "free") == "5", r or "")
    st = schranke.find("status", r"STATUS wifi=")
    check("US6: Ausfahrtsschranke ist offen (Servo gefahren)", field(st, "exit") == "open", st or "")
    time.sleep(5.5)

    # US6: Zahlung laenger als 15 Minuten her (Zeitraffer 600: 15 Minuten = 1.5 s)
    kasse.send("scale 600", "SCALE")
    schranke.find("enter BE 4711", r"RESULT gate=einfahrt")
    kasse.find("pay BE4711 twint", r"^PAY_RESULT ")
    time.sleep(3.0)
    r = schranke.find("exit BE4711", r"RESULT gate=ausfahrt")
    check("US6: Zahlung aelter als 15 Minuten -> Ausfahrt bleibt zu",
          field(r, "result") == "EXPIRED" and field(r, "opened") == "0", r or "")
    kasse.send("scale 60", "SCALE")
    p = kasse.find("pay BE4711 twint", r"^PAY_RESULT ")
    check("US5: nach Ablauf wird nur der Differenzbetrag verlangt", p is not None and "PAY_RESULT OK" in p, p or "")
    r = schranke.find("exit BE4711", r"RESULT gate=ausfahrt")
    check("US6: nach erneuter Zahlung oeffnet die Ausfahrt", field(r, "result") == "OPEN", r or "")
    time.sleep(5.5)

    # US2: Vollbelegung (Kapazitaet 5)
    plates = ["ZH1", "ZH2", "ZH3", "ZH4", "ZH5"]
    for pl in plates:
        r = schranke.find("enter " + pl, r"RESULT gate=einfahrt")
        if field(r, "result") != "OK":
            break
    st = schranke.find("status", r"STATUS wifi=")
    check("US2: bei Vollbelegung zeigt die Anzeige 0 freie Plaetze", field(st, "free") == "0", st or "")
    r = schranke.find("enter ZH6", r"RESULT gate=einfahrt")
    check("US2: bei Vollbelegung bleibt die Schranke zu (gueltiges Kennzeichen)",
          field(r, "result") == "FULL" and field(r, "opened") == "0", r or "")
    time.sleep(5.5)

    # US7: Ruftaste (Anfrage kommt bei der Kasse an; Freigabe durch Personal prueft hwtest_http.py)
    kasse.send("reset", "RESET")
    c = schranke.find("call in", r"CALL gate=einfahrt")
    check("US7: Ruftaste sendet Anfrage an die Kasse", c is not None and "state=PENDING" in c, c or "")

    print()
    failed = [n for n, ok, _ in results if not ok]
    print(f"{len(results) - len(failed)} von {len(results)} Pruefungen bestanden")
    for n in failed:
        print("FEHLGESCHLAGEN:", n)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

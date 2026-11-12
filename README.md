# Parkhaus mit Schranke (M450)

Umsetzung der Aufgabe «Implementierung Parkhaus mit Schranke» (A01-03) mit zwei Raspberry Pi Pico W und dem Arduino-Framework (PlatformIO).

## Gruppenmitglieder

- Jan
- Ylldrit

Modul M450, BBZW, 3. Lehrjahr, 1. Semester. Grundlage sind die Anforderungen und die User Stories US1 bis US8, die wir in den Aufgaben «Anforderungen suchen und definieren» und «Testkonzept» erstellt haben.

## Kurze Beschreibung der Umsetzung

Das System besteht aus zwei Pico W, die über ein eigenes WLAN miteinander sprechen. Es braucht kein Schul-WLAN und keine Zugangsdaten.

```
                 WLAN «Parkhaus» (spannt die Kasse auf)
   ┌──────────────────────────┐   HTTP    ┌──────────────────────────┐
   │ Pico 2: SCHRANKE         │ ────────▶ │ Pico 1: KASSE            │
   │ 192.168.4.100            │           │ 192.168.4.1              │
   │ • Servo Einfahrt (GP8)   │ ◀──────── │ • WLAN-Access-Point      │
   │ • Servo Ausfahrt (GP9)   │   JSON    │ • Datenbank (Fahrzeuge)  │
   │ • simulierte Kennzeichen-│           │ • Gebührenberechnung     │
   │   erkennung, Ruftaste    │           │ • Weboberfläche Gast     │
   │ • Anzeige freie Plätze   │           │   und Personal           │
   └──────────────────────────┘           └──────────────────────────┘
```

- **Kasse (Pico 1)** hält alle Datensätze im RAM, berechnet die Gebühr und stellt die Weboberfläche für Gäste (Gebühr anzeigen, bezahlen) und Personal bereit.
- **Schranke (Pico 2)** steuert die beiden SG90-Servos. Die Kennzeichenerkennung ist simuliert: Das Kennzeichen wird über die Weboberfläche der Schranke oder per serieller Konsole eingegeben. Bei jeder Einfahrt und Ausfahrt fragt die Schranke die Kasse per HTTP ab und öffnet nur bei positiver Antwort.
- **Zeitraffer:** Damit 30-Minuten-Tarifstufen, die 15-Minuten-Frist und die 24-Stunden-Löschfrist in einer Demo sichtbar werden, läuft die Uhr der Kasse schneller (Standard: 1 s echt = 1 min simuliert). Das Personal kann den Faktor umstellen (1 s, 1 min, 10 min, 1 h).
- **Tarif:** CHF 2.00 pro angefangene 30 Minuten. Eine Zahlung gilt 15 Minuten für die Ausfahrt. Danach wird nur der Differenzbetrag zur aktuellen Gebühr verlangt.

## Umgesetzte User Stories

| Story | Thema | Umsetzung |
|---|---|---|
| US1 | Automatische Einfahrt mit Kennzeichenerkennung | Kennzeichen wird geprüft (Kantonskürzel + Ziffern) und registriert, die Schranke öffnet in 22 ms (Ziel unter 3 s). Bei nicht erkanntem oder ungültigem Kennzeichen bleibt sie zu und die Ruftaste wird angeboten. |
| US2 | Anzeige freie Plätze | Die Schranke fragt alle 2 s die Kasse ab (Ziel unter 5 s) und zeigt die freien Plätze. Bei Vollbelegung steht «Besetzt», die Schranke öffnet auch bei gültigem Kennzeichen nicht. |
| US3 | Registrierung als Grundlage der Gebühr | Jede Einfahrt erzeugt einen Datensatz mit Kennzeichen und Einfahrtszeit, ein Fahrzeug kann nicht doppelt einfahren. |
| US4 | Parkgebühr berechnen | Parkdauer, Tarif, Gebühr, bereits bezahlter und offener Betrag werden vor der Zahlung angezeigt. |
| US5 | Bezahlung vor der Ausfahrt | Zahlungsmittel Karte und Twint (simuliert). Nach der Zahlung gilt der Datensatz 15 Minuten als bezahlt. Eine fehlgeschlagene Zahlung (auf der Weboberfläche testbar) bleibt offen und nennt den Grund. |
| US6 | Ausfahrt nach bezahlter Gebühr | Die Ausfahrtsschranke öffnet nur mit gültiger Zahlung. Unbezahlt oder nach über 15 Minuten bleibt sie zu, das Display nennt den offenen Betrag. |
| US7 | Nicht erkanntes Kennzeichen | Ruftaste an Ein- und Ausfahrt sendet eine Anfrage an die Kasse. Das Personal gibt ein Ticket mit Einfahrtszeit aus (Einfahrt) bzw. prüft und gibt die Ausfahrt frei. Das Ticket (`TK0001`) wird bei der Ausfahrt wie ein Kennzeichen verwendet. Wartet die Anfrage über 60 s, wird das markiert. |
| US8 | Schutz der Kennzeichendaten | Datensätze werden 24 Stunden (Simulationszeit) nach der Ausfahrt gelöscht. Fahrzeugliste und Kennzeichen sind nur mit Personal-Login (HTTP Basic) abrufbar. |

Nicht-funktionale Anforderung NFR1 (Reaktionszeit unter 3 s): Die Schranke gibt den Öffnen-Befehl nach rund 20 ms an den Servo, die Bewegung selbst dauert danach rund 0.5 s.

### Bewusst vereinfacht

- Die Kennzeichenerkennung (Kamera) und die Zahlungsabwicklung (Karte, Twint) sind simuliert, wie in der Aufgabe vorgesehen.
- Alle Daten liegen im RAM der Kasse und sind nach einem Neustart weg.
- Der Personal-Login nutzt HTTP Basic über unverschlüsseltes HTTP. Das ist für ein Schulprojekt im geschlossenen WLAN gedacht, nicht für den Echtbetrieb.
- Dauerparkierer und Abos gehören laut Anforderungen nicht zum Umfang.

## Hardware und Verdrahtung

Zwei Raspberry Pi Pico W, beide per USB am Rechner. Zwei SG90-Servos am **Schranken-Pico** (nach dem Plan der Aufgabe):

| SG90 | Farbe | Pico |
|---|---|---|
| Einfahrt | Signal (orange) | GP8 (Pin 11) |
| Ausfahrt | Signal (orange) | GP9 (Pin 12) |
| beide | VCC (rot) | VBUS (Pin 40) |
| beide | GND (braun) | GND (Pin 8) |

Der **Kassen-Pico** braucht keine zusätzliche Verdrahtung. Die Pins stehen in `include/config.h`.

## Inbetriebnahme

Voraussetzung: PlatformIO (VS-Code-Erweiterung oder `pip install platformio`).

```sh
pio test -e native                                           # Unit-Tests, kein Board nötig
pio run -e kasse    -t upload --upload-port <Port Kasse>     # Kassen-Pico flashen
pio run -e schranke -t upload --upload-port <Port Schranke>  # Schranken-Pico flashen
```

Die Ports findest du mit `ls /dev/cu.usbmodem*`. Beim allerersten Flashen eines Boards muss es manuell in den BOOTSEL-Modus: USB abziehen, BOOTSEL-Taste gedrückt halten, einstecken, loslassen. Danach funktioniert der Upload automatisch.

**Demo-Ablauf**

1. Beide Picos mit Strom versorgen. Die Schranke verbindet sich selbst mit dem WLAN der Kasse (LED leuchtet, sobald die Kasse erreichbar ist).
2. Rechner oder Handy mit dem WLAN **`Parkhaus`** verbinden (Passwort `parkhaus2026`).
3. Schranken-Simulator öffnen: <http://192.168.4.100/>. Kennzeichen eingeben (z. B. `ZH 123456`) und «Fahrzeug fährt ein». Die Schranke öffnet.
4. Kasse öffnen: <http://192.168.4.1/>. Kennzeichen eingeben, «Gebühr anzeigen», Zahlungsmittel wählen und bezahlen.
5. Im Schranken-Simulator «Fahrzeug fährt aus». Ohne Zahlung bleibt die Schranke zu, nach der Zahlung öffnet sie.
6. Personalbereich: <http://192.168.4.1/staff> (Benutzer `personal`, Passwort `park2026`) für Ruftaste-Anfragen, Fahrzeugliste und Zeitraffer.

**Serielle Konsole** (115200 Baud, zusätzlich zur Weboberfläche)

| Gerät | Befehle |
|---|---|
| Schranke | `enter <Kennzeichen>`, `exit <Kennzeichen>`, `call in\|out`, `servo in\|out open\|close`, `status`, `log` |
| Kasse | `status`, `list`, `quote <id>`, `pay <id> <karte\|twint>`, `scale <n>`, `reset` |

## Tests

- `pio test -e native`: 51 Unit-Tests der Logik (Gebühr, Fristen, Zahlstatus, Datenbank, Protokoll-Helfer, Pseudonymisierung).
- `python3 tools/testprotokoll.py ergebnis.json`: führt TC01 bis TC22 und den explorativen Test E1 des Testkonzepts über die HTTP-Schnittstellen aus. Resultate: [`docs/testprotokoll/`](docs/testprotokoll/Testprotokoll-Parksystem.pdf) (Protokoll als PDF, Rohdaten in `rohdaten/`).
- `python3 tools/hwtest.py`: Hardware-Test mit beiden Picos über USB-Seriell (Einfahrt, Anzeige, Registrierung, Gebühr, Bezahlung, Ausfahrt, Vollbelegung, Ruftaste).
- `python3 tools/hwtest_http.py`: prüft Weboberfläche, Bezahlung, Personal-Freigabe und Löschfrist über das WLAN `Parkhaus`.

## Hinweise

- **Beim ersten Flashen** eines Boards ist der BOOTSEL-Modus nötig (siehe Inbetriebnahme).
- **Stromversorgung der Servos:** Die SG90 hängen direkt an VBUS. Damit sich der Pico bei Servo-Bewegungen nicht zurücksetzt, fährt der Servo in 3°-Schritten. Falls es trotzdem passiert, helfen ein Elko (470 µF) zwischen VCC und GND der Servos oder ein USB-Hub mit eigener Stromversorgung.
- **Watchdog:** Die Schranke startet sich selbst neu, wenn die Hauptschleife länger als 8 s blockiert.
- **Zugangsdaten sind Demo-Werte:** WLAN-Passwort, Personal-Passwort und Geräte-Token (`DEVICE_TOKEN`) stehen in `include/config.h`, damit die Vorführung ohne Einrichtung läuft. Vor einem echten Einsatz ändern (Testprotokoll, Auflage A2). Die Verbindungen sind unverschlüsseltes HTTP im geschlossenen WLAN.
- **Versionen:** Tag `v1.0` ist der Stand vor dem Testlauf, Tag `v1.1` enthält die Fixes F1 bis F3 aus dem Testprotokoll.

## Projektstruktur

```
platformio.ini            Umgebungen kasse, schranke, native
include/config.h          WLAN, Adressen, Pins, Tarif, Zeitraffer
include/webstyle.h        gemeinsames Stylesheet der Weboberflächen
lib/ParkLogic/            Gebühr, Zahlstatus, Fristen, Datenbank (ohne Hardware testbar)
lib/ParkProtocol/         JSON- und URL-Helfer für die Pico-Schnittstelle
src/kasse/                Firmware Kassen-Pico (Access Point, Weboberfläche, API)
src/schranke/             Firmware Schranken-Pico (Servos, Kennzeichen-Simulation, Ruftaste)
test/                     Unit-Tests (Unity, native)
tools/                    Hardware-Tests (Python)
```

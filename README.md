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

Verifikation: **HW** = auf den beiden Picos geprüft (`tools/hwtest.py`), **Unit** = automatisierte Tests ohne Board (`pio test -e native`), **offen** = implementiert und kompiliert, aber noch nicht auf der Hardware geprüft.

| Story | Thema | Umsetzung | Verifikation |
|---|---|---|---|
| US1 | Automatische Einfahrt mit Kennzeichenerkennung | Kennzeichen wird geprüft (Kantonskürzel + Ziffern) und registriert, die Schranke öffnet in 22 ms (Ziel unter 3 s). Bei nicht erkanntem oder ungültigem Kennzeichen bleibt sie zu und die Ruftaste wird angeboten. | HW, Unit |
| US2 | Anzeige freie Plätze | Die Schranke fragt alle 2 s die Kasse ab (Ziel unter 5 s) und zeigt die freien Plätze. Bei Vollbelegung steht «Besetzt», die Schranke öffnet auch bei gültigem Kennzeichen nicht. | HW (Anzeige 5 → 4 → 5), Unit (Vollbelegung), offen (Vollbelegung auf HW) |
| US3 | Registrierung als Grundlage der Gebühr | Jede Einfahrt erzeugt einen Datensatz mit Kennzeichen und Einfahrtszeit, ein Fahrzeug kann nicht doppelt einfahren. | HW, Unit |
| US4 | Parkgebühr berechnen | Parkdauer, Tarif, Gebühr, bereits bezahlter und offener Betrag werden vor der Zahlung angezeigt. | HW (serielle Abfrage), Unit, offen (Weboberfläche im Browser) |
| US5 | Bezahlung vor der Ausfahrt | Zahlungsmittel Karte und Twint (simuliert). Nach der Zahlung gilt der Datensatz 15 Minuten als bezahlt. Eine fehlgeschlagene Zahlung (auf der Weboberfläche testbar) bleibt offen und nennt den Grund. | HW (Karte, Twint, ungültiges Zahlungsmittel), Unit (Ablehnung), offen (Weboberfläche) |
| US6 | Ausfahrt nach bezahlter Gebühr | Die Ausfahrtsschranke öffnet nur mit gültiger Zahlung. Unbezahlt oder nach über 15 Minuten bleibt sie zu, das Display nennt den offenen Betrag. | HW, Unit |
| US7 | Nicht erkanntes Kennzeichen | Ruftaste an Ein- und Ausfahrt sendet eine Anfrage an die Kasse. Das Personal gibt ein Ticket mit Einfahrtszeit aus (Einfahrt) bzw. prüft und gibt die Ausfahrt frei. Das Ticket (`TK0001`) wird bei der Ausfahrt wie ein Kennzeichen verwendet. Wartet die Anfrage über 60 s, wird das markiert. | Unit (Ticket), offen (Ruftaste und Personal-Freigabe auf HW) |
| US8 | Schutz der Kennzeichendaten | Datensätze werden 24 Stunden (Simulationszeit) nach der Ausfahrt gelöscht. Fahrzeugliste und Kennzeichen sind nur mit Personal-Login (HTTP Basic) abrufbar. | Unit (Löschfrist), offen (Login und Löschung auf HW) |

Nicht-funktionale Anforderung NFR1 (Reaktionszeit unter 3 s): auf der Hardware gemessen, Einfahrt 22 ms und Ausfahrt 21 ms bis zum Servo-Befehl. Die Servo-Bewegung selbst dauert danach rund 0.5 s.

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

- `pio test -e native`: 46 Unit-Tests der Logik (Gebühr, Fristen, Zahlstatus, Datenbank, Protokoll-Helfer). Alle bestehen.
- `python3 tools/hwtest.py`: Hardware-Test mit beiden Picos über USB-Seriell. Beim letzten Lauf bestanden 22 von 25 Prüfungen. Die drei übrigen (Vollbelegung zweimal, Ruftaste) konnten nicht ausgewertet werden, weil die Schranke damals in der Hauptschleife hängen blieb (siehe unten).
- `python3 tools/hwtest_http.py`: prüft Weboberfläche, Bezahlung, Personal-Freigabe und Löschfrist. **Noch nicht gegen die Hardware gelaufen.**

## Bekannte Probleme

- **Schranke hing, Pico setzte sich zurück:** Anfangs setzte sich der Schranken-Pico bei Servo-Bewegungen zurück, weil die SG90 direkt an VBUS hängen und ein Sprung von 0° auf 90° kurz viel Strom zieht. Der Servo fährt deshalb in 3°-Schritten. Später hing die Hauptschleife einmal. Wahrscheinliche Ursache: `Servo::detach()` im Arduino-Core wartet unbegrenzt auf einen PIO-Zustand. Der Code nutzt deshalb kein `detach()` mehr und die Schranke hat einen Watchdog (8 s), der sie bei einem Hänger neu startet. **Diese letzte Änderung ist noch nicht auf der Hardware geprüft.** Bleibt es instabil, hilft ein Elko (470 µF) zwischen VCC und GND der Servos oder ein USB-Hub mit eigener Stromversorgung.
- Beim ersten Flashen eines Boards ist der BOOTSEL-Modus nötig (siehe Inbetriebnahme).

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

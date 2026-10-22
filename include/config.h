#ifndef PARK_CONFIG_H
#define PARK_CONFIG_H

#include <stdint.h>

// Gemeinsame Konfiguration von Kassen-Pico und Schranken-Pico.

// WLAN: Der Kassen-Pico spannt das Netz auf (Access Point), der Schranken-Pico
// verbindet sich damit. So braucht es weder Schul-WLAN noch Zugangsdaten.
#define WIFI_SSID "Parkhaus"
#define WIFI_PASSWORD "parkhaus2026"

// Feste Adressen im Parkhaus-Netz (255.255.255.0).
#define KASSE_IP_OCTETS 192, 168, 4, 1
#define SCHRANKE_IP_OCTETS 192, 168, 4, 100
#define NETMASK_OCTETS 255, 255, 255, 0
#define KASSE_BASE_URL "http://192.168.4.1"

// Personal-Zugang der Kasse (US8: Zugriff auf Kennzeichen nur fuer Betriebspersonal).
#define STAFF_USER "personal"
#define STAFF_PASSWORD "park2026"

// Parkhaus (US2): kleine Kapazitaet, damit "Besetzt" in der Demo schnell erreichbar ist.
const uint32_t PARK_CAPACITY = 5;

// Zeitraffer (siehe ParkLogic.h): 1 echte Sekunde = 60 Simulationssekunden.
// So sind 30-Minuten-Tarifstufen, die 15-Minuten-Frist und die 24-Stunden-Loeschfrist demonstrierbar.
const uint32_t DEFAULT_TIME_SCALE = 60;
const uint32_t SIM_START_SECONDS = 8UL * 3600UL;  // Simulation startet um 08:00:00

// Schranken (US1, US6): Servo-Anschluesse laut Verdrahtungsplan der Aufgabe (GP8 = Pin 11).
const uint8_t PIN_SERVO_EINFAHRT = 8;   // GP8
const uint8_t PIN_SERVO_AUSFAHRT = 9;   // GP9
const int SERVO_ANGLE_CLOSED = 0;
const int SERVO_ANGLE_OPEN = 90;
const uint32_t BARRIER_OPEN_MS = 5000;  // Schranke schliesst nach 5 s von selbst
// Sanfte Servo-Bewegung: Der SG90 haengt direkt an VBUS (USB). Ein Sprung von 0 auf 90 Grad
// zieht kurz einen hohen Strom und kann den Pico per Spannungseinbruch zuruecksetzen.
// Deshalb in Schritten fahren.
const int SERVO_STEP_DEG = 3;
const uint32_t SERVO_STEP_MS = 15;   // 90 Grad dauern so rund 0.45 s
const uint32_t SERVO_INIT_MS = 700;  // Wartezeit je Servo beim Start (Grundstellung)
// Watchdog der Schranke: startet neu, wenn die Hauptschleife laenger haengt (max. 8388 ms).
const uint32_t WATCHDOG_MS = 8000;

// Reaktionszeit (NFR1, US1/US6): Schranke muss innerhalb von 3 s oeffnen.
const uint32_t MAX_REACTION_MS = 3000;
// Ruftaste (US7): Servicezentrale meldet sich innerhalb von 60 s.
const uint32_t SERVICE_CALL_TARGET_MS = 60000;
// Anzeige freie Plaetze (US2): Aktualisierung spaetestens alle 5 s, hier alle 2 s.
const uint32_t FREE_POLL_MS = 2000;

#endif

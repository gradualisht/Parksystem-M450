#ifndef PARK_LOGIC_H
#define PARK_LOGIC_H

#include <stdint.h>
#include <string>
#include <vector>

// Hardwareunabhaengige Parkhaus-Logik (Gebuehr, Zahlstatus, Fristen, Datenhaltung).
// Keine Arduino-Abhaengigkeit, damit sie ohne Board getestet werden kann.
// Alle Zeiten sind (simulierte) Sekunden als uint32_t, siehe SimClock.

// Tarif und Fristen (US4, US5, US6, US8).
const uint32_t BLOCK_SECONDS = 30UL * 60UL;           // CHF pro angefangene 30 Minuten (US4)
const uint32_t BLOCK_RAPPEN = 200;                    // 2.00 CHF pro Stufe
const uint32_t PAID_VALID_SECONDS = 15UL * 60UL;      // Zahlung gilt 15 Minuten fuer die Ausfahrt (US5, US6)
const uint32_t RETENTION_SECONDS = 24UL * 60UL * 60UL;  // Kennzeichen max. 24 h nach Ausfahrt (US8)

// ---- Kennzeichen und Ticketcodes (US1, US7) -------------------------------

// Grossbuchstaben, ohne Leerzeichen/Bindestriche/Punkte ("zh 123-456" -> "ZH123456").
std::string normalizeId(const std::string &raw);

// Schweizer Kennzeichen: Kantonskuerzel (26 Kantone) + 1 bis 6 Ziffern.
bool isValidPlate(const std::string &id);

// Manuelles Ticket bei Ruftaste (US7): "TK" + 4 Ziffern.
bool isTicketCode(const std::string &id);

// ---- Betraege und Zeitanzeige (US4) ---------------------------------------

// Gebuehr in Rappen: jede angefangene Stufe zaehlt voll, 0 Sekunden kosten nichts.
uint32_t calcFeeRappen(uint32_t durationSeconds);

// 400 -> "4.00"
std::string formatChf(uint32_t rappen);

// 30 -> "00:00:30", 3725 -> "01:02:05" (Dauer)
std::string formatDuration(uint32_t seconds);

// 29415 -> "T1 08:10:15" (Tag 1, Uhrzeit der Simulation)
std::string formatSimTime(uint32_t seconds);

// ---- Zeitraffer -----------------------------------------------------------

// Simulationsuhr: laeuft mit einem Faktor schneller als die echte Zeit, damit
// 30-Minuten-Stufen, die 15-Minuten-Frist und die 24-Stunden-Loeschfrist demonstrierbar sind.
// update() mit millis() muss regelmaessig aufgerufen werden (loop). Die Differenz wird
// auf uint32_t gerechnet und ist deshalb bei einem millis()-Ueberlauf weiterhin korrekt.
class SimClock {
 public:
  SimClock(uint32_t startSeconds, uint32_t scale);
  void update(uint32_t realMillis);
  uint32_t nowSeconds() const;
  uint32_t scale() const;
  // Faktor 1..3600; gilt ab dem letzten update(). Ausserhalb des Bereichs: false.
  bool setScale(uint32_t scale);

 private:
  uint64_t simMillis_;
  uint32_t lastRealMillis_;
  uint32_t scale_;
  bool started_;
};

// ---- Datenhaltung ---------------------------------------------------------

struct ParkRecord {
  uint32_t no;          // laufende Nummer (US3)
  std::string id;       // Kennzeichen oder Ticketcode
  bool isTicket;
  uint32_t entrySec;    // Einfahrtszeit (US3)
  uint32_t paidRappen;  // bisher bezahlt
  bool hasPaid;
  uint32_t paidAtSec;   // Zeitpunkt der letzten Zahlung
  std::string method;   // "karte" oder "twint"
  bool exited;
  uint32_t exitSec;
};

enum EntryResult { ENTRY_OK, ENTRY_FULL, ENTRY_INVALID, ENTRY_DUPLICATE };
enum PayResult { PAY_OK, PAY_UNKNOWN, PAY_BAD_METHOD, PAY_DECLINED };
enum ExitResult { EXIT_OPEN, EXIT_UNKNOWN, EXIT_UNPAID, EXIT_EXPIRED };

struct Quote {
  bool found;
  uint32_t entrySec;
  uint32_t durationSec;
  uint32_t feeRappen;
  uint32_t paidRappen;
  uint32_t dueRappen;      // noch offener Betrag
  bool payValid;           // Zahlung vorhanden und nicht aelter als 15 Minuten
  uint32_t validLeftSec;   // Restdauer der Frist (0, wenn nicht gueltig)
};

struct ExitDecision {
  ExitResult result;
  uint32_t dueRappen;
};

// Parkhaus-Datenbank im RAM: ein Datensatz pro Fahrzeug von der Einfahrt bis zur
// bezahlten Ausfahrt, danach nur noch bis zum Ablauf der Loeschfrist (US8).
class ParkingDb {
 public:
  explicit ParkingDb(uint32_t capacity);

  uint32_t capacity() const;
  uint32_t parkedCount() const;
  uint32_t freeSpots() const;

  // Einfahrt registrieren (US1, US3). Reihenfolge der Pruefung: voll, ungueltig, doppelt (US2).
  EntryResult enter(const std::string &id, uint32_t nowSec);

  // Manuelles Ticket bei Ruftaste (US7): registriert die Einfahrtszeit unter einem Ticketcode.
  EntryResult issueTicket(uint32_t nowSec, std::string &codeOut);

  // Aktuelle Gebuehr und Zahlstatus (US4).
  Quote quote(const std::string &id, uint32_t nowSec) const;

  // Bezahlen (US5). Bei PAY_DECLINED bleibt der Datensatz offen.
  PayResult pay(const std::string &id, const std::string &method, bool simulateDecline,
                uint32_t nowSec, uint32_t &paidNowRappen);

  // Ausfahrt pruefen und bei Freigabe vollziehen (US6): nur mit gueltiger Zahlung.
  ExitDecision tryExit(const std::string &id, uint32_t nowSec);

  // Loescht Datensaetze, deren Ausfahrt mehr als 24 h zurueckliegt (US8). Liefert die Anzahl.
  // Mit removedIds erhaelt der Aufrufer die geloeschten Kennzeichen, um sie auch aus Protokollen zu entfernen.
  uint32_t purge(uint32_t nowSec, std::vector<std::string> *removedIds = NULL);

  const std::vector<ParkRecord> &records() const;

  // Leert alle Datensaetze (Demo/Test), die Kapazitaet bleibt.
  void clear();

 private:
  int findParked(const std::string &id) const;
  void addRecord(const std::string &id, bool isTicket, uint32_t nowSec);

  uint32_t capacity_;
  uint32_t nextNo_;
  uint32_t nextTicket_;
  std::vector<ParkRecord> records_;
};

#endif

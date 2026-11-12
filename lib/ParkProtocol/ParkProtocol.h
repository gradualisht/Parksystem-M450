#ifndef PARK_PROTOCOL_H
#define PARK_PROTOCOL_H

#include <string>

// Kleine Helfer fuer die HTTP/JSON-Schnittstelle zwischen Schranken-Pico und Kassen-Pico.
// Die Geraete-Antworten sind bewusst flache JSON-Objekte ({"result":"OK","free":4}),
// damit der Schranken-Pico sie ohne JSON-Bibliothek auslesen kann.

// Maskiert ", \ und Steuerzeichen fuer JSON-Strings.
std::string jsonEscape(const std::string &text);

// Liest den String-Wert zu "key" aus einem flachen JSON-Objekt; sonst fallback.
std::string jsonGetString(const std::string &body, const std::string &key,
                          const std::string &fallback);

// Liest den Zahlenwert zu "key" aus einem flachen JSON-Objekt; sonst fallback.
long jsonGetInt(const std::string &body, const std::string &key, long fallback);

// Prozent-Kodierung fuer URL-Parameter.
std::string urlEncode(const std::string &text);

// Pseudonym fuer ein Kennzeichen: "#" plus 4 Hex-Stellen (FNV-1a). Gleiches Kennzeichen ergibt
// dasselbe Pseudonym, ein Rueckschluss auf das Kennzeichen ist nicht moeglich (US8).
std::string pseudonymize(const std::string &id);

// Ersetzt in einer Protokollzeile jeden Wert von "id=<wert>" durch sein Pseudonym. Mit onlyId
// wird nur dieses eine Kennzeichen ersetzt (Anonymisierung nach der Loeschfrist).
std::string maskIdInLogLine(const std::string &line, const std::string &onlyId = "");

#endif

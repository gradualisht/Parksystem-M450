#include "ParkLogic.h"

#include <stdio.h>

// Alle 26 Kantonskuerzel hintereinander, je zwei Zeichen.
static const char CANTONS[] = "AGAIARBEBLBSFRGEGLGRJULUNENWOWSGSHSOSZTGTIURVDVSZGZH";

static bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

std::string normalizeId(const std::string &raw) {
  std::string out;
  for (size_t i = 0; i < raw.size(); i++) {
    char c = raw[i];
    if (c == ' ' || c == '-' || c == '.' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    if (c >= 'a' && c <= 'z') {
      c = (char)(c - 'a' + 'A');
    }
    out += c;
  }
  return out;
}

bool isValidPlate(const std::string &id) {
  if (id.size() < 3 || id.size() > 8) {
    return false;
  }
  if (!isUpper(id[0]) || !isUpper(id[1])) {
    return false;
  }
  bool knownCanton = false;
  for (size_t i = 0; CANTONS[i] != '\0'; i += 2) {
    if (CANTONS[i] == id[0] && CANTONS[i + 1] == id[1]) {
      knownCanton = true;
      break;
    }
  }
  if (!knownCanton) {
    return false;
  }
  for (size_t i = 2; i < id.size(); i++) {
    if (!isDigitChar(id[i])) {
      return false;
    }
  }
  return true;
}

bool isTicketCode(const std::string &id) {
  if (id.size() != 6 || id[0] != 'T' || id[1] != 'K') {
    return false;
  }
  for (size_t i = 2; i < id.size(); i++) {
    if (!isDigitChar(id[i])) {
      return false;
    }
  }
  return true;
}

uint32_t calcFeeRappen(uint32_t durationSeconds) {
  uint32_t blocks = (durationSeconds + BLOCK_SECONDS - 1) / BLOCK_SECONDS;
  return blocks * BLOCK_RAPPEN;
}

std::string formatChf(uint32_t rappen) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%02u", (unsigned)(rappen / 100), (unsigned)(rappen % 100));
  return buf;
}

std::string formatDuration(uint32_t seconds) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(seconds / 3600),
           (unsigned)((seconds / 60) % 60), (unsigned)(seconds % 60));
  return buf;
}

std::string formatSimTime(uint32_t seconds) {
  uint32_t day = seconds / 86400UL + 1;
  uint32_t rest = seconds % 86400UL;
  char buf[32];
  snprintf(buf, sizeof(buf), "T%u %02u:%02u:%02u", (unsigned)day, (unsigned)(rest / 3600),
           (unsigned)((rest / 60) % 60), (unsigned)(rest % 60));
  return buf;
}

// ---- SimClock -------------------------------------------------------------

SimClock::SimClock(uint32_t startSeconds, uint32_t scale)
    : simMillis_((uint64_t)startSeconds * 1000ULL),
      lastRealMillis_(0),
      scale_(scale),
      started_(false) {}

void SimClock::update(uint32_t realMillis) {
  if (!started_) {
    started_ = true;
    lastRealMillis_ = realMillis;
    return;
  }
  uint32_t delta = realMillis - lastRealMillis_;
  simMillis_ += (uint64_t)delta * scale_;
  lastRealMillis_ = realMillis;
}

uint32_t SimClock::nowSeconds() const { return (uint32_t)(simMillis_ / 1000ULL); }

uint32_t SimClock::scale() const { return scale_; }

bool SimClock::setScale(uint32_t scale) {
  if (scale < 1 || scale > 3600) {
    return false;
  }
  scale_ = scale;
  return true;
}

// ---- ParkingDb ------------------------------------------------------------

ParkingDb::ParkingDb(uint32_t capacity) : capacity_(capacity), nextNo_(1), nextTicket_(1) {}

uint32_t ParkingDb::capacity() const { return capacity_; }

uint32_t ParkingDb::parkedCount() const {
  uint32_t count = 0;
  for (size_t i = 0; i < records_.size(); i++) {
    if (!records_[i].exited) {
      count++;
    }
  }
  return count;
}

uint32_t ParkingDb::freeSpots() const {
  uint32_t parked = parkedCount();
  return parked >= capacity_ ? 0 : capacity_ - parked;
}

int ParkingDb::findParked(const std::string &id) const {
  for (size_t i = 0; i < records_.size(); i++) {
    if (!records_[i].exited && records_[i].id == id) {
      return (int)i;
    }
  }
  return -1;
}

EntryResult ParkingDb::enter(const std::string &id, uint32_t nowSec) {
  if (freeSpots() == 0) {
    return ENTRY_FULL;
  }
  if (!isValidPlate(id)) {
    return ENTRY_INVALID;
  }
  if (findParked(id) >= 0) {
    return ENTRY_DUPLICATE;
  }
  addRecord(id, false, nowSec);
  return ENTRY_OK;
}

void ParkingDb::addRecord(const std::string &id, bool isTicket, uint32_t nowSec) {
  ParkRecord rec;
  rec.no = nextNo_++;
  rec.id = id;
  rec.isTicket = isTicket;
  rec.entrySec = nowSec;
  rec.paidRappen = 0;
  rec.hasPaid = false;
  rec.paidAtSec = 0;
  rec.exited = false;
  rec.exitSec = 0;
  records_.push_back(rec);
}

EntryResult ParkingDb::issueTicket(uint32_t nowSec, std::string &codeOut) {
  if (freeSpots() == 0) {
    return ENTRY_FULL;
  }
  char code[8];
  do {
    snprintf(code, sizeof(code), "TK%04u", (unsigned)nextTicket_);
    nextTicket_ = nextTicket_ >= 9999 ? 1 : nextTicket_ + 1;
  } while (findParked(code) >= 0);
  codeOut = code;
  addRecord(codeOut, true, nowSec);  // Kapazitaet ist geprueft, Ticketcode ist kein Kennzeichen
  return ENTRY_OK;
}

Quote ParkingDb::quote(const std::string &id, uint32_t nowSec) const {
  Quote q;
  q.found = false;
  q.entrySec = 0;
  q.durationSec = 0;
  q.feeRappen = 0;
  q.paidRappen = 0;
  q.dueRappen = 0;
  q.payValid = false;
  q.validLeftSec = 0;

  int idx = findParked(id);
  if (idx < 0) {
    return q;
  }
  const ParkRecord &rec = records_[idx];
  q.found = true;
  q.entrySec = rec.entrySec;
  q.durationSec = nowSec - rec.entrySec;
  q.feeRappen = calcFeeRappen(q.durationSec);
  q.paidRappen = rec.paidRappen;
  q.dueRappen = q.feeRappen > rec.paidRappen ? q.feeRappen - rec.paidRappen : 0;
  if (rec.hasPaid && nowSec - rec.paidAtSec <= PAID_VALID_SECONDS) {
    q.payValid = true;
    q.validLeftSec = PAID_VALID_SECONDS - (nowSec - rec.paidAtSec);
  }
  return q;
}

PayResult ParkingDb::pay(const std::string &id, const std::string &method, bool simulateDecline,
                         uint32_t nowSec, uint32_t &paidNowRappen) {
  paidNowRappen = 0;
  int idx = findParked(id);
  if (idx < 0) {
    return PAY_UNKNOWN;
  }
  if (method != "karte" && method != "twint") {
    return PAY_BAD_METHOD;
  }
  if (simulateDecline) {
    return PAY_DECLINED;
  }
  Quote q = quote(id, nowSec);
  ParkRecord &rec = records_[idx];
  rec.paidRappen += q.dueRappen;
  rec.hasPaid = true;
  rec.paidAtSec = nowSec;
  rec.method = method;
  paidNowRappen = q.dueRappen;
  return PAY_OK;
}

ExitDecision ParkingDb::tryExit(const std::string &id, uint32_t nowSec) {
  ExitDecision d;
  d.dueRappen = 0;
  int idx = findParked(id);
  if (idx < 0) {
    d.result = EXIT_UNKNOWN;
    return d;
  }
  Quote q = quote(id, nowSec);
  d.dueRappen = q.dueRappen;
  if (!records_[idx].hasPaid) {
    d.result = EXIT_UNPAID;
    return d;
  }
  if (!q.payValid) {
    d.result = EXIT_EXPIRED;
    return d;
  }
  records_[idx].exited = true;
  records_[idx].exitSec = nowSec;
  d.result = EXIT_OPEN;
  return d;
}

uint32_t ParkingDb::purge(uint32_t nowSec) {
  uint32_t removed = 0;
  for (size_t i = records_.size(); i > 0; i--) {
    const ParkRecord &rec = records_[i - 1];
    if (rec.exited && nowSec - rec.exitSec >= RETENTION_SECONDS) {
      records_.erase(records_.begin() + (long)(i - 1));
      removed++;
    }
  }
  return removed;
}

const std::vector<ParkRecord> &ParkingDb::records() const { return records_; }

#include <unity.h>
#include "ParkLogic.h"

void setUp(void) {}
void tearDown(void) {}

// ---- Kennzeichen (US1) ----------------------------------------------------

void test_normalize_uppercases_and_strips_separators(void) {
  TEST_ASSERT_EQUAL_STRING("ZH123456", normalizeId("zh 123-456").c_str());
  TEST_ASSERT_EQUAL_STRING("BE1", normalizeId(" be.1 ").c_str());
}

void test_valid_plates(void) {
  TEST_ASSERT_TRUE(isValidPlate("ZH123456"));
  TEST_ASSERT_TRUE(isValidPlate("BE1"));
  TEST_ASSERT_TRUE(isValidPlate("TI4567"));
  TEST_ASSERT_TRUE(isValidPlate("AG999999"));
}

void test_invalid_plates(void) {
  TEST_ASSERT_FALSE(isValidPlate(""));
  TEST_ASSERT_FALSE(isValidPlate("ZH"));             // keine Ziffern
  TEST_ASSERT_FALSE(isValidPlate("XX1234"));         // kein Kanton
  TEST_ASSERT_FALSE(isValidPlate("ZH1234567"));      // 7 Ziffern
  TEST_ASSERT_FALSE(isValidPlate("ZH12A4"));         // Buchstabe unter den Ziffern
  TEST_ASSERT_FALSE(isValidPlate("zh1234"));         // nicht normalisiert
  TEST_ASSERT_FALSE(isValidPlate("???"));
}

void test_ticket_code_is_not_a_plate(void) {
  TEST_ASSERT_TRUE(isTicketCode("TK0001"));
  TEST_ASSERT_FALSE(isTicketCode("TK001"));
  TEST_ASSERT_FALSE(isTicketCode("ZH0001"));
  TEST_ASSERT_FALSE(isValidPlate("TK0001"));
}

// ---- Gebuehr (US4) --------------------------------------------------------

void test_fee_zero_seconds_is_free(void) {
  TEST_ASSERT_EQUAL_UINT32(0, calcFeeRappen(0));
}

void test_fee_first_started_block_counts_full(void) {
  TEST_ASSERT_EQUAL_UINT32(200, calcFeeRappen(1));
  TEST_ASSERT_EQUAL_UINT32(200, calcFeeRappen(1800));
}

void test_fee_next_block_starts_after_30_minutes(void) {
  TEST_ASSERT_EQUAL_UINT32(400, calcFeeRappen(1801));
  TEST_ASSERT_EQUAL_UINT32(400, calcFeeRappen(3600));
  TEST_ASSERT_EQUAL_UINT32(600, calcFeeRappen(3601));
}

void test_format_helpers(void) {
  TEST_ASSERT_EQUAL_STRING("4.00", formatChf(400).c_str());
  TEST_ASSERT_EQUAL_STRING("0.05", formatChf(5).c_str());
  TEST_ASSERT_EQUAL_STRING("01:02:05", formatDuration(3725).c_str());
  TEST_ASSERT_EQUAL_STRING("T1 08:10:15", formatSimTime(8 * 3600 + 10 * 60 + 15).c_str());
  TEST_ASSERT_EQUAL_STRING("T2 01:00:00", formatSimTime(86400 + 3600).c_str());
}

// ---- Zeitraffer -----------------------------------------------------------

void test_clock_runs_with_scale(void) {
  SimClock c(1000, 60);
  c.update(5000);  // erster Aufruf setzt nur den Bezugspunkt
  c.update(7000);  // 2 s echt = 120 s simuliert
  TEST_ASSERT_EQUAL_UINT32(1120, c.nowSeconds());
}

void test_clock_scale_change_keeps_time_continuous(void) {
  SimClock c(0, 1);
  c.update(0);
  c.update(10000);  // 10 s bei Faktor 1
  TEST_ASSERT_TRUE(c.setScale(60));
  c.update(11000);  // +1 s bei Faktor 60
  TEST_ASSERT_EQUAL_UINT32(70, c.nowSeconds());
}

void test_clock_rejects_invalid_scale(void) {
  SimClock c(0, 1);
  TEST_ASSERT_FALSE(c.setScale(0));
  TEST_ASSERT_FALSE(c.setScale(3601));
  TEST_ASSERT_EQUAL_UINT32(1, c.scale());
}

void test_clock_handles_millis_rollover(void) {
  // 6 ms vor dem Ueberlauf von uint32_t gestartet, 4 ms danach gemessen -> 10 ms vergangen.
  SimClock c(0, 1000);
  c.update(4294967290UL);
  c.update(4UL);
  TEST_ASSERT_EQUAL_UINT32(10, c.nowSeconds());
}

// ---- Einfahrt / Registrierung (US1, US2, US3) -----------------------------

void test_enter_registers_plate_with_entry_time(void) {
  ParkingDb db(3);
  TEST_ASSERT_EQUAL(ENTRY_OK, db.enter("ZH123456", 28800));
  TEST_ASSERT_EQUAL_UINT32(1, db.records().size());
  TEST_ASSERT_EQUAL_STRING("ZH123456", db.records()[0].id.c_str());
  TEST_ASSERT_EQUAL_UINT32(28800, db.records()[0].entrySec);
  TEST_ASSERT_EQUAL_UINT32(2, db.freeSpots());
}

void test_enter_rejects_invalid_plate(void) {
  ParkingDb db(3);
  TEST_ASSERT_EQUAL(ENTRY_INVALID, db.enter("XX1", 0));
  TEST_ASSERT_EQUAL_UINT32(0, db.records().size());
}

void test_enter_rejects_duplicate_plate(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  TEST_ASSERT_EQUAL(ENTRY_DUPLICATE, db.enter("ZH1", 10));
  TEST_ASSERT_EQUAL_UINT32(1, db.parkedCount());
}

void test_enter_refuses_when_full_even_with_valid_plate(void) {
  ParkingDb db(2);
  db.enter("ZH1", 0);
  db.enter("ZH2", 0);
  TEST_ASSERT_EQUAL_UINT32(0, db.freeSpots());
  TEST_ASSERT_EQUAL(ENTRY_FULL, db.enter("ZH3", 0));
  TEST_ASSERT_EQUAL_UINT32(2, db.records().size());
}

// ---- Gebuehr und Bezahlung (US4, US5) -------------------------------------

void test_quote_shows_duration_and_fee(void) {
  ParkingDb db(3);
  db.enter("ZH1", 1000);
  Quote q = db.quote("ZH1", 1000 + 35 * 60);
  TEST_ASSERT_TRUE(q.found);
  TEST_ASSERT_EQUAL_UINT32(35 * 60, q.durationSec);
  TEST_ASSERT_EQUAL_UINT32(400, q.feeRappen);
  TEST_ASSERT_EQUAL_UINT32(400, q.dueRappen);
  TEST_ASSERT_FALSE(q.payValid);
}

void test_quote_unknown_vehicle(void) {
  ParkingDb db(3);
  TEST_ASSERT_FALSE(db.quote("ZH9", 0).found);
}

void test_pay_marks_record_as_paid(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  TEST_ASSERT_EQUAL(PAY_OK, db.pay("ZH1", "karte", false, 3600, paid));
  TEST_ASSERT_EQUAL_UINT32(400, paid);
  Quote q = db.quote("ZH1", 3600);
  TEST_ASSERT_TRUE(q.payValid);
  TEST_ASSERT_EQUAL_UINT32(0, q.dueRappen);
  TEST_ASSERT_EQUAL_UINT32(PAID_VALID_SECONDS, q.validLeftSec);
}

void test_pay_accepts_twint_and_rejects_other_methods(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  TEST_ASSERT_EQUAL(PAY_BAD_METHOD, db.pay("ZH1", "bitcoin", false, 60, paid));
  TEST_ASSERT_EQUAL(PAY_OK, db.pay("ZH1", "twint", false, 60, paid));
}

void test_declined_payment_keeps_record_open(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  uint32_t paid = 99;
  TEST_ASSERT_EQUAL(PAY_DECLINED, db.pay("ZH1", "karte", true, 3600, paid));
  TEST_ASSERT_EQUAL_UINT32(0, paid);
  Quote q = db.quote("ZH1", 3600);
  TEST_ASSERT_FALSE(q.payValid);
  TEST_ASSERT_EQUAL_UINT32(400, q.dueRappen);
}

void test_pay_unknown_vehicle(void) {
  ParkingDb db(3);
  uint32_t paid = 0;
  TEST_ASSERT_EQUAL(PAY_UNKNOWN, db.pay("ZH9", "karte", false, 0, paid));
}

void test_paying_twice_does_not_charge_twice(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  db.pay("ZH1", "karte", false, 600, paid);
  TEST_ASSERT_EQUAL(PAY_OK, db.pay("ZH1", "karte", false, 660, paid));
  TEST_ASSERT_EQUAL_UINT32(0, paid);
  TEST_ASSERT_EQUAL_UINT32(200, db.quote("ZH1", 660).paidRappen);
}

// ---- Ausfahrt (US6) -------------------------------------------------------

void test_exit_denied_when_unpaid(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  ExitDecision d = db.tryExit("ZH1", 3600);
  TEST_ASSERT_EQUAL(EXIT_UNPAID, d.result);
  TEST_ASSERT_EQUAL_UINT32(400, d.dueRappen);
  TEST_ASSERT_EQUAL_UINT32(1, db.parkedCount());
}

void test_exit_allowed_when_paid_and_frees_spot(void) {
  ParkingDb db(2);
  db.enter("ZH1", 0);
  db.enter("ZH2", 0);
  uint32_t paid = 0;
  db.pay("ZH1", "karte", false, 3600, paid);
  TEST_ASSERT_EQUAL(EXIT_OPEN, db.tryExit("ZH1", 3700).result);
  TEST_ASSERT_EQUAL_UINT32(1, db.freeSpots());
}

void test_exit_allowed_exactly_15_minutes_after_payment(void) {
  ParkingDb db(2);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  db.pay("ZH1", "karte", false, 3600, paid);
  TEST_ASSERT_EQUAL(EXIT_OPEN, db.tryExit("ZH1", 3600 + PAID_VALID_SECONDS).result);
}

void test_exit_denied_after_payment_expired(void) {
  ParkingDb db(2);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  db.pay("ZH1", "karte", false, 3600, paid);
  ExitDecision d = db.tryExit("ZH1", 3600 + PAID_VALID_SECONDS + 1);
  TEST_ASSERT_EQUAL(EXIT_EXPIRED, d.result);
  TEST_ASSERT_EQUAL_UINT32(1, db.parkedCount());
}

void test_expired_payment_only_charges_difference(void) {
  ParkingDb db(2);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  db.pay("ZH1", "karte", false, 3600, paid);  // 2 Stufen = 4.00
  uint32_t later = 3600 + PAID_VALID_SECONDS + 1;  // inzwischen 3. Stufe angefangen
  Quote q = db.quote("ZH1", later);
  TEST_ASSERT_EQUAL_UINT32(600, q.feeRappen);
  TEST_ASSERT_EQUAL_UINT32(200, q.dueRappen);
  TEST_ASSERT_EQUAL(PAY_OK, db.pay("ZH1", "twint", false, later, paid));
  TEST_ASSERT_EQUAL_UINT32(200, paid);
  TEST_ASSERT_EQUAL(EXIT_OPEN, db.tryExit("ZH1", later + 5).result);
}

void test_exit_unknown_vehicle(void) {
  ParkingDb db(2);
  TEST_ASSERT_EQUAL(EXIT_UNKNOWN, db.tryExit("ZH1", 0).result);
}

void test_vehicle_can_enter_again_after_exit(void) {
  ParkingDb db(2);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  db.pay("ZH1", "karte", false, 60, paid);
  db.tryExit("ZH1", 100);
  TEST_ASSERT_EQUAL(ENTRY_OK, db.enter("ZH1", 200));
}

// ---- Fallback mit Ticket (US7) --------------------------------------------

void test_ticket_gets_entry_time_and_is_used_for_fee(void) {
  ParkingDb db(3);
  std::string code;
  TEST_ASSERT_EQUAL(ENTRY_OK, db.issueTicket(5000, code));
  TEST_ASSERT_EQUAL_STRING("TK0001", code.c_str());
  TEST_ASSERT_TRUE(db.records()[0].isTicket);
  Quote q = db.quote(code, 5000 + 3600);
  TEST_ASSERT_TRUE(q.found);
  TEST_ASSERT_EQUAL_UINT32(5000, q.entrySec);
  TEST_ASSERT_EQUAL_UINT32(400, q.feeRappen);
}

void test_ticket_codes_are_unique(void) {
  ParkingDb db(3);
  std::string a, b;
  db.issueTicket(0, a);
  db.issueTicket(0, b);
  TEST_ASSERT_TRUE(a != b);
}

void test_ticket_refused_when_full(void) {
  ParkingDb db(1);
  db.enter("ZH1", 0);
  std::string code;
  TEST_ASSERT_EQUAL(ENTRY_FULL, db.issueTicket(0, code));
}

void test_ticket_holder_can_pay_and_exit(void) {
  ParkingDb db(3);
  std::string code;
  db.issueTicket(0, code);
  uint32_t paid = 0;
  TEST_ASSERT_EQUAL(PAY_OK, db.pay(code, "karte", false, 1000, paid));
  TEST_ASSERT_EQUAL(EXIT_OPEN, db.tryExit(code, 1100).result);
}

// ---- Datenschutz (US8) ----------------------------------------------------

void test_purge_removes_exited_record_after_24_hours(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  uint32_t paid = 0;
  db.pay("ZH1", "karte", false, 60, paid);
  db.tryExit("ZH1", 100);
  TEST_ASSERT_EQUAL_UINT32(0, db.purge(100 + RETENTION_SECONDS - 1));
  TEST_ASSERT_EQUAL_UINT32(1, db.records().size());
  TEST_ASSERT_EQUAL_UINT32(1, db.purge(100 + RETENTION_SECONDS));
  TEST_ASSERT_EQUAL_UINT32(0, db.records().size());
}

void test_purge_keeps_parked_vehicles(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  TEST_ASSERT_EQUAL_UINT32(0, db.purge(10 * RETENTION_SECONDS));
  TEST_ASSERT_EQUAL_UINT32(1, db.records().size());
}

void test_clear_empties_database_and_restarts_numbering(void) {
  ParkingDb db(3);
  db.enter("ZH1", 0);
  std::string code;
  db.issueTicket(0, code);
  db.clear();
  TEST_ASSERT_EQUAL_UINT32(0, db.records().size());
  TEST_ASSERT_EQUAL_UINT32(3, db.freeSpots());
  db.issueTicket(0, code);
  TEST_ASSERT_EQUAL_STRING("TK0001", code.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, db.records()[0].no);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_normalize_uppercases_and_strips_separators);
  RUN_TEST(test_valid_plates);
  RUN_TEST(test_invalid_plates);
  RUN_TEST(test_ticket_code_is_not_a_plate);
  RUN_TEST(test_fee_zero_seconds_is_free);
  RUN_TEST(test_fee_first_started_block_counts_full);
  RUN_TEST(test_fee_next_block_starts_after_30_minutes);
  RUN_TEST(test_format_helpers);
  RUN_TEST(test_clock_runs_with_scale);
  RUN_TEST(test_clock_scale_change_keeps_time_continuous);
  RUN_TEST(test_clock_rejects_invalid_scale);
  RUN_TEST(test_clock_handles_millis_rollover);
  RUN_TEST(test_enter_registers_plate_with_entry_time);
  RUN_TEST(test_enter_rejects_invalid_plate);
  RUN_TEST(test_enter_rejects_duplicate_plate);
  RUN_TEST(test_enter_refuses_when_full_even_with_valid_plate);
  RUN_TEST(test_quote_shows_duration_and_fee);
  RUN_TEST(test_quote_unknown_vehicle);
  RUN_TEST(test_pay_marks_record_as_paid);
  RUN_TEST(test_pay_accepts_twint_and_rejects_other_methods);
  RUN_TEST(test_declined_payment_keeps_record_open);
  RUN_TEST(test_pay_unknown_vehicle);
  RUN_TEST(test_paying_twice_does_not_charge_twice);
  RUN_TEST(test_exit_denied_when_unpaid);
  RUN_TEST(test_exit_allowed_when_paid_and_frees_spot);
  RUN_TEST(test_exit_allowed_exactly_15_minutes_after_payment);
  RUN_TEST(test_exit_denied_after_payment_expired);
  RUN_TEST(test_expired_payment_only_charges_difference);
  RUN_TEST(test_exit_unknown_vehicle);
  RUN_TEST(test_vehicle_can_enter_again_after_exit);
  RUN_TEST(test_ticket_gets_entry_time_and_is_used_for_fee);
  RUN_TEST(test_ticket_codes_are_unique);
  RUN_TEST(test_ticket_refused_when_full);
  RUN_TEST(test_ticket_holder_can_pay_and_exit);
  RUN_TEST(test_purge_removes_exited_record_after_24_hours);
  RUN_TEST(test_purge_keeps_parked_vehicles);
  RUN_TEST(test_clear_empties_database_and_restarts_numbering);
  return UNITY_END();
}

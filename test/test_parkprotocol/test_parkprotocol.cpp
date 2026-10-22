#include <unity.h>
#include "ParkProtocol.h"

void setUp(void) {}
void tearDown(void) {}

void test_get_string_from_flat_json(void) {
  std::string body = "{\"result\":\"OK\",\"free\":4,\"id\":\"ZH123456\"}";
  TEST_ASSERT_EQUAL_STRING("OK", jsonGetString(body, "result", "?").c_str());
  TEST_ASSERT_EQUAL_STRING("ZH123456", jsonGetString(body, "id", "?").c_str());
}

void test_get_string_tolerates_spaces(void) {
  std::string body = "{ \"result\" : \"FULL\" }";
  TEST_ASSERT_EQUAL_STRING("FULL", jsonGetString(body, "result", "?").c_str());
}

void test_get_string_missing_key_gives_fallback(void) {
  TEST_ASSERT_EQUAL_STRING("?", jsonGetString("{\"a\":\"b\"}", "x", "?").c_str());
  TEST_ASSERT_EQUAL_STRING("?", jsonGetString("", "x", "?").c_str());
}

void test_get_string_unterminated_gives_fallback(void) {
  TEST_ASSERT_EQUAL_STRING("?", jsonGetString("{\"result\":\"OK", "result", "?").c_str());
}

void test_get_string_handles_escapes(void) {
  std::string body = "{\"msg\":\"a\\\"b\\\\c\"}";
  TEST_ASSERT_EQUAL_STRING("a\"b\\c", jsonGetString(body, "msg", "?").c_str());
}

void test_get_int(void) {
  std::string body = "{\"free\":4,\"capacity\":5,\"neg\":-3}";
  TEST_ASSERT_EQUAL_INT32(4, jsonGetInt(body, "free", -1));
  TEST_ASSERT_EQUAL_INT32(5, jsonGetInt(body, "capacity", -1));
  TEST_ASSERT_EQUAL_INT32(-3, jsonGetInt(body, "neg", 0));
}

void test_get_int_missing_or_not_a_number_gives_fallback(void) {
  TEST_ASSERT_EQUAL_INT32(-1, jsonGetInt("{\"free\":\"x\"}", "free", -1));
  TEST_ASSERT_EQUAL_INT32(-1, jsonGetInt("{}", "free", -1));
}

void test_json_escape(void) {
  TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\n", jsonEscape("a\"b\\c\n").c_str());
  TEST_ASSERT_EQUAL_STRING("\\u0001", jsonEscape("\x01").c_str());
}

void test_url_encode(void) {
  TEST_ASSERT_EQUAL_STRING("ZH123456", urlEncode("ZH123456").c_str());
  TEST_ASSERT_EQUAL_STRING("a%20b%26c", urlEncode("a b&c").c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_get_string_from_flat_json);
  RUN_TEST(test_get_string_tolerates_spaces);
  RUN_TEST(test_get_string_missing_key_gives_fallback);
  RUN_TEST(test_get_string_unterminated_gives_fallback);
  RUN_TEST(test_get_string_handles_escapes);
  RUN_TEST(test_get_int);
  RUN_TEST(test_get_int_missing_or_not_a_number_gives_fallback);
  RUN_TEST(test_json_escape);
  RUN_TEST(test_url_encode);
  return UNITY_END();
}

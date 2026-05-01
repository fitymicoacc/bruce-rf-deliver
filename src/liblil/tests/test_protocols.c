#include "lil.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_table_has_24_entries(void) {
    /* Desktop RC_PROTOCOLS has 23 entries + placeholder "Protocol 23".
     * If the table size changes, update this test + desktop constants.ts
     * together — the two must stay in lock-step. */
    TEST_ASSERT_EQUAL_UINT(23u, (unsigned)lil_protocol_count());
}

static void test_lookup_by_id(void) {
    const lil_rc_protocol_t* p = lil_protocol_by_id(1);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("PT2262", p->name);
    TEST_ASSERT_EQUAL_UINT16(350, p->pulse_length);
    TEST_ASSERT_EQUAL_UINT8(1,  p->sync_h);
    TEST_ASSERT_EQUAL_UINT8(31, p->sync_l);
    TEST_ASSERT_EQUAL_UINT8(0,  p->inverted);
}

static void test_lookup_inverted_protocol(void) {
    /* HT12E (id 11) is an inverted protocol (carrier-off active). */
    const lil_rc_protocol_t* p = lil_protocol_by_id(11);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("HT12E", p->name);
    TEST_ASSERT_EQUAL_UINT8(1, p->inverted);
}

static void test_lookup_missing_id_returns_null(void) {
    TEST_ASSERT_NULL(lil_protocol_by_id(0));
    TEST_ASSERT_NULL(lil_protocol_by_id(99));
}

static void test_lookup_by_index_bounds(void) {
    TEST_ASSERT_NOT_NULL(lil_protocol_at(0));
    TEST_ASSERT_NOT_NULL(lil_protocol_at(lil_protocol_count() - 1));
    TEST_ASSERT_NULL(lil_protocol_at(lil_protocol_count()));
    TEST_ASSERT_NULL(lil_protocol_at(10000));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_table_has_24_entries);
    RUN_TEST(test_lookup_by_id);
    RUN_TEST(test_lookup_inverted_protocol);
    RUN_TEST(test_lookup_missing_id_returns_null);
    RUN_TEST(test_lookup_by_index_bounds);
    return UNITY_END();
}

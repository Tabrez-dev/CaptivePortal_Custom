#include "unity.h"
#include "aws_iot.h"

void setUp(void) {
    // Setup code here
}

void tearDown(void) {
    // Cleanup code here
}

void test_aws_iot_init(void) {
    TEST_ASSERT_EQUAL(ESP_OK, aws_iot_init());
}

void test_aws_iot_publish(void) {
    TEST_ASSERT_EQUAL(ESP_OK, aws_iot_publish("test/topic", "test message"));
}

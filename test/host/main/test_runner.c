// Runs every registered TEST_CASE and exits with the failure count.
#include <stdlib.h>
#include "unity.h"

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    exit(UNITY_END());
}

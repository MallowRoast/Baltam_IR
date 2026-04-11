#include "m_script_test_support.h"

using namespace baltam::test_support;

int main() {
    return run_runtime_test("test_test7_short", [] {
        execute_no_output_test_script("test7_short");
    });
}

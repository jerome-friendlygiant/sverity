#include "testing.h"
std::vector<TestCase>& allTests() { static std::vector<TestCase> t; return t; }
int g_failures = 0;
int main() {
    int failedTests = 0;
    for (auto& t : allTests()) {
        int before = g_failures;
        t.fn();
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
        if (g_failures != before) failedTests++;
    }
    std::printf("%zu tests, %d failed\n", allTests().size(), failedTests);
    return failedTests ? 1 : 0;
}

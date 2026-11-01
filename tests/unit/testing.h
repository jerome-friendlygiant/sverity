// Tiny dependency-free test harness (keeps the build offline-reproducible).
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase { const char* name; std::function<void()> fn; };
std::vector<TestCase>& allTests();
extern int g_failures;
struct Registrar { Registrar(const char* n, std::function<void()> f) { allTests().push_back({n, std::move(f)}); } };

#define TEST(name) static void name(); static Registrar reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { std::printf("  CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::printf("  CHECK_EQ FAILED %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); g_failures++; } } while (0)

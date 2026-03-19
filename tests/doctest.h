// Minimal vendored doctest-like single-header for sfd_tool tests.
// This is a tiny stub providing TEST_CASE/CHECK/CHECK_FALSE and a main().
// If you need full doctest features later, replace this file with the
// official doctest.h from https://github.com/doctest/doctest.

#ifndef SFD_TOOL_MINI_DOCTEST_H
#define SFD_TOOL_MINI_DOCTEST_H

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <functional>

struct doctest_test_case_reg {
    using func_t = void(*)();
    func_t f;
    const char* name;
};

inline std::vector<doctest_test_case_reg>& doctest_registry() {
    static std::vector<doctest_test_case_reg> tests;
    return tests;
}

inline void doctest_register(doctest_test_case_reg::func_t f, const char* name) {
    doctest_registry().push_back({f, name});
}

#define DOCTEST_CAT_IMPL(s1, s2) s1##s2
#define DOCTEST_CAT(s1, s2) DOCTEST_CAT_IMPL(s1, s2)

#define TEST_CASE(name) \
    static void DOCTEST_CAT(_doctest_tc_, __LINE__)(); \
    static struct DOCTEST_CAT(_doctest_reg_, __LINE__) { \
        DOCTEST_CAT(_doctest_reg_, __LINE__)() { doctest_register(&DOCTEST_CAT(_doctest_tc_, __LINE__), name); } \
    } DOCTEST_CAT(_doctest_reg_inst_, __LINE__); \
    static void DOCTEST_CAT(_doctest_tc_, __LINE__)()

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
    } \
} while (0)

#define CHECK_FALSE(expr) CHECK(!(expr))

#define REQUIRE(expr) do { \
    if (!(expr)) { \
        std::cerr << "REQUIRE failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while (0)

int main() {
    auto& tests = doctest_registry();
    int failures = 0;
    for (const auto& tc : tests) {
        try {
            tc.f();
        } catch (...) {
            std::cerr << "Unhandled exception in test: " << tc.name << "\n";
            ++failures;
        }
    }
    if (failures) {
        std::cerr << failures << " test(s) reported failures" << std::endl;
    }
    return failures ? 1 : 0;
}

#endif // SFD_TOOL_MINI_DOCTEST_H

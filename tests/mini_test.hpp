// A tiny dependency-free test harness. Not a general-purpose framework —
// just enough structure to keep this project's tests organized and give
// clear pass/fail output, without pulling in gtest/catch2 as a build dependency.
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mini_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct AssertionFailure : std::runtime_error {
    explicit AssertionFailure(const std::string& msg) : std::runtime_error(msg) {}
};

inline int run_all() {
    int passed = 0, failed = 0;
    for (auto& t : registry()) {
        try {
            t.fn();
            std::cout << "[ PASS ] " << t.name << '\n';
            ++passed;
        } catch (const AssertionFailure& e) {
            std::cout << "[ FAIL ] " << t.name << " -- " << e.what() << '\n';
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[ FAIL ] " << t.name << " -- unexpected exception: " << e.what() << '\n';
            ++failed;
        } catch (...) {
            std::cout << "[ FAIL ] " << t.name << " -- unknown exception\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << registry().size() << " total\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace mini_test

#define MTP_CONCAT_INNER(a, b) a##b
#define MTP_CONCAT(a, b) MTP_CONCAT_INNER(a, b)

#define TEST(name)                                                        \
    void MTP_CONCAT(test_fn_, __LINE__)();                                \
    static mini_test::Registrar MTP_CONCAT(test_reg_, __LINE__)(          \
        name, MTP_CONCAT(test_fn_, __LINE__));                           \
    void MTP_CONCAT(test_fn_, __LINE__)()

#define ASSERT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::ostringstream oss;                                        \
            oss << "ASSERT_TRUE failed: " #cond << " (" << __FILE__ << ":" \
                << __LINE__ << ")";                                        \
            throw mini_test::AssertionFailure(oss.str());                  \
        }                                                                   \
    } while (0)

#define ASSERT_EQ(a, b)                                                    \
    do {                                                                   \
        auto va = (a);                                                     \
        auto vb = (b);                                                     \
        if (!(va == vb)) {                                                 \
            std::ostringstream oss;                                        \
            oss << "ASSERT_EQ failed: " #a " != " #b " (" << va << " vs "  \
                << vb << ") at " << __FILE__ << ":" << __LINE__;           \
            throw mini_test::AssertionFailure(oss.str());                  \
        }                                                                   \
    } while (0)

#define ASSERT_THROWS(expr, exception_type)                                \
    do {                                                                   \
        bool threw = false;                                                \
        try {                                                              \
            (expr);                                                        \
        } catch (const exception_type&) {                                  \
            threw = true;                                                  \
        }                                                                   \
        if (!threw) {                                                      \
            std::ostringstream oss;                                        \
            oss << "ASSERT_THROWS failed: " #expr " did not throw "        \
                << #exception_type << " at " << __FILE__ << ":" << __LINE__;\
            throw mini_test::AssertionFailure(oss.str());                  \
        }                                                                   \
    } while (0)

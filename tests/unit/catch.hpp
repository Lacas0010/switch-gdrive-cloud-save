#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <functional>
#include <chrono>
#include <exception>
#include <cmath>

namespace Catch {
    struct TestFailureException : public std::exception {};

    struct TestCaseInfo {
        std::string name;
        std::string tag;
        std::function<void()> func;
    };

    class Registry {
    public:
        static Registry& instance() {
            static Registry reg;
            return reg;
        }

        void registerTest(const std::string& name, const std::string& tag, std::function<void()> func) {
            tests.push_back({name, tag, func});
        }

        int runAll() {
            int passedTests = 0;
            int failedTests = 0;
            int totalAssertions = 0;
            int failedAssertions = 0;

            std::cout << "===============================================================================\n";
            std::cout << "Running Catch2 Test Suite (Unit Tests)\n";
            std::cout << "===============================================================================\n";

            for (const auto& test : tests) {
                currentTestName = test.name;
                currentSection = "";
                testFailed = false;
                std::cout << "[ RUN      ] " << test.name << " " << test.tag << "\n";
                auto start = std::chrono::high_resolution_clock::now();

                try {
                    test.func();
                } catch (const TestFailureException&) {
                    testFailed = true;
                } catch (const std::exception& e) {
                    std::cout << "  [EXCEPTION] Unexpected std::exception: " << e.what() << "\n";
                    testFailed = true;
                    assertionFailures++;
                } catch (...) {
                    std::cout << "  [EXCEPTION] Unknown non-standard exception thrown.\n";
                    testFailed = true;
                    assertionFailures++;
                }

                auto end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(end - start).count();

                if (testFailed) {
                    failedTests++;
                    std::cout << "[  FAILED  ] " << test.name << " (" << ms << " ms)\n";
                } else {
                    passedTests++;
                    std::cout << "[       OK ] " << test.name << " (" << ms << " ms)\n";
                }
            }

            totalAssertions = assertionPasses + assertionFailures;
            std::cout << "===============================================================================\n";
            std::cout << "test cases: " << tests.size() << " | "
                      << passedTests << " passed | "
                      << failedTests << " failed\n";
            std::cout << "assertions: " << totalAssertions << " | "
                      << assertionPasses << " passed | "
                      << assertionFailures << " failed\n";
            std::cout << "===============================================================================\n";

            return failedTests == 0 ? 0 : 1;
        }

        std::string currentTestName;
        std::string currentSection;
        bool testFailed = false;
        int assertionPasses = 0;
        int assertionFailures = 0;

    private:
        std::vector<TestCaseInfo> tests;
    };

    struct AutoRegister {
        AutoRegister(const std::string& name, const std::string& tag, std::function<void()> func) {
            Registry::instance().registerTest(name, tag, func);
        }
    };

    inline void recordAssertion(bool ok, const char* expr, const char* file, int line, bool fatal) {
        if (ok) {
            Registry::instance().assertionPasses++;
        } else {
            Registry::instance().assertionFailures++;
            Registry::instance().testFailed = true;
            std::cout << "  [FAILED] " << file << ":" << line << "\n"
                      << "    FAILED REQUIRE( " << expr << " )\n";
            if (!Registry::instance().currentSection.empty()) {
                std::cout << "    in section: \"" << Registry::instance().currentSection << "\"\n";
            }
            if (fatal) {
                throw TestFailureException();
            }
        }
    }

    struct SectionGuard {
        std::string name;
        SectionGuard(const std::string& n) : name(n) {
            Registry::instance().currentSection = n;
        }
        ~SectionGuard() {
            Registry::instance().currentSection = "";
        }
        operator bool() const { return true; }
    };
}

#define CATCH_INTERNAL_CONCAT2(a, b) a##b
#define CATCH_INTERNAL_CONCAT(a, b) CATCH_INTERNAL_CONCAT2(a, b)

#define TEST_CASE(name, tags) \
    static void CATCH_INTERNAL_CONCAT(test_case_func_, __LINE__)(); \
    static Catch::AutoRegister CATCH_INTERNAL_CONCAT(auto_reg_, __LINE__)(name, tags, CATCH_INTERNAL_CONCAT(test_case_func_, __LINE__)); \
    static void CATCH_INTERNAL_CONCAT(test_case_func_, __LINE__)()

#define SECTION(name) \
    if (Catch::SectionGuard CATCH_INTERNAL_CONCAT(sec_guard_, __LINE__) = Catch::SectionGuard(name))

#define REQUIRE(expr) \
    Catch::recordAssertion(static_cast<bool>(expr), #expr, __FILE__, __LINE__, true)

#define CHECK(expr) \
    Catch::recordAssertion(static_cast<bool>(expr), #expr, __FILE__, __LINE__, false)

#define REQUIRE_FALSE(expr) \
    Catch::recordAssertion(!static_cast<bool>(expr), "!(" #expr ")", __FILE__, __LINE__, true)

#define CHECK_FALSE(expr) \
    Catch::recordAssertion(!static_cast<bool>(expr), "!(" #expr ")", __FILE__, __LINE__, false)

#ifdef CATCH_CONFIG_MAIN
int main(int argc, char* argv[]) {
    return Catch::Registry::instance().runAll();
}
#endif

#pragma once
#include <string>
#include <string_view>

namespace Utils {

    class Logger {
    public:
        static void init();
        static void log(std::string_view tag, std::string_view message);
        static void logSync(uint64_t titleId, std::string_view message);
        static void logRestore(uint64_t titleId, std::string_view message);
    };

} // namespace Utils

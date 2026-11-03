#include "time_util.hpp"
#include <cstdio>
#include <cstring>

namespace Utils {

    // Algoritmo civil puro de Howard Hinnant para dias UTC a partir de 1970-01-01
    static inline int64_t daysFromCivilUtc(int y, unsigned m, unsigned d) noexcept {
        y -= (m <= 2);
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
    }

    std::time_t TimeUtil::parseIso8601ToUnixTime(std::string_view iso) {
        if (iso.empty()) return 0;

        int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
        std::string str(iso);

        if (std::sscanf(str.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) < 6) {
            return 0;
        }

        if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
            hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
            return 0;
        }

        int64_t days = daysFromCivilUtc(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
        int64_t epochSeconds = days * 86400LL + hour * 3600LL + min * 60LL + sec;

        size_t tzPos = str.find_first_of("+-", 19);
        if (tzPos != std::string::npos && tzPos + 5 < str.size()) {
            char sign = str[tzPos];
            int tzHour = 0, tzMin = 0;
            if (std::sscanf(str.c_str() + tzPos + 1, "%d:%d", &tzHour, &tzMin) >= 1) {
                int offsetSeconds = tzHour * 3600 + tzMin * 60;
                if (sign == '+') {
                    epochSeconds -= offsetSeconds;
                } else if (sign == '-') {
                    epochSeconds += offsetSeconds;
                }
            }
        }

        return static_cast<std::time_t>(epochSeconds);
    }

    std::string TimeUtil::formatUtcDateTime(std::time_t epochSeconds) {
        if (epochSeconds <= 0) return "Nunca";

        struct tm ti;
#if defined(_WIN32)
        gmtime_s(&ti, &epochSeconds);
#else
        gmtime_r(&epochSeconds, &ti);
#endif

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d UTC",
                      ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
                      ti.tm_hour, ti.tm_min);
        return std::string(buf);
    }

    std::time_t TimeUtil::getNowUtc() noexcept {
        return std::time(nullptr);
    }

} // namespace Utils

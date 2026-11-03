#include "catch.hpp"
#include "switch_mock.hpp"
#include <ctime>
#include <string>
#include <cstdint>

// Reutiliza o algoritmo Howard Hinnant (estritamente UTC) do GDriveClient
static inline int64_t daysFromCivilUtc(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
}

static std::time_t parseIso8601ToUnixTime(const std::string& iso) {
    if (iso.empty()) return 0;

    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) < 6) {
        return 0;
    }

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
        return 0;
    }

    int64_t days = daysFromCivilUtc(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int64_t epochSeconds = days * 86400LL + hour * 3600LL + min * 60LL + sec;

    // Offset explicito (+HH:MM ou -HH:MM)
    size_t tzPos = iso.find_first_of("+-", 19);
    if (tzPos != std::string::npos && tzPos + 5 < iso.size()) {
        char sign = iso[tzPos];
        int tzHour = 0, tzMin = 0;
        if (sscanf(iso.c_str() + tzPos + 1, "%d:%d", &tzHour, &tzMin) >= 1) {
            int offsetSeconds = tzHour * 3600 + tzMin * 60;
            if (sign == '+') {
                epochSeconds -= offsetSeconds; // Normaliza para UTC
            } else if (sign == '-') {
                epochSeconds += offsetSeconds; // Normaliza para UTC
            }
        }
    }

    return static_cast<std::time_t>(epochSeconds);
}

// 01/01/2025 00:00:00 UTC = 1735689600
constexpr std::time_t MIN_SANE_RTC_TIMESTAMP = 1735689600;

static inline bool isTimestampSane(std::time_t ts) {
    return (ts >= MIN_SANE_RTC_TIMESTAMP);
}

static bool shouldAllowAutomaticRestore(std::time_t localClock, std::time_t cloudTimestamp) {
    // Trava de Seguranca: se o RTC do Switch estiver corrompido ou resetado (bateria zerada < 2025),
    // bloqueia sobrescrita automatica imediatamente para nao corromper nem apagar saves
    if (!isTimestampSane(localClock)) {
        return false;
    }
    return cloudTimestamp > localClock;
}

TEST_CASE("Timestamp Engine - RFC 3339 / ISO 8601 Precise UTC Parsing", "[timestamp]") {
    SECTION("Standard UTC timestamp with 'Z' suffix") {
        std::string tsStr = "2026-11-03T10:15:30Z";
        std::time_t epoch = parseIso8601ToUnixTime(tsStr);
        REQUIRE(epoch > 0);

        // 2026-11-03 10:15:30 UTC:
        // Days to 2026-11-03 = 20760. 20760 * 86400 + 10*3600 + 15*60 + 30 = 1793700930
        REQUIRE(epoch == 1793700930);
    }

    SECTION("Fractional seconds (.123Z) in Google Drive RFC 3339 format") {
        std::string tsStr = "2026-11-03T10:15:30.123Z";
        std::time_t epoch = parseIso8601ToUnixTime(tsStr);
        // Subsegundos sao truncados para precisao de 1 segundo em time_t (padrao posix)
        REQUIRE(epoch == 1793700930);
    }

    SECTION("Explicit positive timezone offset (+02:00)") {
        // 10:15:30 +02:00 equivale a 08:15:30 UTC
        std::string tsStr = "2026-11-03T10:15:30+02:00";
        std::time_t epoch = parseIso8601ToUnixTime(tsStr);
        std::time_t expectedUtc = 1793700930 - (2 * 3600);
        REQUIRE(epoch == expectedUtc);
    }

    SECTION("Explicit negative timezone offset (-03:00 Brasilia / Buenos Aires)") {
        // 10:15:30 -03:00 equivale a 13:15:30 UTC
        std::string tsStr = "2026-11-03T10:15:30-03:00";
        std::time_t epoch = parseIso8601ToUnixTime(tsStr);
        std::time_t expectedUtc = 1793700930 + (3 * 3600);
        REQUIRE(epoch == expectedUtc);
    }

    SECTION("Subseconds with explicit negative offset (-03:00)") {
        std::string tsStr = "2026-11-03T10:15:30.456-03:00";
        std::time_t epoch = parseIso8601ToUnixTime(tsStr);
        std::time_t expectedUtc = 1793700930 + (3 * 3600);
        REQUIRE(epoch == expectedUtc);
    }

    SECTION("Leap year handling (2024-02-29)") {
        std::string leapStr = "2024-02-29T12:00:00Z";
        std::time_t epoch = parseIso8601ToUnixTime(leapStr);
        REQUIRE(epoch == 1709208000);
    }

    SECTION("Invalid and corrupt inputs") {
        REQUIRE(parseIso8601ToUnixTime("") == 0);
        REQUIRE(parseIso8601ToUnixTime("invalid-date-string") == 0);
        REQUIRE(parseIso8601ToUnixTime("1969-12-31T23:59:59Z") == 0); // Before Epoch
        REQUIRE(parseIso8601ToUnixTime("2026-13-01T10:00:00Z") == 0); // Invalid month 13
        REQUIRE(parseIso8601ToUnixTime("2026-05-32T10:00:00Z") == 0); // Invalid day 32
        REQUIRE(parseIso8601ToUnixTime("2026-05-10T25:00:00Z") == 0); // Invalid hour 25
    }
}

TEST_CASE("Timestamp Engine - RTC Clock Sanity Lock (MIN_SANE_RTC_TIMESTAMP)", "[sanity_rtc]") {
    SECTION("Reset battery / zeroed clock (1970) is rejected") {
        std::time_t epochZero = 0; // 1970-01-01
        REQUIRE_FALSE(isTimestampSane(epochZero));
    }

    SECTION("Year 2000 reset timestamp is rejected") {
        std::time_t y2k = 946684800; // 2000-01-01
        REQUIRE_FALSE(isTimestampSane(y2k));
    }

    SECTION("Year 2024 timestamp (< 2025 threshold) is rejected") {
        std::time_t late2024 = 1735689599; // 2024-12-31 23:59:59 UTC
        REQUIRE_FALSE(isTimestampSane(late2024));
    }

    SECTION("Boundary timestamp (2025-01-01 00:00:00 UTC) is accepted") {
        std::time_t threshold = MIN_SANE_RTC_TIMESTAMP;
        REQUIRE(isTimestampSane(threshold));
    }

    SECTION("Current / future valid timestamp (2026) is accepted") {
        std::time_t year2026 = 1793700930;
        REQUIRE(isTimestampSane(year2026));
    }

    SECTION("Automatic restore block guard prevents overwriting when RTC is invalid") {
        std::time_t cloudTimestamp = 1793700930; // Nov 2026 in cloud
        std::time_t deadBatteryClock = 0;        // Jan 1970 local

        // Cloud is mathematically > Local, BUT local clock is insane (< 2025).
        // Must immediately block restore to avoid disaster!
        bool allowed = shouldAllowAutomaticRestore(deadBatteryClock, cloudTimestamp);
        REQUIRE_FALSE(allowed);

        // When clock is sane (e.g. 2026-01-01) and cloud is newer:
        std::time_t validLocalClock = 1767225600; // 2026-01-01
        REQUIRE(shouldAllowAutomaticRestore(validLocalClock, cloudTimestamp) == true);
    }
}

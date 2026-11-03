#pragma once
#include <ctime>
#include <string>
#include <string_view>
#include <cstdint>

namespace Utils {

    class TimeUtil {
    public:
        // Limite mínimo de sanidade do relógio do Switch: 01/01/2025 00:00:00 UTC
        static constexpr std::time_t MIN_SANE_RTC_TIMESTAMP = 1735689600;

        // Converte string RFC 3339 / ISO 8601 para timestamp Unix UTC em segundos
        static std::time_t parseIso8601ToUnixTime(std::string_view iso);

        // Formata epoch seconds em data legível UTC ("DD/MM/YYYY HH:MM:SS UTC")
        static std::string formatUtcDateTime(std::time_t epochSeconds);

        // Verifica se um timestamp é plausível (ano >= 2025)
        static bool isTimestampSane(std::time_t ts) noexcept {
            return (ts >= MIN_SANE_RTC_TIMESTAMP);
        }

        // Obtém o timestamp UTC atual do sistema
        static std::time_t getNowUtc() noexcept;
    };

} // namespace Utils

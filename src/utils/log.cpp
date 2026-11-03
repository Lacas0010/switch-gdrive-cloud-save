#include "log.hpp"
#include <cstdio>
#include <ctime>
#include <mutex>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR_P(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR_P(p) mkdir(p, 0777)
#endif

namespace Utils {

    static std::mutex s_logMutex;
    static constexpr const char* LOG_PATH = "sdmc:/config/gdrive-sync/sync.log";
    static constexpr const char* RESTORE_LOG_PATH = "sdmc:/config/gdrive-sync/restore.log";

    void Logger::init() {
        MKDIR_P("sdmc:/config");
        MKDIR_P("sdmc:/config/gdrive-sync");
    }

    static void writeLogEntry(const char* filePath, std::string_view tag, uint64_t titleId, std::string_view message) {
        std::lock_guard<std::mutex> lock(s_logMutex);
        FILE* fp = std::fopen(filePath, "a");
        if (!fp) return;

        std::time_t now = std::time(nullptr);
        struct tm ti;
#if defined(_WIN32)
        gmtime_s(&ti, &now);
#else
        gmtime_r(&now, &ti);
#endif

        char timeBuf[32];
        std::snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
                      ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);

        if (titleId > 0) {
            std::fprintf(fp, "[%s] [%.*s] [%016llX] %.*s\n",
                         timeBuf, static_cast<int>(tag.size()), tag.data(),
                         static_cast<unsigned long long>(titleId),
                         static_cast<int>(message.size()), message.data());
        } else {
            std::fprintf(fp, "[%s] [%.*s] %.*s\n",
                         timeBuf, static_cast<int>(tag.size()), tag.data(),
                         static_cast<int>(message.size()), message.data());
        }

        std::fclose(fp);
    }

    void Logger::log(std::string_view tag, std::string_view message) {
        writeLogEntry(LOG_PATH, tag, 0, message);
    }

    void Logger::logSync(uint64_t titleId, std::string_view message) {
        writeLogEntry(LOG_PATH, "SYNC", titleId, message);
    }

    void Logger::logRestore(uint64_t titleId, std::string_view message) {
        writeLogEntry(RESTORE_LOG_PATH, "RESTORE", titleId, message);
    }

} // namespace Utils

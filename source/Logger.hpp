#pragma once
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <switch.h>
#include "Config.hpp"

namespace Logger {
    inline void logSync(const char* fmt, ...) {
        FILE* f = fopen(Config::SYNC_LOG_FILE.c_str(), "a");
        if (!f) return;

        time_t rawtime;
        time(&rawtime);
        struct tm* timeinfo = localtime(&rawtime);
        char timeBuf[32] = "[0000-00-00 00:00:00]";
        if (timeinfo) {
            strftime(timeBuf, sizeof(timeBuf), "[%Y-%m-%d %H:%M:%S]", timeinfo);
        }

        fprintf(f, "%s ", timeBuf);

        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);

        fprintf(f, "\n");
        fflush(f);
        fclose(f);
    }
}


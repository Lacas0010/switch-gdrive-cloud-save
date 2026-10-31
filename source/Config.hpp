#pragma once
#include <switch.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace Config {
    constexpr u64 SYSMODULE_TITLE_ID = 0x4200000000000010ULL;

    inline const std::string BASE_DIR            = "sdmc:/config/gdrive-sync";
    inline const std::string TEMP_DIR            = "sdmc:/config/gdrive-sync/temp";
    inline const std::string PENDING_DIR         = "sdmc:/config/gdrive-sync/pending";
    inline const std::string BACKUP_DIR          = "sdmc:/config/gdrive-sync/backups";
    inline const std::string TIMESTAMPS_FILE     = "sdmc:/config/gdrive-sync/timestamps.json";
    inline const std::string SYNC_LOG_FILE       = "sdmc:/config/gdrive-sync/sync.log";
    inline const std::string RESTORE_LOG_FILE    = "sdmc:/config/gdrive-sync/restore.log";
    inline const std::string CONFIG_FILE         = "sdmc:/config/gdrive-sync/config.json";
    inline const std::string AUTH_FILE           = "sdmc:/config/gdrive-sync/auth_request.txt";
    inline const std::string STATE_FILE          = "sdmc:/config/gdrive-sync/state.json";
    inline const std::string CA_BUNDLE_1         = "sdmc:/config/gdrive-sync/cacert.pem";
    inline const std::string CA_BUNDLE_2         = "sdmc:/switch/cacert.pem";
    inline const std::string DEFAULT_FOLDER_NAME = "Switch Saves";

    inline const std::string SYSMODULE_DIR       = "sdmc:/atmosphere/contents/4200000000000010";
    inline const std::string BOOT2_FLAG_PATH     = "sdmc:/atmosphere/contents/4200000000000010/flags/boot2.flag";

    inline void ensureDirectories() {
        mkdir("sdmc:/config", 0777);
        mkdir(BASE_DIR.c_str(), 0777);
        mkdir(TEMP_DIR.c_str(), 0777);
        mkdir(PENDING_DIR.c_str(), 0777);
        mkdir(BACKUP_DIR.c_str(), 0777);
    }

    inline std::string getCaBundlePath() {
        struct stat st;
        if (stat(CA_BUNDLE_1.c_str(), &st) == 0) {
            return CA_BUNDLE_1;
        }
        if (stat(CA_BUNDLE_2.c_str(), &st) == 0) {
            return CA_BUNDLE_2;
        }
        return CA_BUNDLE_1;
    }

    inline bool isBootFlagPresent() {
        struct stat st;
        return (stat(BOOT2_FLAG_PATH.c_str(), &st) == 0);
    }

    inline void setBootFlag(bool enable) {
        if (enable) {
            mkdir("sdmc:/atmosphere", 0777);
            mkdir("sdmc:/atmosphere/contents", 0777);
            mkdir(SYSMODULE_DIR.c_str(), 0777);
            mkdir((SYSMODULE_DIR + "/flags").c_str(), 0777);
            FILE* f = fopen(BOOT2_FLAG_PATH.c_str(), "w");
            if (f) fclose(f);
        } else {
            remove(BOOT2_FLAG_PATH.c_str());
        }
    }
}

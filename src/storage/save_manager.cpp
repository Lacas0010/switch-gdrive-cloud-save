#include "save_manager.hpp"
#include "zip_util.hpp"
#include "../utils/log.hpp"
#include "../utils/time_util.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR_M(p) _mkdir(p)
#else
#define MKDIR_M(p) mkdir(p, 0777)
#endif

namespace Storage {

    SaveManager::SaveManager()
        : m_timestampsPath("sdmc:/config/gdrive-sync/timestamps.json") {
        loadTimestamps();
    }

    SaveManager::~SaveManager() {
        saveTimestamps();
    }

    std::string SaveManager::formatUid(const AccountUid& uid) {
        char buf[33];
        std::snprintf(buf, sizeof(buf), "%016llX%016llX",
                      static_cast<unsigned long long>(uid.uid[0]),
                      static_cast<unsigned long long>(uid.uid[1]));
        return std::string(buf);
    }

    AccountUid SaveManager::parseUid(std::string_view str) {
        AccountUid uid{};
        if (str.length() >= 32) {
            std::string s(str);
            std::string hiStr = s.substr(0, 16);
            std::string loStr = s.substr(16, 16);
            uid.uid[0] = std::strtoull(hiStr.c_str(), nullptr, 16);
            uid.uid[1] = std::strtoull(loStr.c_str(), nullptr, 16);
        }
        return uid;
    }

    bool SaveManager::getActiveAccount(AccountUid& outUid) {
#ifdef __SWITCH__
        Result rc = accountInitialize(AccountServiceType_Application);
        if (R_FAILED(rc)) {
            rc = accountInitialize(AccountServiceType_System);
            if (R_FAILED(rc)) {
                rc = accountInitialize(AccountServiceType_Administrator);
                if (R_FAILED(rc)) return false;
            }
        }
        auto accountGuard = Core::makeScopeGuard([]() {
            accountExit();
        });

        AccountUid uid{};
        rc = accountGetPreselectedUser(&uid);
        if (R_SUCCEEDED(rc) && accountUidIsValid(&uid)) {
            outUid = uid;
            return true;
        }

        rc = accountGetLastOpenedUser(&uid);
        if (R_SUCCEEDED(rc) && accountUidIsValid(&uid)) {
            outUid = uid;
            return true;
        }

        s32 userCount = 0;
        AccountUid uids[8];
        rc = accountListAllUsers(uids, 8, &userCount);
        if (R_SUCCEEDED(rc) && userCount > 0) {
            outUid = uids[0];
            return true;
        }
        return false;
#else
        outUid.uid[0] = 0x0123456789ABCDEFULL;
        outUid.uid[1] = 0xFEDCBA9876543210ULL;
        return true;
#endif
    }

    std::vector<GameSaveInfo> SaveManager::listAvailableSaves(const AccountUid& uid) {
        std::vector<GameSaveInfo> results;
#ifdef __SWITCH__
        FsSaveDataInfoReader reader;
        Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
        if (R_FAILED(rc)) {
            return results;
        }
        auto readerGuard = Core::makeScopeGuard([&reader]() {
            fsSaveDataInfoReaderClose(&reader);
        });

        FsSaveDataInfo infoBuffer[32];
        s64 totalRead = 0;

        while (true) {
            s64 count = 0;
            rc = fsSaveDataInfoReaderRead(&reader, infoBuffer, 32, &count);
            if (R_FAILED(rc) || count == 0) break;

            for (s64 i = 0; i < count; ++i) {
                const auto& item = infoBuffer[i];
                if (item.save_data_type != FsSaveDataType_Account) {
                    continue;
                }

                if (item.uid.uid[0] == uid.uid[0] && item.uid.uid[1] == uid.uid[1]) {
                    GameSaveInfo info;
                    info.titleId = item.application_id;
                    info.uid = uid;
                    info.uidStr = formatUid(uid);
                    info.lastModified = getLocalSaveTimestamp(info.titleId, uid);
                    results.push_back(info);
                }
            }
            totalRead += count;
        }
#else
        // Mock de teste de desenvolvimento
        GameSaveInfo s1;
        s1.titleId = 0x01006F8002326000ULL; // Super Smash Bros
        s1.uid = uid;
        s1.uidStr = formatUid(uid);
        s1.lastModified = 1762164930;
        results.push_back(s1);

        GameSaveInfo s2;
        s2.titleId = 0x0100000000010000ULL; // Super Mario Odyssey
        s2.uid = uid;
        s2.uidStr = formatUid(uid);
        s2.lastModified = 1762163000;
        results.push_back(s2);
#endif
        return results;
    }

    std::time_t SaveManager::getLocalSaveTimestamp(u64 titleId, const AccountUid& uid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        (void)uid;
        auto it = m_timestamps.find(titleId);
        if (it != m_timestamps.end()) {
            return it->second;
        }
        return 0;
    }

    void SaveManager::setLocalSaveTimestamp(u64 titleId, std::time_t ts) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timestamps[titleId] = ts;
        saveTimestamps();
    }

    void SaveManager::loadTimestamps() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timestamps.clear();

        auto fp = Core::openFile(m_timestampsPath.c_str(), "r");
        if (!fp) return;

        char line[256];
        while (std::fgets(line, sizeof(line), fp.get())) {
            unsigned long long tid = 0;
            long long ts = 0;
            if (std::sscanf(line, " \"%llx\": %lld", &tid, &ts) == 2 ||
                std::sscanf(line, " \"%llX\": %lld", &tid, &ts) == 2) {
                m_timestamps[tid] = static_cast<std::time_t>(ts);
            }
        }
    }

    void SaveManager::saveTimestamps() {
        MKDIR_M("sdmc:/config");
        MKDIR_M("sdmc:/config/gdrive-sync");

        auto fp = Core::openFile(m_timestampsPath.c_str(), "w");
        if (!fp) return;

        std::fprintf(fp.get(), "{\n");
        size_t idx = 0;
        for (const auto& [tid, ts] : m_timestamps) {
            std::fprintf(fp.get(), "  \"%016llX\": %lld%s\n",
                         static_cast<unsigned long long>(tid),
                         static_cast<long long>(ts),
                         (idx + 1 < m_timestamps.size() ? "," : ""));
            idx++;
        }
        std::fprintf(fp.get(), "}\n");
    }

    TransactionalRestoreResult SaveManager::restoreSaveFromZip(
        u64 titleId,
        const AccountUid& uid,
        std::string_view localZipPath,
        const std::atomic<bool>* cancelToken
    ) {
        TransactionalRestoreResult res;
        u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;

        // 1. Validacao do arquivo zip baixado
        struct stat st;
        std::string zipStr(localZipPath);
        if (stat(zipStr.c_str(), &st) != 0 || st.st_size == 0) {
            res.success = false;
            res.errorMessage = "Arquivo zip de restauracao vazio ou inexistente.";
            Utils::Logger::logRestore(baseTitleId, res.errorMessage);
            return res;
        }

        u32 checkCrc = 0;
        if (!ZipUtil::calculateFileCrc32(localZipPath, checkCrc)) {
            res.success = false;
            res.errorMessage = "Arquivo zip corrompido (falha de checksum CRC32).";
            Utils::Logger::logRestore(baseTitleId, res.errorMessage);
            return res;
        }

        // 2. Prepara backup preventivo
        MKDIR_M("sdmc:/config/gdrive-sync/backups");
        std::time_t now = Utils::TimeUtil::getNowUtc();
        char bakPath[256];
        std::snprintf(bakPath, sizeof(bakPath), "sdmc:/config/gdrive-sync/backups/%016llX_before_restore_%lld.zip",
                      static_cast<unsigned long long>(baseTitleId), static_cast<long long>(now));
        res.backupPath = bakPath;

#ifdef __SWITCH__
        // 3. Monta particao de save via RAII
        Core::ScopedMountedSave saveMount(baseTitleId, uid);
        if (!saveMount.isMounted()) {
            res.success = false;
            res.errorMessage = "Falha ao montar particao save:/ no Horizon OS.";
            Utils::Logger::logRestore(baseTitleId, res.errorMessage);
            return res;
        }

        // 4. Cria backup preventivo dos dados atuais
        std::vector<std::string> existingFiles;
        ZipUtil::listFilesRecursive("save:/", "", existingFiles);
        bool hadExistingData = !existingFiles.empty();

        if (hadExistingData) {
            if (!ZipUtil::zipDirectory("save:/", bakPath, cancelToken)) {
                res.success = false;
                res.errorMessage = "Falha ao criar backup preventivo. Operacao abortada por seguranca.";
                Utils::Logger::logRestore(baseTitleId, res.errorMessage);
                return res;
            }
            Utils::Logger::logRestore(baseTitleId, std::string("Backup preventivo criado: ") + bakPath);
        }

        // 5. Limpa a particao save:/ para receber os novos arquivos
        ZipUtil::cleanDirectory("save:/");

        // 6. Descompacta snapshot
        bool unzipOk = ZipUtil::unzipToDirectory(localZipPath, "save:/", cancelToken);
        std::vector<std::string> restoredFiles;
        if (unzipOk) {
            ZipUtil::listFilesRecursive("save:/", "", restoredFiles);
        }
        bool extractionValid = (unzipOk && !restoredFiles.empty());

        // 7. Commit no Horizon OS
        bool commitOk = false;
        if (extractionValid) {
            commitOk = saveMount.commit();
        }

        // 8. Rollback caso haja falha ou cancelamento
        if (!extractionValid || !commitOk) {
            std::string reason = !unzipOk ? "Falha na extracao do ZIP" :
                                 (restoredFiles.empty() ? "ZIP sem arquivos validos" : "Falha no commit");

            Utils::Logger::logRestore(baseTitleId, "ERRO NA RESTAURACAO (" + reason + "). Executando Rollback...");

            if (hadExistingData) {
                ZipUtil::cleanDirectory("save:/");
                if (ZipUtil::unzipToDirectory(bakPath, "save:/")) {
                    saveMount.commit();
                    res.errorMessage = "Restauracao abortada (" + reason + "). Rollback concluido: save original mantido.";
                } else {
                    res.errorMessage = "Erro critico: falha ao extrair backup preventivo no rollback.";
                }
            } else {
                res.errorMessage = "Falha na restauracao: " + reason;
            }

            res.success = false;
            return res;
        }

        res.success = true;
        res.restoredFilesCount = restoredFiles.size();
        Utils::Logger::logRestore(baseTitleId, "Restauracao concluida com sucesso. Arquivos gravados: " + std::to_string(restoredFiles.size()));
        return res;
#else
        res.success = true;
        res.restoredFilesCount = 1;
        return res;
#endif
    }

} // namespace Storage

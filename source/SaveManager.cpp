#include "SaveManager.hpp"
#include "ZipHelper.hpp"
#include "GDriveClient.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>

namespace {
    static void logSys(const char* fmt, ...) {
        FILE* f = fopen(Config::SYNC_LOG_FILE.c_str(), "a");
        if (!f) return;
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        fprintf(f, "\n");
        va_end(args);
        fclose(f);
    }

    static std::string extractJsonField(const std::string& json, const std::string& key) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return "";

        size_t colon = json.find(':', pos + pattern.size());
        if (colon == std::string::npos) return "";

        size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
        if (start == std::string::npos) return "";

        if (json[start] == '"') {
            size_t end = json.find('"', start + 1);
            if (end == std::string::npos) return "";
            return json.substr(start + 1, end - start - 1);
        } else {
            size_t end = json.find_first_of(",}\r\n", start);
            if (end == std::string::npos) end = json.size();
            return json.substr(start, end - start);
        }
    }
}

SaveManager::SaveManager() {
    loadState();
    loadTimestamps();
}

SaveManager::~SaveManager() {
    saveState();
    saveTimestamps();
}

void SaveManager::Shutdown() {
    fsdevUnmountDevice("save");
    m_state = SyncState::DISABLED;
}

std::string SaveManager::formatUid(const AccountUid& uid) {
    char buf[33];
    u64 hi = (u64)(uid.uid >> 64);
    u64 lo = (u64)(uid.uid & 0xFFFFFFFFFFFFFFFFULL);
    snprintf(buf, sizeof(buf), "%016llX%016llX", (unsigned long long)hi, (unsigned long long)lo);
    return std::string(buf);
}

AccountUid SaveManager::parseUid(const std::string& str) {
    AccountUid uid{};
    if (str.length() >= 32) {
        unsigned long long hi = 0, lo = 0;
        std::string hiStr = str.substr(0, 16);
        std::string loStr = str.substr(16, 16);
        hi = std::strtoull(hiStr.c_str(), nullptr, 16);
        lo = std::strtoull(loStr.c_str(), nullptr, 16);
        uid.uid = ((u128)hi << 64) | (u128)lo;
    }
    return uid;
}

bool SaveManager::getActiveAccount(AccountUid& outUid) {
    Result rc = accountInitialize(AccountServiceType_Application);
    if (R_FAILED(rc)) {
        rc = accountInitialize(AccountServiceType_System);
        if (R_FAILED(rc)) {
            rc = accountInitialize(AccountServiceType_Administrator);
            if (R_FAILED(rc)) return false;
        }
    }

    AccountUid uid{};

    // 1. Tenta usuario pre-selecionado (o perfil que iniciou o jogo)
    rc = accountGetPreselectedUser(&uid);
    if (R_SUCCEEDED(rc) && accountUidIsValid(&uid)) {
        outUid = uid;
        accountExit();
        return true;
    }

    // 2. Tenta ultimo usuario aberto
    rc = accountGetLastOpenedUser(&uid);
    if (R_SUCCEEDED(rc) && accountUidIsValid(&uid)) {
        outUid = uid;
        accountExit();
        return true;
    }

    // 3. Fallback: enumera todos os usuarios registrados
    s32 userCount = 0;
    AccountUid uids[8];
    if (R_SUCCEEDED(accountListAllUsers(uids, 8, &userCount)) && userCount > 0) {
        for (s32 i = 0; i < userCount; ++i) {
            if (accountUidIsValid(&uids[i])) {
                outUid = uids[i];
                accountExit();
                return true;
            }
        }
    }

    accountExit();
    return false;
}

std::vector<GameSaveInfo> SaveManager::listAvailableSaves(AccountUid uid) {
    std::vector<GameSaveInfo> saves;

    FsSaveDataInfoReader reader;
    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        return saves;
    }

    bool nsInitialized = R_SUCCEEDED(nsInitialize());
    NsApplicationControlData* ctlData = nullptr;
    if (nsInitialized) {
        ctlData = static_cast<NsApplicationControlData*>(malloc(sizeof(NsApplicationControlData)));
    }

    FsSaveDataInfo infoBuf[16];
    s64 totalRead = 0;

    while (true) {
        rc = fsSaveDataInfoReaderRead(&reader, infoBuf, 16, &totalRead);
        if (R_FAILED(rc) || totalRead <= 0) break;

        for (s64 i = 0; i < totalRead; i++) {
            if (infoBuf[i].save_data_type != FsSaveDataType_Account) continue;
            if (infoBuf[i].application_id == 0) continue;
            if (memcmp(&infoBuf[i].uid, &uid, sizeof(AccountUid)) != 0) continue;

            u64 tid = infoBuf[i].application_id;

            bool alreadyInList = false;
            for (const auto& existing : saves) {
                if (existing.titleId == tid) {
                    alreadyInList = true;
                    break;
                }
            }
            if (alreadyInList) continue;

            GameSaveInfo item;
            item.titleId = tid;
            item.saveSize = infoBuf[i].size;
            item.selected = false;

            std::string gameTitle;
            if (nsInitialized && ctlData) {
                memset(ctlData, 0, sizeof(NsApplicationControlData));
                u64 actualSize = 0;
                Result nsrc = nsGetApplicationControlData(
                    NsApplicationControlSource_Storage,
                    tid,
                    ctlData,
                    sizeof(NsApplicationControlData),
                    &actualSize
                );

                if (R_SUCCEEDED(nsrc)) {
                    NacpLanguageEntry* langEntry = nullptr;
                    if (R_SUCCEEDED(nacpGetLanguageEntry(&ctlData->nacp, &langEntry)) && langEntry != nullptr) {
                        if (langEntry->name[0] != '\0') {
                            gameTitle = langEntry->name;
                        }
                    }
                }
            }

            if (gameTitle.empty()) {
                char tidBuf[32];
                snprintf(tidBuf, sizeof(tidBuf), "Title 0x%016llX", (unsigned long long)tid);
                gameTitle = tidBuf;
            }

            item.titleName = gameTitle;
            saves.push_back(item);
        }
    }

    if (ctlData) free(ctlData);
    if (nsInitialized) nsExit();
    fsSaveDataInfoReaderClose(&reader);

    return saves;
}

bool SaveManager::processGameSave(u64 titleId, AccountUid uid, std::string& outZipPath, bool& outChanged, bool forceArchive) {
    outChanged = false;
    u64 baseTid = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;

    FsSaveDataInfoReader reader;
    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        Logger::logSync("[%016llX] Mount save: FAILED [Code: 0x%08X] (fsOpenSaveDataInfoReader)",
                        (unsigned long long)baseTid, (unsigned int)rc);
        return false;
    }

    bool accountSaveFound = false;
    bool exactUidFound = false;
    bool deviceSaveFound = false;
    AccountUid targetUid = uid;
    u64 targetTitleId = baseTid;
    s64 totalRead = 0;
    FsSaveDataInfo infoBuf[16];

    while (true) {
        rc = fsSaveDataInfoReaderRead(&reader, infoBuf, 16, &totalRead);
        if (R_FAILED(rc) || totalRead == 0) break;

        for (s64 i = 0; i < totalRead; i++) {
            u64 appTid = infoBuf[i].application_id;
            u64 appBaseTid = ((appTid & 0x800ULL) != 0) ? (appTid & ~0x800ULL) : appTid;

            if (appTid == titleId || appBaseTid == baseTid) {
                targetTitleId = appTid;
                if (infoBuf[i].save_data_type == FsSaveDataType_Account) {
                    accountSaveFound = true;
                    if (memcmp(&infoBuf[i].uid, &uid, sizeof(AccountUid)) == 0) {
                        exactUidFound = true;
                        targetUid = uid;
                        break;
                    } else if (!exactUidFound) {
                        targetUid = infoBuf[i].uid;
                    }
                } else if (infoBuf[i].save_data_type == FsSaveDataType_Device) {
                    deviceSaveFound = true;
                }
            }
        }
        if (exactUidFound) break;
    }
    fsSaveDataInfoReaderClose(&reader);

    if (!accountSaveFound && !deviceSaveFound) {
        Logger::logSync("[%016llX] Mount save: FAILED (No save data exists for this title)",
                        (unsigned long long)baseTid);
        return false;
    }

    bool isDevice = (!accountSaveFound && deviceSaveFound);

    // Montagem estritamente READ-ONLY
    rc = 0;
    if (isDevice) {
        rc = fsdevMountDeviceSaveData("save", targetTitleId);
    } else {
        rc = fsdevMountSaveDataReadOnly("save", targetTitleId, targetUid);
    }

    if (R_FAILED(rc)) {
        Logger::logSync("[%016llX] Mount save READONLY: FAILED [Code: 0x%08X]",
                        (unsigned long long)baseTid, (unsigned int)rc);
        return false;
    }

    std::vector<std::string> fileList;
    ZipHelper::listFilesRecursive("save:/", "", fileList);
    if (fileList.empty()) {
        Logger::logSync("[%016llX] Mount save: FAILED (0 files found)", (unsigned long long)baseTid);
        fsdevUnmountDevice("save");
        return false;
    }

    char filename[128];
    snprintf(filename, sizeof(filename), "%s/save_%016llx.zip", Config::TEMP_DIR.c_str(), (unsigned long long)baseTid);
    outZipPath = filename;

    bool zipOk = ZipHelper::zipDirectory("save:/", outZipPath);
    fsdevUnmountDevice("save");

    if (!zipOk) {
        remove(outZipPath.c_str());
        return false;
    }

    u32 currentCrc = 0;
    if (!ZipHelper::calculateFileCrc32(outZipPath, currentCrc)) {
        remove(outZipPath.c_str());
        return false;
    }

    auto it = m_savedCrcs.find(baseTid);
    if (forceArchive || it == m_savedCrcs.end() || it->second != currentCrc) {
        outChanged = true;
        return true;
    } else {
        remove(outZipPath.c_str());
        outChanged = false;
        return true;
    }
}

void SaveManager::markSaveUploaded(u64 titleId, u32 crc) {
    u64 baseTid = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;
    m_savedCrcs[baseTid] = crc;
    m_timestamps[baseTid] = std::time(nullptr);
    saveState();
    saveTimestamps();
}

void SaveManager::OnProcessExit(u64 titleId) {
    if (m_state == SyncState::DISABLED) return;

    m_currentTitleId = titleId;
    m_baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;
    m_state = SyncState::PROCESS_EXIT_DETECTED;

    Logger::logSync("[%016llX] Process terminated", (unsigned long long)m_baseTitleId);
}

void SaveManager::HandleProcessExitDetected(GDriveClient& gdrive) {
    if (m_currentTitleId == 0) {
        m_state = SyncState::IDLE;
        return;
    }

    // Valida se o TitleID e elegivel conforme politica de sincronizacao
    if (!gdrive.isTitleTracked(m_baseTitleId)) {
        Logger::logSync("[%016llX] Game ignored by whitelist.", (unsigned long long)m_baseTitleId);
        m_state = SyncState::CLEANUP;
        return;
    }

    m_lockWaitTicks = 0;
    m_mountRetryAttempts = 0;
    m_state = SyncState::WAIT_LOCKS;
}

void SaveManager::HandleWaitLocks() {
    // Horizon OS necessita de um delay protetivo de 3 a 5 segundos para liberar os descritores de arquivo
    m_lockWaitTicks++;
    if (m_lockWaitTicks >= 35) { // 35 * 100ms = 3.5 segundos
        m_state = SyncState::MOUNT_READONLY;
    }
}

void SaveManager::HandleMountReadOnly() {
    m_mountRetryAttempts++;

    // 1. Identifica o usuario ativo
    if (!getActiveAccount(m_currentUid)) {
        Logger::logSync("[%016llX] FAILED: Could not obtain active user account.", (unsigned long long)m_baseTitleId);
        m_state = SyncState::FAILURE_RECOVERY;
        return;
    }
    m_currentUidStr = formatUid(m_currentUid);

    // 2. Localiza informacoes de particao de save
    FsSaveDataInfoReader reader;
    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        Logger::logSync("[%016llX] Mount save: FAILED [Code: 0x%08X] (fsOpenSaveDataInfoReader)",
                        (unsigned long long)m_baseTitleId, (unsigned int)rc);
        m_state = SyncState::FAILURE_RECOVERY;
        return;
    }

    bool accountSaveFound = false;
    bool exactUidFound = false;
    bool deviceSaveFound = false;
    AccountUid targetUid = m_currentUid;
    u64 targetTitleId = m_baseTitleId;
    s64 totalRead = 0;
    FsSaveDataInfo infoBuf[16];

    while (true) {
        rc = fsSaveDataInfoReaderRead(&reader, infoBuf, 16, &totalRead);
        if (R_FAILED(rc) || totalRead == 0) break;

        for (s64 i = 0; i < totalRead; i++) {
            u64 appTid = infoBuf[i].application_id;
            u64 appBaseTid = ((appTid & 0x800ULL) != 0) ? (appTid & ~0x800ULL) : appTid;

            if (appTid == m_currentTitleId || appBaseTid == m_baseTitleId) {
                targetTitleId = appTid;
                if (infoBuf[i].save_data_type == FsSaveDataType_Account) {
                    accountSaveFound = true;
                    if (memcmp(&infoBuf[i].uid, &m_currentUid, sizeof(AccountUid)) == 0) {
                        exactUidFound = true;
                        targetUid = m_currentUid;
                        break;
                    } else if (!exactUidFound) {
                        targetUid = infoBuf[i].uid;
                    }
                } else if (infoBuf[i].save_data_type == FsSaveDataType_Device) {
                    deviceSaveFound = true;
                }
            }
        }
        if (exactUidFound) break;
    }
    fsSaveDataInfoReaderClose(&reader);

    if (!accountSaveFound && !deviceSaveFound) {
        Logger::logSync("[%016llX] Mount save: FAILED (No save data exists for this title)",
                        (unsigned long long)m_baseTitleId);
        m_state = SyncState::CLEANUP;
        return;
    }

    m_isDeviceSave = (!accountSaveFound && deviceSaveFound);

    // 3. Montagem estritamente READ-ONLY da particao
    rc = 0;
    if (m_isDeviceSave) {
        rc = fsdevMountDeviceSaveData("save", targetTitleId);
    } else {
        rc = fsdevMountSaveDataReadOnly("save", targetTitleId, targetUid);
    }

    if (R_FAILED(rc)) {
        if (m_mountRetryAttempts < 5) {
            Logger::logSync("[%016llX] Mount save attempt %d BUSY/FAILED [Code: 0x%08X]. Backing off...",
                            (unsigned long long)m_baseTitleId, m_mountRetryAttempts, (unsigned int)rc);
            // Permanece no estado WAIT_LOCKS para tentar novamente
            m_lockWaitTicks = 20; // aguarda mais 1.5s
            m_state = SyncState::WAIT_LOCKS;
            return;
        } else {
            Logger::logSync("[%016llX] Mount save: FAILED after retries [Code: 0x%08X]",
                            (unsigned long long)m_baseTitleId, (unsigned int)rc);
            m_state = SyncState::FAILURE_RECOVERY;
            return;
        }
    }

    // 4. Validacao: a particao deve conter arquivos validos
    std::vector<std::string> fileList;
    ZipHelper::listFilesRecursive("save:/", "", fileList);
    if (fileList.empty()) {
        Logger::logSync("[%016llX] Mount save: FAILED (save:/ is empty, 0 files found)",
                        (unsigned long long)m_baseTitleId);
        fsdevUnmountDevice("save");
        m_state = SyncState::CLEANUP;
        return;
    }

    m_currentFileCount = fileList.size();
    Logger::logSync("[%016llX] Mount save READONLY: SUCCESS (%zu files found)",
                    (unsigned long long)m_baseTitleId, m_currentFileCount);

    m_state = SyncState::SNAPSHOT_PREPARE;
}

void SaveManager::HandleSnapshotPrepare() {
    isConsoleClockSane(&m_currentTimestamp);
    if (m_currentTimestamp < MIN_SANE_RTC_TIMESTAMP) {
        m_currentTimestamp = std::time(nullptr);
    }

    Config::ensureDirectories();
    m_state = SyncState::COMPRESS;
}

void SaveManager::HandleCompress() {
    char tempStaging[256];
    snprintf(tempStaging, sizeof(tempStaging), "%s/staging_%016llx.zip",
             Config::TEMP_DIR.c_str(), (unsigned long long)m_baseTitleId);

    // 1. Compacta save:/ diretamente para o staging no SDMC em blocos seguros de 32KB
    bool zipOk = ZipHelper::zipDirectory("save:/", tempStaging);

    // 2. Desmonta a particao imediatamente apos terminar a leitura
    fsdevUnmountDevice("save");

    if (!zipOk) {
        Logger::logSync("[%016llX] Failed to create ZIP archive.", (unsigned long long)m_baseTitleId);
        remove(tempStaging);
        m_state = SyncState::FAILURE_RECOVERY;
        return;
    }

    // 3. Calcula o CRC32 do snapshot gerado
    m_currentCrc = 0;
    if (!ZipHelper::calculateFileCrc32(tempStaging, m_currentCrc)) {
        Logger::logSync("[%016llX] Failed to calculate CRC32 of generated ZIP.", (unsigned long long)m_baseTitleId);
        remove(tempStaging);
        m_state = SyncState::FAILURE_RECOVERY;
        return;
    }

    // 4. Verifica se houve alteracao real de conteudo em relacao ao backup anterior
    auto it = m_savedCrcs.find(m_baseTitleId);
    if (it != m_savedCrcs.end() && it->second == m_currentCrc) {
        Logger::logSync("[%016llX] Save unchanged (CRC identical 0x%08X). Skipping upload.",
                        (unsigned long long)m_baseTitleId, m_currentCrc);
        remove(tempStaging);
        m_state = SyncState::CLEANUP;
        return;
    }

    // 5. Nomeia o snapshot com padrao append-only:
    // <TitleID>_<AccountUid>_<TimestampUTC>_<CRC32>.zip
    char snapshotName[256];
    snprintf(snapshotName, sizeof(snapshotName), "%016llX_%s_%lld_%08X.zip",
             (unsigned long long)m_baseTitleId,
             m_currentUidStr.c_str(),
             (long long)m_currentTimestamp,
             m_currentCrc);

    char finalSnapshotPath[384];
    snprintf(finalSnapshotPath, sizeof(finalSnapshotPath), "%s/%s",
             Config::TEMP_DIR.c_str(), snapshotName);

    remove(finalSnapshotPath);
    rename(tempStaging, finalSnapshotPath);
    m_snapshotZipPath = finalSnapshotPath;

    // 6. Registra o snapshot na fila persistente de upload em disco
    char jobIdBuf[128];
    snprintf(jobIdBuf, sizeof(jobIdBuf), "%016llX_%lld_%08X",
             (unsigned long long)m_baseTitleId, (long long)m_currentTimestamp, m_currentCrc);
    m_currentJobId = jobIdBuf;

    PendingUploadJob job;
    job.jobId = m_currentJobId;
    job.titleId = m_baseTitleId;
    job.uid = m_currentUid;
    job.accountUidStr = m_currentUidStr;
    job.timestampUtc = m_currentTimestamp;
    job.crc32 = m_currentCrc;
    job.fileCount = m_currentFileCount;
    job.zipPath = m_snapshotZipPath;
    job.retryCount = 0;

    savePendingJob(job);

    Logger::logSync("[%016llX] Snapshot created: %s (CRC: 0x%08X, %zu files). Enqueued for upload.",
                    (unsigned long long)m_baseTitleId, snapshotName, m_currentCrc, m_currentFileCount);

    m_state = SyncState::UPLOAD;
}

void SaveManager::HandleUpload(GDriveClient& gdrive) {
    FlushPendingUploads(gdrive);
    m_state = SyncState::CLEANUP;
}

void SaveManager::HandleCleanup() {
    fsdevUnmountDevice("save");

    m_currentTitleId = 0;
    m_baseTitleId = 0;
    m_currentUid = {};
    m_currentUidStr.clear();
    m_isDeviceSave = false;
    m_currentTimestamp = 0;
    m_currentCrc = 0;
    m_currentFileCount = 0;
    m_snapshotZipPath.clear();
    m_currentJobId.clear();
    m_lockWaitTicks = 0;
    m_mountRetryAttempts = 0;
    m_recoveryAttempts = 0;

    m_state = SyncState::IDLE;
}

void SaveManager::HandleFailureRecovery() {
    fsdevUnmountDevice("save");
    m_recoveryAttempts++;

    Logger::logSync("[%016llX] Failure recovery step (attempt %d). Retaining pending jobs on SD.",
                    (unsigned long long)m_baseTitleId, m_recoveryAttempts);

    // Retorna para IDLE para nao travar o console; se houver pendencias, TryFlushPending tentara reenviar
    m_state = SyncState::IDLE;
}

void SaveManager::TryFlushPending(GDriveClient& gdrive) {
    m_lastFlushTick++;
    if (m_lastFlushTick >= 100) { // a cada ~10 segundos em repouso
        m_lastFlushTick = 0;
        FlushPendingUploads(gdrive);
    }
}

bool SaveManager::FlushPendingUploads(GDriveClient& gdrive) {
    std::vector<PendingUploadJob> pending = loadPendingJobs();
    if (pending.empty()) return true;

    if (!gdrive.isConfigured() || !gdrive.isAuthorized()) {
        return false;
    }

    // 1. Garante que a raiz "Switch Saves" exista no Drive
    std::string rootFolderId = gdrive.ensureFolderExists(Config::DEFAULT_FOLDER_NAME);
    if (rootFolderId.empty()) {
        return false;
    }

    bool allSuccess = true;

    for (auto& job : pending) {
        // Valida se o arquivo local do snapshot realmente existe no SD
        struct stat st;
        if (stat(job.zipPath.c_str(), &st) != 0 || st.st_size == 0) {
            removePendingJob(job.jobId);
            continue;
        }

        char tidHex[32];
        snprintf(tidHex, sizeof(tidHex), "%016llX", (unsigned long long)job.titleId);

        // 2. Garante a estrutura versionada de pastas: Switch Saves/<TitleID>/<AccountUid>/
        std::string titleFolderId = gdrive.ensureSubFolderExists(tidHex, rootFolderId);
        if (titleFolderId.empty()) {
            allSuccess = false;
            continue;
        }

        std::string accountFolderId = gdrive.ensureSubFolderExists(job.accountUidStr, titleFolderId);
        if (accountFolderId.empty()) {
            allSuccess = false;
            continue;
        }

        // 3. Extrai nome do snapshot do caminho completo
        std::string snapshotFileName = job.zipPath;
        size_t lastSlash = snapshotFileName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            snapshotFileName = snapshotFileName.substr(lastSlash + 1);
        }

        // 4. Executa upload para a pasta versionada
        std::string upErr;
        bool uploaded = gdrive.uploadSnapshotFile(accountFolderId, snapshotFileName, job.zipPath, &upErr);

        if (uploaded) {
            // REGRA DE OURO: So deleta o .zip temporario apos confirmacao de sucesso HTTP 200/201 do Google Drive
            remove(job.zipPath.c_str());
            removePendingJob(job.jobId);

            markSaveUploaded(job.titleId, job.crc32);
            setLocalSaveTimestamp(job.titleId, job.timestampUtc);

            Logger::logSync("[%016llX] Snapshot uploaded successfully to Drive versioned folder: %s",
                            (unsigned long long)job.titleId, snapshotFileName.c_str());
        } else {
            allSuccess = false;
            job.retryCount++;
            job.lastError = upErr;
            savePendingJob(job);
            Logger::logSync("[%016llX] Snapshot upload failed (%s). Retaining in pending queue.",
                            (unsigned long long)job.titleId, upErr.c_str());
        }
    }

    return allSuccess;
}

bool SaveManager::savePendingJob(const PendingUploadJob& job) {
    mkdir(Config::PENDING_DIR.c_str(), 0777);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.json", Config::PENDING_DIR.c_str(), job.jobId.c_str());

    FILE* f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"job_id\": \"%s\",\n", job.jobId.c_str());
    fprintf(f, "  \"title_id\": \"%016llx\",\n", (unsigned long long)job.titleId);
    fprintf(f, "  \"account_uid\": \"%s\",\n", job.accountUidStr.c_str());
    fprintf(f, "  \"timestamp_utc\": %lld,\n", (long long)job.timestampUtc);
    fprintf(f, "  \"crc32\": \"%08x\",\n", job.crc32);
    fprintf(f, "  \"file_count\": %zu,\n", job.fileCount);
    fprintf(f, "  \"zip_path\": \"%s\",\n", job.zipPath.c_str());
    fprintf(f, "  \"retry_count\": %d,\n", job.retryCount);
    fprintf(f, "  \"last_error\": \"%s\"\n", job.lastError.c_str());
    fprintf(f, "}\n");

    fclose(f);
    return true;
}

std::vector<PendingUploadJob> SaveManager::loadPendingJobs() {
    std::vector<PendingUploadJob> list;
    DIR* dir = opendir(Config::PENDING_DIR.c_str());
    if (!dir) return list;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            std::string name = entry->d_name;
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                std::string fullPath = Config::PENDING_DIR + "/" + name;
                FILE* f = fopen(fullPath.c_str(), "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long sz = ftell(f);
                    fseek(f, 0, SEEK_SET);

                    if (sz > 0 && sz < 8192) {
                        std::string json(sz, '\0');
                        fread(json.data(), 1, sz, f);
                        fclose(f);

                        PendingUploadJob job;
                        job.jobId = extractJsonField(json, "job_id");
                        std::string tidStr = extractJsonField(json, "title_id");
                        job.titleId = std::strtoull(tidStr.c_str(), nullptr, 16);
                        job.accountUidStr = extractJsonField(json, "account_uid");
                        job.uid = parseUid(job.accountUidStr);
                        std::string tsStr = extractJsonField(json, "timestamp_utc");
                        job.timestampUtc = static_cast<std::time_t>(std::strtoll(tsStr.c_str(), nullptr, 10));
                        std::string crcStr = extractJsonField(json, "crc32");
                        job.crc32 = static_cast<u32>(std::strtoul(crcStr.c_str(), nullptr, 16));
                        std::string cntStr = extractJsonField(json, "file_count");
                        job.fileCount = static_cast<size_t>(std::strtoull(cntStr.c_str(), nullptr, 10));
                        job.zipPath = extractJsonField(json, "zip_path");
                        std::string retryStr = extractJsonField(json, "retry_count");
                        job.retryCount = std::atoi(retryStr.c_str());
                        job.lastError = extractJsonField(json, "last_error");

                        if (!job.jobId.empty() && job.titleId != 0 && !job.zipPath.empty()) {
                            list.push_back(job);
                        }
                    } else {
                        fclose(f);
                    }
                }
            }
        }
    }
    closedir(dir);
    return list;
}

bool SaveManager::removePendingJob(const std::string& jobId) {
    if (jobId.empty()) return false;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.json", Config::PENDING_DIR.c_str(), jobId.c_str());
    return (remove(path) == 0);
}

void SaveManager::loadState() {
    m_savedCrcs.clear();
    FILE* f = fopen(Config::STATE_FILE.c_str(), "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long tid = 0;
        u32 crc = 0;
        if (sscanf(line, "%llx:%x", &tid, &crc) == 2) {
            m_savedCrcs[tid] = crc;
        }
    }
    fclose(f);
}

void SaveManager::saveState() {
    FILE* f = fopen(Config::STATE_FILE.c_str(), "w");
    if (!f) return;

    for (const auto& [tid, crc] : m_savedCrcs) {
        fprintf(f, "%016llx:%08x\n", (unsigned long long)tid, crc);
    }
    fclose(f);
}

void SaveManager::loadTimestamps() {
    m_timestamps.clear();
    FILE* f = fopen(Config::TIMESTAMPS_FILE.c_str(), "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long tid = 0;
        long long ts = 0;
        if (sscanf(line, " \"%llx\": %lld", &tid, &ts) == 2 ||
            sscanf(line, " \"%llX\": %lld", &tid, &ts) == 2 ||
            sscanf(line, "%llx:%lld", &tid, &ts) == 2) {
            if (tid != 0) {
                m_timestamps[tid] = static_cast<std::time_t>(ts);
            }
        }
    }
    fclose(f);
}

void SaveManager::saveTimestamps() {
    FILE* f = fopen(Config::TIMESTAMPS_FILE.c_str(), "w");
    if (!f) return;

    fprintf(f, "{\n");
    bool first = true;
    for (const auto& [tid, ts] : m_timestamps) {
        if (!first) fprintf(f, ",\n");
        fprintf(f, "  \"%016llx\": %lld", (unsigned long long)tid, (long long)ts);
        first = false;
    }
    fprintf(f, "\n}\n");
    fclose(f);
}

std::time_t SaveManager::getLocalSaveTimestamp(u64 titleId, AccountUid uid) {
    u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;
    std::time_t recorded = 0;
    auto it = m_timestamps.find(baseTitleId);
    if (it != m_timestamps.end()) {
        recorded = it->second;
    }

    std::time_t maxMtime = 0;
    Result rc = fsdevMountSaveDataReadOnly("save", baseTitleId, uid);
    if (R_SUCCEEDED(rc)) {
        std::vector<std::string> fileList;
        ZipHelper::listFilesRecursive("save:/", "", fileList);
        for (const auto& rel : fileList) {
            std::string full = "save:/" + rel;
            struct stat st;
            if (stat(full.c_str(), &st) == 0 && st.st_mtime > 0) {
                if (st.st_mtime > maxMtime) {
                    maxMtime = st.st_mtime;
                }
            }
        }
        fsdevUnmountDevice("save");
    }

    return std::max(recorded, maxMtime);
}

void SaveManager::setLocalSaveTimestamp(u64 titleId, std::time_t ts) {
    u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;
    m_timestamps[baseTitleId] = ts;
    saveTimestamps();
}

bool SaveManager::isTimestampSane(std::time_t ts) {
    return (ts >= MIN_SANE_RTC_TIMESTAMP);
}

bool SaveManager::isConsoleClockSane(std::time_t* outCurrentTime) {
    std::time_t current = 0;
    u64 switchPosixTime = 0;
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &switchPosixTime)) && switchPosixTime > 0) {
        current = static_cast<std::time_t>(switchPosixTime);
    } else {
        current = std::time(nullptr);
    }

    if (outCurrentTime) {
        *outCurrentTime = current;
    }

    return isTimestampSane(current);
}

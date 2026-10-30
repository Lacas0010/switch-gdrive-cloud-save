#include "SaveManager.hpp"
#include "ZipHelper.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <cstdio>
#include <cstring>
#include <sstream>

SaveManager::SaveManager() {
    loadState();
    loadTimestamps();
}

SaveManager::~SaveManager() {
    saveState();
    saveTimestamps();
}

bool SaveManager::getActiveAccount(AccountUid& outUid) {
    // 1. Tenta AccountServiceType_Application (acc:u0) que eh o padrao mais confiavel para ler perfis
    Result rc = accountInitialize(AccountServiceType_Application);
    if (R_FAILED(rc)) {
        // Fallback para System (acc:u1) ou Administrator (acc:su)
        rc = accountInitialize(AccountServiceType_System);
        if (R_FAILED(rc)) {
            rc = accountInitialize(AccountServiceType_Administrator);
            if (R_FAILED(rc)) return false;
        }
    }

    AccountUid uid{};

    // 1. Tenta usuario pre-selecionado (usuario ativo que abriu o jogo)
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

    // 3. Fallback: lista todos os usuarios registrados no console
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

    // Inicializa NS para obtencao dos metadados dos jogos (nomes)
    bool nsInitialized = R_SUCCEEDED(nsInitialize());

    // Aloca buffer para controle NACP dinamicamente no Heap para evitar estourar a stack (~144KB)
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
            // Filtra apenas saves de conta do usuario especificado e ignora Title ID 0
            if (infoBuf[i].save_data_type != FsSaveDataType_Account) continue;
            if (infoBuf[i].application_id == 0) continue;
            if (memcmp(&infoBuf[i].uid, &uid, sizeof(AccountUid)) != 0) continue;

            u64 tid = infoBuf[i].application_id;

            // Evita duplicatas se houver mais de um registro do mesmo Title ID
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

            // Tenta obter o nome legivel do jogo via NACP
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

            // Fallback para formatacao hexadecimal do Title ID se nao encontrar o nome
            if (gameTitle.empty()) {
                char tidBuf[32];
                snprintf(tidBuf, sizeof(tidBuf), "Title 0x%016llX", (unsigned long long)tid);
                gameTitle = tidBuf;
            }

            item.titleName = gameTitle;
            saves.push_back(item);
        }
    }

    if (ctlData) {
        free(ctlData);
    }
    if (nsInitialized) {
        nsExit();
    }
    fsSaveDataInfoReaderClose(&reader);

    return saves;
}

bool SaveManager::processGameSave(u64 titleId, AccountUid uid, std::string& outZipPath, bool& outChanged, bool forceArchive) {
    outChanged = false;

    // Se o titleId fornecido contiver a flag de update/patch (bit 11), normaliza para a base
    u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;

    // 1. Abre leitor de informacoes de saves para localizar a particao
    FsSaveDataInfoReader reader;
    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        Logger::logSync("[%016llX] Mount save: FAILED [Code: 0x%08X] (fsOpenSaveDataInfoReader)",
                        (unsigned long long)baseTitleId, (unsigned int)rc);
        return false;
    }

    bool accountSaveFound = false;
    bool exactUidFound = false;
    bool deviceSaveFound = false;
    AccountUid targetUid = uid;
    u64 targetTitleId = baseTitleId;
    s64 totalRead = 0;
    FsSaveDataInfo infoBuf[16];

    while (true) {
        rc = fsSaveDataInfoReaderRead(&reader, infoBuf, 16, &totalRead);
        if (R_FAILED(rc) || totalRead == 0) break;

        for (s64 i = 0; i < totalRead; i++) {
            u64 appTid = infoBuf[i].application_id;
            u64 appBaseTid = ((appTid & 0x800ULL) != 0) ? (appTid & ~0x800ULL) : appTid;

            if (appTid == titleId || appBaseTid == baseTitleId) {
                targetTitleId = appTid;
                if (infoBuf[i].save_data_type == FsSaveDataType_Account) {
                    accountSaveFound = true;
                    if (memcmp(&infoBuf[i].uid, &uid, sizeof(AccountUid)) == 0) {
                        exactUidFound = true;
                        targetUid = uid;
                        break;
                    } else if (!exactUidFound) {
                        targetUid = infoBuf[i].uid; // Salva perfil existente como fallback
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
                        (unsigned long long)baseTitleId);
        return false;
    }

    bool isDeviceSave = (!accountSaveFound && deviceSaveFound);

    // 2. Retry loop com backoff seguro caso a partição esteja com file lock do Horizon OS
    bool mounted = false;
    rc = 0;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (isDeviceSave) {
            rc = fsdevMountDeviceSaveDataReadOnly("save", targetTitleId);
            if (R_FAILED(rc)) {
                rc = fsdevMountDeviceSaveData("save", targetTitleId);
            }
        } else {
            rc = fsdevMountSaveDataReadOnly("save", targetTitleId, targetUid);
            if (R_FAILED(rc)) {
                rc = fsdevMountSaveData("save", targetTitleId, targetUid);
            }
        }

        if (R_SUCCEEDED(rc)) {
            mounted = true;
            break;
        }

        if (attempt < 3) {
            Logger::logSync("[%016llX] Mount save attempt %d: BUSY/FAILED [Code: 0x%08X]. Retrying...",
                            (unsigned long long)baseTitleId, attempt, (unsigned int)rc);
            svcSleepThread(1'500'000'000ULL * attempt);
        }
    }

    if (!mounted) {
        Logger::logSync("[%016llX] Mount save: FAILED [Code: 0x%08X]",
                        (unsigned long long)baseTitleId, (unsigned int)rc);
        return false;
    }

    Logger::logSync("[%016llX] Mount save: SUCCESS", (unsigned long long)baseTitleId);

    // 3. Valida se a partição possui arquivos válidos dentro
    std::vector<std::string> fileList;
    ZipHelper::listFilesRecursive("save:/", "", fileList);

    if (fileList.empty()) {
        Logger::logSync("[%016llX] Mount save: FAILED (save:/ is empty, 0 files found)",
                        (unsigned long long)baseTitleId);
        fsdevUnmountDevice("save");
        return false;
    }

    // Formata nome do arquivo de destino
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/save_%016llx.zip", Config::TEMP_DIR.c_str(), (unsigned long long)baseTitleId);
    outZipPath = filename;

    // 4. Compacta o diretorio montado "save:/" diretamente para o SDMC
    bool zipOk = ZipHelper::zipDirectory("save:/", outZipPath);

    // 5. Desmonta o save imediatamente apos terminar a leitura
    fsdevUnmountDevice("save");

    if (!zipOk) {
        Logger::logSync("[%016llX] Failed to create ZIP archive.", (unsigned long long)baseTitleId);
        remove(outZipPath.c_str());
        return false;
    }

    // 6. Calcula o CRC32 e tamanho do .zip gerado para checar alteracao
    u32 currentCrc = 0;
    if (!ZipHelper::calculateFileCrc32(outZipPath, currentCrc)) {
        Logger::logSync("[%016llX] Failed to calculate CRC32 of generated ZIP.", (unsigned long long)baseTitleId);
        remove(outZipPath.c_str());
        return false;
    }

    struct stat st;
    off_t fileSize = 0;
    if (stat(outZipPath.c_str(), &st) == 0) {
        fileSize = st.st_size;
    }

    Logger::logSync("[%016llX] ZIP generated: %s (CRC: 0x%08X, Size: %lld bytes)",
                    (unsigned long long)baseTitleId, outZipPath.c_str(), (unsigned int)currentCrc, (long long)fileSize);

    auto it = m_savedCrcs.find(baseTitleId);
    if (forceArchive || it == m_savedCrcs.end() || it->second != currentCrc) {
        // Houve alteracao nos dados ou upload manual forcado!
        outChanged = true;
        return true;
    } else {
        // Save identico ao do backup anterior; apaga o zip temporario
        Logger::logSync("[%016llX] Save unchanged (CRC identical to last backup). Skipping upload.",
                        (unsigned long long)baseTitleId);
        remove(outZipPath.c_str());
        outChanged = false;
        return true;
    }
}

void SaveManager::markSaveUploaded(u64 titleId, u32 crc) {
    u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;
    m_savedCrcs[baseTitleId] = crc;
    m_timestamps[baseTitleId] = std::time(nullptr);
    saveState();
    saveTimestamps();
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

bool SaveManager::restoreGameSave(u64 titleId, AccountUid uid, const std::string& zipPath, std::string* outError) {
    u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;

    // 1. Cria pasta de backups se nao existir
    mkdir(Config::BACKUP_DIR.c_str(), 0777);
    char backupPath[128];
    snprintf(backupPath, sizeof(backupPath), "%s/%016llx_backup.zip", Config::BACKUP_DIR.c_str(), (unsigned long long)baseTitleId);

    // 2. Backup de seguranca do save local atual (se existir)
    if (R_SUCCEEDED(fsdevMountSaveDataReadOnly("save", baseTitleId, uid))) {
        ZipHelper::zipDirectory("save:/", backupPath);
        fsdevUnmountDevice("save");
    }

    // 3. Monta o save em modo escrita (read-write)
    Result rc = fsdevMountSaveData("save", baseTitleId, uid);
    if (R_FAILED(rc)) {
        if (outError) *outError = "Falha ao montar particao de save para gravacao (rc=0x" + std::to_string(rc) + ")";
        return false;
    }

    // 4. Limpa todo o conteudo de save:/
    ZipHelper::cleanDirectory("save:/");

    // 5. Descompacta o zip baixado para save:/
    bool unzipOk = ZipHelper::unzipToDirectory(zipPath, "save:/");
    if (!unzipOk) {
        if (outError) *outError = "Falha ao descompactar os arquivos no save";
        fsdevUnmountDevice("save");
        return false;
    }

    // 6. Commit obrigatorio para persistir no cartao/NAND
    rc = fsdevCommitDevice("save");
    fsdevUnmountDevice("save");

    if (R_FAILED(rc)) {
        if (outError) *outError = "Falha ao persistir (commit) alteracoes no save";
        return false;
    }

    // 7. Atualiza o CRC salvo para que o sysmodule nao re-envie o save restaurado
    u32 newCrc = 0;
    if (ZipHelper::calculateFileCrc32(zipPath, newCrc)) {
        m_savedCrcs[baseTitleId] = newCrc;
        saveState();
    }

    return true;
}

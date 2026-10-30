#include "SaveManager.hpp"
#include "ZipHelper.hpp"
#include "Config.hpp"
#include <cstdio>
#include <cstring>
#include <sstream>

SaveManager::SaveManager() {
    loadState();
}

SaveManager::~SaveManager() {
    saveState();
}

bool SaveManager::getActiveAccount(AccountUid& outUid) {
    Result rc = accountInitialize(AccountServiceType_System);
    if (R_FAILED(rc)) {
        // Fallback para User caso System falhe
        rc = accountInitialize(AccountServiceType_Administrator);
        if (R_FAILED(rc)) return false;
    }

    AccountUid uid{};
    rc = accountGetLastOpenedUser(&uid);
    accountExit();

    if (R_SUCCEEDED(rc) && accountUidIsValid(&uid)) {
        outUid = uid;
        return true;
    }

    // Se nao houver ultimo aberto, tenta listar o primeiro usuario disponivel
    rc = accountInitialize(AccountServiceType_Administrator);
    if (R_SUCCEEDED(rc)) {
        s32 userCount = 0;
        AccountUid uids[8];
        if (R_SUCCEEDED(accountListAllUsers(uids, 8, &userCount)) && userCount > 0) {
            outUid = uids[0];
            accountExit();
            return true;
        }
        accountExit();
    }

    return false;
}

bool SaveManager::processGameSave(u64 titleId, AccountUid uid, std::string& outZipPath, bool& outChanged) {
    outChanged = false;

    // 1. Abre leitor de informacoes de saves para garantir que o save existe
    FsSaveDataInfoReader reader;
    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        return false;
    }

    bool saveExists = false;
    s64 totalRead = 0;
    FsSaveDataInfo infoBuf[16];

    while (true) {
        rc = fsSaveDataInfoReaderRead(&reader, infoBuf, 16, &totalRead);
        if (R_FAILED(rc) || totalRead == 0) break;

        for (s64 i = 0; i < totalRead; i++) {
            if (infoBuf[i].application_id == titleId &&
                infoBuf[i].save_data_type == FsSaveDataType_Account &&
                memcmp(&infoBuf[i].uid, &uid, sizeof(AccountUid)) == 0) {
                saveExists = true;
                break;
            }
        }
        if (saveExists) break;
    }
    fsSaveDataInfoReaderClose(&reader);

    if (!saveExists) {
        return false;
    }

    // 2. Monta o save no caminho "save:/"
    rc = fsdevMountSaveData("save", titleId, uid);
    if (R_FAILED(rc)) {
        return false;
    }

    // Formata nome do arquivo de destino
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/save_%016llx.zip", Config::TEMP_DIR.c_str(), (unsigned long long)titleId);
    outZipPath = filename;

    // 3. Compacta o diretorio montado "save:/" diretamente para o SD
    bool zipOk = ZipHelper::zipDirectory("save:/", outZipPath);

    // 4. Desmonta o save imediatamente apos terminar a leitura
    fsdevUnmountDevice("save");

    if (!zipOk) {
        remove(outZipPath.c_str());
        return false;
    }

    // 5. Calcula o CRC32 do .zip gerado para checar alteracao
    u32 currentCrc = 0;
    if (ZipHelper::calculateFileCrc32(outZipPath, currentCrc)) {
        auto it = m_savedCrcs.find(titleId);
        if (it == m_savedCrcs.end() || it->second != currentCrc) {
            // Houve alteracao nos dados!
            outChanged = true;
            return true;
        } else {
            // Save identico ao do backup anterior; apaga o zip temporario
            remove(outZipPath.c_str());
            outChanged = false;
            return true;
        }
    }

    return false;
}

void SaveManager::markSaveUploaded(u64 titleId, u32 crc) {
    m_savedCrcs[titleId] = crc;
    saveState();
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

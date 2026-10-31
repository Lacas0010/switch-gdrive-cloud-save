#include "RestoreManager.hpp"
#include "../../source/Config.hpp"
#include "../../source/ZipHelper.hpp"
#include "../../source/GDriveClient.hpp"
#include "../../source/Logger.hpp"
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

RestoreManager::RestoreManager() {}
RestoreManager::~RestoreManager() {}

void RestoreManager::logRestoreEvent(u64 titleId, const std::string& message) {
    FILE* f = fopen(Config::RESTORE_LOG_FILE.c_str(), "a");
    if (!f) return;

    std::time_t now = std::time(nullptr);
    struct tm ti;
    gmtime_r(&now, &ti);

    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);

    fprintf(f, "[%s] [%016llX] %s\n", timeBuf, (unsigned long long)titleId, message.c_str());
    fclose(f);
}

RestoreResult RestoreManager::restoreSnapshotFromDrive(
    GDriveClient& client,
    u64 titleId,
    AccountUid uid,
    const std::string& driveFileId,
    const std::string& snapshotFileName
) {
    RestoreResult res;
    Config::ensureDirectories();

    std::string tempZip = Config::TEMP_DIR + "/restore_download.zip";
    remove(tempZip.c_str());

    logRestoreEvent(titleId, "Iniciando download de snapshot: " + snapshotFileName + " (ID: " + driveFileId + ")");

    std::string dlErr;
    if (!client.downloadFile(driveFileId, tempZip, &dlErr)) {
        res.success = false;
        res.errorMessage = "Falha no download do Drive: " + dlErr;
        logRestoreEvent(titleId, "FALHA: " + res.errorMessage);
        return res;
    }

    res = restoreFromLocalZip(titleId, uid, tempZip);
    remove(tempZip.c_str());

    return res;
}

RestoreResult RestoreManager::restoreFromLocalZip(
    u64 titleId,
    AccountUid uid,
    const std::string& localZipPath
) {
    RestoreResult res;
    u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;

    // 1. Valida existencia e integridade basica do arquivo .zip
    struct stat st;
    if (stat(localZipPath.c_str(), &st) != 0 || st.st_size == 0) {
        res.success = false;
        res.errorMessage = "Arquivo zip de restauracao vazio ou inacessivel.";
        logRestoreEvent(baseTitleId, "FALHA: " + res.errorMessage);
        return res;
    }

    u32 checkCrc = 0;
    if (!ZipHelper::calculateFileCrc32(localZipPath, checkCrc)) {
        res.success = false;
        res.errorMessage = "Arquivo zip corrompido (falha de checksum CRC32).";
        logRestoreEvent(baseTitleId, "FALHA: " + res.errorMessage);
        return res;
    }

    // 2. Prepara caminho do Backup Preventivo
    mkdir(Config::BACKUP_DIR.c_str(), 0777);
    std::time_t now = std::time(nullptr);
    char bakPath[256];
    snprintf(bakPath, sizeof(bakPath), "%s/%016llX_before_restore_%lld.zip",
             Config::BACKUP_DIR.c_str(), (unsigned long long)baseTitleId, (long long)now);
    res.backupPath = bakPath;

    // 3. Monta particao de save em modo gravacao (read-write)
    Result rc = fsdevMountSaveData("save", baseTitleId, uid);
    if (R_FAILED(rc)) {
        rc = fsdevMountDeviceSaveData("save", baseTitleId);
    }

    if (R_FAILED(rc)) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Falha ao montar particao save:/ para gravacao (rc=0x%X)", rc);
        res.success = false;
        res.errorMessage = errBuf;
        logRestoreEvent(baseTitleId, "FALHA: " + res.errorMessage);
        return res;
    }

    // 4. Backup Preventivo: salva versao atual antes de qualquer alteracao
    std::vector<std::string> existingFiles;
    ZipHelper::listFilesRecursive("save:/", "", existingFiles);
    bool hadExistingData = !existingFiles.empty();

    if (hadExistingData) {
        if (!ZipHelper::zipDirectory("save:/", bakPath)) {
            fsdevUnmountDevice("save");
            res.success = false;
            res.errorMessage = "Falha ao criar backup preventivo local antes de sobrescrever. Operacao abortada.";
            logRestoreEvent(baseTitleId, "FALHA: " + res.errorMessage);
            return res;
        }
        logRestoreEvent(baseTitleId, "Backup preventivo criado com sucesso: " + std::string(bakPath));
    }

    // 5. Limpa a particao save:/ para receber os novos arquivos
    ZipHelper::cleanDirectory("save:/");

    // 6. Descompacta o snapshot baixado
    bool unzipOk = ZipHelper::unzipToDirectory(localZipPath, "save:/");

    // 7. Valida se os arquivos foram gravados na particao
    std::vector<std::string> restoredFiles;
    if (unzipOk) {
        ZipHelper::listFilesRecursive("save:/", "", restoredFiles);
    }
    bool extractionValid = (unzipOk && !restoredFiles.empty());

    // 8. Commit obrigatorio no Horizon OS
    Result commitRc = 0;
    if (extractionValid) {
        commitRc = fsdevCommitDevice("save");
    }

    // 9. Rollback seguro caso haja falha
    if (!extractionValid || R_FAILED(commitRc)) {
        std::string reason = !unzipOk ? "Falha na extracao do ZIP" :
                             (restoredFiles.empty() ? "ZIP sem arquivos validos" : "Falha no fsdevCommitDevice");

        logRestoreEvent(baseTitleId, "ERRO NA RESTAURACAO (" + reason + "). Iniciando Rollback...");

        if (hadExistingData) {
            ZipHelper::cleanDirectory("save:/");
            if (ZipHelper::unzipToDirectory(bakPath, "save:/")) {
                fsdevCommitDevice("save");
                logRestoreEvent(baseTitleId, "ROLLBACK CONCLUIDO: Save local restaurado do backup preventivo.");
                res.errorMessage = "Erro na restauracao (" + reason + "). ROLLBACK EXECUTADO COM SUCESSO: save original mantido.";
            } else {
                logRestoreEvent(baseTitleId, "CRITICO: Falha no rollback de descompactacao do backup preventivo!");
                res.errorMessage = "Erro critico na restauracao e falha ao retornar save original.";
            }
        } else {
            res.errorMessage = "Falha ao extrair save: " + reason;
        }

        fsdevUnmountDevice("save");
        res.success = false;
        return res;
    }

    // 10. Desmonta dispositivo apos commit
    fsdevUnmountDevice("save");

    res.success = true;
    res.restoredFilesCount = restoredFiles.size();
    logRestoreEvent(baseTitleId, "RESTAURACAO CONCLUIDA: " + std::to_string(restoredFiles.size()) + " arquivos persistidos.");

    return res;
}


#pragma once
#include <switch.h>
#include <string>
#include <vector>

class GDriveClient;

struct RestoreResult {
    bool success = false;
    std::string errorMessage;
    std::string backupPath;
    size_t restoredFilesCount = 0;
};

class RestoreManager {
public:
    RestoreManager();
    ~RestoreManager();

    // Baixa o snapshot do Google Drive, cria backup preventivo, extrai em save:/ e executa fsdevCommitDevice
    static RestoreResult restoreSnapshotFromDrive(
        GDriveClient& client,
        u64 titleId,
        AccountUid uid,
        const std::string& driveFileId,
        const std::string& snapshotFileName
    );

    // Executa a restauracao com rollback a partir de um arquivo .zip ja localizado no SDMC
    static RestoreResult restoreFromLocalZip(
        u64 titleId,
        AccountUid uid,
        const std::string& localZipPath
    );

    // Registra evento de restauracao em sdmc:/config/gdrive-sync/restore.log
    static void logRestoreEvent(u64 titleId, const std::string& message);
};


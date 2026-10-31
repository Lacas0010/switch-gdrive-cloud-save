#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <ctime>

class GDriveClient;

struct DriveSnapshotVersion {
    std::string fileId;
    std::string fileName;
    u64 titleId = 0;
    std::string accountUid;
    std::time_t timestampUtc = 0;
    std::string formattedUtcTime; // Ex: "30/10/2026 21:40:00 UTC"
    u32 crc32 = 0;
    size_t sizeBytes = 0;
};

class DriveVersionManager {
public:
    explicit DriveVersionManager(GDriveClient& client);
    ~DriveVersionManager();

    // Consulta os snapshots na pasta versionada: Switch Saves/<TitleID>/<AccountUid>/
    // Se a pasta versionada nao existir, faz fallback para a raiz de saves (compatibilidade legada)
    std::vector<DriveSnapshotVersion> listGameVersions(
        u64 titleId,
        const std::string& accountUidStr,
        std::string* outError = nullptr
    );

    // Formata Unix Epoch UTC em formato legivel DD/MM/YYYY HH:MM:SS UTC
    static std::string formatUtcDateTime(std::time_t epochSeconds);

private:
    GDriveClient& m_client;
};

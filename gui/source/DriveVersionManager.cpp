#include "DriveVersionManager.hpp"
#include "../../source/GDriveClient.hpp"
#include "../../source/Config.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>

DriveVersionManager::DriveVersionManager(GDriveClient& client)
    : m_client(client) {}

DriveVersionManager::~DriveVersionManager() {}

std::string DriveVersionManager::formatUtcDateTime(std::time_t epochSeconds) {
    if (epochSeconds <= 0) return "Data Desconhecida";

    struct tm timeinfo;
    gmtime_r(&epochSeconds, &timeinfo);

    char buf[64];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d UTC",
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return std::string(buf);
}

std::vector<DriveSnapshotVersion> DriveVersionManager::listGameVersions(
    u64 titleId,
    const std::string& accountUidStr,
    std::string* outError
) {
    std::vector<DriveSnapshotVersion> versions;

    u64 baseTitleId = ((titleId & 0x800ULL) != 0) ? (titleId & ~0x800ULL) : titleId;
    char tidHex[32];
    snprintf(tidHex, sizeof(tidHex), "%016llX", (unsigned long long)baseTitleId);

    // 1. Obtem a pasta raiz de saves ("Switch Saves")
    std::string rootFolderId = m_client.ensureFolderExists(Config::DEFAULT_FOLDER_NAME);
    if (rootFolderId.empty()) {
        if (outError) *outError = "Pasta raiz 'Switch Saves' nao encontrada no Google Drive";
        return versions;
    }

    // 2. Busca subpasta do TitleID
    std::string titleFolderId = m_client.ensureSubFolderExists(tidHex, rootFolderId);
    if (titleFolderId.empty()) {
        // Nao ha versoes versionadas ainda para este jogo
        return versions;
    }

    // 3. Busca subpasta do AccountUid
    std::string targetUid = accountUidStr.empty() ? "DEFAULT_ACCOUNT" : accountUidStr;
    std::string accountFolderId = m_client.ensureSubFolderExists(targetUid, titleFolderId);
    if (accountFolderId.empty()) {
        return versions;
    }

    // 4. Lista todos os arquivos da pasta versionada: Switch Saves/<TitleID>/<AccountUid>/
    std::vector<DriveFileInfo> driveFiles = m_client.listFilesInFolder(accountFolderId, outError);

    for (const auto& df : driveFiles) {
        DriveSnapshotVersion ver;
        ver.fileId = df.id;
        ver.fileName = df.name;
        ver.titleId = baseTitleId;
        ver.accountUid = targetUid;
        ver.sizeBytes = df.size;
        ver.timestampUtc = df.modifiedTime;

        // Tenta extrair timestamp e CRC32 do padrao append-only:
        // <TitleID>_<AccountUid>_<TimestampUTC>_<CRC32>.zip
        unsigned long long parsedTid = 0;
        char parsedUid[64] = {0};
        long long parsedTs = 0;
        unsigned int parsedCrc = 0;

        if (sscanf(df.name.c_str(), "%llX_%63[^_]_%lld_%x.zip", &parsedTid, parsedUid, &parsedTs, &parsedCrc) == 4 ||
            sscanf(df.name.c_str(), "%llx_%63[^_]_%lld_%x.zip", &parsedTid, parsedUid, &parsedTs, &parsedCrc) == 4) {
            ver.titleId = parsedTid;
            ver.accountUid = parsedUid;
            if (parsedTs > 0) ver.timestampUtc = static_cast<std::time_t>(parsedTs);
            ver.crc32 = parsedCrc;
        }

        ver.formattedUtcTime = formatUtcDateTime(ver.timestampUtc);
        versions.push_back(ver);
    }

    // Ordena da versao mais recente para a mais antiga
    std::sort(versions.begin(), versions.end(), [](const DriveSnapshotVersion& a, const DriveSnapshotVersion& b) {
        return a.timestampUtc > b.timestampUtc;
    });

    return versions;
}

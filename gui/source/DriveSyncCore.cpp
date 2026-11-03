#include "DriveSyncCore.hpp"
#include "RestoreManager.hpp"
#include "../../source/ZipHelper.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Core {

    DriveSyncCore::DriveSyncCore() = default;

    DriveSyncCore::~DriveSyncCore() {
        close();
    }

    bool DriveSyncCore::init() {
        if (m_initialized) return true;

        m_saveManager.init();
        m_client.init();

        m_saveManager.getActiveAccount(m_activeUid);
        m_initialized = true;
        return true;
    }

    void DriveSyncCore::close() {
        m_initialized = false;
    }

    bool DriveSyncCore::isNetworkConnected() const {
        NifmInternetConnectionType connType;
        u32 wifiState = 0;
        NifmInternetConnectionStatus connStatus;
        Result rc = nifmGetInternetConnectionStatus(&connType, &wifiState, &connStatus);
        return (R_SUCCEEDED(rc) && connStatus == NifmInternetConnectionStatus_Connected);
    }

    bool DriveSyncCore::isAuthorized() const {
        return m_client.isAuthorized();
    }

    std::string DriveSyncCore::getProfileNickname() const {
        AccountProfile profile;
        Result rc = accountGetProfile(&profile, m_activeUid);
        if (R_FAILED(rc)) return "Perfil Switch";

        AccountProfileBase profileBase;
        rc = accountProfileGet(&profile, nullptr, &profileBase);
        accountProfileClose(&profile);

        if (R_SUCCEEDED(rc) && profileBase.nickname[0] != '\0') {
            return std::string(profileBase.nickname);
        }
        return "Perfil Switch";
    }

    std::string DriveSyncCore::getFolderName() const {
        return m_client.getFolderName();
    }

    void DriveSyncCore::setFolderName(const std::string& folderName) {
        m_client.setFolderName(folderName);
    }

    std::string DriveSyncCore::getClientId() const {
        return m_client.getClientId();
    }

    void DriveSyncCore::setClientId(const std::string& id) {
        m_client.setClientId(id);
    }

    std::string DriveSyncCore::getClientSecret() const {
        return m_client.getClientSecret();
    }

    void DriveSyncCore::setClientSecret(const std::string& secret) {
        m_client.setClientSecret(secret);
    }

    std::time_t DriveSyncCore::getLastBackupTimestamp() const {
        return m_lastBackupTime;
    }

    void DriveSyncCore::saveConfig() {
        m_client.saveConfig();
    }

    std::vector<Assets::GameEntry> DriveSyncCore::loadGamesList(Assets::TitleManager& titleManager) {
        std::vector<Assets::GameEntry> result;
        std::vector<GameSaveInfo> localSaves = m_saveManager.listAvailableSaves(m_activeUid);

        for (const auto& item : localSaves) {
            Assets::GameEntry entry;
            entry.titleId = item.titleId;
            entry.titleName = titleManager.getTitleName(item.titleId, item.titleName);
            entry.saveSize = item.saveSize;
            entry.lastModified = m_saveManager.getLocalSaveTimestamp(item.titleId, m_activeUid);
            entry.isFavorite = m_client.isTitleTracked(item.titleId);
            entry.selected = entry.isFavorite;

            result.push_back(std::move(entry));
        }

        return result;
    }

    SyncResult DriveSyncCore::performFullBackup(Assets::TitleManager& titleManager, ProgressCallback onProgress) {
        std::vector<GameSaveInfo> allSaves = m_saveManager.listAvailableSaves(m_activeUid);
        std::vector<u64> tids;
        for (const auto& s : allSaves) tids.push_back(s.titleId);
        return performSelectiveBackup(tids, titleManager, onProgress);
    }

    SyncResult DriveSyncCore::performSelectiveBackup(const std::vector<u64>& selectedTitles,
                                                    Assets::TitleManager& titleManager,
                                                    ProgressCallback onProgress) {
        SyncResult result;
        if (selectedTitles.empty()) return result;

        int total = static_cast<int>(selectedTitles.size());

        for (int idx = 0; idx < total; ++idx) {
            u64 tid = selectedTitles[idx];
            std::string gameName = titleManager.getTitleName(tid);

            if (onProgress) {
                onProgress(gameName, "Compactando arquivos de save...", (static_cast<float>(idx) + 0.3f) / total);
            }

            fsdevUnmountDevice("save");

            std::string zipPath;
            bool changed = false;
            bool procOk = m_saveManager.processGameSave(tid, m_activeUid, zipPath, changed, true);

            if (!procOk) {
                result.failCount++;
                result.details.push_back(gameName + " (Falha ao ler save)");
                continue;
            }

            if (onProgress) {
                onProgress(gameName, "Transmitindo para o Google Drive...", (static_cast<float>(idx) + 0.7f) / total);
            }

            u32 newCrc = 0;
            ZipHelper::calculateFileCrc32(zipPath, newCrc);

            // Snapshot versionado no Drive
            std::string rootFid = m_client.ensureFolderExists(m_client.getFolderName());
            if (!rootFid.empty()) {
                char tidHex[32];
                snprintf(tidHex, sizeof(tidHex), "%016llX", (unsigned long long)tid);
                std::string tFid = m_client.ensureSubFolderExists(tidHex, rootFid);
                if (!tFid.empty()) {
                    std::string uStr = SaveManager::formatUid(m_activeUid);
                    std::string aFid = m_client.ensureSubFolderExists(uStr, tFid);
                    if (!aFid.empty()) {
                        std::time_t nowTs = std::time(nullptr);
                        char snapName[256];
                        snprintf(snapName, sizeof(snapName), "%016llX_%s_%lld_%08X.zip",
                                 (unsigned long long)tid, uStr.c_str(), (long long)nowTs, newCrc);
                        m_client.uploadSnapshotFile(aFid, snapName, zipPath, nullptr);
                    }
                }
            }

            // Upload do arquivo principal
            std::string uploadErr;
            bool upOk = m_client.uploadSaveArchive(tid, zipPath, &uploadErr);
            if (upOk) {
                m_saveManager.markSaveUploaded(tid, newCrc);
                m_saveManager.setLocalSaveTimestamp(tid, std::time(nullptr));
                result.successCount++;
            } else {
                result.failCount++;
                result.details.push_back(gameName + " (" + (uploadErr.empty() ? "Erro HTTP" : uploadErr) + ")");
            }

            remove(zipPath.c_str());
        }

        if (result.successCount > 0) {
            m_lastBackupTime = std::time(nullptr);
        }

        return result;
    }

    SyncResult DriveSyncCore::performRestore(Assets::TitleManager& titleManager, ProgressCallback onProgress) {
        SyncResult result;

        std::time_t currentRtc = 0;
        if (!SaveManager::isConsoleClockSane(&currentRtc)) {
            result.failCount = 1;
            result.details.push_back("Relogio do console descalibrado. Ajuste a data/hora do sistema.");
            return result;
        }

        std::string listErr;
        std::vector<DriveFileInfo> driveFiles = m_client.listDriveSaves(&listErr);
        if (driveFiles.empty()) {
            result.details.push_back("Nenhum save encontrado na pasta do Google Drive.");
            return result;
        }

        int total = static_cast<int>(driveFiles.size());

        for (int idx = 0; idx < total; ++idx) {
            const auto& file = driveFiles[idx];
            u64 tid = file.titleId;
            std::string gameName = titleManager.getTitleName(tid);

            std::time_t driveTs = file.modifiedTime;
            std::time_t localTs = m_saveManager.getLocalSaveTimestamp(tid, m_activeUid);

            if (driveTs <= localTs) {
                continue;
            }

            if (onProgress) {
                onProgress(gameName, "Baixando versao mais recente da nuvem...", (static_cast<float>(idx) + 0.4f) / total);
            }

            std::string tempZip = "sdmc:/config/gdrive-sync/temp/restore_" + std::to_string(tid) + ".zip";
            std::string dlErr;
            if (!m_client.downloadSaveFile(file.fileId, tempZip, &dlErr)) {
                result.failCount++;
                result.details.push_back(gameName + " (Falha no download)");
                continue;
            }

            if (onProgress) {
                onProgress(gameName, "Restaurando arquivos no console...", (static_cast<float>(idx) + 0.8f) / total);
            }

            std::string restoreErr;
            bool resOk = RestoreManager::restoreSaveFromZip(tid, m_activeUid, tempZip, &restoreErr);
            remove(tempZip.c_str());

            if (resOk) {
                m_saveManager.setLocalSaveTimestamp(tid, driveTs);
                result.successCount++;
            } else {
                result.failCount++;
                result.details.push_back(gameName + " (" + restoreErr + ")");
            }
        }

        return result;
    }

    bool DriveSyncCore::testConnection(std::string* outError) {
        if (!isNetworkConnected()) {
            if (outError) *outError = "Sem conexao de rede ativa no Switch.";
            return false;
        }

        std::string fid = m_client.ensureFolderExists(m_client.getFolderName());
        if (fid.empty()) {
            if (outError) *outError = "Falha ao autenticar ou acessar a pasta no Google Drive.";
            return false;
        }

        return true;
    }

} // namespace Core

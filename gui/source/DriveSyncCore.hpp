#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <functional>
#include <ctime>
#include "../../source/GDriveClient.hpp"
#include "../../source/SaveManager.hpp"
#include "TitleManager.hpp"

namespace Core {

    struct SyncResult {
        int successCount = 0;
        int failCount = 0;
        std::vector<std::string> details;
    };

    using ProgressCallback = std::function<void(const std::string& gameName, const std::string& stage, float progress)>;

    class DriveSyncCore {
    private:
        GDriveClient m_client;
        SaveManager  m_saveManager;
        AccountUid   m_activeUid{};
        bool         m_initialized = false;
        std::time_t  m_lastBackupTime = 0;

    public:
        DriveSyncCore();
        ~DriveSyncCore();

        bool init();
        void close();

        GDriveClient& getClient() { return m_client; }
        SaveManager& getSaveManager() { return m_saveManager; }
        AccountUid getActiveUid() const { return m_activeUid; }

        bool isNetworkConnected() const;
        bool isAuthorized() const;

        std::string getProfileNickname() const;
        std::string getFolderName() const;
        void setFolderName(const std::string& folderName);

        std::string getClientId() const;
        void setClientId(const std::string& id);

        std::string getClientSecret() const;
        void setClientSecret(const std::string& secret);

        std::time_t getLastBackupTimestamp() const;
        void saveConfig();

        // Carrega a lista completa de jogos com save e status
        std::vector<Assets::GameEntry> loadGamesList(Assets::TitleManager& titleManager);

        // Operações de Backup e Restauração
        SyncResult performFullBackup(Assets::TitleManager& titleManager, ProgressCallback onProgress);
        SyncResult performSelectiveBackup(const std::vector<u64>& selectedTitles, Assets::TitleManager& titleManager, ProgressCallback onProgress);
        SyncResult performRestore(Assets::TitleManager& titleManager, ProgressCallback onProgress);

        // Validação de Conexão
        bool testConnection(std::string* outError = nullptr);
    };

} // namespace Core

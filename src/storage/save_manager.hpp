#pragma once
#include "../common.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <mutex>
#include <ctime>

namespace Storage {

    struct GameSaveInfo {
        u64 titleId = 0;
        AccountUid uid{};
        std::string uidStr;
        std::time_t lastModified = 0;
        u32 lastCrc = 0;
        bool isDeviceSave = false;
    };

    struct TransactionalRestoreResult {
        bool success = false;
        std::string errorMessage;
        std::string backupPath;
        size_t restoredFilesCount = 0;
    };

    class SaveManager {
    public:
        SaveManager();
        ~SaveManager();

        // Obtém o perfil de usuário ativo no console
        static bool getActiveAccount(AccountUid& outUid);

        // Enumera todos os jogos com partição de save existente no console para o usuário
        std::vector<GameSaveInfo> listAvailableSaves(const AccountUid& uid);

        // Gestão de timestamps e metadados locais gravados no SDMC
        std::time_t getLocalSaveTimestamp(u64 titleId, const AccountUid& uid);
        void setLocalSaveTimestamp(u64 titleId, std::time_t ts);

        // Executa a restauração transacional com backup preventivo e rollback garantido
        static TransactionalRestoreResult restoreSaveFromZip(
            u64 titleId,
            const AccountUid& uid,
            std::string_view localZipPath,
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Formatadores utilitários de AccountUid
        static std::string formatUid(const AccountUid& uid);
        static AccountUid parseUid(std::string_view str);

    private:
        void loadTimestamps();
        void saveTimestamps();

        std::mutex m_mutex;
        std::map<u64, std::time_t> m_timestamps;
        std::string m_timestampsPath;
    };

} // namespace Storage

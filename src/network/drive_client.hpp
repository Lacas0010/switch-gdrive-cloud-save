#pragma once
#include "../common.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>

namespace Network {

    struct DriveFileInfo {
        std::string id;
        std::string name;
        std::time_t modifiedTime = 0;
        size_t size = 0;
        u64 titleId = 0;
    };

    struct DriveSnapshotVersion {
        std::string fileId;
        std::string fileName;
        u64 titleId = 0;
        std::string accountUid;
        size_t sizeBytes = 0;
        std::time_t timestampUtc = 0;
        u32 crc32 = 0;
        std::string formattedUtcTime;
    };

    class DriveClient {
    public:
        using ProgressCallback = std::function<void(float progress, size_t currentBytes, size_t totalBytes)>;

        DriveClient();
        ~DriveClient();

        // Configuração e credenciais
        bool loadConfig();
        bool saveConfig();
        bool isConfigured() const noexcept;

        const std::string& getFolderName() const noexcept { return m_folderName; }
        void setFolderName(std::string_view name) { m_folderName = name; }
        const std::string& getClientId() const noexcept { return m_clientId; }
        void setClientId(std::string_view id) { m_clientId = id; }
        const std::string& getClientSecret() const noexcept { return m_clientSecret; }
        void setClientSecret(std::string_view secret) { m_clientSecret = secret; }
        const std::string& getAccessToken() const noexcept { return m_accessToken; }
        const std::string& getRefreshToken() const noexcept { return m_refreshToken; }

        // Teste de conectividade com a API
        bool testConnection(std::string* outError = nullptr);

        // Renovação automática de access token
        bool refreshAccessToken(
            std::string* outError = nullptr,
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Navegação de pastas no Google Drive
        std::string ensureFolderExists(
            std::string_view folderName,
            const std::atomic<bool>* cancelToken = nullptr
        );
        std::string ensureSubFolderExists(
            std::string_view folderName,
            std::string_view parentFolderId,
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Upload em Lockstep: Raiz Mutável (POST/PATCH) e Snapshot Append-Only Imutável
        bool uploadSaveLockstep(
            u64 titleId,
            std::string_view accountUid,
            std::string_view zipFilePath,
            u32 crc32,
            std::time_t timestampUtc,
            const std::atomic<bool>* cancelToken = nullptr,
            ProgressCallback onProgress = nullptr,
            std::string* outError = nullptr
        );

        // Download de arquivo do Google Drive em blocos de 32 KB com checagem de cancelamento
        bool downloadFile(
            std::string_view fileId,
            std::string_view destinationPath,
            const std::atomic<bool>* cancelToken = nullptr,
            ProgressCallback onProgress = nullptr,
            std::string* outError = nullptr
        );

        // Listagem de saves para restauração
        std::vector<DriveFileInfo> listDriveSaves(
            std::string* outError = nullptr,
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Listagem de histórico versionado para gerenciamento por jogo
        std::vector<DriveSnapshotVersion> listGameVersions(
            u64 titleId,
            std::string_view accountUid,
            std::string* outError = nullptr,
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Busca arquivo na pasta (incluindo recuperação automática da lixeira via PATCH)
        std::string findFileInFolder(
            std::string_view fileName,
            std::string_view parentFolderId,
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Requisições HTTP base padronizadas e protegidas contra congelamento
        std::string httpGet(
            std::string_view url,
            std::string_view bearerToken = "",
            const std::atomic<bool>* cancelToken = nullptr
        );
        std::string httpPost(
            std::string_view url,
            std::string_view postData,
            std::string_view bearerToken = "",
            const std::atomic<bool>* cancelToken = nullptr
        );
        std::string httpPatch(
            std::string_view url,
            std::string_view patchJson,
            std::string_view bearerToken = "",
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Utilitários JSON e URL
        static std::string extractJsonString(std::string_view json, std::string_view key);
        static std::string urlEncode(std::string_view value);

    private:
        std::mutex m_mutex;
        std::string m_folderName;
        std::string m_folderId;
        std::string m_clientId;
        std::string m_clientSecret;
        std::string m_accessToken;
        std::string m_refreshToken;
        std::string m_configFilePath;

        // Executa requisição curl configurando timeouts e callback de cancelamento
        CURLcode executeCurlRequest(
            CURL* curl,
            std::string* outResponse,
            const std::atomic<bool>* cancelToken
        );
    };

} // namespace Network

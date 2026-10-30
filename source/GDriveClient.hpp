#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <ctime>

struct DriveFileInfo {
    std::string id;
    std::string name;
    u64 titleId = 0;
    std::time_t modifiedTime = 0; // Unix Epoch UTC em segundos
    size_t size = 0;
};

class GDriveClient {
public:
    GDriveClient();
    ~GDriveClient();

    // Carrega credenciais e tokens salvos no SD
    bool loadConfig();
    bool saveConfig();

    bool isConfigured() const { return !m_clientId.empty() && !m_clientSecret.empty(); }
    bool isAuthorized() const { return !m_refreshToken.empty(); }

    // Getters e Setters para manipulacao via GUI / Teclado
    std::string getClientId() const { return m_clientId; }
    void setClientId(const std::string& id) { m_clientId = id; }

    std::string getClientSecret() const { return m_clientSecret; }
    void setClientSecret(const std::string& secret) { m_clientSecret = secret; }

    std::string getFolderName() const { return m_folderName; }
    void setFolderName(const std::string& name) { m_folderName = name; }

    std::string getFolderId() const { return m_folderId; }
    void setFolderId(const std::string& id) { m_folderId = id; }

    std::string getPendingUserCode() const { return m_pendingUserCode; }
    std::string getPendingVerifyUrl() const { return m_pendingVerifyUrl; }

    // Sync mode ("all" | "whitelist") e tracked_titles
    std::string getSyncMode() const { return m_syncMode; }
    void setSyncMode(const std::string& mode) { m_syncMode = mode; }

    const std::vector<std::string>& getTrackedTitles() const { return m_trackedTitles; }
    void setTrackedTitles(const std::vector<std::string>& titles) { m_trackedTitles = titles; }
    bool isTitleTracked(u64 titleId) const;

    // Inicia fluxo OAuth2 Device Flow gravando auth_request.txt no SD
    bool requestDeviceCode();

    // Faz uma checagem de polling para ver se o usuario autorizou no navegador/celular
    bool pollAuthorization();

    // Renova o token de acesso de curta duracao
    bool refreshAccessToken(std::string* outError = nullptr);

    // Garante que a pasta destino exista no Google Drive (cria se necessario e salva folder_id)
    std::string ensureFolderExists(const std::string& folderName = "");

    // Busca se um arquivo com o nome informado ja existe dentro da pasta informada
    std::string findFileInFolder(const std::string& fileName, const std::string& parentFolderId);

    // Faz upload ou atualizacao (PATCH) do .zip do save sem duplicatas
    bool uploadSaveArchive(u64 titleId, const std::string& zipPath, std::string* outError = nullptr);

    // Lista os saves existentes na pasta do Google Drive com timestamps e metadados
    std::vector<DriveFileInfo> listDriveSaves(std::string* outError = nullptr);

    // Baixa o arquivo do Google Drive diretamente para o arquivo destinationPath no SD
    bool downloadFile(const std::string& fileId, const std::string& destinationPath, std::string* outError = nullptr);

    // Converte timestamp ISO 8601 UTC (ex: "2026-10-30T21:40:00.000Z") para std::time_t
    static std::time_t parseIso8601ToUnixTime(const std::string& isoStr);

private:
    std::string m_clientId;
    std::string m_clientSecret;
    std::string m_refreshToken;
    std::string m_accessToken;
    std::string m_folderId;
    std::string m_folderName;

    std::string m_pendingDeviceCode;
    std::string m_pendingUserCode;
    std::string m_pendingVerifyUrl;

    std::string m_syncMode = "all"; // "all" ou "whitelist"
    std::vector<std::string> m_trackedTitles;
    u64         m_lastPollTime;
    u32         m_pollIntervalSec;

    // Metodos auxiliares REST via libcurl
    std::string httpPost(const std::string& url, const std::string& postData, const std::string& authHeader = "");
    std::string httpPatch(const std::string& url, const std::string& patchData, const std::string& authHeader = "");
    std::string httpGet(const std::string& url, const std::string& authHeader);

    static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

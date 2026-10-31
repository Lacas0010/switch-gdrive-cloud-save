#include "GDriveClient.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdarg>
#include <sstream>
#include <iomanip>
#include <vector>
#include <curl/curl.h>

namespace {
    void logClient(const char* fmt, ...) {
        FILE* f = fopen("sdmc:/config/gdrive-sync/sysmodule.log", "a");
        if (!f) return;
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        fprintf(f, "\n");
        va_end(args);
        fclose(f);
    }

    size_t curlFileReadCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
        FILE* fp = static_cast<FILE*>(userdata);
        return fread(buffer, size, nitems, fp);
    }

    std::string urlEncode(const std::string& value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (char c : value) {
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::uppercase << std::setw(2) << ((int)(unsigned char)c);
            }
        }

        return escaped.str();
    }

    std::string extractJsonString(const std::string& json, const std::string& key) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return "";

        pos = json.find(':', pos + pattern.size());
        if (pos == std::string::npos) return "";

        pos = json.find('\"', pos);
        if (pos == std::string::npos) return "";

        size_t start = pos + 1;
        size_t end = json.find('\"', start);
        if (end == std::string::npos) return "";

        return json.substr(start, end - start);
    }

    int extractJsonInt(const std::string& json, const std::string& key, int defaultVal = 5) {
        std::string pattern = "\"" + key + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return defaultVal;

        pos = json.find(':', pos + pattern.size());
        if (pos == std::string::npos) return defaultVal;

        while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t')) {
            pos++;
        }

        int val = defaultVal;
        if (sscanf(json.c_str() + pos, "%d", &val) == 1) {
            return val;
        }
        return defaultVal;
    }

    std::vector<std::string> extractJsonStringArray(const std::string& json, const std::string& key) {
        std::vector<std::string> results;
        std::string pattern = "\"" + key + "\"";
        size_t keyPos = json.find(pattern);
        if (keyPos == std::string::npos) return results;

        size_t startBracket = json.find('[', keyPos + pattern.size());
        if (startBracket == std::string::npos) return results;

        size_t endBracket = json.find(']', startBracket);
        if (endBracket == std::string::npos) return results;

        std::string inside = json.substr(startBracket + 1, endBracket - startBracket - 1);
        bool inString = false;
        std::string current;

        for (size_t i = 0; i < inside.length(); ++i) {
            char c = inside[i];
            if (c == '\\' && i + 1 < inside.length()) {
                current += inside[++i];
                continue;
            }
            if (c == '"') {
                inString = !inString;
                if (!inString) {
                    if (!current.empty()) results.push_back(current);
                    current.clear();
                }
            } else if (inString) {
                current += c;
            }
        }
        return results;
    }

    std::string extractFirstFileId(const std::string& json) {
        size_t filesPos = json.find("\"files\"");
        if (filesPos == std::string::npos) return "";

        size_t arrayStart = json.find('[', filesPos);
        if (arrayStart == std::string::npos) return "";

        size_t arrayEnd = json.find(']', arrayStart);
        if (arrayEnd == std::string::npos) return "";

        std::string filesArray = json.substr(arrayStart, arrayEnd - arrayStart + 1);
        return extractJsonString(filesArray, "id");
    }
}

size_t GDriveClient::curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

GDriveClient::GDriveClient()
    : m_folderName(Config::DEFAULT_FOLDER_NAME),
      m_lastPollTime(0),
      m_pollIntervalSec(5) {
    loadConfig();
}

GDriveClient::~GDriveClient() {
    saveConfig();
}

bool GDriveClient::loadConfig() {
    FILE* f = fopen(Config::CONFIG_FILE.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 64 * 1024) {
        fclose(f);
        return false;
    }

    std::string content(size, '\0');
    fread(content.data(), 1, size, f);
    fclose(f);

    m_clientId     = extractJsonString(content, "client_id");
    m_clientSecret = extractJsonString(content, "client_secret");
    m_refreshToken = extractJsonString(content, "refresh_token");
    m_accessToken  = extractJsonString(content, "access_token");

    // Aceita tanto "folder_id" quanto legado "root_folder_id"
    std::string fid = extractJsonString(content, "folder_id");
    if (fid.empty()) fid = extractJsonString(content, "root_folder_id");
    m_folderId = fid;

    std::string fname = extractJsonString(content, "folder_name");
    if (!fname.empty()) {
        m_folderName = fname;
    } else {
        m_folderName = Config::DEFAULT_FOLDER_NAME;
    }

    std::string mode = extractJsonString(content, "sync_mode");
    if (!mode.empty()) {
        m_syncMode = mode;
    } else {
        m_syncMode = "all";
    }

    m_trackedTitles = extractJsonStringArray(content, "tracked_titles");

    return isConfigured();
}

bool GDriveClient::saveConfig() {
    FILE* f = fopen(Config::CONFIG_FILE.c_str(), "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"client_id\": \"%s\",\n", m_clientId.c_str());
    fprintf(f, "  \"client_secret\": \"%s\",\n", m_clientSecret.c_str());
    fprintf(f, "  \"refresh_token\": \"%s\",\n", m_refreshToken.c_str());
    fprintf(f, "  \"access_token\": \"%s\",\n", m_accessToken.c_str());
    fprintf(f, "  \"folder_id\": \"%s\",\n", m_folderId.c_str());
    fprintf(f, "  \"folder_name\": \"%s\",\n", m_folderName.c_str());
    fprintf(f, "  \"sync_mode\": \"%s\",\n", m_syncMode.c_str());
    fprintf(f, "  \"tracked_titles\": [\n");
    for (size_t i = 0; i < m_trackedTitles.size(); ++i) {
        fprintf(f, "    \"%s\"%s\n", m_trackedTitles[i].c_str(), (i + 1 < m_trackedTitles.size()) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    return true;
}

bool GDriveClient::isTitleTracked(u64 titleId) const {
    if (m_syncMode != "whitelist") {
        return true; // Se for "all", todos os jogos sao rastreados
    }

    char tidHex[32];
    snprintf(tidHex, sizeof(tidHex), "%016llx", (unsigned long long)titleId);
    char tidHexUpper[32];
    snprintf(tidHexUpper, sizeof(tidHexUpper), "%016llX", (unsigned long long)titleId);

    for (const auto& t : m_trackedTitles) {
        if (strcasecmp(t.c_str(), tidHex) == 0 || strcasecmp(t.c_str(), tidHexUpper) == 0) {
            return true;
        }
    }
    return false;
}

std::string GDriveClient::httpPost(const std::string& url, const std::string& postData, const std::string& authHeader) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    if (!authHeader.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + authHeader).c_str());
    }
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Expect:");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        logClient("[HTTP] POST %s falhou (code %d): %s", url.c_str(), res, curl_easy_strerror(res));
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? response : "";
}

std::string GDriveClient::httpPatch(const std::string& url, const std::string& patchData, const std::string& authHeader) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    if (!authHeader.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + authHeader).c_str());
    }
    headers = curl_slist_append(headers, "Expect:");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, patchData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        logClient("[HTTP] PATCH %s falhou (code %d): %s", url.c_str(), res, curl_easy_strerror(res));
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? response : "";
}

std::string GDriveClient::httpGet(const std::string& url, const std::string& authHeader) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    if (!authHeader.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + authHeader).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        logClient("[HTTP] GET %s falhou (code %d): %s", url.c_str(), res, curl_easy_strerror(res));
    }
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? response : "";
}

bool GDriveClient::requestDeviceCode() {
    if (!isConfigured()) return false;

    std::string postData = "client_id=" + m_clientId + "&scope=https://www.googleapis.com/auth/drive.file";
    std::string resp = httpPost("https://oauth2.googleapis.com/device/code", postData);

    if (resp.empty()) return false;

    m_pendingDeviceCode = extractJsonString(resp, "device_code");
    m_pendingUserCode   = extractJsonString(resp, "user_code");
    m_pendingVerifyUrl  = extractJsonString(resp, "verification_url");
    m_pollIntervalSec   = extractJsonInt(resp, "interval", 5);

    if (m_pendingDeviceCode.empty() || m_pendingUserCode.empty()) {
        return false;
    }

    FILE* f = fopen(Config::AUTH_FILE.c_str(), "w");
    if (f) {
        fprintf(f, "=== AUTORIZACAO GOOGLE DRIVE (DEVICE FLOW) ===\n\n");
        fprintf(f, "1. Acesse pelo celular ou PC:\n   %s\n\n", m_pendingVerifyUrl.c_str());
        fprintf(f, "2. Digite o codigo:\n   %s\n\n", m_pendingUserCode.c_str());
        fprintf(f, "Aguardando confirmacao do usuario em segundo plano...\n");
        fclose(f);
    }

    m_lastPollTime = time(nullptr);
    return true;
}

bool GDriveClient::pollAuthorization() {
    if (m_pendingDeviceCode.empty()) return false;

    u64 now = time(nullptr);
    if (now - m_lastPollTime < m_pollIntervalSec) {
        return false;
    }
    m_lastPollTime = now;

    std::string postData = "client_id=" + m_clientId +
                           "&client_secret=" + m_clientSecret +
                           "&device_code=" + m_pendingDeviceCode +
                           "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

    std::string resp = httpPost("https://oauth2.googleapis.com/token", postData);
    if (resp.empty()) return false;

    std::string error = extractJsonString(resp, "error");
    if (!error.empty()) {
        if (error == "authorization_pending") {
            return false;
        } else if (error == "slow_down") {
            m_pollIntervalSec += 5;
            return false;
        }
        m_pendingDeviceCode.clear();
        m_pendingUserCode.clear();
        m_pendingVerifyUrl.clear();
        remove(Config::AUTH_FILE.c_str());
        return false;
    }

    std::string accessToken = extractJsonString(resp, "access_token");
    std::string refreshToken = extractJsonString(resp, "refresh_token");

    if (!refreshToken.empty()) {
        m_accessToken = accessToken;
        m_refreshToken = refreshToken;
        m_pendingDeviceCode.clear();
        m_pendingUserCode.clear();
        m_pendingVerifyUrl.clear();

        saveConfig();
        remove(Config::AUTH_FILE.c_str());
        return true;
    }

    return false;
}

bool GDriveClient::refreshAccessToken(std::string* outError) {
    if (m_refreshToken.empty()) {
        if (outError) *outError = "Refresh token ausente no config.json";
        logClient("[AUTH] refreshAccessToken falhou: refresh_token vazio.");
        return false;
    }

    std::string postData = "client_id=" + urlEncode(m_clientId) +
                           "&client_secret=" + urlEncode(m_clientSecret) +
                           "&refresh_token=" + urlEncode(m_refreshToken) +
                           "&grant_type=refresh_token";

    std::string resp = httpPost("https://oauth2.googleapis.com/token", postData);
    if (resp.empty()) {
        if (outError) *outError = "Sem resposta do servidor OAuth (verifique Wi-Fi/SSL)";
        logClient("[AUTH] refreshAccessToken falhou: sem resposta do Google.");
        return false;
    }

    std::string err = extractJsonString(resp, "error_description");
    if (err.empty()) err = extractJsonString(resp, "error");
    if (!err.empty()) {
        if (outError) *outError = "OAuth: " + err;
        logClient("[AUTH] refreshAccessToken erro: %s", err.c_str());
        return false;
    }

    std::string newToken = extractJsonString(resp, "access_token");
    if (!newToken.empty()) {
        m_accessToken = newToken;
        saveConfig();
        return true;
    }

    if (outError) *outError = "access_token ausente na resposta OAuth";
    logClient("[AUTH] refreshAccessToken falhou: access_token ausente na resposta.");
    return false;
}

std::string GDriveClient::ensureFolderExists(const std::string& folderName) {
    std::string targetName = folderName.empty() ? m_folderName : folderName;
    if (targetName.empty()) {
        targetName = Config::DEFAULT_FOLDER_NAME;
    }

    // 1. Se ja temos folder_id salvo, valida se a pasta ainda existe
    if (!m_folderId.empty()) {
        std::string checkUrl = "https://www.googleapis.com/drive/v3/files/" + m_folderId + "?fields=id,trashed";
        std::string checkResp = httpGet(checkUrl, m_accessToken);
        std::string trashed = extractJsonString(checkResp, "trashed");
        if (trashed != "true" && !extractJsonString(checkResp, "id").empty()) {
            return m_folderId;
        }
        // Se a pasta foi apagada ou o ID e invalido, limpa o cache
        m_folderId.clear();
    }

    // 2. Busca pasta pelo nome via GET: q=mimeType='application/vnd.google-apps.folder' and name='<nome>' and trashed=false
    std::string rawQuery = "mimeType='application/vnd.google-apps.folder' and name='" + targetName + "' and trashed=false";
    std::string searchUrl = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(rawQuery) + "&fields=files(id,name)";

    std::string searchResp = httpGet(searchUrl, m_accessToken);
    std::string foundId = extractFirstFileId(searchResp);

    if (!foundId.empty()) {
        m_folderId = foundId;
        m_folderName = targetName;
        saveConfig();
        return m_folderId;
    }

    // 3. Se nao existe, cria via POST: {"name": "<nome>", "mimeType": "application/vnd.google-apps.folder"}
    std::string createBody = "{\"name\": \"" + targetName + "\", \"mimeType\": \"application/vnd.google-apps.folder\"}";

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_accessToken).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");

    curl_easy_setopt(curl, CURLOPT_URL, "https://www.googleapis.com/drive/v3/files");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, createBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        std::string newId = extractJsonString(response, "id");
        if (!newId.empty()) {
            m_folderId = newId;
            m_folderName = targetName;
            saveConfig();
            return m_folderId;
        }
    }

    return "";
}

std::string GDriveClient::ensureSubFolderExists(const std::string& folderName, const std::string& parentFolderId) {
    if (folderName.empty() || parentFolderId.empty()) return "";

    // 1. Busca se a subpasta ja existe dentro de parentFolderId
    std::string rawQuery = "mimeType='application/vnd.google-apps.folder' and name='" + folderName + "' and '" + parentFolderId + "' in parents and trashed=false";
    std::string searchUrl = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(rawQuery) + "&fields=files(id,name)";

    std::string searchResp = httpGet(searchUrl, m_accessToken);
    std::string foundId = extractFirstFileId(searchResp);
    if (!foundId.empty()) {
        return foundId;
    }

    // 2. Se nao existe, cria via POST com parentFolderId
    std::string createBody = "{\"name\": \"" + folderName + "\", \"mimeType\": \"application/vnd.google-apps.folder\", \"parents\": [\"" + parentFolderId + "\"]}";

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_accessToken).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");

    curl_easy_setopt(curl, CURLOPT_URL, "https://www.googleapis.com/drive/v3/files");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, createBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        std::string newId = extractJsonString(response, "id");
        if (!newId.empty()) {
            return newId;
        }
    }
    return "";
}

std::string GDriveClient::findFileInFolder(const std::string& fileName, const std::string& parentFolderId) {
    // Busca se ja existe arquivo com o mesmo nome na pasta (inclusive se estiver na lixeira)
    std::string rawQuery = "name='" + fileName + "' and '" + parentFolderId + "' in parents";
    std::string searchUrl = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(rawQuery) + "&fields=files(id,name,trashed)";

    std::string searchResp = httpGet(searchUrl, m_accessToken);
    std::string foundId = extractFirstFileId(searchResp);

    if (!foundId.empty()) {
        std::string trashedVal = extractJsonString(searchResp, "trashed");
        if (trashedVal == "true") {
            httpPatch("https://www.googleapis.com/drive/v3/files/" + foundId, "{\"trashed\": false}", m_accessToken);
            logClient("[DRIVE] Arquivo %s (ID %s) recuperado da lixeira.", fileName.c_str(), foundId.c_str());
        }
    }

    return foundId;
}

bool GDriveClient::uploadSaveArchive(u64 titleId, const std::string& zipPath, std::string* outError) {
    if (!refreshAccessToken(outError)) {
        logClient("[UPLOAD] Falha ao renovar token de acesso para titleId 0x%016llX", (unsigned long long)titleId);
        Logger::logSync("[%016llX] Google Drive API: FAILED (Token refresh failed: %s)",
                        (unsigned long long)titleId, (outError ? outError->c_str() : "unknown"));
        remove(zipPath.c_str());
        return false;
    }

    // Garante que a pasta destino exista no Google Drive (ex: "Switch Saves")
    std::string folderId = ensureFolderExists(m_folderName);
    if (folderId.empty()) {
        if (outError) *outError = "Pasta de destino nao encontrada/criada no Drive";
        logClient("[UPLOAD] ensureFolderExists retornou vazio para '%s'", m_folderName.c_str());
        Logger::logSync("[%016llX] Google Drive API: FAILED (Destination folder '%s' not found)",
                        (unsigned long long)titleId, m_folderName.c_str());
        remove(zipPath.c_str());
        return false;
    }

    // Extrai o nome do arquivo a ser salvo (ex: save_0100000000010000.zip)
    char fileNameBuf[64];
    snprintf(fileNameBuf, sizeof(fileNameBuf), "save_%016llx.zip", (unsigned long long)titleId);
    std::string fileName = fileNameBuf;

    // Checa se ja existe um arquivo com esse nome dentro de folderId para evitar duplicatas
    std::string existingFileId = findFileInFolder(fileName, folderId);
    bool isUpdate = !existingFileId.empty();

    FILE* fd = fopen(zipPath.c_str(), "rb");
    if (!fd) {
        if (outError) *outError = "Nao foi possivel abrir o arquivo .zip";
        logClient("[UPLOAD] fopen falhou para zipPath: %s", zipPath.c_str());
        Logger::logSync("[%016llX] Google Drive API: FAILED (fopen zip failed)",
                        (unsigned long long)titleId);
        return false;
    }

    std::string boundary = "-------CloudSaveBoundary7MA4YWxkTrZu0gW";
    std::string metadata;

    if (isUpdate) {
        // PATCH: apenas atualiza o arquivo sem reenviar a lista de parents
        metadata = "--" + boundary + "\r\n" +
                   "Content-Type: application/json; charset=UTF-8\r\n\r\n" +
                   "{\"name\": \"" + fileName + "\"}\r\n" +
                   "--" + boundary + "\r\n" +
                   "Content-Type: application/zip\r\n\r\n";
    } else {
        // POST: novo arquivo vinculado a folderId
        metadata = "--" + boundary + "\r\n" +
                   "Content-Type: application/json; charset=UTF-8\r\n\r\n" +
                   "{\"name\": \"" + fileName + "\", \"parents\": [\"" + folderId + "\"]}\r\n" +
                   "--" + boundary + "\r\n" +
                   "Content-Type: application/zip\r\n\r\n";
    }
    std::string closing = "\r\n--" + boundary + "--\r\n";

    // Cria payload temporario em streaming de disco para economizar RAM
    std::string payloadPath = Config::TEMP_DIR + "/upload_stream.tmp";
    FILE* streamFile = fopen(payloadPath.c_str(), "wb");
    if (!streamFile) {
        fclose(fd);
        remove(zipPath.c_str());
        if (outError) *outError = "Falha ao criar stream temporario";
        logClient("[UPLOAD] fopen falhou para streamFile: %s", payloadPath.c_str());
        return false;
    }

    fwrite(metadata.data(), 1, metadata.size(), streamFile);

    {
        constexpr size_t BUF_SIZE = 16 * 1024;
        std::vector<unsigned char> buffer(BUF_SIZE);
        size_t readBytes = 0;
        while ((readBytes = fread(buffer.data(), 1, BUF_SIZE, fd)) > 0) {
            fwrite(buffer.data(), 1, readBytes, streamFile);
        }
    }
    fclose(fd);

    fwrite(closing.data(), 1, closing.size(), streamFile);
    fclose(streamFile);

    FILE* uploadFp = fopen(payloadPath.c_str(), "rb");
    if (!uploadFp) {
        remove(payloadPath.c_str());
        remove(zipPath.c_str());
        if (outError) *outError = "Falha ao abrir stream de upload";
        logClient("[UPLOAD] fopen falhou para uploadFp: %s", payloadPath.c_str());
        return false;
    }

    fseek(uploadFp, 0, SEEK_END);
    size_t totalPayloadSize = ftell(uploadFp);
    fseek(uploadFp, 0, SEEK_SET);

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(uploadFp);
        remove(payloadPath.c_str());
        remove(zipPath.c_str());
        if (outError) *outError = "Falha ao inicializar cURL";
        logClient("[UPLOAD] curl_easy_init falhou");
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_accessToken).c_str());
    headers = curl_slist_append(headers, ("Content-Type: multipart/related; boundary=" + boundary).c_str());
    headers = curl_slist_append(headers, "Expect:"); // Evita travamento de espera '100-continue' em uploads maiores

    std::string uploadUrl;
    if (isUpdate) {
        // Sobrescreve versao existente sem duplicar arquivos via PATCH
        uploadUrl = "https://www.googleapis.com/upload/drive/v3/files/" + existingFileId + "?uploadType=multipart";
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else {
        uploadUrl = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, curlFileReadCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, uploadFp);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(totalPayloadSize));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L); // 10 minutos para suportar saves de qualquer tamanho
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L); // Se cair para < 1KB/s
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);    // por mais de 45 segundos, aborta

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(uploadFp);

    remove(payloadPath.c_str());

    if (res != CURLE_OK) {
        std::string errStr = curl_easy_strerror(res);
        if (outError) *outError = "curl: " + errStr;
        logClient("[UPLOAD] curl_easy_perform falhou (code %d): %s", res, errStr.c_str());
        Logger::logSync("[%016llX] Google Drive API: Network Error (%s)",
                        (unsigned long long)titleId, errStr.c_str());
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        std::string apiErr = extractJsonString(response, "message");
        if (apiErr.empty()) apiErr = "HTTP " + std::to_string(httpCode);
        else apiErr = "HTTP " + std::to_string(httpCode) + " (" + apiErr + ")";
        if (outError) *outError = apiErr;
        logClient("[UPLOAD] Google Drive retornou erro %ld: %s", httpCode, response.c_str());
        Logger::logSync("[%016llX] Google Drive API: HTTP %ld (Error: %s)",
                        (unsigned long long)titleId, httpCode, apiErr.c_str());
        return false;
    }

    std::string retFileId = extractJsonString(response, "id");
    if (retFileId.empty() && isUpdate) {
        retFileId = existingFileId;
    }

    Logger::logSync("[%016llX] Google Drive API: HTTP %ld (File ID: %s, Action: %s)",
                    (unsigned long long)titleId, httpCode, retFileId.c_str(), isUpdate ? "PATCH" : "POST");

    // Remove o zip temporario apenas apos confirmacao de sucesso HTTP 2xx
    remove(zipPath.c_str());

    logClient("[UPLOAD] Upload concluido com sucesso para titleId 0x%016llX (%s)",
              (unsigned long long)titleId, isUpdate ? "PATCH" : "POST");
    return true;
}

bool GDriveClient::uploadSnapshotFile(const std::string& parentFolderId, const std::string& fileName, const std::string& zipPath, std::string* outError) {
    if (!refreshAccessToken(outError)) {
        return false;
    }

    FILE* fd = fopen(zipPath.c_str(), "rb");
    if (!fd) {
        if (outError) *outError = "Nao foi possivel abrir o arquivo .zip";
        return false;
    }

    std::string boundary = "-------CloudSaveBoundary7MA4YWxkTrZu0gW";
    std::string metadata = "--" + boundary + "\r\n" +
                           "Content-Type: application/json; charset=UTF-8\r\n\r\n" +
                           "{\"name\": \"" + fileName + "\", \"parents\": [\"" + parentFolderId + "\"]}\r\n" +
                           "--" + boundary + "\r\n" +
                           "Content-Type: application/zip\r\n\r\n";
    std::string closing = "\r\n--" + boundary + "--\r\n";

    std::string payloadPath = Config::TEMP_DIR + "/upload_stream.tmp";
    FILE* streamFile = fopen(payloadPath.c_str(), "wb");
    if (!streamFile) {
        fclose(fd);
        if (outError) *outError = "Falha ao criar stream temporario";
        return false;
    }

    fwrite(metadata.data(), 1, metadata.size(), streamFile);

    {
        constexpr size_t BUF_SIZE = 16 * 1024;
        std::vector<unsigned char> buffer(BUF_SIZE);
        size_t readBytes = 0;
        while ((readBytes = fread(buffer.data(), 1, BUF_SIZE, fd)) > 0) {
            fwrite(buffer.data(), 1, readBytes, streamFile);
        }
    }
    fclose(fd);

    fwrite(closing.data(), 1, closing.size(), streamFile);
    fclose(streamFile);

    FILE* uploadFp = fopen(payloadPath.c_str(), "rb");
    if (!uploadFp) {
        remove(payloadPath.c_str());
        if (outError) *outError = "Falha ao abrir stream de upload";
        return false;
    }

    fseek(uploadFp, 0, SEEK_END);
    size_t totalPayloadSize = ftell(uploadFp);
    fseek(uploadFp, 0, SEEK_SET);

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(uploadFp);
        remove(payloadPath.c_str());
        if (outError) *outError = "Falha ao inicializar cURL";
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_accessToken).c_str());
    headers = curl_slist_append(headers, ("Content-Type: multipart/related; boundary=" + boundary).c_str());
    headers = curl_slist_append(headers, "Expect:");

    std::string uploadUrl = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, curlFileReadCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, uploadFp);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(totalPayloadSize));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(uploadFp);
    remove(payloadPath.c_str());

    if (res != CURLE_OK) {
        std::string errStr = curl_easy_strerror(res);
        if (outError) *outError = errStr;
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        std::string apiErr = extractJsonString(response, "message");
        if (apiErr.empty()) apiErr = "HTTP " + std::to_string(httpCode);
        else apiErr = "HTTP " + std::to_string(httpCode) + " (" + apiErr + ")";
        if (outError) *outError = apiErr;
        return false;
    }

    // Regra de ouro: Arquivo enviado com sucesso (HTTP 200/201). O arquivo ZIP em temp/
    // NAO e deletado aqui; a exclusao so acontece apos o caller registrar o snapshot na fila.
    return true;
}

static size_t curlFileWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    FILE* fp = static_cast<FILE*>(userp);
    return fwrite(contents, size, nmemb, fp);
}

// Algoritmo civil de Howard Hinnant (estritamente UTC, independente do fuso local)
// Calcula dias transcorridos desde 1970-01-01 de forma puramente matematica
static inline int64_t daysFromCivilUtc(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);            // [0, 399]
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
    return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
}

std::time_t GDriveClient::parseIso8601ToUnixTime(const std::string& iso) {
    if (iso.empty()) return 0;

    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    // Google Drive API retorna RFC 3339 / ISO 8601: "YYYY-MM-DDTHH:MM:SS" (com .fffZ ou Z opcional)
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) < 6) {
        return 0;
    }

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
        return 0;
    }

    int64_t days = daysFromCivilUtc(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int64_t epochSeconds = days * 86400LL + hour * 3600LL + min * 60LL + sec;

    // Tratamento de fuso horario caso haja offset explicito (+HH:MM ou -HH:MM) em vez de 'Z'
    size_t tzPos = iso.find_first_of("+-", 19);
    if (tzPos != std::string::npos && tzPos + 5 < iso.size()) {
        char sign = iso[tzPos];
        int tzHour = 0, tzMin = 0;
        if (sscanf(iso.c_str() + tzPos + 1, "%d:%d", &tzHour, &tzMin) >= 1) {
            int offsetSeconds = tzHour * 3600 + tzMin * 60;
            if (sign == '+') {
                epochSeconds -= offsetSeconds; // Normaliza para UTC
            } else if (sign == '-') {
                epochSeconds += offsetSeconds;
            }
        }
    }

    return static_cast<std::time_t>(epochSeconds);
}

std::vector<DriveFileInfo> GDriveClient::listDriveSaves(std::string* outError) {
    std::vector<DriveFileInfo> results;
    if (!refreshAccessToken(outError)) {
        return results;
    }

    std::string folderId = ensureFolderExists(m_folderName);
    if (folderId.empty()) {
        if (outError) *outError = "Pasta de saves no Drive nao encontrada";
        return results;
    }

    std::string rawQuery = "'" + folderId + "' in parents and trashed = false";
    std::string url = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(rawQuery) +
                      "&fields=" + urlEncode("files(id,name,modifiedTime,size)") + "&pageSize=100";

    std::string jsonResponse = httpGet(url, m_accessToken);
    if (jsonResponse.empty()) {
        if (outError) *outError = "Sem resposta da API do Google Drive";
        return results;
    }

    size_t filesPos = jsonResponse.find("\"files\"");
    if (filesPos == std::string::npos) {
        return results;
    }

    size_t arrayStart = jsonResponse.find('[', filesPos);
    size_t arrayEnd = jsonResponse.rfind(']');
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos) {
        return results;
    }

    size_t cur = arrayStart;
    while ((cur = jsonResponse.find('{', cur)) != std::string::npos && cur < arrayEnd) {
        size_t objEnd = jsonResponse.find('}', cur);
        if (objEnd == std::string::npos || objEnd > arrayEnd) break;

        std::string obj = jsonResponse.substr(cur, objEnd - cur + 1);
        DriveFileInfo info;
        info.id = extractJsonString(obj, "id");
        info.name = extractJsonString(obj, "name");
        std::string modTimeStr = extractJsonString(obj, "modifiedTime");
        info.modifiedTime = parseIso8601ToUnixTime(modTimeStr);

        std::string sizeStr = extractJsonString(obj, "size");
        if (!sizeStr.empty()) {
            info.size = static_cast<size_t>(strtoull(sizeStr.c_str(), nullptr, 10));
        }

        unsigned long long tid = 0;
        if (sscanf(info.name.c_str(), "save_%llx.zip", &tid) == 1 ||
            sscanf(info.name.c_str(), "save_%llX.zip", &tid) == 1) {
            info.titleId = tid;
        }

        if (!info.id.empty() && !info.name.empty()) {
            results.push_back(info);
        }

        cur = objEnd + 1;
    }

    return results;
}

std::vector<DriveFileInfo> GDriveClient::listFilesInFolder(const std::string& parentFolderId, std::string* outError) {
    std::vector<DriveFileInfo> results;
    if (parentFolderId.empty()) {
        if (outError) *outError = "ID de pasta pai invalido";
        return results;
    }

    if (!refreshAccessToken(outError)) {
        return results;
    }

    std::string rawQuery = "'" + parentFolderId + "' in parents and trashed = false";
    std::string url = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(rawQuery) +
                      "&fields=" + urlEncode("files(id,name,modifiedTime,size)") +
                      "&orderBy=" + urlEncode("modifiedTime desc") +
                      "&pageSize=100";

    std::string jsonResponse = httpGet(url, m_accessToken);
    if (jsonResponse.empty()) {
        if (outError) *outError = "Sem resposta da API do Google Drive";
        return results;
    }

    size_t filesPos = jsonResponse.find("\"files\"");
    if (filesPos == std::string::npos) {
        return results;
    }

    size_t arrayStart = jsonResponse.find('[', filesPos);
    size_t arrayEnd = jsonResponse.rfind(']');
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos) {
        return results;
    }

    size_t cur = arrayStart;
    while ((cur = jsonResponse.find('{', cur)) != std::string::npos && cur < arrayEnd) {
        size_t objEnd = jsonResponse.find('}', cur);
        if (objEnd == std::string::npos || objEnd > arrayEnd) break;

        std::string obj = jsonResponse.substr(cur, objEnd - cur + 1);
        DriveFileInfo info;
        info.id = extractJsonString(obj, "id");
        info.name = extractJsonString(obj, "name");
        std::string modTimeStr = extractJsonString(obj, "modifiedTime");
        info.modifiedTime = parseIso8601ToUnixTime(modTimeStr);

        std::string sizeStr = extractJsonString(obj, "size");
        if (!sizeStr.empty()) {
            info.size = static_cast<size_t>(strtoull(sizeStr.c_str(), nullptr, 10));
        }

        unsigned long long tid = 0;
        if (sscanf(info.name.c_str(), "save_%llx.zip", &tid) == 1 ||
            sscanf(info.name.c_str(), "save_%llX.zip", &tid) == 1 ||
            sscanf(info.name.c_str(), "%llx_", &tid) == 1 ||
            sscanf(info.name.c_str(), "%llX_", &tid) == 1) {
            info.titleId = tid;
        }

        if (!info.id.empty() && !info.name.empty()) {
            results.push_back(info);
        }

        cur = objEnd + 1;
    }

    return results;
}

bool GDriveClient::downloadFile(const std::string& fileId, const std::string& destinationPath, std::string* outError) {
    if (!refreshAccessToken(outError)) {
        return false;
    }

    FILE* fp = fopen(destinationPath.c_str(), "wb");
    if (!fp) {
        if (outError) *outError = "Falha ao criar arquivo local: " + destinationPath;
        logClient("[DOWNLOAD] fopen falhou para %s", destinationPath.c_str());
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        remove(destinationPath.c_str());
        if (outError) *outError = "Falha ao inicializar cURL";
        return false;
    }

    std::string url = "https://www.googleapis.com/drive/v3/files/" + fileId + "?alt=media";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_accessToken).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlFileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L); // 10 minutos
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        remove(destinationPath.c_str());
        std::string err = curl_easy_strerror(res);
        if (outError) *outError = "curl: " + err;
        logClient("[DOWNLOAD] curl falhou (code %d): %s", res, err.c_str());
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        remove(destinationPath.c_str());
        if (outError) *outError = "HTTP " + std::to_string(httpCode);
        logClient("[DOWNLOAD] HTTP code invalido: %ld", httpCode);
        return false;
    }

    logClient("[DOWNLOAD] Arquivo baixado com sucesso: %s", destinationPath.c_str());
    return true;
}

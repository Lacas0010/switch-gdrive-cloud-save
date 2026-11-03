#include "drive_client.hpp"
#include "../utils/log.hpp"
#include "../utils/time_util.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR_N(p) _mkdir(p)
#else
#define MKDIR_N(p) mkdir(p, 0777)
#endif

namespace Network {

    namespace {
        struct CurlProgressData {
            const std::atomic<bool>* cancelToken;
            DriveClient::ProgressCallback userCallback;
        };

        static int xferInfoCallback(
            void* clientp,
            curl_off_t dltotal,
            curl_off_t dlnow,
            curl_off_t ultotal,
            curl_off_t ulnow
        ) {
            auto* data = static_cast<CurlProgressData*>(clientp);
            if (data && data->cancelToken && data->cancelToken->load(std::memory_order_relaxed)) {
                return 1; // Cancela transferência curl imediatamente
            }

            if (data && data->userCallback) {
                if (dltotal > 0) {
                    float p = static_cast<float>(dlnow) / static_cast<float>(dltotal);
                    data->userCallback(p, static_cast<size_t>(dlnow), static_cast<size_t>(dltotal));
                } else if (ultotal > 0) {
                    float p = static_cast<float>(ulnow) / static_cast<float>(ultotal);
                    data->userCallback(p, static_cast<size_t>(ulnow), static_cast<size_t>(ultotal));
                }
            }
            return 0;
        }

        static size_t stringWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            size_t total = size * nmemb;
            auto* s = static_cast<std::string*>(userp);
            if (s) {
                s->append(static_cast<char*>(contents), total);
            }
            return total;
        }

        static size_t fileWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            size_t total = size * nmemb;
            auto* fp = static_cast<FILE*>(userp);
            if (fp) {
                return std::fwrite(contents, 1, total, fp);
            }
            return 0;
        }
    }

    DriveClient::DriveClient()
        : m_folderName("Switch Saves"),
          m_configFilePath("sdmc:/config/gdrive-sync/config.json") {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        loadConfig();
    }

    DriveClient::~DriveClient() {
        saveConfig();
        curl_global_cleanup();
    }

    std::string DriveClient::urlEncode(std::string_view value) {
        Core::UniqueCurl curl(curl_easy_init());
        if (!curl) return std::string(value);

        char* encoded = curl_easy_escape(curl.get(), value.data(), static_cast<int>(value.size()));
        if (!encoded) return std::string(value);

        std::string result(encoded);
        curl_free(encoded);
        return result;
    }

    std::string DriveClient::extractJsonString(std::string_view json, std::string_view key) {
        std::string pattern = "\"" + std::string(key) + "\"";
        size_t pos = json.find(pattern);
        if (pos == std::string_view::npos) return "";

        size_t colon = json.find(':', pos + pattern.size());
        if (colon == std::string_view::npos) return "";

        size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
        if (start == std::string_view::npos) return "";

        if (json[start] == '"') {
            size_t end = json.find('"', start + 1);
            if (end == std::string_view::npos) return "";
            return std::string(json.substr(start + 1, end - start - 1));
        } else {
            size_t end = json.find_first_of(",}\r\n", start);
            if (end == std::string_view::npos) end = json.size();
            return std::string(json.substr(start, end - start));
        }
    }

    bool DriveClient::loadConfig() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto fp = Core::openFile(m_configFilePath.c_str(), "r");
        if (!fp) return false;

        std::fseek(fp.get(), 0, SEEK_END);
        long sz = std::ftell(fp.get());
        std::fseek(fp.get(), 0, SEEK_SET);

        if (sz <= 0) return false;
        std::string content(static_cast<size_t>(sz), '\0');
        if (std::fread(&content[0], 1, static_cast<size_t>(sz), fp.get()) != static_cast<size_t>(sz)) {
            return false;
        }

        std::string fn = extractJsonString(content, "folder_name");
        if (!fn.empty()) m_folderName = fn;
        m_folderId = extractJsonString(content, "folder_id");
        m_clientId = extractJsonString(content, "client_id");
        m_clientSecret = extractJsonString(content, "client_secret");
        m_accessToken = extractJsonString(content, "access_token");
        m_refreshToken = extractJsonString(content, "refresh_token");

        return true;
    }

    bool DriveClient::saveConfig() {
        std::lock_guard<std::mutex> lock(m_mutex);
        MKDIR_N("sdmc:/config");
        MKDIR_N("sdmc:/config/gdrive-sync");

        auto fp = Core::openFile(m_configFilePath.c_str(), "w");
        if (!fp) return false;

        std::fprintf(fp.get(), "{\n");
        std::fprintf(fp.get(), "  \"folder_name\": \"%s\",\n", m_folderName.c_str());
        std::fprintf(fp.get(), "  \"folder_id\": \"%s\",\n", m_folderId.c_str());
        std::fprintf(fp.get(), "  \"client_id\": \"%s\",\n", m_clientId.c_str());
        std::fprintf(fp.get(), "  \"client_secret\": \"%s\",\n", m_clientSecret.c_str());
        std::fprintf(fp.get(), "  \"access_token\": \"%s\",\n", m_accessToken.c_str());
        std::fprintf(fp.get(), "  \"refresh_token\": \"%s\"\n", m_refreshToken.c_str());
        std::fprintf(fp.get(), "}\n");
        return true;
    }

    bool DriveClient::isConfigured() const noexcept {
        return !m_clientId.empty() && !m_refreshToken.empty();
    }

    CURLcode DriveClient::executeCurlRequest(
        CURL* curl,
        std::string* outResponse,
        const std::atomic<bool>* cancelToken
    ) {
        CurlProgressData progData{cancelToken, nullptr};

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progData);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        if (outResponse) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, outResponse);
        }

        return curl_easy_perform(curl);
    }

    std::string DriveClient::httpGet(
        std::string_view url,
        std::string_view bearerToken,
        const std::atomic<bool>* cancelToken
    ) {
        Core::UniqueCurl curl(curl_easy_init());
        if (!curl) return "";

        std::string urlStr(url);
        std::string resp;
        curl_easy_setopt(curl.get(), CURLOPT_URL, urlStr.c_str());

        Core::UniqueCurlSlist headers;
        if (!bearerToken.empty()) {
            std::string authHdr = "Authorization: Bearer " + std::string(bearerToken);
            headers.reset(curl_slist_append(headers.release(), authHdr.c_str()));
            curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
        }

        CURLcode res = executeCurlRequest(curl.get(), &resp, cancelToken);
        return (res == CURLE_OK) ? resp : "";
    }

    std::string DriveClient::httpPost(
        std::string_view url,
        std::string_view postData,
        std::string_view bearerToken,
        const std::atomic<bool>* cancelToken
    ) {
        Core::UniqueCurl curl(curl_easy_init());
        if (!curl) return "";

        std::string urlStr(url);
        std::string postStr(postData);
        std::string resp;

        curl_easy_setopt(curl.get(), CURLOPT_URL, urlStr.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, postStr.c_str());

        Core::UniqueCurlSlist headers;
        if (!bearerToken.empty()) {
            std::string authHdr = "Authorization: Bearer " + std::string(bearerToken);
            headers.reset(curl_slist_append(headers.release(), authHdr.c_str()));
            curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
        }

        CURLcode res = executeCurlRequest(curl.get(), &resp, cancelToken);
        return (res == CURLE_OK) ? resp : "";
    }

    std::string DriveClient::httpPatch(
        std::string_view url,
        std::string_view patchJson,
        std::string_view bearerToken,
        const std::atomic<bool>* cancelToken
    ) {
        Core::UniqueCurl curl(curl_easy_init());
        if (!curl) return "";

        std::string urlStr(url);
        std::string jsonStr(patchJson);
        std::string resp;

        curl_easy_setopt(curl.get(), CURLOPT_URL, urlStr.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, jsonStr.c_str());

        Core::UniqueCurlSlist headers;
        headers.reset(curl_slist_append(headers.release(), "Content-Type: application/json; charset=UTF-8"));
        if (!bearerToken.empty()) {
            std::string authHdr = "Authorization: Bearer " + std::string(bearerToken);
            headers.reset(curl_slist_append(headers.release(), authHdr.c_str()));
        }
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        CURLcode res = executeCurlRequest(curl.get(), &resp, cancelToken);
        return (res == CURLE_OK) ? resp : "";
    }

    bool DriveClient::refreshAccessToken(std::string* outError, const std::atomic<bool>* cancelToken) {
        if (m_refreshToken.empty()) {
            if (outError) *outError = "Refresh token ausente no config.json";
            return false;
        }

        std::string postData = "client_id=" + urlEncode(m_clientId) +
                               "&client_secret=" + urlEncode(m_clientSecret) +
                               "&refresh_token=" + urlEncode(m_refreshToken) +
                               "&grant_type=refresh_token";

        std::string resp = httpPost("https://oauth2.googleapis.com/token", postData, "", cancelToken);
        if (resp.empty()) {
            if (outError) *outError = "Sem resposta do servidor de autenticação do Google";
            return false;
        }

        std::string newTok = extractJsonString(resp, "access_token");
        if (!newTok.empty()) {
            m_accessToken = newTok;
            saveConfig();
            return true;
        }

        std::string errDesc = extractJsonString(resp, "error_description");
        if (errDesc.empty()) errDesc = extractJsonString(resp, "error");
        if (outError) *outError = errDesc.empty() ? "Erro ao renovar token" : errDesc;
        return false;
    }

    bool DriveClient::testConnection(std::string* outError) {
        if (!refreshAccessToken(outError)) {
            return false;
        }
        std::string resp = httpGet("https://www.googleapis.com/drive/v3/about?fields=user", m_accessToken);
        if (resp.find("\"user\"") != std::string::npos || resp.find("\"displayName\"") != std::string::npos) {
            return true;
        }
        if (outError) *outError = "Falha ao validar permissões no Google Drive API";
        return false;
    }

    std::string DriveClient::ensureFolderExists(std::string_view folderName, const std::atomic<bool>* cancelToken) {
        refreshAccessToken(nullptr, cancelToken);

        std::string q = "mimeType='application/vnd.google-apps.folder' and name='" + std::string(folderName) + "' and trashed=false";
        std::string url = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(q) + "&fields=files(id,name)";
        std::string resp = httpGet(url, m_accessToken, cancelToken);

        std::string foundId = extractJsonString(resp, "id");
        if (!foundId.empty()) {
            m_folderId = foundId;
            return foundId;
        }

        std::string createJson = "{\"name\": \"" + std::string(folderName) + "\", \"mimeType\": \"application/vnd.google-apps.folder\"}";
        std::string createResp = httpPost("https://www.googleapis.com/drive/v3/files", createJson, m_accessToken, cancelToken);
        std::string newId = extractJsonString(createResp, "id");
        if (!newId.empty()) {
            m_folderId = newId;
            saveConfig();
            return newId;
        }
        return "";
    }

    std::string DriveClient::ensureSubFolderExists(
        std::string_view folderName,
        std::string_view parentFolderId,
        const std::atomic<bool>* cancelToken
    ) {
        refreshAccessToken(nullptr, cancelToken);

        std::string q = "mimeType='application/vnd.google-apps.folder' and name='" + std::string(folderName) + "' and '" + std::string(parentFolderId) + "' in parents and trashed=false";
        std::string url = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(q) + "&fields=files(id,name)";
        std::string resp = httpGet(url, m_accessToken, cancelToken);

        std::string foundId = extractJsonString(resp, "id");
        if (!foundId.empty()) {
            return foundId;
        }

        std::string createJson = "{\"name\": \"" + std::string(folderName) + "\", \"mimeType\": \"application/vnd.google-apps.folder\", \"parents\": [\"" + std::string(parentFolderId) + "\"]}";
        std::string createResp = httpPost("https://www.googleapis.com/drive/v3/files", createJson, m_accessToken, cancelToken);
        return extractJsonString(createResp, "id");
    }

    std::string DriveClient::findFileInFolder(
        std::string_view fileName,
        std::string_view parentFolderId,
        const std::atomic<bool>* cancelToken
    ) {
        refreshAccessToken(nullptr, cancelToken);

        // 1. Procura arquivo ativo
        std::string rawQ = "name='" + std::string(fileName) + "' and '" + std::string(parentFolderId) + "' in parents and trashed=false";
        std::string searchUrl = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(rawQ) + "&fields=files(id,name,trashed)&orderBy=createdTime";
        std::string resp = httpGet(searchUrl, m_accessToken, cancelToken);

        std::string foundId = extractJsonString(resp, "id");
        if (!foundId.empty()) {
            return foundId;
        }

        // 2. Se não encontrou, busca na lixeira e recupera via PATCH {"trashed": false}
        std::string trashQ = "name='" + std::string(fileName) + "' and '" + std::string(parentFolderId) + "' in parents and trashed=true";
        std::string trashUrl = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(trashQ) + "&fields=files(id,name,trashed)";
        std::string trashResp = httpGet(trashUrl, m_accessToken, cancelToken);
        foundId = extractJsonString(trashResp, "id");

        if (!foundId.empty()) {
            httpPatch("https://www.googleapis.com/drive/v3/files/" + foundId, "{\"trashed\": false}", m_accessToken, cancelToken);
            Utils::Logger::log("DRIVE", "Arquivo recuperado da lixeira com sucesso: " + std::string(fileName));
            return foundId;
        }

        return "";
    }

    bool DriveClient::uploadSaveLockstep(
        u64 titleId,
        std::string_view accountUid,
        std::string_view zipFilePath,
        u32 crc32,
        std::time_t timestampUtc,
        const std::atomic<bool>* cancelToken,
        ProgressCallback onProgress,
        std::string* outError
    ) {
        if (!refreshAccessToken(outError, cancelToken)) {
            return false;
        }

        std::string rootFolderId = ensureFolderExists(m_folderName, cancelToken);
        if (rootFolderId.empty()) {
            if (outError) *outError = "Pasta raiz '" + m_folderName + "' não encontrada no Google Drive";
            return false;
        }

        char tidHex[32];
        std::snprintf(tidHex, sizeof(tidHex), "%016llX", static_cast<unsigned long long>(titleId));

        std::string titleFolderId = ensureSubFolderExists(tidHex, rootFolderId, cancelToken);
        std::string accountFolderId = ensureSubFolderExists(accountUid, titleFolderId, cancelToken);

        std::string zipStr(zipFilePath);
        auto fp = Core::openFile(zipStr.c_str(), "rb");
        if (!fp) {
            if (outError) *outError = "Arquivo ZIP local inacessível para upload";
            return false;
        }
        std::fseek(fp.get(), 0, SEEK_END);
        size_t fileSize = static_cast<size_t>(std::ftell(fp.get()));
        std::fseek(fp.get(), 0, SEEK_SET);

        std::vector<char> fileBuffer(fileSize);
        if (std::fread(fileBuffer.data(), 1, fileSize, fp.get()) != fileSize) {
            if (outError) *outError = "Falha na leitura do arquivo ZIP local";
            return false;
        }
        fp.reset();

        // 1. Envio do Arquivo Raiz Mutável: Switch Saves/save_<TitleID>.zip
        std::string rootFileName = "save_" + std::string(tidHex) + ".zip";
        std::string existingRootFileId = findFileInFolder(rootFileName, rootFolderId, cancelToken);

        Core::UniqueCurl curl(curl_easy_init());
        if (!curl) return false;

        std::string response;
        Core::UniqueCurlSlist headers;
        std::string authHdr = "Authorization: Bearer " + m_accessToken;
        headers.reset(curl_slist_append(headers.release(), authHdr.c_str()));

        if (!existingRootFileId.empty()) {
            // PATCH no arquivo raiz existente
            std::string patchUrl = "https://www.googleapis.com/upload/drive/v3/files/" + existingRootFileId + "?uploadType=media";
            curl_easy_setopt(curl.get(), CURLOPT_URL, patchUrl.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PATCH");
            headers.reset(curl_slist_append(headers.release(), "Content-Type: application/zip"));
        } else {
            // POST multipart para novo arquivo
            std::string postUrl = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";
            curl_easy_setopt(curl.get(), CURLOPT_URL, postUrl.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
            headers.reset(curl_slist_append(headers.release(), "Content-Type: multipart/related; boundary=\"foo_bar_baz\""));
        }

        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        // Monta payload
        std::string uploadPayload;
        if (existingRootFileId.empty()) {
            uploadPayload = "--foo_bar_baz\r\n"
                            "Content-Type: application/json; charset=UTF-8\r\n\r\n"
                            "{\"name\": \"" + rootFileName + "\", \"parents\": [\"" + rootFolderId + "\"]}\r\n\r\n"
                            "--foo_bar_baz\r\n"
                            "Content-Type: application/zip\r\n\r\n";
            uploadPayload.append(fileBuffer.data(), fileSize);
            uploadPayload += "\r\n--foo_bar_baz--";
        } else {
            uploadPayload.assign(fileBuffer.data(), fileSize);
        }

        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, uploadPayload.data());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(uploadPayload.size()));

        CurlProgressData progData{cancelToken, onProgress};
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &progData);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);

        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, stringWriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl.get());
        if (res != CURLE_OK) {
            if (outError) *outError = (res == CURLE_ABORTED_BY_CALLBACK) ? "Upload cancelado" : curl_easy_strerror(res);
            return false;
        }

        // 2. Envio do Snapshot Append-Only Versionado:
        // Switch Saves/<TitleID>/<AccountUid>/<TitleID>_<AccountUid>_<TimestampUTC>_<CRC32>.zip
        char snapFileName[256];
        std::snprintf(snapFileName, sizeof(snapFileName), "%s_%.*s_%lld_%08X.zip",
                      tidHex, static_cast<int>(accountUid.size()), accountUid.data(),
                      static_cast<long long>(timestampUtc), crc32);

        Core::UniqueCurl snapCurl(curl_easy_init());
        if (snapCurl) {
            std::string snapResp;
            Core::UniqueCurlSlist snapHeaders;
            snapHeaders.reset(curl_slist_append(snapHeaders.release(), authHdr.c_str()));
            snapHeaders.reset(curl_slist_append(snapHeaders.release(), "Content-Type: multipart/related; boundary=\"snap_bound\""));

            std::string snapPayload = "--snap_bound\r\n"
                                      "Content-Type: application/json; charset=UTF-8\r\n\r\n"
                                      "{\"name\": \"" + std::string(snapFileName) + "\", \"parents\": [\"" + accountFolderId + "\"]}\r\n\r\n"
                                      "--snap_bound\r\n"
                                      "Content-Type: application/zip\r\n\r\n";
            snapPayload.append(fileBuffer.data(), fileSize);
            snapPayload += "\r\n--snap_bound--";

            curl_easy_setopt(snapCurl.get(), CURLOPT_URL, "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart");
            curl_easy_setopt(snapCurl.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(snapCurl.get(), CURLOPT_HTTPHEADER, snapHeaders.get());
            curl_easy_setopt(snapCurl.get(), CURLOPT_POSTFIELDS, snapPayload.data());
            curl_easy_setopt(snapCurl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(snapPayload.size()));
            curl_easy_setopt(snapCurl.get(), CURLOPT_WRITEFUNCTION, stringWriteCallback);
            curl_easy_setopt(snapCurl.get(), CURLOPT_WRITEDATA, &snapResp);

            curl_easy_perform(snapCurl.get());
        }

        return true;
    }

    bool DriveClient::downloadFile(
        std::string_view fileId,
        std::string_view destinationPath,
        const std::atomic<bool>* cancelToken,
        ProgressCallback onProgress,
        std::string* outError
    ) {
        if (!refreshAccessToken(outError, cancelToken)) {
            return false;
        }

        std::string destStr(destinationPath);
        auto fp = Core::openFile(destStr.c_str(), "wb");
        if (!fp) {
            if (outError) *outError = "Falha ao criar arquivo de destino local";
            return false;
        }

        Core::UniqueCurl curl(curl_easy_init());
        if (!curl) return false;

        std::string url = "https://www.googleapis.com/drive/v3/files/" + std::string(fileId) + "?alt=media";
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());

        Core::UniqueCurlSlist headers;
        std::string authHdr = "Authorization: Bearer " + m_accessToken;
        headers.reset(curl_slist_append(headers.release(), authHdr.c_str()));
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, fileWriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, fp.get());

        CurlProgressData progData{cancelToken, onProgress};
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &progData);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);

        CURLcode res = executeCurlRequest(curl.get(), nullptr, cancelToken);
        fp.reset();

        if (res != CURLE_OK) {
            std::remove(destStr.c_str());
            if (outError) *outError = (res == CURLE_ABORTED_BY_CALLBACK) ? "Download cancelado" : curl_easy_strerror(res);
            return false;
        }

        return true;
    }

    std::vector<DriveFileInfo> DriveClient::listDriveSaves(std::string* outError, const std::atomic<bool>* cancelToken) {
        std::vector<DriveFileInfo> results;
        if (!refreshAccessToken(outError, cancelToken)) {
            return results;
        }

        std::string folderId = ensureFolderExists(m_folderName, cancelToken);
        if (folderId.empty()) return results;

        std::string q = "'" + folderId + "' in parents and trashed=false";
        std::string url = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(q) +
                          "&fields=" + urlEncode("files(id,name,modifiedTime,size)") + "&pageSize=100";

        std::string resp = httpGet(url, m_accessToken, cancelToken);
        size_t cur = 0;
        while ((cur = resp.find("{\"id\":", cur)) != std::string::npos) {
            size_t objEnd = resp.find("}", cur);
            if (objEnd == std::string::npos) break;

            std::string_view obj = std::string_view(resp).substr(cur, objEnd - cur + 1);
            DriveFileInfo info;
            info.id = extractJsonString(obj, "id");
            info.name = extractJsonString(obj, "name");
            info.modifiedTime = Utils::TimeUtil::parseIso8601ToUnixTime(extractJsonString(obj, "modifiedTime"));

            std::string szStr = extractJsonString(obj, "size");
            if (!szStr.empty()) {
                info.size = static_cast<size_t>(std::strtoull(szStr.c_str(), nullptr, 10));
            }

            unsigned long long tid = 0;
            if (std::sscanf(info.name.c_str(), "save_%llx.zip", &tid) == 1 ||
                std::sscanf(info.name.c_str(), "save_%llX.zip", &tid) == 1) {
                info.titleId = tid;
            }

            if (!info.id.empty() && !info.name.empty()) {
                results.push_back(info);
            }
            cur = objEnd + 1;
        }

        return results;
    }

    std::vector<DriveSnapshotVersion> DriveClient::listGameVersions(
        u64 titleId,
        std::string_view accountUid,
        std::string* outError,
        const std::atomic<bool>* cancelToken
    ) {
        std::vector<DriveSnapshotVersion> versions;
        if (!refreshAccessToken(outError, cancelToken)) {
            return versions;
        }

        std::string rootFolderId = ensureFolderExists(m_folderName, cancelToken);
        char tidHex[32];
        std::snprintf(tidHex, sizeof(tidHex), "%016llX", static_cast<unsigned long long>(titleId));

        std::string titleFolderId = ensureSubFolderExists(tidHex, rootFolderId, cancelToken);
        std::string targetUid = accountUid.empty() ? "DEFAULT_ACCOUNT" : std::string(accountUid);
        std::string accountFolderId = ensureSubFolderExists(targetUid, titleFolderId, cancelToken);

        std::string q = "'" + accountFolderId + "' in parents and trashed=false";
        std::string url = "https://www.googleapis.com/drive/v3/files?q=" + urlEncode(q) +
                          "&fields=" + urlEncode("files(id,name,modifiedTime,size)") +
                          "&orderBy=" + urlEncode("modifiedTime desc");

        std::string resp = httpGet(url, m_accessToken, cancelToken);
        size_t cur = 0;
        while ((cur = resp.find("{\"id\":", cur)) != std::string::npos) {
            size_t objEnd = resp.find("}", cur);
            if (objEnd == std::string::npos) break;

            std::string_view obj = std::string_view(resp).substr(cur, objEnd - cur + 1);
            DriveSnapshotVersion ver;
            ver.fileId = extractJsonString(obj, "id");
            ver.fileName = extractJsonString(obj, "name");
            ver.titleId = titleId;
            ver.accountUid = targetUid;
            ver.timestampUtc = Utils::TimeUtil::parseIso8601ToUnixTime(extractJsonString(obj, "modifiedTime"));

            std::string szStr = extractJsonString(obj, "size");
            if (!szStr.empty()) {
                ver.sizeBytes = static_cast<size_t>(std::strtoull(szStr.c_str(), nullptr, 10));
            }

            unsigned long long parsedTid = 0;
            char parsedUid[64] = {0};
            long long parsedTs = 0;
            unsigned int parsedCrc = 0;

            if (std::sscanf(ver.fileName.c_str(), "%llX_%63[^_]_%lld_%x.zip", &parsedTid, parsedUid, &parsedTs, &parsedCrc) == 4 ||
                std::sscanf(ver.fileName.c_str(), "%llx_%63[^_]_%lld_%x.zip", &parsedTid, parsedUid, &parsedTs, &parsedCrc) == 4) {
                ver.titleId = parsedTid;
                ver.accountUid = parsedUid;
                if (parsedTs > 0) ver.timestampUtc = static_cast<std::time_t>(parsedTs);
                ver.crc32 = parsedCrc;
            }

            ver.formattedUtcTime = Utils::TimeUtil::formatUtcDateTime(ver.timestampUtc);
            versions.push_back(ver);
            cur = objEnd + 1;
        }

        std::sort(versions.begin(), versions.end(), [](const DriveSnapshotVersion& a, const DriveSnapshotVersion& b) {
            return a.timestampUtc > b.timestampUtc;
        });

        return versions;
    }

} // namespace Network

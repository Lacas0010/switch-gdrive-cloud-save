#include "OAuthManager.hpp"
#include "../../source/Config.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    static size_t curlWriteCb(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total = size * nmemb;
        std::string* s = static_cast<std::string*>(userp);
        s->append(static_cast<char*>(contents), total);
        return total;
    }

    static std::string extractJsonString(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";

        size_t colonPos = json.find(':', keyPos + searchKey.length());
        if (colonPos == std::string::npos) return "";

        size_t startQuote = json.find('"', colonPos);
        if (startQuote == std::string::npos) return "";

        size_t endQuote = json.find('"', startQuote + 1);
        if (endQuote == std::string::npos) return "";

        return json.substr(startQuote + 1, endQuote - startQuote - 1);
    }

    static int extractJsonInt(const std::string& json, const std::string& key, int defaultVal = 0) {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return defaultVal;

        size_t colonPos = json.find(':', keyPos + searchKey.length());
        if (colonPos == std::string::npos) return defaultVal;

        size_t start = json.find_first_not_of(" \t\r\n", colonPos + 1);
        if (start == std::string::npos) return defaultVal;

        size_t end = json.find_first_of(",}\r\n", start);
        if (end == std::string::npos) end = json.length();

        std::string valStr = json.substr(start, end - start);
        return std::atoi(valStr.c_str());
    }

    static std::string postHttp(const std::string& url, const std::string& postData, std::string* outError = nullptr) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            if (outError) *outError = "Falha ao inicializar cURL";
            return "";
        }

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, Config::getCaBundlePath().c_str());

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            if (outError) *outError = curl_easy_strerror(res);
            response.clear();
        }
        curl_easy_cleanup(curl);
        return response;
    }
}

OAuthManager::OAuthManager() {}
OAuthManager::~OAuthManager() {}

bool OAuthManager::requestDeviceAuth(const std::string& clientId, DeviceAuthInfo& outInfo, std::string* outError) {
    if (clientId.empty()) {
        if (outError) *outError = "Client ID esta vazio";
        return false;
    }

    std::string postData = "client_id=" + clientId + "&scope=https://www.googleapis.com/auth/drive.file";
    std::string resp = postHttp("https://oauth2.googleapis.com/device/code", postData, outError);
    if (resp.empty()) return false;

    outInfo.deviceCode = extractJsonString(resp, "device_code");
    outInfo.userCode   = extractJsonString(resp, "user_code");
    outInfo.verificationUrl = extractJsonString(resp, "verification_url");
    outInfo.interval   = extractJsonInt(resp, "interval", 5);
    outInfo.expiresIn  = extractJsonInt(resp, "expires_in", 1800);

    if (outInfo.deviceCode.empty() || outInfo.userCode.empty()) {
        std::string errDesc = extractJsonString(resp, "error_description");
        if (errDesc.empty()) errDesc = extractJsonString(resp, "error");
        if (outError) *outError = errDesc.empty() ? "Resposta OAuth invalida" : errDesc;
        return false;
    }

    return true;
}

OAuthPollStatus OAuthManager::pollDeviceAuth(
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& deviceCode,
    std::string& outRefreshToken,
    std::string& outAccessToken,
    std::string* outError
) {
    if (deviceCode.empty()) {
        if (outError) *outError = "device_code vazio";
        return OAuthPollStatus::ERROR;
    }

    std::string postData = "client_id=" + clientId +
                           "&client_secret=" + clientSecret +
                           "&device_code=" + deviceCode +
                           "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

    std::string resp = postHttp("https://oauth2.googleapis.com/token", postData, outError);
    if (resp.empty()) {
        return OAuthPollStatus::ERROR;
    }

    std::string error = extractJsonString(resp, "error");
    if (!error.empty()) {
        if (error == "authorization_pending") {
            return OAuthPollStatus::PENDING;
        } else if (error == "slow_down") {
            return OAuthPollStatus::SLOW_DOWN;
        } else if (error == "expired_token") {
            if (outError) *outError = "Tempo limite para digitar o codigo expirou";
            return OAuthPollStatus::EXPIRED;
        } else {
            std::string desc = extractJsonString(resp, "error_description");
            if (outError) *outError = desc.empty() ? error : desc;
            return OAuthPollStatus::ERROR;
        }
    }

    std::string accessToken = extractJsonString(resp, "access_token");
    std::string refreshToken = extractJsonString(resp, "refresh_token");

    if (!refreshToken.empty()) {
        outRefreshToken = refreshToken;
        outAccessToken = accessToken;
        saveTokensToConfig(refreshToken, accessToken);
        return OAuthPollStatus::SUCCESS;
    }

    if (outError) *outError = "refresh_token ausente na resposta de autorizacao";
    return OAuthPollStatus::ERROR;
}

bool OAuthManager::saveTokensToConfig(const std::string& refreshToken, const std::string& accessToken) {
    FILE* f = fopen(Config::CONFIG_FILE.c_str(), "rb");
    std::string content;
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 64 * 1024) {
            content.resize(sz);
            fread(content.data(), 1, sz, f);
        }
        fclose(f);
    }

    std::string clientId     = extractJsonString(content, "client_id");
    std::string clientSecret = extractJsonString(content, "client_secret");
    std::string folderId     = extractJsonString(content, "folder_id");
    std::string folderName   = extractJsonString(content, "folder_name");
    std::string syncMode     = extractJsonString(content, "sync_mode");

    if (folderName.empty()) folderName = Config::DEFAULT_FOLDER_NAME;
    if (syncMode.empty()) syncMode = "all";

    FILE* out = fopen(Config::CONFIG_FILE.c_str(), "w");
    if (!out) return false;

    fprintf(out, "{\n");
    fprintf(out, "  \"client_id\": \"%s\",\n", clientId.c_str());
    fprintf(out, "  \"client_secret\": \"%s\",\n", clientSecret.c_str());
    fprintf(out, "  \"refresh_token\": \"%s\",\n", refreshToken.c_str());
    fprintf(out, "  \"access_token\": \"%s\",\n", accessToken.c_str());
    fprintf(out, "  \"folder_id\": \"%s\",\n", folderId.c_str());
    fprintf(out, "  \"folder_name\": \"%s\",\n", folderName.c_str());
    fprintf(out, "  \"sync_mode\": \"%s\"\n", syncMode.c_str());
    fprintf(out, "}\n");
    fclose(out);

    return true;
}


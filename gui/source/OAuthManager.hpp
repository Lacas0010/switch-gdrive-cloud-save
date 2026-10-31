#pragma once
#include <switch.h>
#include <string>

struct DeviceAuthInfo {
    std::string deviceCode;
    std::string userCode;
    std::string verificationUrl;
    u32 interval = 5;
    u32 expiresIn = 1800;
};

enum class OAuthPollStatus {
    PENDING,
    SUCCESS,
    SLOW_DOWN,
    EXPIRED,
    ERROR
};

class OAuthManager {
public:
    OAuthManager();
    ~OAuthManager();

    // Inicia o fluxo Device Flow solicitando user_code e verification_url a Google API
    bool requestDeviceAuth(const std::string& clientId, DeviceAuthInfo& outInfo, std::string* outError = nullptr);

    // Consulta se o usuario autorizou o codigo no celular/PC
    OAuthPollStatus pollDeviceAuth(
        const std::string& clientId,
        const std::string& clientSecret,
        const std::string& deviceCode,
        std::string& outRefreshToken,
        std::string& outAccessToken,
        std::string* outError = nullptr
    );

    // Salva os tokens recebidos no arquivo sdmc:/config/gdrive-sync/config.json
    static bool saveTokensToConfig(const std::string& refreshToken, const std::string& accessToken);
};

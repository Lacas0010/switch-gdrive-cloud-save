#pragma once
#include "drive_client.hpp"
#include <string>
#include <ctime>

namespace Network {

    enum class OAuthState {
        UNAUTHENTICATED,
        AWAITING_USER_CODE,
        AUTHORIZED,
        FAILED
    };

    class OAuthManager {
    public:
        explicit OAuthManager(DriveClient& client);
        ~OAuthManager();

        bool startDeviceAuth(std::string* outError = nullptr);
        OAuthState pollAuthStatus();
        void cancelAuth();

        const std::string& getUserCode() const noexcept { return m_userCode; }
        const std::string& getVerificationUrl() const noexcept { return m_verificationUrl; }
        OAuthState getState() const noexcept { return m_state; }
        const std::string& getErrorMessage() const noexcept { return m_errorMessage; }

    private:
        DriveClient& m_client;
        OAuthState m_state;
        std::string m_deviceCode;
        std::string m_userCode;
        std::string m_verificationUrl;
        std::string m_errorMessage;
        int m_pollIntervalSec;
        std::time_t m_lastPollTime;
    };

} // namespace Network

#include "oauth_manager.hpp"
#include "../utils/log.hpp"
#include <cstdio>

namespace Network {

    OAuthManager::OAuthManager(DriveClient& client)
        : m_client(client),
          m_state(OAuthState::UNAUTHENTICATED),
          m_pollIntervalSec(5),
          m_lastPollTime(0) {}

    OAuthManager::~OAuthManager() {
        cancelAuth();
    }

    bool OAuthManager::startDeviceAuth(std::string* outError) {
        m_errorMessage.clear();
        m_deviceCode.clear();
        m_userCode.clear();
        m_verificationUrl.clear();

        if (m_client.getClientId().empty()) {
            m_errorMessage = "Client ID ausente em sdmc:/config/gdrive-sync/config.json";
            if (outError) *outError = m_errorMessage;
            m_state = OAuthState::FAILED;
            Utils::Logger::log("AUTH", m_errorMessage);
            return false;
        }

        std::string postData = "client_id=" + DriveClient::urlEncode(m_client.getClientId()) +
                               "&scope=" + DriveClient::urlEncode("https://www.googleapis.com/auth/drive.file");

        std::string resp = m_client.httpPost("https://oauth2.googleapis.com/device/code", postData);
        if (resp.empty()) {
            m_errorMessage = "Sem resposta do Google OAuth2 (verifique Wi-Fi e DNS)";
            if (outError) *outError = m_errorMessage;
            m_state = OAuthState::FAILED;
            Utils::Logger::log("AUTH", m_errorMessage);
            return false;
        }

        m_deviceCode = DriveClient::extractJsonString(resp, "device_code");
        m_userCode = DriveClient::extractJsonString(resp, "user_code");
        m_verificationUrl = DriveClient::extractJsonString(resp, "verification_url");
        if (m_verificationUrl.empty()) {
            m_verificationUrl = "https://www.google.com/device";
        }

        std::string intervalStr = DriveClient::extractJsonString(resp, "interval");
        m_pollIntervalSec = intervalStr.empty() ? 5 : std::max(3, std::atoi(intervalStr.c_str()));
        m_lastPollTime = std::time(nullptr);

        if (!m_deviceCode.empty() && !m_userCode.empty()) {
            m_state = OAuthState::AWAITING_USER_CODE;
            Utils::Logger::log("AUTH", "Device Flow iniciado. Codigo gerado para " + m_verificationUrl);
            return true;
        }

        std::string errDesc = DriveClient::extractJsonString(resp, "error_description");
        std::string errCode = DriveClient::extractJsonString(resp, "error");
        if (!errDesc.empty()) {
            m_errorMessage = "Google OAuth2: " + errDesc;
        } else if (!errCode.empty()) {
            m_errorMessage = "Google OAuth2 erro: " + errCode;
        } else {
            m_errorMessage = "Resposta invalida do servidor OAuth2 (verifique tipo de Client ID: TV/Dispositivos)";
        }

        m_state = OAuthState::FAILED;
        if (outError) *outError = m_errorMessage;
        Utils::Logger::log("AUTH", m_errorMessage);
        return false;
    }

    OAuthState OAuthManager::pollAuthStatus() {
        if (m_state != OAuthState::AWAITING_USER_CODE || m_deviceCode.empty()) {
            return m_state;
        }

        std::time_t now = std::time(nullptr);
        if (now - m_lastPollTime < m_pollIntervalSec) {
            return m_state;
        }
        m_lastPollTime = now;

        std::string postData = "client_id=" + DriveClient::urlEncode(m_client.getClientId()) +
                               "&client_secret=" + DriveClient::urlEncode(m_client.getClientSecret()) +
                               "&device_code=" + DriveClient::urlEncode(m_deviceCode) +
                               "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

        std::string resp = m_client.httpPost("https://oauth2.googleapis.com/token", postData);
        if (resp.empty()) return m_state;

        std::string err = DriveClient::extractJsonString(resp, "error");
        if (!err.empty()) {
            if (err == "authorization_pending") {
                return m_state;
            } else if (err == "slow_down") {
                m_pollIntervalSec += 5;
                return m_state;
            } else if (err == "access_denied") {
                m_errorMessage = "Acesso negado pelo usuário no navegador";
                m_state = OAuthState::FAILED;
                cancelAuth();
                return m_state;
            } else {
                m_errorMessage = "Erro de autenticação: " + err;
                m_state = OAuthState::FAILED;
                cancelAuth();
                return m_state;
            }
        }

        std::string at = DriveClient::extractJsonString(resp, "access_token");
        std::string rt = DriveClient::extractJsonString(resp, "refresh_token");

        if (!rt.empty() || !at.empty()) {
            if (!at.empty()) m_client.setAccessToken(at);
            if (!rt.empty()) m_client.setRefreshToken(rt);
            m_client.saveConfig();
            m_state = OAuthState::AUTHORIZED;
            cancelAuth();
            Utils::Logger::log("AUTH", "Dispositivo vinculado e autenticado com sucesso via OAuth 2.0");
        }

        return m_state;
    }

    void OAuthManager::cancelAuth() {
        m_deviceCode.clear();
        m_userCode.clear();
        m_verificationUrl.clear();
        if (m_state == OAuthState::AWAITING_USER_CODE) {
            m_state = OAuthState::UNAUTHENTICATED;
        }
    }

} // namespace Network

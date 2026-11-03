#include "ui_manager.hpp"
#include <cstdio>
#include <algorithm>

namespace UI {

    UIManager::UIManager(
        Core::StateMachine& stateMachine,
        Core::SyncEngine& syncEngine,
        Network::DriveClient& driveClient,
        Storage::SaveManager& saveManager
    ) : m_stateMachine(stateMachine),
        m_engine(syncEngine),
        m_client(driveClient),
        m_saveManager(saveManager),
        m_oauth(driveClient),
        m_display(),
        m_textures(),
        m_screenMain(m_display, syncEngine, driveClient, saveManager),
        m_screenSelect(m_display, m_textures, saveManager, driveClient),
        m_settingsOption(0) {}

    UIManager::~UIManager() {
        cleanup();
    }

    bool UIManager::init() {
        if (!m_display.init()) return false;
        m_textures.init();
        return true;
    }

    void UIManager::cleanup() {
        m_textures.cleanup();
        m_display.cleanup();
    }

    void UIManager::handleInput(u64 kDown) {
        Core::Screen curScreen = m_stateMachine.getScreen();

        if (curScreen == Core::Screen::MAIN) {
            m_screenMain.handleInput(kDown);
#ifdef __SWITCH__
            if (kDown & HidNpadButton_A) {
                int sel = m_screenMain.getSelectedIndex();
                if (sel == 0) {
                    m_engine.startFullBackup();
                } else if (sel == 1) {
                    m_engine.startFullRestore();
                } else if (sel == 2) {
                    m_screenSelect.refreshGames();
                    m_stateMachine.setScreen(Core::Screen::GAME_SELECT);
                } else if (sel == 3) {
                    if (m_oauth.startDeviceAuth()) {
                        m_stateMachine.setScreen(Core::Screen::OAUTH_MODAL);
                    }
                }
            } else if ((kDown & HidNpadButton_Minus) || (kDown & HidNpadButton_Y)) {
                m_stateMachine.setScreen(Core::Screen::SETTINGS);
            }
#endif
        } else if (curScreen == Core::Screen::GAME_SELECT) {
            m_screenSelect.handleInput(kDown);
#ifdef __SWITCH__
            if (kDown & HidNpadButton_B) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            }
#endif
        } else if (curScreen == Core::Screen::SETTINGS) {
#ifdef __SWITCH__
            if (kDown & HidNpadButton_B) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            } else if (kDown & HidNpadButton_A) {
                if (m_settingsOption == 0) {
                    m_engine.startTestConnection();
                }
            }
#endif
        } else if (curScreen == Core::Screen::OAUTH_MODAL) {
#ifdef __SWITCH__
            if (kDown & HidNpadButton_B) {
                m_oauth.cancelAuth();
                m_stateMachine.setScreen(Core::Screen::MAIN);
            }
#endif
        } else if (curScreen == Core::Screen::OPERATION_PROGRESS) {
#ifdef __SWITCH__
            if (m_engine.isRunning()) {
                if (kDown & HidNpadButton_B) {
                    m_engine.requestCancel();
                }
            } else {
                if ((kDown & HidNpadButton_A) || (kDown & HidNpadButton_B)) {
                    m_stateMachine.setScreen(Core::Screen::MAIN);
                }
            }
#endif
        }
    }

    void UIManager::update() {
        m_engine.poll();

        if (m_stateMachine.getScreen() == Core::Screen::OAUTH_MODAL) {
            auto status = m_oauth.pollAuthStatus();
            if (status == Network::OAuthState::AUTHORIZED) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            }
        }
    }

    void UIManager::renderSettingsModal() {
        // Overlay escuro translúcido
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), static_cast<int>(m_display.getHeight()), RGBA(0, 0, 0, 180));

        int modalW = 600;
        int modalH = 400;
        int modalX = (static_cast<int>(m_display.getWidth()) - modalW) / 2;
        int modalY = (static_cast<int>(m_display.getHeight()) - modalH) / 2;

        m_display.drawRoundedRect(modalX, modalY, modalW, modalH, 16, Colors::CARD_BG);
        m_display.drawRoundedRect(modalX, modalY, modalW, 64, 16, Colors::HEADER_BG);
        m_display.drawText(modalX + 28, modalY + 20, "Configurações Técnicas", 22, Colors::TEXT_WHITE);

        int curY = modalY + 90;
        m_display.drawText(modalX + 28, curY, "Pasta Destino no Drive", 14, Colors::TEXT_MUTED);
        curY += 24;
        m_display.drawText(modalX + 28, curY, m_client.getFolderName(), 18, Colors::JOYCON_CYAN);

        curY += 46;
        m_display.drawText(modalX + 28, curY, "OAuth Client ID", 14, Colors::TEXT_MUTED);
        curY += 24;
        std::string cid = m_client.getClientId();
        std::string shortCid = cid.empty() ? "Não Configurado" : (cid.substr(0, 20) + "...");
        m_display.drawText(modalX + 28, curY, shortCid, 16, Colors::TEXT_PRIMARY);

        curY += 56;
        // Botão Testar Conexão
        m_display.drawRoundedRect(modalX + 28, curY, modalW - 56, 50, 10, Colors::CARD_HOVER_BG);
        m_display.drawText(modalX + 44, curY + 14, "(A) Testar Conexão e Permissões da API", 18, Colors::JOYCON_CYAN);

        // Rodapé do Modal
        m_display.drawText(modalX + 28, modalY + modalH - 36, "(B) Fechar Configurações", 16, Colors::TEXT_MUTED);
    }

    void UIManager::renderOAuthModal() {
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), static_cast<int>(m_display.getHeight()), RGBA(0, 0, 0, 180));

        int modalW = 640;
        int modalH = 380;
        int modalX = (static_cast<int>(m_display.getWidth()) - modalW) / 2;
        int modalY = (static_cast<int>(m_display.getHeight()) - modalH) / 2;

        m_display.drawRoundedRect(modalX, modalY, modalW, modalH, 16, Colors::CARD_BG);
        m_display.drawRoundedRect(modalX, modalY, modalW, 64, 16, Colors::HEADER_BG);
        m_display.drawText(modalX + 28, modalY + 20, "Vincular Conta Google", 22, Colors::TEXT_WHITE);

        m_display.drawText(modalX + 28, modalY + 90, "1. No celular ou PC, acerte a URL no navegador:", 16, Colors::TEXT_MUTED);
        m_display.drawText(modalX + 28, modalY + 116, m_oauth.getVerificationUrl(), 20, Colors::JOYCON_CYAN);

        m_display.drawText(modalX + 28, modalY + 160, "2. Digite o código exibido abaixo:", 16, Colors::TEXT_MUTED);

        // Caixa de destaque do código
        m_display.drawRoundedRect(modalX + 28, modalY + 190, modalW - 56, 70, 12, Colors::CARD_ALT_BG);
        std::string code = m_oauth.getUserCode();
        if (code.empty()) code = "GERANDO...";
        int codeW = m_display.getFont().measureTextWidth(code, 32);
        int codeX = modalX + (modalW - codeW) / 2;
        m_display.drawText(codeX, modalY + 208, code, 32, Colors::JOYCON_YELLOW);

        m_display.drawText(modalX + 28, modalY + 285, "Aguardando confirmação remota da conta...", 14, Colors::TEXT_MUTED);
        m_display.drawText(modalX + 28, modalY + modalH - 36, "(B) Cancelar", 16, Colors::TEXT_MUTED);
    }

    void UIManager::renderOperationModal() {
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), static_cast<int>(m_display.getHeight()), RGBA(0, 0, 0, 180));

        int modalW = 680;
        int modalH = 360;
        int modalX = (static_cast<int>(m_display.getWidth()) - modalW) / 2;
        int modalY = (static_cast<int>(m_display.getHeight()) - modalH) / 2;

        m_display.drawRoundedRect(modalX, modalY, modalW, modalH, 16, Colors::CARD_BG);
        m_display.drawRoundedRect(modalX, modalY, modalW, 64, 16, Colors::HEADER_BG);

        bool running = m_engine.isRunning();
        std::string title = running ? "Sincronização em Andamento" : "Operação Concluída";
        m_display.drawText(modalX + 28, modalY + 20, title, 22, Colors::TEXT_WHITE);

        m_display.drawText(modalX + 28, modalY + 90, m_engine.getCurrentGame(), 20, Colors::JOYCON_CYAN);
        m_display.drawText(modalX + 28, modalY + 120, m_engine.getStatusDetail(), 16, Colors::TEXT_MUTED);

        // Barra de progresso
        int barX = modalX + 28;
        int barY = modalY + 170;
        int barW = modalW - 56;
        int barH = 14;

        m_display.drawRoundedRect(barX, barY, barW, barH, 6, Colors::CARD_ALT_BG);
        float p = std::clamp(m_engine.getProgress(), 0.0f, 1.0f);
        int fillW = static_cast<int>(static_cast<float>(barW) * p);
        if (fillW > 0) {
            m_display.drawRoundedRect(barX, barY, fillW, barH, 6, Colors::JOYCON_CYAN);
        }

        char pctBuf[16];
        std::snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>(p * 100.0f));
        m_display.drawText(modalX + modalW - 68, barY + 24, pctBuf, 14, Colors::TEXT_MUTED);

        if (!running) {
            auto summary = m_engine.getSummary();
            std::string resText = summary.success ? "Todos os saves foram sincronizados com sucesso!" : "Concluído com avisos/erros. Verifique o sync.log.";
            u32 col = summary.success ? Colors::STATUS_GREEN : Colors::JOYCON_YELLOW;
            m_display.drawText(modalX + 28, modalY + 240, resText, 16, col);
            m_display.drawText(modalX + 28, modalY + modalH - 36, "(A) ou (B) Fechar", 16, Colors::JOYCON_CYAN);
        } else {
            m_display.drawText(modalX + 28, modalY + modalH - 36, "(B) Cancelar Operação", 16, Colors::TEXT_MUTED);
        }
    }

    void UIManager::render() {
        m_display.startFrame();

        Core::Screen curScreen = m_stateMachine.getScreen();
        if (curScreen == Core::Screen::GAME_SELECT) {
            m_screenSelect.render();
        } else {
            m_screenMain.render();
        }

        if (curScreen == Core::Screen::SETTINGS) {
            renderSettingsModal();
        } else if (curScreen == Core::Screen::OAUTH_MODAL) {
            renderOAuthModal();
        } else if (curScreen == Core::Screen::OPERATION_PROGRESS) {
            renderOperationModal();
        }

        m_display.endFrame();
    }

} // namespace UI

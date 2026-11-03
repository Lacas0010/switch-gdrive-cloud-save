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
            } else if (kDown & HidNpadButton_X) {
                m_stateMachine.setScreen(Core::Screen::HELP_MODAL);
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
            m_screenSelect.handleInput(kDown);
#ifdef __SWITCH__
            if (kDown & HidNpadButton_B) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            } else if (kDown & HidNpadButton_A) {
                if (m_settingsOption == 0) {
                    m_engine.startTestConnection();
                }
            }
#endif
        } else if (curScreen == Core::Screen::HELP_MODAL) {
#ifdef __SWITCH__
            if ((kDown & HidNpadButton_B) || (kDown & HidNpadButton_A)) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
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

        m_display.drawText(modalX + 28, modalY + 90, "1. No celular ou computador, acesse o endereço abaixo:", 16, Colors::TEXT_MUTED);
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

        int modalW = 720;
        int modalH = 390;
        int modalX = (static_cast<int>(m_display.getWidth()) - modalW) / 2;
        int modalY = (static_cast<int>(m_display.getHeight()) - modalH) / 2;

        m_display.drawRoundedRect(modalX, modalY, modalW, modalH, 16, Colors::CARD_BG);
        m_display.drawRoundedRect(modalX, modalY, modalW, 64, 16, Colors::HEADER_BG);

        bool running = m_engine.isRunning();
        if (running) {
            m_display.drawText(modalX + 32, modalY + 20, "Sincronização em Andamento", 22, Colors::TEXT_WHITE);

            m_display.drawText(modalX + 32, modalY + 90, m_engine.getCurrentGame(), 20, Colors::JOYCON_CYAN);
            m_display.drawText(modalX + 32, modalY + 120, m_engine.getStatusDetail(), 16, Colors::TEXT_MUTED);

            // Barra de progresso
            int barX = modalX + 32;
            int barY = modalY + 170;
            int barW = modalW - 64;
            int barH = 14;

            m_display.drawRoundedRect(barX, barY, barW, barH, 6, Colors::CARD_ALT_BG);
            float p = std::clamp(m_engine.getProgress(), 0.0f, 1.0f);
            int fillW = static_cast<int>(static_cast<float>(barW) * p);
            if (fillW > 0) {
                m_display.drawRoundedRect(barX, barY, fillW, barH, 6, Colors::JOYCON_CYAN);
            }

            char pctBuf[16];
            std::snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>(p * 100.0f));
            m_display.drawText(modalX + modalW - 80, barY + 24, pctBuf, 14, Colors::TEXT_MUTED);

            m_display.drawText(modalX + 32, modalY + modalH - 36, "(B) Cancelar Operação", 16, Colors::TEXT_MUTED);
        } else {
            auto summary = m_engine.getSummary();
            std::string title = summary.success ? "Upload Concluído com Sucesso!" : "Operação Concluída com Avisos";
            u32 titleColor = summary.success ? Colors::STATUS_GREEN : Colors::JOYCON_YELLOW;
            m_display.drawText(modalX + 32, modalY + 20, title, 22, titleColor);

            int statY = modalY + 90;
            m_display.drawText(modalX + 32, statY, "Estatísticas da Operação:", 16, Colors::JOYCON_YELLOW);
            statY += 28;

            char sBuf[96];
            std::snprintf(sBuf, sizeof(sBuf), "• Saves sincronizados com sucesso: %d", summary.successCount);
            m_display.drawText(modalX + 44, statY, sBuf, 15, Colors::STATUS_GREEN);
            statY += 26;

            std::snprintf(sBuf, sizeof(sBuf), "• Falhas ou erros de envio: %d", summary.failCount);
            m_display.drawText(modalX + 44, statY, sBuf, 15, summary.failCount == 0 ? Colors::TEXT_MUTED : Colors::STATUS_RED);
            statY += 26;

            m_display.drawText(modalX + 44, statY, "• Pasta de destino na nuvem: \"Switch Saves/\"", 15, Colors::JOYCON_CYAN);
            statY += 26;

            m_display.drawText(modalX + 44, statY, "• Histórico versionado salvo em snapshots com CRC32 verificado.", 15, Colors::TEXT_PRIMARY);

            m_display.drawRect(modalX, modalY + modalH - 52, modalW, 1, Colors::CARD_OUTLINE);
            m_display.drawText(modalX + modalW - 160, modalY + modalH - 36, "(A) Continuar", 16, Colors::JOYCON_CYAN);
        }
    }

    void UIManager::renderHelpModal() {
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), static_cast<int>(m_display.getHeight()), RGBA(0, 0, 0, 180));

        int modalW = 860;
        int modalH = 500;
        int modalX = (static_cast<int>(m_display.getWidth()) - modalW) / 2;
        int modalY = (static_cast<int>(m_display.getHeight()) - modalH) / 2;

        m_display.drawRoundedRect(modalX, modalY, modalW, modalH, 16, Colors::CARD_BG);
        m_display.drawRoundedRect(modalX, modalY, modalW, 64, 16, Colors::HEADER_BG);
        m_display.drawText(modalX + 32, modalY + 18, "Guia do Utilizador & Ajuda", 24, Colors::TEXT_WHITE);
        m_display.drawText(modalX + 32, modalY + 44, "Google Drive Cloud Save Suite para Nintendo Switch", 13, Colors::JOYCON_CYAN);

        int curY = modalY + 84;

        // Seção 1: Como Funciona
        m_display.drawText(modalX + 32, curY, "Como Funciona a Sincronização:", 16, Colors::JOYCON_YELLOW);
        curY += 24;
        m_display.drawText(modalX + 44, curY, "• Sincronização sob demanda: executa backups e restaurações apenas quando solicitado.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Execução em primeiro plano: sem processos em segundo plano ou impacto de desempenho.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Saves empacotados em PKZIP e validados por verificação de integridade CRC32 bloco a bloco.", 14, Colors::TEXT_PRIMARY);

        curY += 34;
        // Seção 2: Requisitos e Proteção de Dados
        m_display.drawText(modalX + 32, curY, "Requisitos & Proteção de Dados:", 16, Colors::JOYCON_YELLOW);
        curY += 24;
        m_display.drawText(modalX + 44, curY, "• Credenciais OAuth 2.0 armazenadas com segurança em sdmc:/config/gdrive-sync/config.json.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Backup preventivo automático local gerado antes de qualquer restauração para save:/.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Trava de segurança temporal RTC: impede substituição acidental se o relógio estiver descalibrado.", 14, Colors::TEXT_PRIMARY);

        curY += 34;
        // Seção 3: Controlos da Interface
        m_display.drawText(modalX + 32, curY, "Controlos da Interface:", 16, Colors::JOYCON_YELLOW);
        curY += 24;
        m_display.drawText(modalX + 44, curY, "• [D-Pad / Analógico]: Navegar pelas opções e listas de jogos", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• [A]: Confirmar / [B]: Voltar ou Cancelar / [-]: Configurações / [+]: Sair", 14, Colors::TEXT_PRIMARY);

        // Rodapé
        m_display.drawRect(modalX, modalY + modalH - 52, modalW, 1, Colors::CARD_OUTLINE);
        m_display.drawText(modalX + modalW - 130, modalY + modalH - 36, "(B) Fechar", 16, Colors::JOYCON_CYAN);
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
        } else if (curScreen == Core::Screen::HELP_MODAL) {
            renderHelpModal();
        }

        m_display.endFrame();
    }

} // namespace UI

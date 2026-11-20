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
        m_screenRestore(m_display, m_textures, saveManager, driveClient),
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

    void UIManager::activateMainCard(int sel) {
        if (sel == 0) {
            m_engine.startFullBackup();
        } else if (sel == 1) {
            // Abre a tela de comparação jogo por jogo (Drive vs Local)
            m_screenRestore.refreshAsync();
            m_stateMachine.setScreen(Core::Screen::RESTORE_SELECT);
        } else if (sel == 2) {
            m_screenSelect.refreshGames();
            m_stateMachine.setScreen(Core::Screen::GAME_SELECT);
        } else if (sel == 3) {
            // Sempre abre o modal de vinculação (exibe código ou erro detalhado)
            m_oauth.startDeviceAuth();
            m_stateMachine.setScreen(Core::Screen::OAUTH_MODAL);
        }
    }

    void UIManager::handleInput(u64 kDown) {
        Core::Screen curScreen = m_stateMachine.getScreen();

        if (curScreen == Core::Screen::MAIN) {
            m_screenMain.handleInput(kDown);
#ifdef __SWITCH__
            if (kDown & HidNpadButton_A) {
                activateMainCard(m_screenMain.getSelectedIndex());
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
            } else if (kDown & HidNpadButton_A) {
                u64 tid = m_screenSelect.getSelectedTitleId();
                if (tid != 0) {
                    m_engine.startSingleBackup(tid, m_textures.getTitleName(tid));
                }
            }
#endif
        } else if (curScreen == Core::Screen::RESTORE_SELECT) {
            m_screenRestore.handleInput(kDown);
#ifdef __SWITCH__
            if (kDown & HidNpadButton_B) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            } else if (kDown & HidNpadButton_A) {
                if (!m_screenRestore.isLoading() && m_screenRestore.hasSelectedCandidate()) {
                    auto cand = m_screenRestore.getSelectedCandidate();
                    m_engine.startSingleRestore(cand.driveFile, m_textures.getTitleName(cand.driveFile.titleId));
                }
            } else if (kDown & HidNpadButton_Y) {
                m_engine.startFullRestore();
            } else if (kDown & HidNpadButton_X) {
                m_screenRestore.refreshAsync();
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
            } else if (kDown & HidNpadButton_X) {
                m_oauth.startDeviceAuth();
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

    bool UIManager::handleTouch(const TouchEvent& touch) {
        if (!touch.tapped) return false;

        Core::Screen curScreen = m_stateMachine.getScreen();
        int dispW = static_cast<int>(m_display.getWidth());
        int dispH = static_cast<int>(m_display.getHeight());

        if (curScreen == Core::Screen::MAIN) {
            auto act = m_screenMain.handleTouch(touch);
            if (act == MainTouchAction::ACTIVATE_CARD) {
                activateMainCard(m_screenMain.getSelectedIndex());
            } else if (act == MainTouchAction::OPEN_HELP) {
                m_stateMachine.setScreen(Core::Screen::HELP_MODAL);
            } else if (act == MainTouchAction::OPEN_SETTINGS) {
                m_stateMachine.setScreen(Core::Screen::SETTINGS);
            } else if (act == MainTouchAction::EXIT_APP) {
                return true;
            }
        } else if (curScreen == Core::Screen::GAME_SELECT) {
            auto act = m_screenSelect.handleTouch(touch);
            if (act == SelectTouchAction::BACK) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            } else if (act == SelectTouchAction::SYNC_SELECTED) {
                u64 tid = m_screenSelect.getSelectedTitleId();
                if (tid != 0) {
                    m_engine.startSingleBackup(tid, m_textures.getTitleName(tid));
                }
            }
        } else if (curScreen == Core::Screen::RESTORE_SELECT) {
            auto act = m_screenRestore.handleTouch(touch);
            if (act == RestoreTouchAction::BACK) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            } else if (act == RestoreTouchAction::REFRESH) {
                m_screenRestore.refreshAsync();
            } else if (act == RestoreTouchAction::RESTORE_ALL) {
                m_engine.startFullRestore();
            } else if (act == RestoreTouchAction::RESTORE_SELECTED) {
                if (!m_screenRestore.isLoading() && m_screenRestore.hasSelectedCandidate()) {
                    auto cand = m_screenRestore.getSelectedCandidate();
                    m_engine.startSingleRestore(cand.driveFile, m_textures.getTitleName(cand.driveFile.titleId));
                }
            }
        } else if (curScreen == Core::Screen::SETTINGS) {
            int modalW = 600, modalH = 400;
            int modalX = (dispW - modalW) / 2;
            int modalY = (dispH - modalH) / 2;
            bool inside = (touch.x >= modalX && touch.x <= modalX + modalW && touch.y >= modalY && touch.y <= modalY + modalH);
            if (!inside || touch.y >= modalY + modalH - 56) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            } else if (touch.y >= modalY + 230 && touch.y <= modalY + 300) {
                m_engine.startTestConnection();
            }
        } else if (curScreen == Core::Screen::OAUTH_MODAL) {
            int modalW = 680, modalH = 400;
            int modalX = (dispW - modalW) / 2;
            int modalY = (dispH - modalH) / 2;
            bool inside = (touch.x >= modalX && touch.x <= modalX + modalW && touch.y >= modalY && touch.y <= modalY + modalH);
            if (!inside) {
                m_oauth.cancelAuth();
                m_stateMachine.setScreen(Core::Screen::MAIN);
            } else if (touch.y >= modalY + modalH - 56) {
                if (touch.x < modalX + 260) {
                    m_oauth.startDeviceAuth();
                } else {
                    m_oauth.cancelAuth();
                    m_stateMachine.setScreen(Core::Screen::MAIN);
                }
            }
        } else if (curScreen == Core::Screen::HELP_MODAL) {
            int modalW = 860, modalH = 500;
            int modalX = (dispW - modalW) / 2;
            int modalY = (dispH - modalH) / 2;
            bool inside = (touch.x >= modalX && touch.x <= modalX + modalW && touch.y >= modalY && touch.y <= modalY + modalH);
            if (!inside || touch.y >= modalY + modalH - 56) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            }
        } else if (curScreen == Core::Screen::OPERATION_PROGRESS) {
            int modalW = 720, modalH = 390;
            int modalX = (dispW - modalW) / 2;
            int modalY = (dispH - modalH) / 2;
            if (m_engine.isRunning()) {
                if (touch.y >= modalY + modalH - 56) {
                    m_engine.requestCancel();
                }
            } else {
                // Qualquer toque após concluir fecha o modal de resumo
                m_stateMachine.setScreen(Core::Screen::MAIN);
            }
        }

        return false;
    }

    void UIManager::update() {
        m_engine.poll();
        m_screenRestore.poll();

        if (m_stateMachine.getScreen() == Core::Screen::OAUTH_MODAL) {
            auto status = m_oauth.pollAuthStatus();
            if (status == Network::OAuthState::AUTHORIZED) {
                m_stateMachine.setScreen(Core::Screen::MAIN);
            }
        }
    }

    void UIManager::renderSettingsModal() {
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
        std::string shortCid = cid.empty() ? "Não Configurado" : (cid.substr(0, std::min<size_t>(20, cid.size())) + "...");
        m_display.drawText(modalX + 28, curY, shortCid, 16, Colors::TEXT_PRIMARY);

        curY += 56;
        m_display.drawRoundedRect(modalX + 28, curY, modalW - 56, 50, 10, Colors::CARD_HOVER_BG);
        m_display.drawText(modalX + 44, curY + 14, "(A) Testar Conexão e Permissões da API", 18, Colors::JOYCON_CYAN);

        m_display.drawText(modalX + 28, modalY + modalH - 36, "(B) Fechar Configurações", 16, Colors::TEXT_MUTED);
    }

    void UIManager::renderOAuthModal() {
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), static_cast<int>(m_display.getHeight()), RGBA(0, 0, 0, 180));

        int modalW = 680;
        int modalH = 400;
        int modalX = (static_cast<int>(m_display.getWidth()) - modalW) / 2;
        int modalY = (static_cast<int>(m_display.getHeight()) - modalH) / 2;

        m_display.drawRoundedRect(modalX, modalY, modalW, modalH, 16, Colors::CARD_BG);
        m_display.drawRoundedRect(modalX, modalY, modalW, 64, 16, Colors::HEADER_BG);
        m_display.drawText(modalX + 28, modalY + 20, "Vincular Conta Google", 22, Colors::TEXT_WHITE);

        if (m_oauth.getState() == Network::OAuthState::FAILED) {
            m_display.drawText(modalX + 28, modalY + 94, "Não foi possível iniciar o pareamento OAuth 2.0:", 17, Colors::STATUS_RED);

            std::string errMsg = m_oauth.getErrorMessage();
            if (errMsg.empty()) errMsg = "Erro desconhecido ao contatar oauth2.googleapis.com";
            m_display.drawRoundedRect(modalX + 28, modalY + 130, modalW - 56, 76, 10, Colors::CARD_ALT_BG);
            m_display.drawText(modalX + 42, modalY + 156, errMsg, 15, Colors::JOYCON_YELLOW);

            m_display.drawText(modalX + 28, modalY + 230, "Verifique:", 15, Colors::TEXT_WHITE);
            m_display.drawText(modalX + 36, modalY + 256, "• Arquivo sdmc:/config/gdrive-sync/config.json possui client_id válido", 14, Colors::TEXT_MUTED);
            m_display.drawText(modalX + 36, modalY + 280, "• Conexão Wi-Fi ativa e relógio do Switch ajustado", 14, Colors::TEXT_MUTED);
            m_display.drawText(modalX + 36, modalY + 304, "• Client ID do tipo TVs e dispositivos de entrada limitada", 14, Colors::TEXT_MUTED);

            m_display.drawText(modalX + 28, modalY + modalH - 36, "(X) Tentar Novamente   (B) Fechar", 16, Colors::JOYCON_CYAN);
            return;
        }

        m_display.drawText(modalX + 28, modalY + 90, "1. No celular ou computador, acesse o endereço abaixo:", 16, Colors::TEXT_MUTED);
        m_display.drawText(modalX + 28, modalY + 116, m_oauth.getVerificationUrl(), 20, Colors::JOYCON_CYAN);

        m_display.drawText(modalX + 28, modalY + 160, "2. Digite o código exibido abaixo:", 16, Colors::TEXT_MUTED);

        m_display.drawRoundedRect(modalX + 28, modalY + 190, modalW - 56, 70, 12, Colors::CARD_ALT_BG);
        std::string code = m_oauth.getUserCode();
        if (code.empty()) code = "GERANDO...";
        int codeW = m_display.getFont().measureTextWidth(code, 32);
        int codeX = modalX + (modalW - codeW) / 2;
        m_display.drawText(codeX, modalY + 208, code, 32, Colors::JOYCON_YELLOW);

        m_display.drawText(modalX + 28, modalY + 285, "Aguardando confirmação remota da conta...", 14, Colors::TEXT_MUTED);
        m_display.drawText(modalX + 28, modalY + modalH - 36, "(X) Novo Código   (B) Cancelar", 16, Colors::TEXT_MUTED);
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
            std::string title = summary.success ? "Operação Concluída com Sucesso!" : "Operação Concluída com Avisos";
            u32 titleColor = summary.success ? Colors::STATUS_GREEN : Colors::JOYCON_YELLOW;
            m_display.drawText(modalX + 32, modalY + 20, title, 22, titleColor);

            int statY = modalY + 90;
            m_display.drawText(modalX + 32, statY, "Resumo da Operação:", 16, Colors::JOYCON_YELLOW);
            statY += 28;

            char sBuf[128];
            std::snprintf(sBuf, sizeof(sBuf), "• Concluídos com sucesso: %d", summary.successCount);
            m_display.drawText(modalX + 44, statY, sBuf, 15, Colors::STATUS_GREEN);
            statY += 26;

            std::snprintf(sBuf, sizeof(sBuf), "• Falhas ou erros: %d", summary.failCount);
            m_display.drawText(modalX + 44, statY, sBuf, 15, summary.failCount == 0 ? Colors::TEXT_MUTED : Colors::STATUS_RED);
            statY += 26;

            if (!summary.logMessages.empty()) {
                m_display.drawText(modalX + 44, statY, "• " + summary.logMessages.back(), 15, Colors::JOYCON_CYAN);
                statY += 26;
            }

            m_display.drawText(modalX + 44, statY, "• Backup preventivo local e verificação CRC32 ativos.", 14, Colors::TEXT_PRIMARY);

            m_display.drawRect(modalX, modalY + modalH - 52, modalW, 1, Colors::CARD_OUTLINE);
            m_display.drawText(modalX + modalW - 180, modalY + modalH - 36, "(A) / Toque Continuar", 15, Colors::JOYCON_CYAN);
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

        m_display.drawText(modalX + 32, curY, "Como Funciona a Sincronização:", 16, Colors::JOYCON_YELLOW);
        curY += 24;
        m_display.drawText(modalX + 44, curY, "• Sincronização sob demanda: backup completo ou individual por jogo.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Restauração seletiva: compare data e tamanho (Drive vs Local) antes de restaurar.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Saves empacotados em PKZIP e validados por CRC32 bloco a bloco.", 14, Colors::TEXT_PRIMARY);

        curY += 34;
        m_display.drawText(modalX + 32, curY, "Requisitos & Proteção de Dados:", 16, Colors::JOYCON_YELLOW);
        curY += 24;
        m_display.drawText(modalX + 44, curY, "• Credenciais OAuth 2.0 armazenadas em sdmc:/config/gdrive-sync/config.json.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Backup preventivo local gerado automaticamente antes de sobrescrever save:/.", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• Rollback automático em caso de falha na extração ou commit.", 14, Colors::TEXT_PRIMARY);

        curY += 34;
        m_display.drawText(modalX + 32, curY, "Controlos & Tela de Toque:", 16, Colors::JOYCON_YELLOW);
        curY += 24;
        m_display.drawText(modalX + 44, curY, "• [Touchscreen]: Toque diretamente nos cards, botões e rodapé em qualquer tela", 14, Colors::TEXT_PRIMARY);
        curY += 22;
        m_display.drawText(modalX + 44, curY, "• [D-Pad / Analógico]: Navegar | [A]: Confirmar | [B]: Voltar | [-]: Config | [+]: Sair", 14, Colors::TEXT_PRIMARY);

        m_display.drawRect(modalX, modalY + modalH - 52, modalW, 1, Colors::CARD_OUTLINE);
        m_display.drawText(modalX + modalW - 130, modalY + modalH - 36, "(B) Fechar", 16, Colors::JOYCON_CYAN);
    }

    void UIManager::render() {
        m_display.startFrame();

        Core::Screen curScreen = m_stateMachine.getScreen();
        if (curScreen == Core::Screen::GAME_SELECT) {
            m_screenSelect.render();
        } else if (curScreen == Core::Screen::RESTORE_SELECT) {
            m_screenRestore.render();
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

#include "screen_main.hpp"
#include <cstdio>

namespace UI {

    ScreenMain::ScreenMain(
        Display& display,
        Core::SyncEngine& engine,
        Network::DriveClient& client,
        Storage::SaveManager& saveManager
    ) : m_display(display),
        m_engine(engine),
        m_client(client),
        m_saveManager(saveManager),
        m_selectedCard(0) {}

    void ScreenMain::handleInput(u64 kDown) {
#ifdef __SWITCH__
        if (kDown & HidNpadButton_Down) {
            m_selectedCard = (m_selectedCard + 1) % 4;
        } else if (kDown & HidNpadButton_Up) {
            m_selectedCard = (m_selectedCard + 3) % 4;
        }
#else
        (void)kDown;
#endif
    }

    void ScreenMain::renderHeader() {
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), 64, Colors::HEADER_BG);
        m_display.drawText(36, 18, "Google Drive Cloud Save", 26, Colors::TEXT_PRIMARY);

        bool online = m_client.isConfigured();
        u32 badgeColor = online ? Colors::STATUS_GREEN : Colors::JOYCON_YELLOW;
        std::string_view statusText = online ? "● Conectado" : "● Não Vinculado";

        int textW = m_display.getFont().measureTextWidth(statusText, 18);
        int badgeX = static_cast<int>(m_display.getWidth()) - textW - 36;
        m_display.drawText(badgeX, 22, statusText, 18, badgeColor);

        // Linha divisória sutil
        m_display.drawRect(0, 63, static_cast<int>(m_display.getWidth()), 1, Colors::CARD_OUTLINE);
    }

    void ScreenMain::renderLeftStatus() {
        int panelX = 36;
        int panelY = 88;
        int panelW = 340;
        int panelH = static_cast<int>(m_display.getHeight()) - 148;

        m_display.drawRoundedRect(panelX, panelY, panelW, panelH, 12, Colors::CARD_BG);
        m_display.drawText(panelX + 24, panelY + 24, "Status do Console", 20, Colors::TEXT_WHITE);

        int curY = panelY + 70;
        // 1. Conta ativa
        m_display.drawText(panelX + 24, curY, "Perfil Switch", 14, Colors::TEXT_MUTED);
        curY += 22;
        std::string uidStr = Storage::SaveManager::formatUid(m_engine.getActiveUid());
        std::string shortUid = uidStr.empty() ? "Nenhum" : (uidStr.substr(0, 12) + "...");
        m_display.drawText(panelX + 24, curY, shortUid, 16, Colors::TEXT_PRIMARY);

        curY += 46;
        // 2. Pasta destino
        m_display.drawText(panelX + 24, curY, "Pasta no Google Drive", 14, Colors::TEXT_MUTED);
        curY += 22;
        m_display.drawText(panelX + 24, curY, m_client.getFolderName(), 16, Colors::JOYCON_CYAN);

        curY += 46;
        // 3. Status da Nuvem
        m_display.drawText(panelX + 24, curY, "Status da Conexão", 14, Colors::TEXT_MUTED);
        curY += 22;
        bool ready = m_client.isConfigured();
        m_display.drawText(panelX + 24, curY, ready ? "Pronto para Sincronia" : "Aguardando Vinculação", 16, ready ? Colors::STATUS_GREEN : Colors::JOYCON_YELLOW);

        curY += 46;
        // 4. Jogos Rasteados
        m_display.drawText(panelX + 24, curY, "Versão do Sistema", 14, Colors::TEXT_MUTED);
        curY += 22;
        m_display.drawText(panelX + 24, curY, "v0.1.0 (Produção)", 16, Colors::TEXT_MUTED);
    }

    void ScreenMain::renderRightCards() {
        int startX = 400;
        int startY = 88;
        int cardW = static_cast<int>(m_display.getWidth()) - startX - 36;
        int cardH = 92;
        int spacing = 18;

        struct CardDef {
            const char* title;
            const char* subtitle;
            u32 iconColor;
        };

        static constexpr CardDef cards[4] = {
            {"Backup Completo", "Envia todos os saves do perfil ativo para o Google Drive", Colors::JOYCON_CYAN},
            {"Restaurar da Nuvem", "Atualiza saves locais que estejam mais recentes na nuvem", Colors::STATUS_GREEN},
            {"Gerenciar por Jogo", "Visualizar histórico, selecionar títulos e restaurar snapshots", Colors::JOYCON_YELLOW},
            {"Vincular Conta Google", "Autenticar via código na tela em google.com/device", Colors::DRIVE_BLUE}
        };

        for (int i = 0; i < 4; ++i) {
            int y = startY + i * (cardH + spacing);
            bool isSelected = (m_selectedCard == i);

            u32 bgCol = isSelected ? Colors::CARD_HOVER_BG : Colors::CARD_BG;
            u32 borderCol = isSelected ? Colors::CARD_HOVER_OUT : Colors::CARD_OUTLINE;

            m_display.drawRoundedRect(startX, y, cardW, cardH, 12, bgCol);
            if (isSelected) {
                // Indicador de foco Joy-Con no lado esquerdo do card
                m_display.drawRect(startX, y + 8, 4, cardH - 16, Colors::JOYCON_CYAN);
            }

            // Ícone colorido de identificação visual
            m_display.drawRoundedRect(startX + 20, y + 22, 48, 48, 8, cards[i].iconColor);

            // Título e subtítulo
            m_display.drawText(startX + 84, y + 20, cards[i].title, 20, Colors::TEXT_WHITE);
            m_display.drawText(startX + 84, y + 50, cards[i].subtitle, 14, Colors::TEXT_MUTED);
        }
    }

    void ScreenMain::renderFooter() {
        int footerY = static_cast<int>(m_display.getHeight()) - 48;
        m_display.drawRect(0, footerY, static_cast<int>(m_display.getWidth()), 48, Colors::FOOTER_BG);
        m_display.drawRect(0, footerY, static_cast<int>(m_display.getWidth()), 1, Colors::CARD_OUTLINE);

        int curX = 36;
        auto drawBtn = [this, footerY, &curX](std::string_view badge, std::string_view label) {
            m_display.drawText(curX, footerY + 14, badge, 16, Colors::JOYCON_CYAN);
            curX += m_display.getFont().measureTextWidth(badge, 16) + 6;
            m_display.drawText(curX, footerY + 14, label, 16, Colors::TEXT_MUTED);
            curX += m_display.getFont().measureTextWidth(label, 16) + 24;
        };

        drawBtn("(A)", "Confirmar");
        drawBtn("(B)", "Voltar");
        drawBtn("(-)", "Configurações");
        drawBtn("(+)", "Sair");
    }

    void ScreenMain::render() {
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), static_cast<int>(m_display.getHeight()), Colors::BG_DARK_TOP);
        renderHeader();
        renderLeftStatus();
        renderRightCards();
        renderFooter();
    }

} // namespace UI

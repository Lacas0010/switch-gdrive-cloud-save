#include "screen_select.hpp"
#include "../utils/time_util.hpp"
#include <cstdio>
#include <algorithm>

namespace UI {

    ScreenSelect::ScreenSelect(
        Display& display,
        TextureCache& textures,
        Storage::SaveManager& saveManager,
        Network::DriveClient& client
    ) : m_display(display),
        m_textures(textures),
        m_saveManager(saveManager),
        m_client(client),
        m_selectedIndex(0),
        m_scrollOffset(0) {
        refreshGames();
    }

    void ScreenSelect::refreshGames() {
        AccountUid uid{};
        Storage::SaveManager::getActiveAccount(uid);
        m_games = m_saveManager.listAvailableSaves(uid);
        if (m_selectedIndex >= static_cast<int>(m_games.size())) {
            m_selectedIndex = std::max(0, static_cast<int>(m_games.size()) - 1);
        }
    }

    u64 ScreenSelect::getSelectedTitleId() const noexcept {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_games.size())) {
            return m_games[static_cast<size_t>(m_selectedIndex)].titleId;
        }
        return 0;
    }

    void ScreenSelect::handleInput(u64 kDown) {
#ifdef __SWITCH__
        if (m_games.empty()) return;

        if (kDown & HidNpadButton_Down) {
            if (m_selectedIndex + 1 < static_cast<int>(m_games.size())) {
                m_selectedIndex++;
                if (m_selectedIndex - m_scrollOffset >= 5) {
                    m_scrollOffset++;
                }
            }
        } else if (kDown & HidNpadButton_Up) {
            if (m_selectedIndex > 0) {
                m_selectedIndex--;
                if (m_selectedIndex < m_scrollOffset) {
                    m_scrollOffset--;
                }
            }
        }
#else
        (void)kDown;
#endif
    }

    void ScreenSelect::render() {
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), static_cast<int>(m_display.getHeight()), Colors::BG_DARK_TOP);

        // Cabeçalho
        m_display.drawRect(0, 0, static_cast<int>(m_display.getWidth()), 64, Colors::HEADER_BG);
        m_display.drawText(36, 18, "Gerenciar Saves por Jogo", 26, Colors::TEXT_PRIMARY);

        char countBuf[32];
        std::snprintf(countBuf, sizeof(countBuf), "%zu Jogos Rastreados", m_games.size());
        int countW = m_display.getFont().measureTextWidth(countBuf, 18);
        m_display.drawText(static_cast<int>(m_display.getWidth()) - countW - 36, 22, countBuf, 18, Colors::TEXT_MUTED);
        m_display.drawRect(0, 63, static_cast<int>(m_display.getWidth()), 1, Colors::CARD_OUTLINE);

        // Lista de Jogos
        int startX = 36;
        int startY = 88;
        int cardW = static_cast<int>(m_display.getWidth()) - 72;
        int cardH = 80;
        int spacing = 14;
        int maxVisible = 5;

        if (m_games.empty()) {
            m_display.drawText(startX, startY + 40, "Nenhum save de jogo encontrado no console.", 20, Colors::TEXT_MUTED);
        } else {
            int endIndex = std::min(static_cast<int>(m_games.size()), m_scrollOffset + maxVisible);
            for (int i = m_scrollOffset; i < endIndex; ++i) {
                int y = startY + (i - m_scrollOffset) * (cardH + spacing);
                bool isSelected = (i == m_selectedIndex);

                u32 bg = isSelected ? Colors::CARD_HOVER_BG : Colors::CARD_BG;
                m_display.drawRoundedRect(startX, y, cardW, cardH, 12, bg);

                if (isSelected) {
                    m_display.drawRect(startX, y + 8, 4, cardH - 16, Colors::JOYCON_CYAN);
                }

                u64 tid = m_games[static_cast<size_t>(i)].titleId;
                const auto& meta = m_textures.getGameMetadata(tid);

                // Renderiza Ícone do Jogo
                if (meta.icon.valid && !meta.icon.pixels.empty()) {
                    m_display.drawTexture(startX + 16, y + 12, 56, 56, meta.icon.pixels.data(), meta.icon.width, meta.icon.height);
                } else {
                    m_display.drawRoundedRect(startX + 16, y + 12, 56, 56, 8, Colors::CARD_OUTLINE);
                }

                // Título e Detalhes
                m_display.drawText(startX + 88, y + 16, meta.name, 20, Colors::TEXT_WHITE);

                char subText[128];
                std::time_t ts = m_games[static_cast<size_t>(i)].lastModified;
                std::string tsStr = Utils::TimeUtil::formatUtcDateTime(ts);
                std::snprintf(subText, sizeof(subText), "%s | Último Backup: %s", meta.version.c_str(), tsStr.c_str());
                m_display.drawText(startX + 88, y + 46, subText, 14, Colors::TEXT_MUTED);
            }
        }

        // Rodapé
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

        drawBtn("(A)", "Sincronizar");
        drawBtn("(X)", "Histórico");
        drawBtn("(B)", "Voltar");
    }

} // namespace UI

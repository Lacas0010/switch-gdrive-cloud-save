#include "screen_restore.hpp"
#include "../utils/time_util.hpp"
#include <cstdio>
#include <algorithm>
#include <unordered_map>

namespace UI {

    ScreenRestore::ScreenRestore(
        Display& display,
        TextureCache& textures,
        Storage::SaveManager& saveManager,
        Network::DriveClient& client
    ) : m_display(display),
        m_textures(textures),
        m_saveManager(saveManager),
        m_client(client),
        m_selectedIndex(0),
        m_scrollOffset(0),
        m_loading(false),
        m_cancelFetch(false) {}

    ScreenRestore::~ScreenRestore() {
        m_cancelFetch.store(true, std::memory_order_relaxed);
        if (m_fetchThread.joinable()) {
            m_fetchThread.join();
        }
    }

    void ScreenRestore::poll() {
        if (!m_loading.load(std::memory_order_relaxed) && m_fetchThread.joinable()) {
            m_fetchThread.join();
        }
    }

    void ScreenRestore::refreshAsync() {
        if (m_loading.load(std::memory_order_relaxed)) return;
        poll();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_errorMessage.clear();
        }

        m_cancelFetch.store(false, std::memory_order_relaxed);
        m_loading.store(true, std::memory_order_relaxed);
        m_fetchThread = std::thread(&ScreenRestore::fetchWorker, this);
    }

    void ScreenRestore::fetchWorker() {
        std::string err;
        std::vector<Network::DriveFileInfo> driveFiles = m_client.listDriveSaves(&err, &m_cancelFetch);

        AccountUid uid{};
        Storage::SaveManager::getActiveAccount(uid);
        std::vector<Storage::GameSaveInfo> localSaves = m_saveManager.listAvailableSaves(uid);
        std::unordered_map<u64, Storage::GameSaveInfo> localMap;
        for (const auto& ls : localSaves) {
            localMap[ls.titleId] = ls;
        }

        std::vector<RestoreCandidate> built;
        built.reserve(driveFiles.size());

        for (const auto& df : driveFiles) {
            if (m_cancelFetch.load(std::memory_order_relaxed)) break;
            if (df.titleId == 0) continue; // Ignora pastas ou arquivos sem TitleID

            RestoreCandidate cand;
            cand.driveFile = df;

            auto it = localMap.find(df.titleId);
            if (it != localMap.end()) {
                cand.hasLocalSave = true;
                size_t realBytes = 0;
                std::time_t realTs = 0;
                if (m_saveManager.getLocalSaveStats(df.titleId, uid, realBytes, realTs)) {
                    cand.localSizeBytes = realBytes;
                    cand.localModifiedTime = realTs;
                } else {
                    cand.localSizeBytes = it->second.sizeBytes;
                    cand.localModifiedTime = it->second.lastModified;
                }
            }

            built.push_back(std::move(cand));
        }

        // Ordena por data de modificação na nuvem (mais recentes primeiro)
        std::sort(built.begin(), built.end(), [](const RestoreCandidate& a, const RestoreCandidate& b) {
            return a.driveFile.modifiedTime > b.driveFile.modifiedTime;
        });

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_candidates = std::move(built);
            if (!err.empty() && m_candidates.empty()) {
                m_errorMessage = err;
            }
            if (m_selectedIndex >= static_cast<int>(m_candidates.size())) {
                m_selectedIndex = std::max(0, static_cast<int>(m_candidates.size()) - 1);
            }
            if (m_scrollOffset > m_selectedIndex) {
                m_scrollOffset = m_selectedIndex;
            }
        }

        m_loading.store(false, std::memory_order_relaxed);
    }

    std::string ScreenRestore::formatSize(size_t bytes) {
        char buf[32];
        if (bytes == 0) {
            return "0 B";
        } else if (bytes < 1024) {
            std::snprintf(buf, sizeof(buf), "%zu B", bytes);
        } else if (bytes < 1024 * 1024) {
            std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        }
        return std::string(buf);
    }

    bool ScreenRestore::hasSelectedCandidate() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_candidates.size());
    }

    RestoreCandidate ScreenRestore::getSelectedCandidate() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_candidates.size())) {
            return m_candidates[static_cast<size_t>(m_selectedIndex)];
        }
        return RestoreCandidate{};
    }

    void ScreenRestore::handleInput(u64 kDown) {
#ifdef __SWITCH__
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_candidates.empty()) return;

        int maxVisible = 5;
        if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            if (m_selectedIndex + 1 < static_cast<int>(m_candidates.size())) {
                m_selectedIndex++;
                if (m_selectedIndex - m_scrollOffset >= maxVisible) {
                    m_scrollOffset++;
                }
            }
        } else if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
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

    RestoreTouchAction ScreenRestore::handleTouch(const TouchEvent& touch) {
        if (!touch.tapped) return RestoreTouchAction::NONE;

        int dispW = static_cast<int>(m_display.getWidth());
        int dispH = static_cast<int>(m_display.getHeight());
        int footerY = dispH - 48;

        // Toque no rodapé
        if (touch.y >= footerY) {
            if (touch.x < 260) return RestoreTouchAction::RESTORE_SELECTED;
            if (touch.x < 500) return RestoreTouchAction::RESTORE_ALL;
            if (touch.x < 700) return RestoreTouchAction::REFRESH;
            return RestoreTouchAction::BACK;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_candidates.empty()) return RestoreTouchAction::NONE;

        // Botões de scroll ▲ / ▼ no canto direito
        int scrollBtnX = dispW - 64;
        if (touch.x >= scrollBtnX && touch.x <= dispW - 12) {
            if (touch.y >= 88 && touch.y <= 148) {
                if (m_scrollOffset > 0) m_scrollOffset--;
                if (m_selectedIndex > 0) m_selectedIndex--;
                return RestoreTouchAction::NONE;
            }
            if (touch.y >= footerY - 72 && touch.y <= footerY - 12) {
                if (m_selectedIndex + 1 < static_cast<int>(m_candidates.size())) m_selectedIndex++;
                if (m_scrollOffset + 5 < static_cast<int>(m_candidates.size())) m_scrollOffset++;
                return RestoreTouchAction::NONE;
            }
        }

        // Lista de cards
        int startX = 36;
        int startY = 82;
        int cardW = dispW - 112;
        int cardH = 98;
        int spacing = 12;
        int maxVisible = 5;

        int endIndex = std::min(static_cast<int>(m_candidates.size()), m_scrollOffset + maxVisible);
        for (int i = m_scrollOffset; i < endIndex; ++i) {
            int y = startY + (i - m_scrollOffset) * (cardH + spacing);
            if (touch.x >= startX && touch.x <= startX + cardW && touch.y >= y && touch.y <= y + cardH) {
                int btnX = startX + cardW - 156;
                bool hitRestoreBtn = (touch.x >= btnX);
                bool wasAlreadySelected = (m_selectedIndex == i);
                m_selectedIndex = i;
                if (hitRestoreBtn || wasAlreadySelected) {
                    return RestoreTouchAction::RESTORE_SELECTED;
                }
                return RestoreTouchAction::NONE;
            }
        }

        return RestoreTouchAction::NONE;
    }

    void ScreenRestore::render() {
        int dispW = static_cast<int>(m_display.getWidth());
        int dispH = static_cast<int>(m_display.getHeight());

        m_display.drawRect(0, 0, dispW, dispH, Colors::BG_DARK_TOP);

        // Cabeçalho
        m_display.drawRect(0, 0, dispW, 64, Colors::HEADER_BG);
        m_display.drawText(36, 18, "Restaurar da Nuvem — Escolha por Jogo", 24, Colors::TEXT_PRIMARY);

        bool loading = m_loading.load(std::memory_order_relaxed);
        std::vector<RestoreCandidate> snapshot;
        std::string errCopy;
        int selIdx = 0;
        int scrollOff = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            snapshot = m_candidates;
            errCopy = m_errorMessage;
            selIdx = m_selectedIndex;
            scrollOff = m_scrollOffset;
        }

        char countBuf[48];
        if (loading) {
            std::snprintf(countBuf, sizeof(countBuf), "Carregando Drive...");
        } else {
            std::snprintf(countBuf, sizeof(countBuf), "%zu Saves no Drive", snapshot.size());
        }
        int countW = m_display.getFont().measureTextWidth(countBuf, 18);
        m_display.drawText(dispW - countW - 36, 22, countBuf, 18, Colors::JOYCON_CYAN);
        m_display.drawRect(0, 63, dispW, 1, Colors::CARD_OUTLINE);

        int startX = 36;
        int startY = 82;
        int cardW = dispW - 112;
        int cardH = 98;
        int spacing = 12;
        int maxVisible = 5;

        if (loading && snapshot.empty()) {
            m_display.drawText(startX, startY + 40, "Consultando saves no Google Drive e comparando com o Switch...", 20, Colors::JOYCON_CYAN);
            m_display.drawText(startX, startY + 76, "Aguarde alguns segundos.", 16, Colors::TEXT_MUTED);
        } else if (!errCopy.empty() && snapshot.empty()) {
            m_display.drawText(startX, startY + 40, "Erro ao consultar Google Drive:", 20, Colors::STATUS_RED);
            m_display.drawText(startX, startY + 72, errCopy, 16, Colors::JOYCON_YELLOW);
            m_display.drawText(startX, startY + 110, "Pressione (X) ou toque em Atualizar para tentar novamente.", 15, Colors::TEXT_MUTED);
        } else if (snapshot.empty()) {
            m_display.drawText(startX, startY + 40, "Nenhum save encontrado na pasta 'Switch Saves' do Google Drive.", 20, Colors::TEXT_MUTED);
        } else {
            int endIndex = std::min(static_cast<int>(snapshot.size()), scrollOff + maxVisible);
            for (int i = scrollOff; i < endIndex; ++i) {
                int y = startY + (i - scrollOff) * (cardH + spacing);
                bool isSelected = (i == selIdx);
                const auto& cand = snapshot[static_cast<size_t>(i)];

                u32 bg = isSelected ? Colors::CARD_HOVER_BG : Colors::CARD_BG;
                m_display.drawRoundedRect(startX, y, cardW, cardH, 12, bg);

                if (isSelected) {
                    m_display.drawRect(startX, y + 8, 4, cardH - 16, Colors::STATUS_GREEN);
                }

                const auto& meta = m_textures.getGameMetadata(cand.driveFile.titleId);

                // Ícone do jogo
                if (meta.icon.valid && !meta.icon.pixels.empty()) {
                    m_display.drawTexture(startX + 16, y + 21, 56, 56, meta.icon.pixels.data(), meta.icon.width, meta.icon.height);
                } else {
                    m_display.drawRoundedRect(startX + 16, y + 21, 56, 56, 8, Colors::CARD_ALT_BG);
                    m_display.drawGamepadIcon(startX + 44, y + 49, 28, Colors::JOYCON_CYAN);
                }

                // Nome do jogo
                m_display.drawText(startX + 86, y + 10, meta.name, 19, Colors::TEXT_WHITE);

                // Linha Drive: Data + Tamanho
                std::string driveDate = Utils::TimeUtil::formatUtcDateTime(cand.driveFile.modifiedTime);
                std::string driveSize = formatSize(cand.driveFile.size);
                char driveLine[160];
                std::snprintf(driveLine, sizeof(driveLine), "Nuvem (Drive):  %s   |   %s", driveDate.c_str(), driveSize.c_str());
                m_display.drawText(startX + 86, y + 40, driveLine, 14, Colors::JOYCON_CYAN);

                // Linha Local: Data + Tamanho
                char localLine[160];
                if (cand.hasLocalSave) {
                    std::string localDate = Utils::TimeUtil::formatUtcDateTime(cand.localModifiedTime);
                    std::string localSize = formatSize(cand.localSizeBytes);
                    std::snprintf(localLine, sizeof(localLine), "Local (Switch): %s   |   %s", localDate.c_str(), localSize.c_str());
                    u32 localCol = (cand.driveFile.modifiedTime > cand.localModifiedTime) ? Colors::JOYCON_YELLOW : Colors::STATUS_GREEN;
                    m_display.drawText(startX + 86, y + 64, localLine, 14, localCol);
                } else {
                    std::snprintf(localLine, sizeof(localLine), "Local (Switch): Nenhum save local encontrado");
                    m_display.drawText(startX + 86, y + 64, localLine, 14, Colors::TEXT_MUTED);
                }

                // Botão tátil [ Restaurar ] no lado direito do card
                int btnW = 136;
                int btnH = 40;
                int btnX = startX + cardW - btnW - 16;
                int btnY = y + (cardH - btnH) / 2;
                u32 btnBg = isSelected ? Colors::STATUS_GREEN : Colors::CARD_ALT_BG;
                u32 btnTxt = isSelected ? Colors::TEXT_DARK : Colors::STATUS_GREEN;
                m_display.drawRoundedRect(btnX, btnY, btnW, btnH, 8, btnBg);
                int lblW = m_display.getFont().measureTextWidth("Restaurar", 16);
                m_display.drawText(btnX + (btnW - lblW) / 2, btnY + 10, "Restaurar", 16, btnTxt);
            }

            // Botões táteis de rolagem ▲ / ▼ se houver mais de 5 jogos
            if (static_cast<int>(snapshot.size()) > maxVisible) {
                int scrollX = dispW - 60;
                int footerY = dispH - 48;
                m_display.drawRoundedRect(scrollX, 88, 44, 54, 8, scrollOff > 0 ? Colors::CARD_HOVER_BG : Colors::CARD_ALT_BG);
                m_display.drawText(scrollX + 14, 104, "^", 20, scrollOff > 0 ? Colors::JOYCON_CYAN : Colors::TEXT_MUTED);

                bool canDown = (scrollOff + maxVisible < static_cast<int>(snapshot.size()));
                m_display.drawRoundedRect(scrollX, footerY - 68, 44, 54, 8, canDown ? Colors::CARD_HOVER_BG : Colors::CARD_ALT_BG);
                m_display.drawText(scrollX + 14, footerY - 52, "v", 20, canDown ? Colors::JOYCON_CYAN : Colors::TEXT_MUTED);
            }
        }

        // Rodapé tátil
        int footerY = dispH - 48;
        m_display.drawRect(0, footerY, dispW, 48, Colors::FOOTER_BG);
        m_display.drawRect(0, footerY, dispW, 1, Colors::CARD_OUTLINE);

        int curX = 36;
        auto drawBtn = [this, footerY, &curX](std::string_view badge, std::string_view label) {
            m_display.drawText(curX, footerY + 14, badge, 16, Colors::JOYCON_CYAN);
            curX += m_display.getFont().measureTextWidth(badge, 16) + 6;
            m_display.drawText(curX, footerY + 14, label, 16, Colors::TEXT_MUTED);
            curX += m_display.getFont().measureTextWidth(label, 16) + 26;
        };

        drawBtn("(A)", "Restaurar Jogo");
        drawBtn("(Y)", "Restaurar Todos");
        drawBtn("(X)", "Atualizar");
        drawBtn("(B)", "Voltar");
    }

} // namespace UI

#include "UIManager.hpp"
#include <cstdio>
#include <ctime>
#include <algorithm>

namespace UI {

    UIManager::UIManager() = default;

    UIManager::~UIManager() {
        close();
    }

    bool UIManager::init() {
        return m_display.init();
    }

    void UIManager::close() {
        m_display.close();
    }

    bool UIManager::beginFrame() {
        return m_display.beginFrame();
    }

    void UIManager::endFrame() {
        m_display.endFrame();
    }

    // Renderiza textura escalada com cantos arredondados (Ícone Oficial do Jogo)
    void UIManager::drawTextureRounded(int x, int y, int w, int h, const Assets::TextureHandle& tex, int radius) {
        if (!tex.valid || tex.pixels.empty() || tex.width <= 0 || tex.height <= 0) return;

        int r2 = radius * radius;
        int tw = tex.width;
        int th = tex.height;

        for (int dy = 0; dy < h; dy++) {
            int py = y + dy;
            if (py < 0 || py >= m_display.getHeight()) continue;

            // Coordenada V na textura de origem
            int srcY = (dy * th) / h;
            if (srcY >= th) srcY = th - 1;

            for (int dx = 0; dx < w; dx++) {
                int px = x + dx;
                if (px < 0 || px >= m_display.getWidth()) continue;

                // Checagem de cantos arredondados
                bool inCorner = false;
                int cx = 0, cy = 0;

                if (dx < radius && dy < radius) { // Top-Left
                    cx = radius - dx; cy = radius - dy; inCorner = true;
                } else if (dx >= w - radius && dy < radius) { // Top-Right
                    cx = dx - (w - 1 - radius); cy = radius - dy; inCorner = true;
                } else if (dx < radius && dy >= h - radius) { // Bottom-Left
                    cx = radius - dx; cy = dy - (h - 1 - radius); inCorner = true;
                } else if (dx >= w - radius && dy >= h - radius) { // Bottom-Right
                    cx = dx - (w - 1 - radius); cy = dy - (h - 1 - radius); inCorner = true;
                }

                if (inCorner && (cx * cx + cy * cy > r2)) {
                    continue;
                }

                // Coordenada U na textura de origem
                int srcX = (dx * tw) / w;
                if (srcX >= tw) srcX = tw - 1;

                u32 pixelColor = tex.pixels[srcY * tw + srcX];
                m_display.drawPixel(px, py, pixelColor);
            }
        }

        // Borda sutil de 1px ao redor do ícone
        m_display.drawRoundedRectOutline(x, y, w, h, radius, Gfx::Colors::CARD_OUTLINE, 1);
    }

    void UIManager::renderHeader(bool isOnline, const std::string& version) {
        int sw = m_display.getWidth();
        float sc = m_display.getScale();

        int headerH = static_cast<int>(70 * sc);
        m_display.fillRect(0, 0, sw, headerH, Gfx::Colors::HEADER_BG);
        m_display.fillRect(0, headerH - 1, sw, 1, Gfx::Colors::CARD_OUTLINE);

        // Acento Joy-Con Ciano
        m_display.fillRoundedRect(static_cast<int>(24 * sc), static_cast<int>(20 * sc),
                                 static_cast<int>(5 * sc), static_cast<int>(30 * sc),
                                 static_cast<int>(3 * sc), Gfx::Colors::JOYCON_CYAN);

        m_display.drawText(static_cast<int>(38 * sc), static_cast<int>(18 * sc),
                          "Google Drive Sync", Gfx::Colors::TEXT_WHITE, static_cast<int>(20 * sc));

        std::string subText = version + "  •  Nintendo Switch Cloud Backup";
        m_display.drawText(static_cast<int>(40 * sc), static_cast<int>(42 * sc),
                          subText, Gfx::Colors::TEXT_MUTED, static_cast<int>(13 * sc));

        // Indicador de Status de Rede (Direita)
        int netX = sw - static_cast<int>(240 * sc);
        m_display.fillCircle(netX, static_cast<int>(35 * sc), static_cast<int>(5 * sc),
                            isOnline ? Gfx::Colors::STATUS_GREEN : Gfx::Colors::STATUS_RED);

        m_display.drawText(netX + static_cast<int>(12 * sc), static_cast<int>(27 * sc),
                          isOnline ? "Online" : "Offline",
                          isOnline ? Gfx::Colors::TEXT_WHITE : Gfx::Colors::TEXT_MUTED,
                          static_cast<int>(15 * sc));

        // Indicador de Resolução
        int modeX = sw - static_cast<int>(130 * sc);
        m_display.fillRoundedRect(modeX, static_cast<int>(24 * sc), static_cast<int>(95 * sc),
                                 static_cast<int>(24 * sc), static_cast<int>(6 * sc), Gfx::Colors::CARD_ALT_BG);
        m_display.drawRoundedRectOutline(modeX, static_cast<int>(24 * sc), static_cast<int>(95 * sc),
                                        static_cast<int>(24 * sc), static_cast<int>(6 * sc), Gfx::Colors::CARD_OUTLINE, 1);

        m_display.drawText(modeX + static_cast<int>(10 * sc), static_cast<int>(28 * sc),
                          m_display.isDocked() ? "1080p Dock" : "720p Portatil",
                          Gfx::Colors::JOYCON_CYAN, static_cast<int>(11 * sc));
    }

    void UIManager::renderFooter(const std::vector<std::pair<std::string, std::string>>& buttons) {
        int sw = m_display.getWidth(), sh = m_display.getHeight();
        float sc = m_display.getScale();

        int footerH = static_cast<int>(60 * sc);
        int footerY = sh - footerH;
        m_display.fillRect(0, footerY, sw, footerH, Gfx::Colors::FOOTER_BG);
        m_display.fillRect(0, footerY, sw, 1, Gfx::Colors::CARD_OUTLINE);

        int bx = static_cast<int>(30 * sc);
        int by = footerY + static_cast<int>(18 * sc);
        int fBadgeR = static_cast<int>(11 * sc);
        int fTextH = static_cast<int>(15 * sc);

        for (const auto& btn : buttons) {
            const std::string& key = btn.first;
            const std::string& label = btn.second;

            if (key == "A") {
                m_display.drawControllerButtonBadge(bx, by, "A", Gfx::Colors::JOYCON_RED, fBadgeR);
            } else if (key == "B") {
                m_display.drawControllerButtonBadge(bx, by, "B", Gfx::Colors::JOYCON_GRAY, fBadgeR);
            } else if (key == "X") {
                m_display.drawControllerButtonBadge(bx, by, "X", Gfx::Colors::JOYCON_BLUE, fBadgeR);
            } else if (key == "Y") {
                m_display.drawControllerButtonBadge(bx, by, "Y", Gfx::Colors::JOYCON_YELLOW, fBadgeR);
            } else if (key == "+" || key == "-") {
                m_display.drawPillButtonBadge(bx, by + 1, key.c_str(), static_cast<int>(22 * sc), static_cast<int>(18 * sc));
            }

            int tx = bx + static_cast<int>(28 * sc);
            m_display.drawText(tx, by + 2, label, Gfx::Colors::TEXT_WHITE, fTextH);

            float tw = m_display.measureText(label, fTextH);
            bx += static_cast<int>(tw) + static_cast<int>(56 * sc);
        }
    }

    void UIManager::renderToast(const std::string& message, u32 color) {
        if (message.empty()) return;
        int sw = m_display.getWidth(), sh = m_display.getHeight();
        float sc = m_display.getScale();

        int toastW = static_cast<int>(580 * sc), toastH = static_cast<int>(42 * sc);
        int toastX = (sw - toastW) / 2;
        int toastY = sh - static_cast<int>(115 * sc);

        m_display.fillRoundedRect(toastX, toastY, toastW, toastH, static_cast<int>(8 * sc), Gfx::Colors::CARD_BG);
        m_display.drawRoundedRectOutline(toastX, toastY, toastW, toastH, static_cast<int>(8 * sc), color, 1);
        m_display.fillCircle(toastX + static_cast<int>(24 * sc), toastY + toastH / 2, static_cast<int>(5 * sc), color);
        m_display.drawText(toastX + static_cast<int>(40 * sc), toastY + static_cast<int>(12 * sc), message, Gfx::Colors::TEXT_WHITE, static_cast<int>(15 * sc));
    }

    // ==========================================================================
    // TELA PRINCIPAL (2 COLUNAS)
    // ==========================================================================
    void UIManager::renderMainScreen(const Core::DriveSyncCore& core, int selectedIndex, size_t totalGames) {
        m_display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);
        renderHeader(core.isNetworkConnected());

        int sw = m_display.getWidth();
        float sc = m_display.getScale();

        // 1. Coluna Esquerda: Status Real (Largura ~380px em 720p)
        int leftX = static_cast<int>(30 * sc);
        int leftW = static_cast<int>(380 * sc);
        int leftY = static_cast<int>(90 * sc);
        int leftH = static_cast<int>(540 * sc);

        m_display.fillRoundedRect(leftX, leftY, leftW, leftH, static_cast<int>(14 * sc), Gfx::Colors::CARD_BG);
        m_display.drawRoundedRectOutline(leftX, leftY, leftW, leftH, static_cast<int>(14 * sc), Gfx::Colors::CARD_OUTLINE, 1);

        int padX = leftX + static_cast<int>(26 * sc);
        int rowY = leftY + static_cast<int>(24 * sc);

        m_display.drawText(padX, rowY, "STATUS DO SISTEMA", Gfx::Colors::TEXT_MUTED, static_cast<int>(13 * sc));
        rowY += static_cast<int>(26 * sc);
        m_display.fillRect(leftX + static_cast<int>(20 * sc), rowY, leftW - static_cast<int>(40 * sc), 1, Gfx::Colors::CARD_OUTLINE);

        // Perfil Local
        rowY += static_cast<int>(22 * sc);
        m_display.drawText(padX, rowY, "Perfil Local:", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
        rowY += static_cast<int>(20 * sc);
        m_display.drawText(padX, rowY, core.getProfileNickname(), Gfx::Colors::TEXT_WHITE, static_cast<int>(16 * sc));

        // Conta Google
        rowY += static_cast<int>(36 * sc);
        m_display.drawText(padX, rowY, "Conta Google Drive:", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
        rowY += static_cast<int>(20 * sc);
        if (core.isAuthorized()) {
            m_display.drawText(padX, rowY, "Conectada (OAuth 2.0)", Gfx::Colors::STATUS_GREEN, static_cast<int>(16 * sc));
        } else {
            m_display.drawText(padX, rowY, "Nao Vinculada", Gfx::Colors::STATUS_YELLOW, static_cast<int>(16 * sc));
        }

        // Pasta de Destino
        rowY += static_cast<int>(36 * sc);
        m_display.drawText(padX, rowY, "Pasta de Destino:", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
        rowY += static_cast<int>(20 * sc);
        m_display.drawText(padX, rowY, "/" + core.getFolderName(), Gfx::Colors::JOYCON_CYAN, static_cast<int>(16 * sc));

        // Último Backup
        rowY += static_cast<int>(36 * sc);
        m_display.drawText(padX, rowY, "Ultimo Backup:", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
        rowY += static_cast<int>(20 * sc);
        std::time_t lastTs = core.getLastBackupTimestamp();
        std::string tsStr = (lastTs > 0) ? "Hoje, salvo recente" : "Nenhum registro";
        m_display.drawText(padX, rowY, tsStr, Gfx::Colors::TEXT_PRIMARY, static_cast<int>(15 * sc));

        // Jogos Detectados
        rowY += static_cast<int>(36 * sc);
        m_display.drawText(padX, rowY, "Jogos com Save:", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
        rowY += static_cast<int>(20 * sc);
        char gamesBuf[64];
        snprintf(gamesBuf, sizeof(gamesBuf), "%zu titulos detectados", totalGames);
        m_display.drawText(padX, rowY, gamesBuf, Gfx::Colors::TEXT_WHITE, static_cast<int>(15 * sc));

        // Integridade
        rowY += static_cast<int>(36 * sc);
        m_display.drawText(padX, rowY, "Integridade:", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
        rowY += static_cast<int>(20 * sc);
        m_display.drawText(padX, rowY, "Checksum CRC32 & RTC", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));

        int hintY = leftY + leftH - static_cast<int>(38 * sc);
        m_display.drawText(padX, hintY, "[-] Configuracoes avancadas", Gfx::Colors::TEXT_MUTED, static_cast<int>(12 * sc));

        // 2. Coluna Direita: 4 Ações Principais
        int rightX = leftX + leftW + static_cast<int>(24 * sc);
        int rightW = sw - rightX - static_cast<int>(30 * sc);
        int cardH = static_cast<int>(118 * sc);
        int cardGap = static_cast<int>(18 * sc);
        int startY = leftY + static_cast<int>(6 * sc);

        struct ActionDef {
            const char* title;
            const char* sub;
            u32 color;
            int iconType; // 0=upload, 1=download, 2=gamepad, 3=google
        };

        const ActionDef actions[4] = {
            { "Backup Completo",    "Envia todos os saves do perfil ativo para o Google Drive", Gfx::Colors::JOYCON_CYAN, 0 },
            { "Restaurar da Nuvem", "Atualiza saves locais que estejam mais recentes na nuvem", Gfx::Colors::STATUS_GREEN, 1 },
            { "Gerenciar por Jogo", "Visualizar historico, selecionar titulos e definir favoritos", Gfx::Colors::JOYCON_BLUE, 2 },
            { "Vincular Conta Google", "Autenticar via codigo na tela em google.com/device", Gfx::Colors::DRIVE_YELLOW, 3 }
        };

        for (int i = 0; i < 4; i++) {
            int cy = startY + i * (cardH + cardGap);
            bool isSel = (i == selectedIndex);
            const auto& act = actions[i];

            if (isSel) {
                m_display.fillRoundedRect(rightX, cy, rightW, cardH, static_cast<int>(12 * sc), Gfx::Colors::CARD_HOVER_BG);
                m_display.drawRoundedRectOutline(rightX, cy, rightW, cardH, static_cast<int>(12 * sc), Gfx::Colors::CARD_HOVER_OUT, 2);
                m_display.fillRoundedRect(rightX + static_cast<int>(6 * sc), cy + static_cast<int>(10 * sc),
                                         static_cast<int>(4 * sc), cardH - static_cast<int>(20 * sc),
                                         static_cast<int>(2 * sc), Gfx::Colors::JOYCON_CYAN);
            } else {
                m_display.fillRoundedRect(rightX, cy, rightW, cardH, static_cast<int>(12 * sc), Gfx::Colors::CARD_BG);
                m_display.drawRoundedRectOutline(rightX, cy, rightW, cardH, static_cast<int>(12 * sc), Gfx::Colors::CARD_OUTLINE, 1);
            }

            int icx = rightX + static_cast<int>(48 * sc);
            int icy = cy + cardH / 2;
            int ir = static_cast<int>(24 * sc);
            m_display.fillCircle(icx, icy, ir, isSel ? Gfx::Colors::HEADER_BG : Gfx::Colors::CARD_ALT_BG);
            m_display.drawRoundedRectOutline(icx - ir, icy - ir, ir * 2, ir * 2, ir, act.color, 1);

            if (act.iconType == 0) m_display.drawCloudUploadIcon(icx, icy, static_cast<int>(26 * sc), act.color);
            else if (act.iconType == 1) m_display.drawCloudDownloadIcon(icx, icy, static_cast<int>(26 * sc), act.color);
            else if (act.iconType == 2) m_display.drawGamepadIcon(icx, icy, static_cast<int>(26 * sc), act.color);
            else if (act.iconType == 3) m_display.drawGoogleDriveIcon(icx, icy, static_cast<int>(24 * sc));

            int textX = rightX + static_cast<int>(92 * sc);
            m_display.drawText(textX, cy + static_cast<int>(28 * sc), act.title, Gfx::Colors::TEXT_WHITE, static_cast<int>(19 * sc));
            m_display.drawText(textX, cy + static_cast<int>(62 * sc), act.sub, isSel ? Gfx::Colors::JOYCON_CYAN : Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));

            if (isSel) {
                m_display.drawText(rightX + rightW - static_cast<int>(34 * sc), cy + static_cast<int>(44 * sc), ">", Gfx::Colors::JOYCON_CYAN, static_cast<int>(20 * sc));
            }
        }

        renderFooter({
            {"A", "Confirmar"},
            {"B", "Sair"},
            {"-", "Configuracoes"},
            {"+", "Fechar App"}
        });
    }

    // ==========================================================================
    // TELA DE CONFIGURACOES
    // ==========================================================================
    void UIManager::renderSettingsScreen(const Core::DriveSyncCore& core, int selectedIndex) {
        m_display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);
        renderHeader(core.isNetworkConnected());

        int sw = m_display.getWidth();
        float sc = m_display.getScale();

        int cx = static_cast<int>(60 * sc);
        int cw = sw - static_cast<int>(120 * sc);
        int sy = static_cast<int>(100 * sc);

        m_display.drawText(cx, sy, "CONFIGURACOES DO SISTEMA", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
        sy += static_cast<int>(28 * sc);

        struct SettingDef {
            const char* title;
            const char* desc;
            std::string val;
        };

        const SettingDef items[4] = {
            { "Pasta de Destino no Google Drive", "Diretorio raiz onde os backups sao armazenados no Drive", core.getFolderName() },
            { "Google OAuth Client ID",           "Identificador da aplicacao registrado no Google Cloud Console", core.getClientId().empty() ? "[Padrao]" : "[Configurado]" },
            { "Google OAuth Client Secret",       "Chave de autenticacao privada do OAuth 2.0", core.getClientSecret().empty() ? "[Padrao]" : "[••••••••••••]" },
            { "Testar Conexao com o Drive",       "Valida certificados SSL e permissoes de escrita na pasta", "Pressione A para testar" }
        };

        int itemH = static_cast<int>(105 * sc);
        int itemGap = static_cast<int>(14 * sc);

        for (int i = 0; i < 4; i++) {
            int iy = sy + i * (itemH + itemGap);
            bool isSel = (i == selectedIndex);
            const auto& item = items[i];

            if (isSel) {
                m_display.fillRoundedRect(cx, iy, cw, itemH, static_cast<int>(12 * sc), Gfx::Colors::CARD_HOVER_BG);
                m_display.drawRoundedRectOutline(cx, iy, cw, itemH, static_cast<int>(12 * sc), Gfx::Colors::CARD_HOVER_OUT, 2);
                m_display.fillRoundedRect(cx + static_cast<int>(6 * sc), iy + static_cast<int>(10 * sc),
                                         static_cast<int>(4 * sc), itemH - static_cast<int>(20 * sc),
                                         static_cast<int>(2 * sc), Gfx::Colors::JOYCON_CYAN);
            } else {
                m_display.fillRoundedRect(cx, iy, cw, itemH, static_cast<int>(12 * sc), Gfx::Colors::CARD_BG);
                m_display.drawRoundedRectOutline(cx, iy, cw, itemH, static_cast<int>(12 * sc), Gfx::Colors::CARD_OUTLINE, 1);
            }

            int tx = cx + static_cast<int>(32 * sc);
            m_display.drawText(tx, iy + static_cast<int>(20 * sc), item.title, Gfx::Colors::TEXT_WHITE, static_cast<int>(18 * sc));
            m_display.drawText(tx, iy + static_cast<int>(48 * sc), item.desc, Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));
            m_display.drawText(tx, iy + static_cast<int>(74 * sc), "Valor atual: " + item.val, Gfx::Colors::JOYCON_CYAN, static_cast<int>(13 * sc));
        }

        renderFooter({
            {"A", "Editar / Executar"},
            {"B", "Voltar"}
        });
    }

    // ==========================================================================
    // TELA DE SELECAO POR JOGO (COM ICONES OFICIAIS DA HOME DO SWITCH)
    // ==========================================================================
    void UIManager::renderManageGamesScreen(const std::vector<Assets::GameEntry>& games,
                                           int selectedIndex, int scrollOffset,
                                           Assets::TitleManager& titleManager) {
        m_display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);
        renderHeader(true);

        int sw = m_display.getWidth();
        float sc = m_display.getScale();

        int lx = static_cast<int>(40 * sc);
        int lw = sw - static_cast<int>(80 * sc);
        int ly = static_cast<int>(90 * sc);
        int lh = static_cast<int>(550 * sc);

        m_display.fillRoundedRect(lx, ly, lw, lh, static_cast<int>(14 * sc), Gfx::Colors::CARD_BG);
        m_display.drawRoundedRectOutline(lx, ly, lw, lh, static_cast<int>(14 * sc), Gfx::Colors::CARD_OUTLINE, 1);

        int itemY = ly + static_cast<int>(16 * sc);
        int itemH = static_cast<int>(74 * sc);
        int cardW = lw - static_cast<int>(32 * sc);
        int cardX = lx + static_cast<int>(16 * sc);

        int totalGames = static_cast<int>(games.size());
        constexpr int GAMES_VISIBLE = 6;

        for (int i = 0; i < GAMES_VISIBLE; i++) {
            int dataIdx = scrollOffset + i;
            if (dataIdx >= totalGames) break;

            const auto& game = games[dataIdx];
            bool isFocused = (dataIdx == selectedIndex);

            if (isFocused) {
                m_display.fillRoundedRect(cardX, itemY, cardW, itemH, static_cast<int>(10 * sc), Gfx::Colors::CARD_HOVER_BG);
                m_display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, static_cast<int>(10 * sc), Gfx::Colors::CARD_HOVER_OUT, 2);
                m_display.fillRoundedRect(cardX + static_cast<int>(4 * sc), itemY + static_cast<int>(8 * sc),
                                         static_cast<int>(4 * sc), itemH - static_cast<int>(16 * sc),
                                         static_cast<int>(2 * sc), Gfx::Colors::JOYCON_CYAN);
            } else {
                m_display.fillRoundedRect(cardX, itemY, cardW, itemH, static_cast<int>(10 * sc), Gfx::Colors::CARD_ALT_BG);
                m_display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, static_cast<int>(10 * sc), Gfx::Colors::CARD_OUTLINE, 1);
            }

            // Checkbox
            int chkX = cardX + static_cast<int>(16 * sc);
            int chkY = itemY + static_cast<int>(22 * sc);
            int chkW = static_cast<int>(30 * sc);
            int chkH = static_cast<int>(30 * sc);

            if (game.selected) {
                m_display.fillRoundedRect(chkX, chkY, chkW, chkH, static_cast<int>(6 * sc), Gfx::Colors::STATUS_GREEN);
                m_display.drawText(chkX + static_cast<int>(8 * sc), chkY + static_cast<int>(6 * sc), "X", Gfx::Colors::TEXT_WHITE, static_cast<int>(18 * sc));
            } else {
                m_display.fillRoundedRect(chkX, chkY, chkW, chkH, static_cast<int>(6 * sc), Gfx::Colors::CARD_BG);
                m_display.drawRoundedRectOutline(chkX, chkY, chkW, chkH, static_cast<int>(6 * sc), Gfx::Colors::TEXT_MUTED, 2);
            }

            // ÍCONE OFICIAL DO JOGO (JPEG decodificado e cacheado em miniatura com cantos arredondados)
            int iconSize = static_cast<int>(56 * sc);
            int iconX = chkX + chkW + static_cast<int>(16 * sc);
            int iconY = itemY + (itemH - iconSize) / 2;

            const Assets::TextureHandle* iconTex = titleManager.getTitleIcon(game.titleId, game.titleName);
            if (iconTex && iconTex->valid) {
                drawTextureRounded(iconX, iconY, iconSize, iconSize, *iconTex, static_cast<int>(8 * sc));
            } else {
                // Fallback elegante
                m_display.fillRoundedRect(iconX, iconY, iconSize, iconSize, static_cast<int>(8 * sc), Gfx::Colors::CARD_BG);
                m_display.drawGamepadIcon(iconX + iconSize / 2, iconY + iconSize / 2, static_cast<int>(26 * sc), Gfx::Colors::JOYCON_CYAN);
            }

            // Nome e Metadados do Jogo
            int tx = iconX + iconSize + static_cast<int>(18 * sc);
            m_display.drawText(tx, itemY + static_cast<int>(14 * sc), game.titleName, Gfx::Colors::TEXT_WHITE, static_cast<int>(18 * sc));

            char infoBuf[128];
            float szMb = static_cast<float>(game.saveSize) / (1024.0f * 1024.0f);
            snprintf(infoBuf, sizeof(infoBuf), "Title ID: 0x%016llX   •   Tamanho: %.2f MB",
                     (unsigned long long)game.titleId, szMb);
            m_display.drawText(tx, itemY + static_cast<int>(42 * sc), infoBuf,
                              isFocused ? Gfx::Colors::JOYCON_CYAN : Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));

            // Badge de Favorito
            if (game.isFavorite) {
                int favX = cardX + cardW - static_cast<int>(120 * sc);
                m_display.fillRoundedRect(favX, itemY + static_cast<int>(24 * sc),
                                         static_cast<int>(96 * sc), static_cast<int>(26 * sc),
                                         static_cast<int>(6 * sc), Gfx::Colors::CARD_BG);
                m_display.drawRoundedRectOutline(favX, itemY + static_cast<int>(24 * sc),
                                                static_cast<int>(96 * sc), static_cast<int>(26 * sc),
                                                static_cast<int>(6 * sc), Gfx::Colors::JOYCON_YELLOW, 1);
                m_display.drawText(favX + static_cast<int>(14 * sc), itemY + static_cast<int>(28 * sc),
                                  "FAVORITO", Gfx::Colors::JOYCON_YELLOW, static_cast<int>(12 * sc));
            }

            itemY += itemH + static_cast<int>(10 * sc);
        }

        renderFooter({
            {"A", "Alternar"},
            {"Y", "Todos/Nenhum"},
            {"X", "Salvar Favoritos"},
            {"+", "Iniciar Backup"},
            {"B", "Voltar"}
        });
    }

    // ==========================================================================
    // MODAIS
    // ==========================================================================
    void UIManager::renderDeviceAuthModal(const std::string& verifyUrl, const std::string& userCode, int animTick) {
        int sw = m_display.getWidth(), sh = m_display.getHeight();
        float sc = m_display.getScale();

        m_display.fillRect(0, 0, sw, sh, Gfx::RGBA(0, 0, 0, 160));

        int mw = static_cast<int>(740 * sc), mh = static_cast<int>(460 * sc);
        int mx = (sw - mw) / 2, my = (sh - mh) / 2;

        m_display.fillRoundedRect(mx, my, mw, mh, static_cast<int>(16 * sc), Gfx::Colors::CARD_BG);
        m_display.drawRoundedRectOutline(mx, my, mw, mh, static_cast<int>(16 * sc), Gfx::Colors::JOYCON_CYAN, 2);

        m_display.drawGoogleDriveIcon(mx + static_cast<int>(45 * sc), my + static_cast<int>(42 * sc), static_cast<int>(32 * sc));
        m_display.drawText(mx + static_cast<int>(85 * sc), my + static_cast<int>(28 * sc), "Vincular Conta Google", Gfx::Colors::TEXT_WHITE, static_cast<int>(22 * sc));
        m_display.drawText(mx + static_cast<int>(85 * sc), my + static_cast<int>(56 * sc), "Autorizacao de Dispositivo (OAuth 2.0)", Gfx::Colors::TEXT_MUTED, static_cast<int>(14 * sc));

        m_display.fillRect(mx + static_cast<int>(30 * sc), my + static_cast<int>(84 * sc), mw - static_cast<int>(60 * sc), 1, Gfx::Colors::CARD_OUTLINE);

        int ty = my + static_cast<int>(108 * sc);
        m_display.drawText(mx + static_cast<int>(45 * sc), ty, "1. Acesse o endereco abaixo no seu celular ou computador:", Gfx::Colors::TEXT_WHITE, static_cast<int>(15 * sc));
        ty += static_cast<int>(28 * sc);
        m_display.drawText(mx + static_cast<int>(65 * sc), ty, verifyUrl.empty() ? "https://www.google.com/device" : verifyUrl, Gfx::Colors::JOYCON_CYAN, static_cast<int>(18 * sc));

        ty += static_cast<int>(42 * sc);
        m_display.drawText(mx + static_cast<int>(45 * sc), ty, "2. Digite o codigo de ativacao exibido:", Gfx::Colors::TEXT_WHITE, static_cast<int>(15 * sc));

        ty += static_cast<int>(26 * sc);
        int codeBoxW = static_cast<int>(420 * sc), codeBoxH = static_cast<int>(68 * sc);
        int codeBoxX = mx + (mw - codeBoxW) / 2;
        m_display.fillRoundedRect(codeBoxX, ty, codeBoxW, codeBoxH, static_cast<int>(10 * sc), Gfx::Colors::CARD_ALT_BG);
        m_display.drawRoundedRectOutline(codeBoxX, ty, codeBoxW, codeBoxH, static_cast<int>(10 * sc), Gfx::Colors::DRIVE_YELLOW, 2);

        float tw = m_display.measureText(userCode, static_cast<int>(28 * sc));
        int cx = codeBoxX + static_cast<int>((codeBoxW - tw) / 2.0f);
        m_display.drawText(cx, ty + static_cast<int>(18 * sc), userCode, Gfx::Colors::DRIVE_YELLOW, static_cast<int>(28 * sc));

        ty += static_cast<int>(92 * sc);
        m_display.drawText(mx + static_cast<int>(45 * sc), ty, "3. Faca login e confirme as permissoes de acesso.", Gfx::Colors::TEXT_WHITE, static_cast<int>(15 * sc));

        ty += static_cast<int>(38 * sc);
        int barW = mw - static_cast<int>(90 * sc), barH = static_cast<int>(8 * sc);
        int barX = mx + static_cast<int>(45 * sc);
        m_display.fillRoundedRect(barX, ty, barW, barH, static_cast<int>(4 * sc), Gfx::Colors::CARD_OUTLINE);
        int pulseW = (animTick % (barW - 40)) + 40;
        m_display.fillRoundedRect(barX, ty, pulseW, barH, static_cast<int>(4 * sc), Gfx::Colors::STATUS_GREEN);

        m_display.drawText(mx + static_cast<int>(45 * sc), ty + static_cast<int>(18 * sc), "Aguardando confirmacao do navegador...", Gfx::Colors::TEXT_MUTED, static_cast<int>(13 * sc));

        int btnY = my + mh - static_cast<int>(48 * sc);
        m_display.drawControllerButtonBadge(mx + mw - static_cast<int>(150 * sc), btnY, "B", Gfx::Colors::JOYCON_GRAY, static_cast<int>(11 * sc));
        m_display.drawText(mx + mw - static_cast<int>(120 * sc), btnY + 2, "Cancelar", Gfx::Colors::TEXT_WHITE, static_cast<int>(14 * sc));
    }

    void UIManager::renderProgressModal(const std::string& gameName, const std::string& stage, float progress) {
        int sw = m_display.getWidth(), sh = m_display.getHeight();
        float sc = m_display.getScale();

        m_display.fillRect(0, 0, sw, sh, Gfx::RGBA(0, 0, 0, 160));

        int mw = static_cast<int>(760 * sc), mh = static_cast<int>(380 * sc);
        int mx = (sw - mw) / 2, my = (sh - mh) / 2;

        m_display.fillRoundedRect(mx, my, mw, mh, static_cast<int>(14 * sc), Gfx::Colors::CARD_BG);
        m_display.drawRoundedRectOutline(mx, my, mw, mh, static_cast<int>(14 * sc), Gfx::Colors::JOYCON_CYAN, 2);

        m_display.drawCloudUploadIcon(mx + static_cast<int>(45 * sc), my + static_cast<int>(42 * sc), static_cast<int>(32 * sc), Gfx::Colors::JOYCON_CYAN);
        m_display.drawText(mx + static_cast<int>(85 * sc), my + static_cast<int>(30 * sc), "Sincronizando Saves", Gfx::Colors::TEXT_WHITE, static_cast<int>(22 * sc));
        m_display.fillRect(mx + static_cast<int>(30 * sc), my + static_cast<int>(74 * sc), mw - static_cast<int>(60 * sc), 1, Gfx::Colors::CARD_OUTLINE);

        int ty = my + static_cast<int>(105 * sc);
        m_display.drawText(mx + static_cast<int>(40 * sc), ty, gameName, Gfx::Colors::JOYCON_CYAN, static_cast<int>(18 * sc));

        ty += static_cast<int>(36 * sc);
        m_display.drawText(mx + static_cast<int>(40 * sc), ty, stage, Gfx::Colors::TEXT_WHITE, static_cast<int>(15 * sc));

        ty += static_cast<int>(34 * sc);
        int barW = mw - static_cast<int>(80 * sc), barH = static_cast<int>(14 * sc);
        int barX = mx + static_cast<int>(40 * sc);
        m_display.fillRoundedRect(barX, ty, barW, barH, static_cast<int>(7 * sc), Gfx::Colors::CARD_OUTLINE);
        int fillW = std::clamp(static_cast<int>(barW * progress), static_cast<int>(14 * sc), barW);
        m_display.fillRoundedRect(barX, ty, fillW, barH, static_cast<int>(7 * sc), Gfx::Colors::STATUS_GREEN);

        m_display.drawText(mx + static_cast<int>(40 * sc), my + mh - static_cast<int>(40 * sc), "Por favor, aguarde e nao feche a aplicacao.", Gfx::Colors::TEXT_MUTED, static_cast<int>(13 * sc));
    }

    void UIManager::renderSummaryModal(const std::string& title, int successCount, int failCount, const std::vector<std::string>& details) {
        int sw = m_display.getWidth(), sh = m_display.getHeight();
        float sc = m_display.getScale();

        m_display.fillRect(0, 0, sw, sh, Gfx::RGBA(0, 0, 0, 160));

        int mw = static_cast<int>(720 * sc), mh = static_cast<int>(380 * sc);
        int mx = (sw - mw) / 2, my = (sh - mh) / 2;

        m_display.fillRoundedRect(mx, my, mw, mh, static_cast<int>(14 * sc), Gfx::Colors::CARD_BG);
        u32 outlineColor = (failCount == 0) ? Gfx::Colors::STATUS_GREEN : Gfx::Colors::STATUS_YELLOW;
        m_display.drawRoundedRectOutline(mx, my, mw, mh, static_cast<int>(14 * sc), outlineColor, 2);

        m_display.drawText(mx + static_cast<int>(36 * sc), my + static_cast<int>(32 * sc), title, Gfx::Colors::TEXT_WHITE, static_cast<int>(22 * sc));
        m_display.fillRect(mx + static_cast<int>(36 * sc), my + static_cast<int>(68 * sc), mw - static_cast<int>(72 * sc), 1, Gfx::Colors::CARD_OUTLINE);

        int ty = my + static_cast<int>(96 * sc);
        int bodyH = static_cast<int>(16 * sc);

        std::string succText = "Arquivos processados com sucesso: " + std::to_string(successCount);
        m_display.drawText(mx + static_cast<int>(40 * sc), ty, succText, Gfx::Colors::STATUS_GREEN, bodyH);

        ty += static_cast<int>(32 * sc);
        std::string failText = "Falhas ou erros de envio: " + std::to_string(failCount);
        m_display.drawText(mx + static_cast<int>(40 * sc), ty, failText, (failCount > 0) ? Gfx::Colors::STATUS_RED : Gfx::Colors::TEXT_MUTED, bodyH);

        if (failCount > 0 && !details.empty()) {
            ty += static_cast<int>(32 * sc);
            m_display.drawText(mx + static_cast<int>(40 * sc), ty, "Detalhes das falhas:", Gfx::Colors::JOYCON_YELLOW, bodyH);
            ty += static_cast<int>(24 * sc);
            for (size_t i = 0; i < details.size() && i < 3; ++i) {
                m_display.drawText(mx + static_cast<int>(56 * sc), ty, "- " + details[i], Gfx::Colors::TEXT_WHITE, static_cast<int>(14 * sc));
                ty += static_cast<int>(22 * sc);
            }
        } else {
            ty += static_cast<int>(36 * sc);
            m_display.drawText(mx + static_cast<int>(40 * sc), ty, "Todos os arquivos estao seguros e sincronizados no Google Drive.", Gfx::Colors::TEXT_MUTED, bodyH);
        }

        int btnY = my + mh - static_cast<int>(52 * sc);
        m_display.drawControllerButtonBadge(mx + mw - static_cast<int>(160 * sc), btnY, "A", Gfx::Colors::JOYCON_RED, static_cast<int>(11 * sc));
        m_display.drawText(mx + mw - static_cast<int>(130 * sc), btnY + 2, "Continuar", Gfx::Colors::TEXT_WHITE, static_cast<int>(15 * sc));
    }

} // namespace UI

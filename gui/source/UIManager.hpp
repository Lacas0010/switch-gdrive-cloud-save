#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include "Renderer2D.hpp"
#include "TitleManager.hpp"
#include "DriveSyncCore.hpp"
#include "UIState.hpp"

namespace UI {

    class UIManager {
    private:
        Gfx::Display m_display;

    public:
        UIManager();
        ~UIManager();

        bool init();
        void close();

        Gfx::Display& getDisplay() { return m_display; }

        bool beginFrame();
        void endFrame();

        int getWidth() const { return m_display.getWidth(); }
        int getHeight() const { return m_display.getHeight(); }
        float getScale() const { return m_display.getScale(); }
        bool isDocked() const { return m_display.isDocked(); }

        // Renderizador de textura com cantos arredondados (para miniaturas oficiais dos jogos)
        void drawTextureRounded(int x, int y, int w, int h, const Assets::TextureHandle& tex, int radius);

        // Componentes Compartilhados
        void renderHeader(bool isOnline, const std::string& version = "v0.1.0");
        void renderFooter(const std::vector<std::pair<std::string, std::string>>& buttons);
        void renderToast(const std::string& message, u32 color);

        // Telas Completas
        void renderMainScreen(const Core::DriveSyncCore& core, int selectedIndex, size_t totalGames);
        void renderSettingsScreen(const Core::DriveSyncCore& core, int selectedIndex);
        void renderManageGamesScreen(const std::vector<Assets::GameEntry>& games,
                                    int selectedIndex, int scrollOffset,
                                    Assets::TitleManager& titleManager);

        // Modais
        void renderDeviceAuthModal(const std::string& verifyUrl, const std::string& userCode, int animTick);
        void renderProgressModal(const std::string& gameName, const std::string& stage, float progress);
        void renderSummaryModal(const std::string& title, int successCount, int failCount, const std::vector<std::string>& details);
    };

} // namespace UI

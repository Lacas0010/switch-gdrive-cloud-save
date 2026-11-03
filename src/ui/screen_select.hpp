#pragma once
#include "renderer2d.hpp"
#include "texture_cache.hpp"
#include "../storage/save_manager.hpp"
#include "../network/drive_client.hpp"
#include <vector>

namespace UI {

    class ScreenSelect {
    public:
        ScreenSelect(
            Display& display,
            TextureCache& textures,
            Storage::SaveManager& saveManager,
            Network::DriveClient& client
        );

        void refreshGames();
        void handleInput(u64 kDown);
        void render();

        u64 getSelectedTitleId() const noexcept;

    private:
        Display& m_display;
        TextureCache& m_textures;
        Storage::SaveManager& m_saveManager;
        Network::DriveClient& m_client;

        std::vector<Storage::GameSaveInfo> m_games;
        int m_selectedIndex;
        int m_scrollOffset;
    };

} // namespace UI

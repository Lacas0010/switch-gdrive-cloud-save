#pragma once
#include "renderer2d.hpp"
#include "../core/sync_engine.hpp"
#include "../network/drive_client.hpp"
#include "../storage/save_manager.hpp"

namespace UI {

    class ScreenMain {
    public:
        ScreenMain(
            Display& display,
            Core::SyncEngine& engine,
            Network::DriveClient& client,
            Storage::SaveManager& saveManager
        );

        void handleInput(u64 kDown);
        void render();

        int getSelectedIndex() const noexcept { return m_selectedCard; }

    private:
        void renderHeader();
        void renderLeftStatus();
        void renderRightCards();
        void renderFooter();

        Display& m_display;
        Core::SyncEngine& m_engine;
        Network::DriveClient& m_client;
        Storage::SaveManager& m_saveManager;

        int m_selectedCard;
    };

} // namespace UI

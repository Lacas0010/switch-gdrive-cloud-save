#pragma once
#include "renderer2d.hpp"
#include "texture_cache.hpp"
#include "screen_main.hpp"
#include "screen_select.hpp"
#include "../core/state_machine.hpp"
#include "../core/sync_engine.hpp"
#include "../network/oauth_manager.hpp"

namespace UI {

    class UIManager {
    public:
        UIManager(
            Core::StateMachine& stateMachine,
            Core::SyncEngine& syncEngine,
            Network::DriveClient& driveClient,
            Storage::SaveManager& saveManager
        );
        ~UIManager();

        bool init();
        void cleanup();

        void handleInput(u64 kDown);
        void update();
        void render();

    private:
        void renderSettingsModal();
        void renderOAuthModal();
        void renderOperationModal();
        void renderHelpModal();

        Core::StateMachine& m_stateMachine;
        Core::SyncEngine& m_engine;
        Network::DriveClient& m_client;
        Storage::SaveManager& m_saveManager;
        Network::OAuthManager m_oauth;

        Display m_display;
        TextureCache m_textures;
        ScreenMain m_screenMain;
        ScreenSelect m_screenSelect;

        int m_settingsOption;
    };

} // namespace UI

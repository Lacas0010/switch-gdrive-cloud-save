#pragma once
#include "renderer2d.hpp"
#include "texture_cache.hpp"
#include "screen_main.hpp"
#include "screen_select.hpp"
#include "screen_restore.hpp"
#include "../core/state_machine.hpp"
#include "../core/sync_engine.hpp"
#include "../network/drive_client.hpp"
#include "../network/oauth_manager.hpp"
#include "../storage/save_manager.hpp"

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
        // Retorna true caso o usuário tenha tocado no botão Sair (+)
        bool handleTouch(const TouchEvent& touch);
        void update();
        void render();

        u32 getDisplayWidth() const noexcept { return m_display.getWidth(); }
        u32 getDisplayHeight() const noexcept { return m_display.getHeight(); }

    private:
        void activateMainCard(int sel);
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
        ScreenRestore m_screenRestore;

        int m_settingsOption;
    };

} // namespace UI

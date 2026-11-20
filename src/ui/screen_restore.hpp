#pragma once
#include "renderer2d.hpp"
#include "texture_cache.hpp"
#include "../storage/save_manager.hpp"
#include "../network/drive_client.hpp"
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

namespace UI {

    struct RestoreCandidate {
        Network::DriveFileInfo driveFile;
        bool hasLocalSave = false;
        size_t localSizeBytes = 0;
        std::time_t localModifiedTime = 0;
    };

    enum class RestoreTouchAction {
        NONE,
        RESTORE_SELECTED,
        RESTORE_ALL,
        REFRESH,
        BACK
    };

    class ScreenRestore {
    public:
        ScreenRestore(
            Display& display,
            TextureCache& textures,
            Storage::SaveManager& saveManager,
            Network::DriveClient& client
        );
        ~ScreenRestore();

        void refreshAsync();
        void poll();
        void handleInput(u64 kDown);
        RestoreTouchAction handleTouch(const TouchEvent& touch);
        void render();

        bool isLoading() const noexcept { return m_loading.load(std::memory_order_relaxed); }
        bool hasSelectedCandidate() const;
        RestoreCandidate getSelectedCandidate() const;

    private:
        void fetchWorker();
        static std::string formatSize(size_t bytes);

        Display& m_display;
        TextureCache& m_textures;
        Storage::SaveManager& m_saveManager;
        Network::DriveClient& m_client;

        mutable std::mutex m_mutex;
        std::vector<RestoreCandidate> m_candidates;
        std::string m_errorMessage;
        int m_selectedIndex;
        int m_scrollOffset;

        std::thread m_fetchThread;
        std::atomic<bool> m_loading;
        std::atomic<bool> m_cancelFetch;
    };

} // namespace UI

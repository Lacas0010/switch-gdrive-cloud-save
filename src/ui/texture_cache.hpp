#pragma once
#include "../common.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace UI {

    struct IconTexture {
        int width = 0;
        int height = 0;
        std::vector<u32> pixels; // Buffer RGBA8888
        bool valid = false;
    };

    struct GameMetadata {
        u64 titleId = 0;
        std::string name;
        std::string author;
        std::string version;
        IconTexture icon;
    };

    class TextureCache {
    public:
        TextureCache();
        ~TextureCache();

        void init();
        void cleanup();

        // Obtém metadados e ícone com cache O(1) permanente
        const GameMetadata& getGameMetadata(u64 titleId);
        std::string getTitleName(u64 titleId);

    private:
        bool loadControlData(u64 titleId, GameMetadata& outMeta);

        bool m_initialized;
        std::unordered_map<u64, GameMetadata> m_cache;
        GameMetadata m_defaultMeta;
    };

} // namespace UI

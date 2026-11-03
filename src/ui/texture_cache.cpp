#include "texture_cache.hpp"
#include <cstdio>
#include <cstring>

#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace UI {

    TextureCache::TextureCache()
        : m_initialized(false) {
        m_defaultMeta.name = "Jogo Desconhecido";
        m_defaultMeta.author = "Nintendo";
        m_defaultMeta.version = "v1.0.0";
    }

    TextureCache::~TextureCache() {
        cleanup();
    }

    void TextureCache::init() {
#ifdef __SWITCH__
        if (!m_initialized) {
            Result rc = nsInitialize();
            m_initialized = R_SUCCEEDED(rc);
        }
#else
        m_initialized = true;
#endif
    }

    void TextureCache::cleanup() {
#ifdef __SWITCH__
        if (m_initialized) {
            nsExit();
            m_initialized = false;
        }
#endif
        m_cache.clear();
    }

    bool TextureCache::loadControlData(u64 titleId, GameMetadata& outMeta) {
        outMeta.titleId = titleId;
        char hexBuf[32];
        std::snprintf(hexBuf, sizeof(hexBuf), "%016llX", static_cast<unsigned long long>(titleId));
        outMeta.name = "Jogo 0x" + std::string(hexBuf);
        outMeta.author = "Nintendo Switch";
        outMeta.version = "1.0.0";

#ifdef __SWITCH__
        if (!m_initialized) init();
        if (!m_initialized) return false;

        auto controlData = std::make_unique<NsApplicationControlData>();
        std::memset(controlData.get(), 0, sizeof(NsApplicationControlData));
        u64 actualSize = 0;

        Result rc = nsGetApplicationControlData(
            NsApplicationControlSource_Storage,
            titleId,
            controlData.get(),
            sizeof(NsApplicationControlData),
            &actualSize
        );

        if (R_SUCCEEDED(rc)) {
            NacpLanguageEntry* langEntry = nullptr;
            rc = nacpGetLanguageEntry(&controlData->nacp, &langEntry);
            if (R_SUCCEEDED(rc) && langEntry) {
                if (langEntry->name[0] != '\0') {
                    outMeta.name = langEntry->name;
                }
                if (langEntry->author[0] != '\0') {
                    outMeta.author = langEntry->author;
                }
            }
            if (controlData->nacp.display_version[0] != '\0') {
                outMeta.version = controlData->nacp.display_version;
            }

            // Decodifica ícone JPEG oficial
            size_t iconBytesSize = actualSize - sizeof(controlData->nacp);
            if (iconBytesSize > 0 && iconBytesSize <= sizeof(controlData->icon)) {
                int w = 0, h = 0, channels = 0;
                unsigned char* decoded = stbi_load_from_memory(
                    controlData->icon,
                    static_cast<int>(iconBytesSize),
                    &w, &h, &channels, 4
                );

                if (decoded && w > 0 && h > 0) {
                    outMeta.icon.width = w;
                    outMeta.icon.height = h;
                    outMeta.icon.pixels.resize(static_cast<size_t>(w * h));
                    std::memcpy(outMeta.icon.pixels.data(), decoded, static_cast<size_t>(w * h * 4));
                    outMeta.icon.valid = true;
                    stbi_image_free(decoded);
                }
            }
            return true;
        }
#endif
        return false;
    }

    const GameMetadata& TextureCache::getGameMetadata(u64 titleId) {
        auto it = m_cache.find(titleId);
        if (it != m_cache.end()) {
            return it->second;
        }

        GameMetadata meta;
        loadControlData(titleId, meta);
        auto insertRes = m_cache.emplace(titleId, std::move(meta));
        return insertRes.first->second;
    }

    std::string TextureCache::getTitleName(u64 titleId) {
        return getGameMetadata(titleId).name;
    }

} // namespace UI

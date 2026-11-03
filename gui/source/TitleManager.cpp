#include "TitleManager.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Assets {

    TitleManager::TitleManager() = default;

    TitleManager::~TitleManager() {
        close();
    }

    bool TitleManager::init() {
        if (m_nsInitialized) return true;
        Result rc = nsInitialize();
        if (R_SUCCEEDED(rc)) {
            m_nsInitialized = true;
            return true;
        }
        return false;
    }

    void TitleManager::close() {
        m_iconCache.clear();
        m_nameCache.clear();
        if (m_nsInitialized) {
            nsExit();
            m_nsInitialized = false;
        }
    }

    void TitleManager::clearCache() {
        m_iconCache.clear();
        m_nameCache.clear();
    }

    TextureHandle TitleManager::decodeJpeg(const void* buffer, size_t size) {
        TextureHandle handle;
        if (!buffer || size == 0) return handle;

        int w = 0, h = 0, channels = 0;
        unsigned char* decoded = stbi_load_from_memory(
            static_cast<const unsigned char*>(buffer),
            static_cast<int>(size),
            &w, &h, &channels, 4
        );

        if (decoded && w > 0 && h > 0) {
            handle.width  = w;
            handle.height = h;
            handle.pixels.resize(w * h);

            for (int i = 0; i < w * h; i++) {
                u8 r = decoded[i * 4 + 0];
                u8 g = decoded[i * 4 + 1];
                u8 b = decoded[i * 4 + 2];
                u8 a = decoded[i * 4 + 3];
                // Formato Switch RGBA8888 de 32 bits
                handle.pixels[i] = (r) | (g << 8) | (b << 16) | (a << 24);
            }

            stbi_image_free(decoded);
            handle.valid = true;
        }

        return handle;
    }

    TextureHandle TitleManager::createFallbackIcon(u64 titleId, const std::string& name) {
        TextureHandle handle;
        int size = 128;
        handle.width = size;
        handle.height = size;
        handle.pixels.resize(size * size);

        // Gera um gradiente escuro refinado baseado no titleId
        u8 baseR = static_cast<u8>(30 + ((titleId >> 0) & 0x1F));
        u8 baseG = static_cast<u8>(38 + ((titleId >> 5) & 0x1F));
        u8 baseB = static_cast<u8>(54 + ((titleId >> 10) & 0x1F));

        for (int y = 0; y < size; y++) {
            float f = static_cast<float>(y) / size;
            u8 r = static_cast<u8>(baseR * (1.0f - f * 0.4f));
            u8 g = static_cast<u8>(baseG * (1.0f - f * 0.4f));
            u8 b = static_cast<u8>(baseB * (1.0f - f * 0.4f));
            u32 color = (r) | (g << 8) | (b << 16) | (0xFF000000);

            for (int x = 0; x < size; x++) {
                handle.pixels[y * size + x] = color;
            }
        }

        // Borda sutil de 2px
        u32 borderColor = (0x00) | (0xD2 << 8) | (0xFF << 16) | (0xFF000000); // Ciano
        for (int i = 0; i < size; i++) {
            handle.pixels[0 * size + i] = borderColor;
            handle.pixels[1 * size + i] = borderColor;
            handle.pixels[(size - 1) * size + i] = borderColor;
            handle.pixels[(size - 2) * size + i] = borderColor;
            handle.pixels[i * size + 0] = borderColor;
            handle.pixels[i * size + 1] = borderColor;
            handle.pixels[i * size + (size - 1)] = borderColor;
            handle.pixels[i * size + (size - 2)] = borderColor;
        }

        handle.valid = true;
        return handle;
    }

    const TextureHandle* TitleManager::getTitleIcon(u64 titleId, const std::string& nameFallback) {
        auto it = m_iconCache.find(titleId);
        if (it != m_iconCache.end()) {
            return &it->second;
        }

        if (m_nsInitialized) {
            NsApplicationControlData* controlData = new (std::nothrow) NsApplicationControlData();
            if (controlData) {
                size_t actualSize = 0;
                Result rc = nsGetApplicationControlData(
                    NsApplicationControlSource_Storage,
                    titleId,
                    controlData,
                    sizeof(NsApplicationControlData),
                    &actualSize
                );

                if (R_SUCCEEDED(rc) && actualSize > sizeof(controlData->nacp)) {
                    size_t iconSize = actualSize - sizeof(controlData->nacp);
                    TextureHandle tex = decodeJpeg(controlData->icon, iconSize);

                    if (m_nameCache.find(titleId) == m_nameCache.end()) {
                        NacpLanguageEntry* langEntry = nullptr;
                        Result nrc = nacpGetLanguageEntry(&controlData->nacp, &langEntry);
                        if (R_SUCCEEDED(nrc) && langEntry && langEntry->name[0] != '\0') {
                            m_nameCache[titleId] = std::string(langEntry->name);
                        }
                    }

                    delete controlData;

                    if (tex.valid) {
                        auto inserted = m_iconCache.emplace(titleId, std::move(tex));
                        return &inserted.first->second;
                    }
                } else {
                    delete controlData;
                }
            }
        }

        // Se não conseguiu extrair do ns:app, gera fallback e armazena em cache
        TextureHandle fallbackTex = createFallbackIcon(titleId, nameFallback);
        auto inserted = m_iconCache.emplace(titleId, std::move(fallbackTex));
        return &inserted.first->second;
    }

    std::string TitleManager::getTitleName(u64 titleId, const std::string& fallback) {
        auto it = m_nameCache.find(titleId);
        if (it != m_nameCache.end()) {
            return it->second;
        }

        if (m_nsInitialized) {
            NsApplicationControlData* controlData = new (std::nothrow) NsApplicationControlData();
            if (controlData) {
                size_t actualSize = 0;
                Result rc = nsGetApplicationControlData(
                    NsApplicationControlSource_Storage,
                    titleId,
                    controlData,
                    sizeof(NsApplicationControlData),
                    &actualSize
                );

                if (R_SUCCEEDED(rc)) {
                    NacpLanguageEntry* langEntry = nullptr;
                    Result nrc = nacpGetLanguageEntry(&controlData->nacp, &langEntry);
                    if (R_SUCCEEDED(nrc) && langEntry && langEntry->name[0] != '\0') {
                        std::string nameStr(langEntry->name);
                        delete controlData;
                        m_nameCache[titleId] = nameStr;
                        return nameStr;
                    }
                }
                delete controlData;
            }
        }

        return fallback.empty() ? "Jogo Switch" : fallback;
    }

} // namespace Assets

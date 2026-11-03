#pragma once
#include "stb_truetype.h"
#include "../common.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace UI {

    struct CachedGlyph {
        int width    = 0;
        int height   = 0;
        int xoff     = 0;
        int yoff     = 0;
        int xadvance = 0;
        std::vector<uint8_t> bitmap;
    };

    class FontRenderer {
    public:
        FontRenderer();
        ~FontRenderer();

        bool init();
        void close();
        bool isReady() const noexcept { return m_fontReady; }

        static uint32_t decodeUtf8(const char*& ptr, const char* end) noexcept;
        const CachedGlyph* getGlyph(uint32_t codepoint, int pixelHeight);
        int measureTextWidth(std::string_view text, int pixelHeight);

    private:
#ifdef __SWITCH__
        PlFontData m_plFontData{};
#endif
        bool m_plInitialized;
        bool m_fontReady;
        stbtt_fontinfo m_fontInfo{};
        std::unordered_map<uint64_t, CachedGlyph> m_glyphCache;
    };

} // namespace UI

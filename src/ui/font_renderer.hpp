#pragma once

#ifndef STB_TRUETYPE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#endif
#include "stb_truetype.h"
#include "../common.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>
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
        FontRenderer()
            : m_plInitialized(false),
              m_fontReady(false) {}

        ~FontRenderer() {
            close();
        }

        bool init() {
            if (m_fontReady) return true;

#ifdef __SWITCH__
            Result rc = plInitialize(PlServiceType_User);
            if (R_SUCCEEDED(rc)) {
                m_plInitialized = true;
                rc = plGetSharedFontByType(&m_plFontData, PlSharedFontType_Standard);
                if (R_SUCCEEDED(rc) && m_plFontData.address != nullptr && m_plFontData.size > 0) {
                    const unsigned char* fontBuffer = static_cast<const unsigned char*>(m_plFontData.address);
                    if (stbtt_InitFont(&m_fontInfo, fontBuffer, stbtt_GetFontOffsetForIndex(fontBuffer, 0))) {
                        m_fontReady = true;
                        return true;
                    }
                }
            }
#endif
            return false;
        }

        void close() {
#ifdef __SWITCH__
            if (m_plInitialized) {
                plExit();
                m_plInitialized = false;
            }
#endif
            m_fontReady = false;
            m_glyphCache.clear();
        }

        bool isReady() const noexcept { return m_fontReady; }

        static uint32_t decodeUtf8(const char*& ptr, const char* end) noexcept {
            if (ptr >= end) return 0;
            auto c = static_cast<unsigned char>(*ptr++);
            if (c < 0x80) return c;
            if ((c & 0xE0) == 0xC0 && ptr < end) {
                auto c2 = static_cast<unsigned char>(*ptr++);
                return ((c & 0x1F) << 6) | (c2 & 0x3F);
            }
            if ((c & 0xF0) == 0xE0 && ptr + 1 < end) {
                auto c2 = static_cast<unsigned char>(*ptr++);
                auto c3 = static_cast<unsigned char>(*ptr++);
                return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            }
            if ((c & 0xF8) == 0xF0 && ptr + 2 < end) {
                auto c2 = static_cast<unsigned char>(*ptr++);
                auto c3 = static_cast<unsigned char>(*ptr++);
                auto c4 = static_cast<unsigned char>(*ptr++);
                return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
            }
            return 0xFFFD;
        }

        const CachedGlyph* getGlyph(uint32_t codepoint, int pixelHeight) {
            if (!m_fontReady) return nullptr;

            uint64_t key = (static_cast<uint64_t>(codepoint) << 16) | (static_cast<uint64_t>(pixelHeight) & 0xFFFF);
            auto it = m_glyphCache.find(key);
            if (it != m_glyphCache.end()) {
                return &it->second;
            }

            int glyphIndex = stbtt_FindGlyphIndex(&m_fontInfo, static_cast<int>(codepoint));
            if (glyphIndex == 0 && codepoint != ' ') {
                glyphIndex = stbtt_FindGlyphIndex(&m_fontInfo, '?');
            }

            float scale = stbtt_ScaleForPixelHeight(&m_fontInfo, static_cast<float>(pixelHeight));
            int advanceWidth = 0, leftSideBearing = 0;
            stbtt_GetGlyphHMetrics(&m_fontInfo, glyphIndex, &advanceWidth, &leftSideBearing);

            CachedGlyph glyph;
            glyph.xadvance = static_cast<int>(std::round(static_cast<float>(advanceWidth) * scale));

            int w = 0, h = 0, xoff = 0, yoff = 0;
            unsigned char* bitmap = stbtt_GetGlyphBitmap(
                &m_fontInfo, scale, scale, glyphIndex,
                &w, &h, &xoff, &yoff
            );

            glyph.width  = w;
            glyph.height = h;
            glyph.xoff   = xoff;
            glyph.yoff   = yoff;

            if (bitmap && w > 0 && h > 0) {
                glyph.bitmap.assign(bitmap, bitmap + (w * h));
                stbtt_FreeBitmap(bitmap, nullptr);
            }

            auto insertRes = m_glyphCache.emplace(key, std::move(glyph));
            return &insertRes.first->second;
        }

        int measureTextWidth(std::string_view text, int pixelHeight) {
            if (text.empty()) return 0;
            int totalWidth = 0;
            const char* cur = text.data();
            const char* end = cur + text.size();

            while (cur < end) {
                uint32_t cp = decodeUtf8(cur, end);
                if (cp == 0) break;
                if (!m_fontReady) {
                    totalWidth += (pixelHeight * 5) / 8;
                    continue;
                }
                const CachedGlyph* g = getGlyph(cp, pixelHeight);
                if (g) {
                    totalWidth += g->xadvance;
                }
            }
            return totalWidth;
        }

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

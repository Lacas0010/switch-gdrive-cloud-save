#include "font_renderer.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font8x16.hpp"
#include "../utils/log.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace UI {

    FontRenderer::FontRenderer()
        : m_plInitialized(false),
          m_fontReady(false) {}

    FontRenderer::~FontRenderer() {
        close();
    }

    bool FontRenderer::init() {
        if (m_fontReady) return true;

#ifdef __SWITCH__
        Result rc = plInitialize(PlServiceType_User);
        if (R_FAILED(rc) && rc != 0x202) {
            Utils::Logger::log("FONT", "plInitialize(User) falhou, tentando System...");
            rc = plInitialize(PlServiceType_System);
        }

        if (R_SUCCEEDED(rc) || rc == 0x202) {
            m_plInitialized = true;
            rc = plGetSharedFontByType(&m_plFontData, PlSharedFontType_Standard);
            if (R_FAILED(rc)) {
                char warnBuf[64];
                std::snprintf(warnBuf, sizeof(warnBuf), "plGetSharedFontByType falhou: 0x%X, tentando Ext...", rc);
                Utils::Logger::log("FONT", warnBuf);
                rc = plGetSharedFontByType(&m_plFontData, PlSharedFontType_NintendoExt);
            }

            if (R_SUCCEEDED(rc) && m_plFontData.address != nullptr && m_plFontData.size > 0) {
                const unsigned char* fontBuffer = static_cast<const unsigned char*>(m_plFontData.address);
                int fontOffset = stbtt_GetFontOffsetForIndex(fontBuffer, 0);
                if (fontOffset < 0) {
                    fontOffset = 0;
                }
                if (stbtt_InitFont(&m_fontInfo, fontBuffer, fontOffset)) {
                    m_fontReady = true;
                    Utils::Logger::log("FONT", "Fonte vetorial do Horizon OS inicializada com sucesso.");
                    return true;
                } else {
                    Utils::Logger::log("FONT", "stbtt_InitFont falhou ao parsear fontBuffer.");
                }
            } else {
                char warnBuf[64];
                std::snprintf(warnBuf, sizeof(warnBuf), "Nao foi possivel obter fonte compartilhada: 0x%X", rc);
                Utils::Logger::log("FONT", warnBuf);
            }
        } else {
            char warnBuf[64];
            std::snprintf(warnBuf, sizeof(warnBuf), "plInitialize falhou: 0x%X", rc);
            Utils::Logger::log("FONT", warnBuf);
        }
#endif
        Utils::Logger::log("FONT", "Utilizando fonte bitmap interna de contingencia.");
        return false;
    }

    void FontRenderer::close() {
#ifdef __SWITCH__
        if (m_plInitialized) {
            plExit();
            m_plInitialized = false;
        }
#endif
        m_fontReady = false;
        m_glyphCache.clear();
    }

    uint32_t FontRenderer::decodeUtf8(const char*& ptr, const char* end) noexcept {
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

    const CachedGlyph* FontRenderer::getGlyph(uint32_t codepoint, int pixelHeight) {
        uint64_t key = (static_cast<uint64_t>(codepoint) << 16) | (static_cast<uint64_t>(pixelHeight) & 0xFFFF);
        auto it = m_glyphCache.find(key);
        if (it != m_glyphCache.end()) {
            return &it->second;
        }

        if (m_fontReady) {
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

        // Fallback de contingência: Fonte Bitmap 8x16
        uint8_t c = (codepoint >= 32 && codepoint <= 126) ? static_cast<uint8_t>(codepoint) : '?';
        int idx = c - 32;
        const uint8_t* rawGlyph = FONT_8X16[idx];

        int scale = std::max(1, pixelHeight / 16);
        int gw = 8 * scale;
        int gh = 16 * scale;

        CachedGlyph glyph;
        glyph.width = gw;
        glyph.height = gh;
        glyph.xoff = 0;
        glyph.yoff = -gh + 2;
        glyph.xadvance = gw + std::max(1, scale);
        glyph.bitmap.resize(static_cast<size_t>(gw * gh), 0);

        for (int r = 0; r < 16; ++r) {
            uint8_t bits = rawGlyph[r];
            for (int col = 0; col < 8; ++col) {
                if (bits & (1 << (7 - col))) {
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale; ++dx) {
                            int px = col * scale + dx;
                            int py = r * scale + dy;
                            glyph.bitmap[static_cast<size_t>(py * gw + px)] = 255;
                        }
                    }
                }
            }
        }

        auto insertRes = m_glyphCache.emplace(key, std::move(glyph));
        return &insertRes.first->second;
    }

    int FontRenderer::measureTextWidth(std::string_view text, int pixelHeight) {
        if (text.empty()) return 0;
        int totalWidth = 0;
        const char* cur = text.data();
        const char* end = cur + text.size();

        while (cur < end) {
            uint32_t cp = decodeUtf8(cur, end);
            if (cp == 0) break;
            const CachedGlyph* g = getGlyph(cp, pixelHeight);
            if (g) {
                totalWidth += g->xadvance;
            } else {
                totalWidth += (pixelHeight * 5) / 8;
            }
        }
        return totalWidth;
    }

} // namespace UI

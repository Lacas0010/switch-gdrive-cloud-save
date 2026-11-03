#pragma once

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <switch.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Gfx {

    // Estrutura de Glifo Rasterizado em Cache com canal Alpha
    struct CachedGlyph {
        int width    = 0;
        int height   = 0;
        int xoff     = 0;
        int yoff     = 0;
        int xadvance = 0;
        std::vector<uint8_t> bitmap;
    };

    class FontRenderer {
    private:
        PlFontData m_plFontData{};
        bool m_plInitialized    = false;
        bool m_fontReady        = false;
        stbtt_fontinfo m_fontInfo{};

        // Cache de glifos: chave de 64 bits = (codepoint << 16) | (pixelHeight & 0xFFFF)
        std::unordered_map<uint64_t, CachedGlyph> m_glyphCache;

    public:
        FontRenderer() = default;

        ~FontRenderer() {
            close();
        }

        bool init() {
            if (m_fontReady) return true;

            // Inicializa serviço de fonte compartilhada do Horizon OS (pl:u)
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

            return false;
        }

        void close() {
            m_glyphCache.clear();
            if (m_plInitialized) {
                plExit();
                m_plInitialized = false;
            }
            m_fontReady = false;
        }

        bool isReady() const {
            return m_fontReady;
        }

        // Decodificador de caractere UTF-8 para Codepoint Unicode
        static uint32_t decodeUtf8(const char*& ptr) {
            uint8_t c = static_cast<uint8_t>(*ptr);
            if (c == 0) return 0;

            if (c < 0x80) {
                ptr++;
                return c;
            } else if ((c & 0xE0) == 0xC0) {
                uint32_t cp = (c & 0x1F) << 6;
                ptr++;
                if (*ptr) cp |= (static_cast<uint8_t>(*ptr++) & 0x3F);
                return cp;
            } else if ((c & 0xF0) == 0xE0) {
                uint32_t cp = (c & 0x0F) << 12;
                ptr++;
                if (*ptr) cp |= (static_cast<uint8_t>(*ptr++) & 0x3F) << 6;
                if (*ptr) cp |= (static_cast<uint8_t>(*ptr++) & 0x3F);
                return cp;
            } else if ((c & 0xF8) == 0xF0) {
                uint32_t cp = (c & 0x07) << 18;
                ptr++;
                if (*ptr) cp |= (static_cast<uint8_t>(*ptr++) & 0x3F) << 12;
                if (*ptr) cp |= (static_cast<uint8_t>(*ptr++) & 0x3F) << 6;
                if (*ptr) cp |= (static_cast<uint8_t>(*ptr++) & 0x3F);
                return cp;
            }

            ptr++;
            return c;
        }

        // Obtém ou gera o glifo rasterizado com antialiasing para a altura solicitada
        const CachedGlyph* getGlyph(uint32_t codepoint, int pixelHeight) {
            if (!m_fontReady || pixelHeight <= 0) return nullptr;

            uint64_t key = (static_cast<uint64_t>(codepoint) << 16) | (static_cast<uint16_t>(pixelHeight));
            auto it = m_glyphCache.find(key);
            if (it != m_glyphCache.end()) {
                return &it->second;
            }

            float scale = stbtt_ScaleForPixelHeight(&m_fontInfo, static_cast<float>(pixelHeight));
            int advanceWidth = 0, leftSideBearing = 0;
            stbtt_GetCodepointHMetrics(&m_fontInfo, codepoint, &advanceWidth, &leftSideBearing);

            int gw = 0, gh = 0, gxoff = 0, gyoff = 0;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(
                &m_fontInfo, 0, scale, codepoint, &gw, &gh, &gxoff, &gyoff
            );

            CachedGlyph glyph;
            glyph.width    = gw;
            glyph.height   = gh;
            glyph.xoff     = gxoff;
            glyph.yoff     = gyoff;
            glyph.xadvance = static_cast<int>(std::round(advanceWidth * scale));

            if (bitmap && gw > 0 && gh > 0) {
                glyph.bitmap.assign(bitmap, bitmap + (gw * gh));
                stbtt_FreeBitmap(bitmap, nullptr);
            }

            auto inserted = m_glyphCache.emplace(key, std::move(glyph));
            return &inserted.first->second;
        }

        // Medição precisa da largura de uma string UTF-8 em pixels
        float measureTextWidth(const std::string& text, int pixelHeight) {
            if (!m_fontReady || text.empty()) return 0.0f;

            float curX = 0.0f;
            const char* ptr = text.c_str();

            while (*ptr) {
                if (*ptr == '\n') break;
                uint32_t cp = decodeUtf8(ptr);
                const CachedGlyph* g = getGlyph(cp, pixelHeight);
                if (g) {
                    curX += g->xadvance;
                } else {
                    curX += pixelHeight * 0.5f;
                }
            }

            return curX;
        }

        // Renderiza texto antialiased diretamente no framebuffer com alpha blending
        void renderText(u32* fb, int stridePixels, int screenW, int screenH,
                        int x, int y, const std::string& text, u32 color, int pixelHeight) {
            if (!m_fontReady || !fb || text.empty() || pixelHeight <= 0) return;

            int curX = x;
            int baselineY = y + static_cast<int>(pixelHeight * 0.85f);
            const char* ptr = text.c_str();

            u8 cr = color & 0xFF;
            u8 cg = (color >> 8) & 0xFF;
            u8 cb = (color >> 16) & 0xFF;

            while (*ptr) {
                if (*ptr == '\n') {
                    curX = x;
                    baselineY += static_cast<int>(pixelHeight * 1.25f);
                    ptr++;
                    continue;
                }

                uint32_t cp = decodeUtf8(ptr);
                const CachedGlyph* g = getGlyph(cp, pixelHeight);
                if (!g) {
                    curX += static_cast<int>(pixelHeight * 0.5f);
                    continue;
                }

                if (g->width > 0 && g->height > 0 && !g->bitmap.empty()) {
                    int drawX = curX + g->xoff;
                    int drawY = baselineY + g->yoff;

                    int x1 = std::max(0, drawX);
                    int y1 = std::max(0, drawY);
                    int x2 = std::min(screenW, drawX + g->width);
                    int y2 = std::min(screenH, drawY + g->height);

                    for (int cy = y1; cy < y2; cy++) {
                        int srcY = cy - drawY;
                        const uint8_t* srcRow = &g->bitmap[srcY * g->width];
                        u32* dstRow = &fb[cy * stridePixels];

                        for (int cx = x1; cx < x2; cx++) {
                            int srcX = cx - drawX;
                            uint8_t alpha = srcRow[srcX];
                            if (alpha == 0) continue;

                            u32 bg = dstRow[cx];
                            if (alpha == 255) {
                                dstRow[cx] = (cr) | (cg << 8) | (cb << 16) | 0xFF000000;
                            } else {
                                uint32_t invA = 255 - alpha;
                                uint8_t dr = static_cast<uint8_t>((cr * alpha + (bg & 0xFF) * invA) / 255);
                                uint8_t dg = static_cast<uint8_t>((cg * alpha + ((bg >> 8) & 0xFF) * invA) / 255);
                                uint8_t db = static_cast<uint8_t>((cb * alpha + ((bg >> 16) & 0xFF) * invA) / 255);
                                dstRow[cx] = (dr) | (dg << 8) | (db << 16) | 0xFF000000;
                            }
                        }
                    }
                }

                curX += g->xadvance;
            }
        }
    };

} // namespace Gfx

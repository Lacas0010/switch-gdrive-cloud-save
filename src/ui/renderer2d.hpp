#pragma once
#include "../common.hpp"
#include "font_renderer.hpp"
#include <cstring>
#include <cmath>
#include <string_view>
#include <algorithm>

namespace UI {

    inline constexpr u32 RGBA(u8 r, u8 g, u8 b, u8 a = 255) noexcept {
        return (static_cast<u32>(r)) |
               (static_cast<u32>(g) << 8) |
               (static_cast<u32>(b) << 16) |
               (static_cast<u32>(a) << 24);
    }

    namespace Colors {
        constexpr u32 BG_DARK_TOP    = RGBA(15, 17, 23);
        constexpr u32 BG_DARK_BOT    = RGBA(21, 25, 36);
        constexpr u32 HEADER_BG      = RGBA(13, 15, 20);
        constexpr u32 FOOTER_BG      = RGBA(13, 15, 20);
        constexpr u32 CARD_BG        = RGBA(24, 29, 41);
        constexpr u32 CARD_ALT_BG    = RGBA(18, 22, 31);
        constexpr u32 CARD_OUTLINE   = RGBA(38, 46, 64);
        constexpr u32 CARD_HOVER_BG  = RGBA(32, 42, 64);
        constexpr u32 CARD_HOVER_OUT = RGBA(0, 210, 255);

        constexpr u32 JOYCON_CYAN    = RGBA(0, 210, 255);
        constexpr u32 JOYCON_RED     = RGBA(255, 60, 86);
        constexpr u32 JOYCON_YELLOW  = RGBA(255, 205, 0);
        constexpr u32 JOYCON_BLUE    = RGBA(40, 120, 255);
        constexpr u32 JOYCON_GRAY    = RGBA(85, 92, 108);

        constexpr u32 DRIVE_YELLOW   = RGBA(255, 186, 0);
        constexpr u32 DRIVE_GREEN    = RGBA(0, 172, 71);
        constexpr u32 DRIVE_BLUE     = RGBA(0, 102, 218);

        constexpr u32 STATUS_GREEN   = RGBA(46, 204, 113);
        constexpr u32 STATUS_RED     = RGBA(231, 76, 60);

        constexpr u32 TEXT_WHITE     = RGBA(255, 255, 255);
        constexpr u32 TEXT_PRIMARY   = RGBA(235, 240, 250);
        constexpr u32 TEXT_MUTED     = RGBA(135, 148, 170);
        constexpr u32 TEXT_DARK      = RGBA(18, 21, 30);
    }

    class Display {
    public:
        Display()
            : m_initialized(false),
              m_width(1280),
              m_height(720),
              m_stride(1280),
              m_framebuffer(nullptr),
              m_isDocked(false) {}

        ~Display() {
            cleanup();
        }

        bool init() {
#ifdef __SWITCH__
            NWindow* win = nwindowGetDefault();
            m_isDocked = (appletGetOperationMode() == AppletOperationMode_Docked);
            m_width = m_isDocked ? 1920 : 1280;
            m_height = m_isDocked ? 1080 : 720;
            framebufferCreate(&m_fb, win, m_width, m_height, PIXEL_FORMAT_RGBA_8888, 2);
            framebufferMakeLinear(&m_fb);
            m_initialized = true;
#else
            m_width = 1280;
            m_height = 720;
            m_stride = 1280;
            m_mockBuffer.resize(static_cast<size_t>(m_width * m_height));
            m_initialized = true;
#endif
            return m_font.init();
        }

        void cleanup() {
            m_font.close();
#ifdef __SWITCH__
            if (m_initialized) {
                framebufferClose(&m_fb);
                m_initialized = false;
            }
#endif
        }

        void updateResolutionMode() {
#ifdef __SWITCH__
            bool currentDocked = (appletGetOperationMode() == AppletOperationMode_Docked);
            if (currentDocked != m_isDocked) {
                cleanup();
                init();
            }
#endif
        }

        u32 getWidth() const noexcept { return m_width; }
        u32 getHeight() const noexcept { return m_height; }
        FontRenderer& getFont() noexcept { return m_font; }

        void startFrame() {
#ifdef __SWITCH__
            updateResolutionMode();
            m_framebuffer = reinterpret_cast<u32*>(framebufferBegin(&m_fb, &m_stride));
            m_stride /= sizeof(u32);
#else
            m_framebuffer = m_mockBuffer.data();
            m_stride = m_width;
#endif
        }

        void endFrame() {
#ifdef __SWITCH__
            if (m_framebuffer) {
                framebufferEnd(&m_fb);
                m_framebuffer = nullptr;
            }
#endif
        }

        inline void setPixel(int x, int y, u32 color) noexcept {
            if (x < 0 || x >= static_cast<int>(m_width) || y < 0 || y >= static_cast<int>(m_height)) return;
            u32 a = (color >> 24) & 0xFF;
            if (a == 0) return;

            u32* dst = &m_framebuffer[static_cast<size_t>(y) * m_stride + static_cast<size_t>(x)];
            if (a == 255) {
                *dst = color;
                return;
            }

            u32 dstColor = *dst;
            u32 rb = (color & 0x00FF00FF) * a + (dstColor & 0x00FF00FF) * (255 - a);
            u32 g  = (color & 0x0000FF00) * a + (dstColor & 0x0000FF00) * (255 - a);
            *dst = ((rb >> 8) & 0x00FF00FF) | ((g >> 8) & 0x0000FF00) | 0xFF000000;
        }

        void drawRect(int x, int y, int w, int h, u32 color) {
            int x1 = std::max(0, x);
            int y1 = std::max(0, y);
            int x2 = std::min(static_cast<int>(m_width), x + w);
            int y2 = std::min(static_cast<int>(m_height), y + h);

            for (int cy = y1; cy < y2; ++cy) {
                for (int cx = x1; cx < x2; ++cx) {
                    setPixel(cx, cy, color);
                }
            }
        }

        void drawRoundedRect(int x, int y, int w, int h, int radius, u32 color) {
            drawRect(x + radius, y, w - 2 * radius, h, color);
            drawRect(x, y + radius, radius, h - 2 * radius, color);
            drawRect(x + w - radius, y + radius, radius, h - 2 * radius, color);

            drawCorner(x + radius, y + radius, radius, 1, color);
            drawCorner(x + w - radius - 1, y + radius, radius, 2, color);
            drawCorner(x + radius, y + h - radius - 1, radius, 4, color);
            drawCorner(x + w - radius - 1, y + h - radius - 1, radius, 8, color);
        }

        void drawText(int x, int y, std::string_view text, int pixelHeight, u32 color) {
            if (text.empty() || !m_framebuffer) return;
            const char* cur = text.data();
            const char* end = cur + text.size();
            int penX = x;

            while (cur < end) {
                uint32_t cp = FontRenderer::decodeUtf8(cur, end);
                if (cp == 0) break;
                const CachedGlyph* g = m_font.getGlyph(cp, pixelHeight);
                if (!g) continue;

                if (!g->bitmap.empty()) {
                    int drawX = penX + g->xoff;
                    int drawY = y + pixelHeight + g->yoff;

                    for (int gy = 0; gy < g->height; ++gy) {
                        for (int gx = 0; gx < g->width; ++gx) {
                            uint8_t alpha = g->bitmap[static_cast<size_t>(gy * g->width + gx)];
                            if (alpha > 0) {
                                u32 blended = (color & 0x00FFFFFF) | (static_cast<u32>(alpha) << 24);
                                setPixel(drawX + gx, drawY + gy, blended);
                            }
                        }
                    }
                }
                penX += g->xadvance;
            }
        }

        void drawTexture(int x, int y, int w, int h, const u32* pixels, int srcW, int srcH) {
            if (!pixels || srcW <= 0 || srcH <= 0 || !m_framebuffer) return;

            for (int dy = 0; dy < h; ++dy) {
                int sy = (dy * srcH) / h;
                for (int dx = 0; dx < w; ++dx) {
                    int sx = (dx * srcW) / w;
                    u32 col = pixels[static_cast<size_t>(sy * srcW + sx)];
                    setPixel(x + dx, y + dy, col);
                }
            }
        }

    private:
        void drawCorner(int cx, int cy, int r, int mask, u32 color) {
            int r2 = r * r;
            for (int dy = -r; dy <= 0; ++dy) {
                for (int dx = -r; dx <= 0; ++dx) {
                    if (dx * dx + dy * dy <= r2) {
                        if (mask == 1) setPixel(cx + dx, cy + dy, color);
                        else if (mask == 2) setPixel(cx - dx, cy + dy, color);
                        else if (mask == 4) setPixel(cx + dx, cy - dy, color);
                        else if (mask == 8) setPixel(cx - dx, cy - dy, color);
                    }
                }
            }
        }

#ifdef __SWITCH__
        Framebuffer m_fb{};
#else
        std::vector<u32> m_mockBuffer;
#endif
        bool m_initialized;
        u32 m_width;
        u32 m_height;
        u32 m_stride;
        u32* m_framebuffer;
        bool m_isDocked;
        FontRenderer m_font;
    };

} // namespace UI

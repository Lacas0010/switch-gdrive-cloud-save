#pragma once
#include <switch.h>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include "FontRenderer.hpp"

namespace Gfx {

    // Converte componentes R, G, B, A (0-255) em formato RGBA8888 de 32 bits
    inline constexpr u32 RGBA(u8 r, u8 g, u8 b, u8 a = 255) {
        return (static_cast<u32>(r)) |
               (static_cast<u32>(g) << 8) |
               (static_cast<u32>(b) << 16) |
               (static_cast<u32>(a) << 24);
    }

    // Paleta de Cores Moderna no padrão Horizon OS
    namespace Colors {
        // Planos de Fundo Escuros Elegantes
        constexpr u32 BG_DARK_TOP    = RGBA(15, 17, 23);
        constexpr u32 BG_DARK_BOT    = RGBA(21, 25, 36);
        constexpr u32 HEADER_BG      = RGBA(13, 15, 20);
        constexpr u32 FOOTER_BG      = RGBA(13, 15, 20);
        constexpr u32 CARD_BG        = RGBA(24, 29, 41);
        constexpr u32 CARD_ALT_BG    = RGBA(18, 22, 31);
        constexpr u32 CARD_OUTLINE   = RGBA(38, 46, 64);
        constexpr u32 CARD_HOVER_BG  = RGBA(32, 42, 64);
        constexpr u32 CARD_HOVER_OUT = RGBA(0, 210, 255);

        // Acentos Joy-Con Oficiais
        constexpr u32 JOYCON_CYAN    = RGBA(0, 210, 255);
        constexpr u32 JOYCON_RED     = RGBA(255, 60, 86);
        constexpr u32 JOYCON_YELLOW  = RGBA(255, 205, 0);
        constexpr u32 JOYCON_BLUE    = RGBA(40, 120, 255);
        constexpr u32 JOYCON_GRAY    = RGBA(85, 92, 108);

        // Google Drive
        constexpr u32 DRIVE_YELLOW   = RGBA(255, 186, 0);
        constexpr u32 DRIVE_GREEN    = RGBA(0, 172, 71);
        constexpr u32 DRIVE_BLUE     = RGBA(0, 102, 218);

        // Status Suaves
        constexpr u32 STATUS_GREEN   = RGBA(46, 204, 113);
        constexpr u32 STATUS_RED     = RGBA(231, 76, 60);
        constexpr u32 STATUS_YELLOW  = RGBA(241, 196, 15);
        constexpr u32 STATUS_BLUE    = RGBA(52, 152, 219);

        // Tipografia
        constexpr u32 TEXT_WHITE     = RGBA(255, 255, 255);
        constexpr u32 TEXT_PRIMARY   = RGBA(235, 240, 250);
        constexpr u32 TEXT_MUTED     = RGBA(135, 148, 170);
        constexpr u32 TEXT_DARK      = RGBA(18, 21, 30);
    }

    class Display {
    private:
        Framebuffer m_fb{};
        bool m_initialized = false;
        u32* m_currentBuf  = nullptr;
        u32  m_stride      = 0;

        int m_width  = 1280;
        int m_height = 720;
        float m_scale = 1.0f;
        AppletOperationMode m_lastOpMode = AppletOperationMode_Handheld;

        FontRenderer m_font;

        void checkAndUpdateResolution() {
            AppletOperationMode currentOpMode = appletGetOperationMode();
            if (currentOpMode != m_lastOpMode) {
                m_lastOpMode = currentOpMode;
                bool isConsole = (currentOpMode == AppletOperationMode_Console);
                int targetW = isConsole ? 1920 : 1280;
                int targetH = isConsole ? 1080 : 720;

                if (targetW != m_width || targetH != m_height) {
                    if (m_initialized) {
                        framebufferClose(&m_fb);
                    }
                    m_width  = targetW;
                    m_height = targetH;
                    m_scale  = isConsole ? 1.5f : 1.0f;

                    NWindow* win = nwindowGetDefault();
                    Result rc = framebufferCreate(&m_fb, win, m_width, m_height, PIXEL_FORMAT_RGBA_8888, 2);
                    if (R_SUCCEEDED(rc)) {
                        framebufferMakeLinear(&m_fb);
                        m_initialized = true;
                    }
                }
            }
        }

    public:
        Display() = default;

        bool init() {
            m_lastOpMode = appletGetOperationMode();
            bool isConsole = (m_lastOpMode == AppletOperationMode_Console);
            m_width  = isConsole ? 1920 : 1280;
            m_height = isConsole ? 1080 : 720;
            m_scale  = isConsole ? 1.5f : 1.0f;

            NWindow* win = nwindowGetDefault();
            Result rc = framebufferCreate(&m_fb, win, m_width, m_height, PIXEL_FORMAT_RGBA_8888, 2);
            if (R_FAILED(rc)) return false;

            rc = framebufferMakeLinear(&m_fb);
            if (R_FAILED(rc)) {
                framebufferClose(&m_fb);
                return false;
            }

            m_initialized = true;
            m_font.init();
            return true;
        }

        void close() {
            m_font.close();
            if (m_initialized) {
                framebufferClose(&m_fb);
                m_initialized = false;
            }
        }

        bool beginFrame() {
            if (!m_initialized) return false;
            checkAndUpdateResolution();

            m_currentBuf = static_cast<u32*>(framebufferBegin(&m_fb, &m_stride));
            return (m_currentBuf != nullptr);
        }

        void endFrame() {
            if (m_initialized && m_currentBuf) {
                framebufferEnd(&m_fb);
                m_currentBuf = nullptr;
            }
        }

        inline int getWidth() const { return m_width; }
        inline int getHeight() const { return m_height; }
        inline float getScale() const { return m_scale; }
        inline bool isDocked() const { return m_lastOpMode == AppletOperationMode_Console; }

        inline int s(int v) const {
            return static_cast<int>(std::round(v * m_scale));
        }

        inline void drawPixel(int x, int y, u32 color) {
            if (x >= 0 && x < m_width && y >= 0 && y < m_height && m_currentBuf) {
                m_currentBuf[y * (m_stride / 4) + x] = color;
            }
        }

        void clearScreenGradient(u32 topColor, u32 botColor) {
            if (!m_currentBuf) return;

            u8 tr = topColor & 0xFF, tg = (topColor >> 8) & 0xFF, tb = (topColor >> 16) & 0xFF;
            u8 br = botColor & 0xFF, bg = (botColor >> 8) & 0xFF, bb = (botColor >> 16) & 0xFF;

            for (int y = 0; y < m_height; y++) {
                float f = static_cast<float>(y) / m_height;
                u8 r = static_cast<u8>(tr * (1.0f - f) + br * f);
                u8 g = static_cast<u8>(tg * (1.0f - f) + bg * f);
                u8 b = static_cast<u8>(tb * (1.0f - f) + bb * f);
                u32 rowColor = RGBA(r, g, b);

                u32* row = &m_currentBuf[y * (m_stride / 4)];
                for (int x = 0; x < m_width; x++) {
                    row[x] = rowColor;
                }
            }
        }

        void fillRect(int x, int y, int w, int h, u32 color) {
            int x1 = std::max(0, x);
            int y1 = std::max(0, y);
            int x2 = std::min(m_width, x + w);
            int y2 = std::min(m_height, y + h);

            for (int cy = y1; cy < y2; cy++) {
                u32* row = &m_currentBuf[cy * (m_stride / 4)];
                for (int cx = x1; cx < x2; cx++) {
                    row[cx] = color;
                }
            }
        }

        void fillRoundedRect(int x, int y, int w, int h, int radius, u32 color) {
            if (radius <= 0) {
                fillRect(x, y, w, h, color);
                return;
            }

            fillRect(x, y + radius, w, h - 2 * radius, color);
            fillRect(x + radius, y, w - 2 * radius, radius, color);
            fillRect(x + radius, y + h - radius, w - 2 * radius, radius, color);

            int r2 = radius * radius;
            for (int cy = 0; cy < radius; cy++) {
                for (int cx = 0; cx < radius; cx++) {
                    int dx = radius - cx;
                    int dy = radius - cy;
                    if (dx * dx + dy * dy <= r2) {
                        drawPixel(x + cx, y + cy, color);
                        drawPixel(x + w - 1 - cx, y + cy, color);
                        drawPixel(x + cx, y + h - 1 - cy, color);
                        drawPixel(x + w - 1 - cx, y + h - 1 - cy, color);
                    }
                }
            }
        }

        void drawRoundedRectOutline(int x, int y, int w, int h, int radius, u32 color, int thickness = 1) {
            for (int t = 0; t < thickness; t++) {
                int curX = x + t;
                int curY = y + t;
                int curW = w - 2 * t;
                int curH = h - 2 * t;
                int curR = std::max(1, radius - t);

                for (int lx = curX + curR; lx < curX + curW - curR; lx++) {
                    drawPixel(lx, curY, color);
                    drawPixel(lx, curY + curH - 1, color);
                }
                for (int ly = curY + curR; ly < curY + curH - curR; ly++) {
                    drawPixel(curX, ly, color);
                    drawPixel(curX + curW - 1, ly, color);
                }

                int r2 = curR * curR;
                int innerR2 = std::max(0, curR - 1) * std::max(0, curR - 1);
                for (int cy = 0; cy <= curR; cy++) {
                    for (int cx = 0; cx <= curR; cx++) {
                        int dx = curR - cx;
                        int dy = curR - cy;
                        int dist2 = dx * dx + dy * dy;
                        if (dist2 <= r2 && dist2 >= innerR2) {
                            drawPixel(curX + cx, curY + cy, color);
                            drawPixel(curX + curW - 1 - cx, curY + cy, color);
                            drawPixel(curX + cx, curY + curH - 1 - cy, color);
                            drawPixel(curX + curW - 1 - cx, curY + curH - 1 - cy, color);
                        }
                    }
                }
            }
        }

        void fillCircle(int cx, int cy, int radius, u32 color) {
            int r2 = radius * radius;
            for (int y = -radius; y <= radius; y++) {
                int dy2 = y * y;
                for (int x = -radius; x <= radius; x++) {
                    if (x * x + dy2 <= r2) {
                        drawPixel(cx + x, cy + y, color);
                    }
                }
            }
        }

        // Renderização de Texto Vetorial Oficial (pl:u Standard Font)
        void drawText(int x, int y, const std::string& text, u32 color, int pixelHeight = 16) {
            if (!m_currentBuf) return;
            m_font.renderText(m_currentBuf, m_stride / 4, m_width, m_height, x, y, text, color, pixelHeight);
        }

        float measureText(const std::string& text, int pixelHeight = 16) {
            return m_font.measureTextWidth(text, pixelHeight);
        }

        // Badges Oficiais do Controle Nintendo Switch
        void drawControllerButtonBadge(int x, int y, const char* label, u32 btnColor, int radius = 11) {
            fillCircle(x + radius, y + radius, radius, btnColor);
            drawRoundedRectOutline(x, y, radius * 2, radius * 2, radius, RGBA(255, 255, 255, 180), 1);

            int fontH = static_cast<int>(radius * 1.3f);
            float tw = measureText(label, fontH);
            int tx = x + radius - static_cast<int>(tw * 0.5f);
            int ty = y + radius - static_cast<int>(fontH * 0.5f);
            drawText(tx, ty, label, Colors::TEXT_WHITE, fontH);
        }

        void drawPillButtonBadge(int x, int y, const char* label, int w = 24, int h = 18) {
            fillRoundedRect(x, y, w, h, 5, Colors::JOYCON_GRAY);
            int fontH = 13;
            float tw = measureText(label, fontH);
            int tx = x + (w - static_cast<int>(tw)) / 2;
            int ty = y + (h - fontH) / 2 - 1;
            drawText(tx, ty, label, Colors::TEXT_WHITE, fontH);
        }

        // ======================================================================
        // ICONES VETORIAIS PROCEDURAIS REFINADOS
        // ======================================================================

        void drawGoogleDriveIcon(int cx, int cy, int size) {
            int half = size / 2;
            int w = static_cast<int>(size * 0.35f);
            for (int dy = -half; dy <= 0; dy++) {
                int sx = cx + (-dy / 2);
                fillRect(sx, cy + dy, w, 2, Colors::DRIVE_YELLOW);
            }
            for (int dy = 0; dy <= half; dy++) {
                int sx = cx + (dy / 2);
                fillRect(sx, cy + dy, w, 2, Colors::DRIVE_GREEN);
            }
            int h = static_cast<int>(size * 0.40f);
            for (int dx = -half; dx <= 0; dx++) {
                int sy = cy + (-dx / 2);
                fillRect(cx + dx, sy, 2, h, Colors::DRIVE_BLUE);
            }
        }

        void drawCloudUploadIcon(int cx, int cy, int size, u32 color) {
            int r = size / 4;
            fillCircle(cx - r, cy, r, color);
            fillCircle(cx, cy - r / 2, static_cast<int>(r * 1.3f), color);
            fillCircle(cx + r, cy, r, color);
            fillRect(cx - r, cy, r * 2, r, color);

            u32 arrowColor = Colors::TEXT_DARK;
            int ah = size / 3;
            fillRect(cx - 2, cy - ah / 4, 4, ah, arrowColor);
            for (int i = 0; i < 6; i++) {
                fillRect(cx - i, cy - ah / 4 + i, i * 2 + 1, 1, arrowColor);
            }
        }

        void drawCloudDownloadIcon(int cx, int cy, int size, u32 color) {
            int r = size / 4;
            fillCircle(cx - r, cy, r, color);
            fillCircle(cx, cy - r / 2, static_cast<int>(r * 1.3f), color);
            fillCircle(cx + r, cy, r, color);
            fillRect(cx - r, cy, r * 2, r, color);

            u32 arrowColor = Colors::TEXT_DARK;
            int ah = size / 3;
            int topY = cy - ah / 3;
            int tipY = topY + ah;
            fillRect(cx - 2, topY, 4, ah - 4, arrowColor);
            for (int i = 0; i < 6; i++) {
                fillRect(cx - (5 - i), tipY - 5 + i, (5 - i) * 2 + 1, 1, arrowColor);
            }
        }

        void drawGamepadIcon(int cx, int cy, int size, u32 color) {
            int w = size;
            int h = size * 2 / 3;
            int x = cx - w / 2;
            int y = cy - h / 2;
            fillRoundedRect(x, y, w, h, 6, color);

            int dX = x + 7, dY = cy - 2;
            fillRect(dX - 3, dY, 7, 3, Colors::TEXT_DARK);
            fillRect(dX - 1, dY - 2, 3, 7, Colors::TEXT_DARK);

            int bX = x + w - 8, bY = cy - 1;
            fillCircle(bX, bY - 3, 1, Colors::JOYCON_YELLOW);
            fillCircle(bX, bY + 3, 1, Colors::STATUS_GREEN);
            fillCircle(bX - 3, bY, 1, Colors::JOYCON_BLUE);
            fillCircle(bX + 3, bY, 1, Colors::JOYCON_RED);
        }

        void drawFolderIcon(int x, int y, int w, int h, u32 color) {
            fillRoundedRect(x, y, w / 2, h / 3, 3, color);
            fillRoundedRect(x, y + h / 4, w, h * 3 / 4, 4, color);
        }

        void drawKeyIcon(int cx, int cy, int size, u32 color) {
            int r = size / 3;
            int ringX = cx - size / 4;
            fillCircle(ringX, cy, r, color);
            fillCircle(ringX, cy, r / 2, Colors::CARD_BG);
            int stemW = size / 2;
            fillRect(ringX + r - 1, cy - 2, stemW, 4, color);
            fillRect(ringX + r + stemW - 4, cy + 2, 3, 4, color);
            fillRect(ringX + r + stemW - 10, cy + 2, 3, 3, color);
        }

        void drawGearIcon(int cx, int cy, int size, u32 color) {
            int r = size / 2;
            int innerR = size / 4;
            fillCircle(cx, cy, r, color);
            fillCircle(cx, cy, innerR, Colors::CARD_BG);
            // Dentes da engrenagem
            int tw = 4, th = 3;
            fillRect(cx - tw / 2, cy - r - th + 1, tw, th, color);
            fillRect(cx - tw / 2, cy + r - 1, tw, th, color);
            fillRect(cx - r - th + 1, cy - tw / 2, th, tw, color);
            fillRect(cx + r - 1, cy - tw / 2, th, tw, color);
        }

        void drawWifiIcon(int cx, int cy, int size, u32 color) {
            fillCircle(cx, cy + size / 2 - 2, 3, color);
            for (int r = size / 3; r <= size / 2; r += size / 6) {
                for (float a = 3.9f; a <= 5.5f; a += 0.05f) {
                    int px = cx + static_cast<int>(std::cos(a) * r);
                    int py = cy + size / 2 + static_cast<int>(std::sin(a) * r);
                    fillCircle(px, py, 1, color);
                }
            }
        }
    };

} // namespace Gfx

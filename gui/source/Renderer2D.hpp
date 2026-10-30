#pragma once
#include <switch.h>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include "Font8x16.hpp"

namespace Gfx {
    constexpr int SCREEN_W = 1280;
    constexpr int SCREEN_H = 720;

    // Converte componentes R, G, B, A (0-255) em formato RGBA8888 de 32 bits
    inline constexpr u32 RGBA(u8 r, u8 g, u8 b, u8 a = 255) {
        return (static_cast<u32>(r)) |
               (static_cast<u32>(g) << 8) |
               (static_cast<u32>(b) << 16) |
               (static_cast<u32>(a) << 24);
    }

    // Paleta de Cores Moderna do Nintendo Switch e Google
    namespace Colors {
        constexpr u32 BG_DARK_TOP    = RGBA(17, 20, 29);
        constexpr u32 BG_DARK_BOT    = RGBA(24, 29, 44);
        constexpr u32 HEADER_BG      = RGBA(15, 18, 26);
        constexpr u32 CARD_BG        = RGBA(26, 32, 48);
        constexpr u32 CARD_OUTLINE   = RGBA(43, 54, 78);
        constexpr u32 CARD_HOVER_BG  = RGBA(36, 48, 75);
        constexpr u32 CARD_HOVER_OUT = RGBA(0, 195, 255);

        // Acentos Joy-Con Oficiais
        constexpr u32 JOYCON_CYAN    = RGBA(0, 210, 255);
        constexpr u32 JOYCON_RED     = RGBA(255, 60, 86);
        constexpr u32 JOYCON_YELLOW  = RGBA(255, 205, 0);
        constexpr u32 JOYCON_BLUE    = RGBA(40, 120, 255);
        constexpr u32 JOYCON_GRAY    = RGBA(90, 95, 110);

        // Google Drive Colors
        constexpr u32 DRIVE_YELLOW   = RGBA(255, 186, 0);
        constexpr u32 DRIVE_GREEN    = RGBA(0, 172, 71);
        constexpr u32 DRIVE_BLUE     = RGBA(0, 102, 218);

        // Status Colors
        constexpr u32 STATUS_GREEN   = RGBA(46, 204, 113);
        constexpr u32 STATUS_RED     = RGBA(231, 76, 60);
        constexpr u32 STATUS_YELLOW  = RGBA(241, 196, 15);
        constexpr u32 STATUS_BLUE    = RGBA(52, 152, 219);

        // Typography Colors
        constexpr u32 TEXT_WHITE     = RGBA(255, 255, 255);
        constexpr u32 TEXT_MUTED     = RGBA(140, 155, 180);
        constexpr u32 TEXT_DARK      = RGBA(20, 24, 34);
    }

    class Display {
    private:
        Framebuffer m_fb;
        bool m_initialized = false;
        u32* m_currentBuf   = nullptr;
        u32  m_stride       = 0;

    public:
        bool init() {
            NWindow* win = nwindowGetDefault();
            Result rc = framebufferCreate(&m_fb, win, SCREEN_W, SCREEN_H, PIXEL_FORMAT_RGBA_8888, 2);
            if (R_FAILED(rc)) return false;

            rc = framebufferMakeLinear(&m_fb);
            if (R_FAILED(rc)) {
                framebufferClose(&m_fb);
                return false;
            }

            m_initialized = true;
            return true;
        }

        void close() {
            if (m_initialized) {
                framebufferClose(&m_fb);
                m_initialized = false;
            }
        }

        bool beginFrame() {
            if (!m_initialized) return false;
            m_currentBuf = static_cast<u32*>(framebufferBegin(&m_fb, &m_stride));
            return (m_currentBuf != nullptr);
        }

        void endFrame() {
            if (m_initialized && m_currentBuf) {
                framebufferEnd(&m_fb);
                m_currentBuf = nullptr;
            }
        }

        inline void drawPixel(int x, int y, u32 color) {
            if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H && m_currentBuf) {
                m_currentBuf[y * (m_stride / 4) + x] = color;
            }
        }

        // Limpa a tela com gradiente vertical suave
        void clearScreenGradient(u32 topColor, u32 botColor) {
            if (!m_currentBuf) return;

            u8 tr = topColor & 0xFF, tg = (topColor >> 8) & 0xFF, tb = (topColor >> 16) & 0xFF;
            u8 br = botColor & 0xFF, bg = (botColor >> 8) & 0xFF, bb = (botColor >> 16) & 0xFF;

            for (int y = 0; y < SCREEN_H; y++) {
                float f = static_cast<float>(y) / SCREEN_H;
                u8 r = static_cast<u8>(tr * (1.0f - f) + br * f);
                u8 g = static_cast<u8>(tg * (1.0f - f) + bg * f);
                u8 b = static_cast<u8>(tb * (1.0f - f) + bb * f);
                u32 rowColor = RGBA(r, g, b);

                u32* row = &m_currentBuf[y * (m_stride / 4)];
                for (int x = 0; x < SCREEN_W; x++) {
                    row[x] = rowColor;
                }
            }
        }

        // Retangulo Preenchido
        void fillRect(int x, int y, int w, int h, u32 color) {
            int x1 = std::max(0, x);
            int y1 = std::max(0, y);
            int x2 = std::min(SCREEN_W, x + w);
            int y2 = std::min(SCREEN_H, y + h);

            for (int cy = y1; cy < y2; cy++) {
                u32* row = &m_currentBuf[cy * (m_stride / 4)];
                for (int cx = x1; cx < x2; cx++) {
                    row[cx] = color;
                }
            }
        }

        // Retangulo Arredondado
        void fillRoundedRect(int x, int y, int w, int h, int radius, u32 color) {
            if (radius <= 0) {
                fillRect(x, y, w, h, color);
                return;
            }

            // Retangulo central vertical
            fillRect(x, y + radius, w, h - 2 * radius, color);
            // Retangulo superior e inferior (sem os cantos)
            fillRect(x + radius, y, w - 2 * radius, radius, color);
            fillRect(x + radius, y + h - radius, w - 2 * radius, radius, color);

            // 4 cantos em circulo
            int r2 = radius * radius;
            for (int cy = 0; cy < radius; cy++) {
                for (int cx = 0; cx < radius; cx++) {
                    int dx = radius - cx;
                    int dy = radius - cy;
                    if (dx * dx + dy * dy <= r2) {
                        // Top-Left
                        drawPixel(x + cx, y + cy, color);
                        // Top-Right
                        drawPixel(x + w - 1 - cx, y + cy, color);
                        // Bottom-Left
                        drawPixel(x + cx, y + h - 1 - cy, color);
                        // Bottom-Right
                        drawPixel(x + w - 1 - cx, y + h - 1 - cy, color);
                    }
                }
            }
        }

        // Borda de Retangulo Arredondado
        void drawRoundedRectOutline(int x, int y, int w, int h, int radius, u32 color, int thickness = 2) {
            for (int t = 0; t < thickness; t++) {
                int curX = x + t;
                int curY = y + t;
                int curW = w - 2 * t;
                int curH = h - 2 * t;
                int curR = std::max(1, radius - t);

                // Linhas horizontais e verticais
                for (int lx = curX + curR; lx < curX + curW - curR; lx++) {
                    drawPixel(lx, curY, color);
                    drawPixel(lx, curY + curH - 1, color);
                }
                for (int ly = curY + curR; ly < curY + curH - curR; ly++) {
                    drawPixel(curX, ly, color);
                    drawPixel(curX + curW - 1, ly, color);
                }

                // Cantos
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

        // Circulo Preenchido
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

        // Renderizador de Caractere (Bitmap 8x16 com fator de escala 1x ou 2x)
        void drawChar(int x, int y, char c, u32 color, int scale = 1) {
            uint8_t code = static_cast<uint8_t>(c);
            if (code < 32 || code > 127) code = 32;
            int idx = code - 32;

            const uint8_t* glyph = FONT_8X16[idx];
            for (int row = 0; row < 16; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < 8; col++) {
                    if (bits & (1 << (7 - col))) {
                        if (scale == 1) {
                            drawPixel(x + col, y + row, color);
                        } else {
                            for (int sy = 0; sy < scale; sy++) {
                                for (int sx = 0; sx < scale; sx++) {
                                    drawPixel(x + col * scale + sx, y + row * scale + sy, color);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Renderizador de Texto com Suporte a Sombra Opcional
        void drawText(int x, int y, const std::string& text, u32 color, int scale = 1, bool shadow = false) {
            if (shadow) {
                u32 shadowColor = RGBA(0, 0, 0, 160);
                drawText(x + scale, y + scale, text, shadowColor, scale, false);
            }

            int curX = x;
            int charW = 8 * scale;
            for (char c : text) {
                if (c == '\n') {
                    curX = x;
                    y += 18 * scale;
                    continue;
                }
                drawChar(curX, y, c, color, scale);
                curX += charW;
            }
        }

        // ======================================================================
        // ICONES VETORIAIS PROCEDURAIS (SEM DEPENDENCIAS DE ARQUIVOS)
        // ======================================================================

        // Icone Oficial do Google Drive (Triangulo Isoceles com 3 cores)
        void drawGoogleDriveIcon(int cx, int cy, int size) {
            int half = size / 2;
            // Trapezio Amarelo (Topo-Direita)
            for (int dy = -half; dy <= 0; dy++) {
                int width = static_cast<int>(size * 0.35f);
                int sx = cx + (-dy / 2);
                fillRect(sx, cy + dy, width, 2, Colors::DRIVE_YELLOW);
            }
            // Trapezio Verde (Base)
            for (int dy = 0; dy <= half; dy++) {
                int width = static_cast<int>(size * 0.35f);
                int sx = cx + (dy / 2);
                fillRect(sx, cy + dy, width, 2, Colors::DRIVE_GREEN);
            }
            // Trapezio Azul (Esquerda)
            for (int dx = -half; dx <= 0; dx++) {
                int height = static_cast<int>(size * 0.40f);
                int sy = cy + (-dx / 2);
                fillRect(cx + dx, sy, 2, height, Colors::DRIVE_BLUE);
            }
        }

        // Icone de Nuvem com Seta de Upload
        void drawCloudSaveIcon(int cx, int cy, int size, u32 color) {
            int r = size / 4;
            // Corpos da nuvem
            fillCircle(cx - r, cy, r, color);
            fillCircle(cx, cy - r / 2, static_cast<int>(r * 1.3f), color);
            fillCircle(cx + r, cy, r, color);
            fillRect(cx - r, cy, r * 2, r, color);

            // Seta de upload no centro da nuvem
            u32 arrowColor = Colors::TEXT_DARK;
            int arrowH = size / 3;
            fillRect(cx - 2, cy - arrowH / 4, 4, arrowH, arrowColor);
            // Ponta da seta
            for (int i = 0; i < 6; i++) {
                fillRect(cx - i, cy - arrowH / 4 + i, i * 2 + 1, 1, arrowColor);
            }
        }

        // Icone de Sinal Wi-Fi
        void drawWifiIcon(int cx, int cy, int size, u32 color) {
            // Ponto base
            fillCircle(cx, cy + size / 2 - 2, 3, color);
            // Arcos de sinal
            for (int r = size / 3; r <= size / 2; r += size / 6) {
                for (float a = 3.9f; a <= 5.5f; a += 0.05f) {
                    int px = cx + static_cast<int>(std::cos(a) * r);
                    int py = cy + size / 2 + static_cast<int>(std::sin(a) * r);
                    fillCircle(px, py, 1, color);
                }
            }
        }

        // Icone de Pasta (Folder)
        void drawFolderIcon(int x, int y, int w, int h, u32 color) {
            // Aba da pasta
            fillRoundedRect(x, y, w / 2, h / 3, 3, color);
            // Corpo principal da pasta
            fillRoundedRect(x, y + h / 4, w, h * 3 / 4, 4, color);
        }

        // Icone de Cadeado / Seguranca
        void drawLockIcon(int cx, int cy, int size, u32 color) {
            // Arco superior
            for (int r = size / 4; r <= size / 3; r++) {
                for (float a = 3.14f; a <= 6.28f; a += 0.08f) {
                    int px = cx + static_cast<int>(std::cos(a) * r);
                    int py = cy - size / 6 + static_cast<int>(std::sin(a) * r);
                    drawPixel(px, py, color);
                }
            }
            // Corpo do cadeado
            fillRoundedRect(cx - size / 3, cy - size / 6, size * 2 / 3, size / 2, 3, color);
            // Buraco da fechadura
            fillCircle(cx, cy + 2, 2, Colors::TEXT_DARK);
        }

        // Badge de Botao do Controle do Nintendo Switch: (A), (B), (X), (Y)
        void drawControllerButtonBadge(int x, int y, const char* label, u32 btnColor) {
            int radius = 11;
            fillCircle(x + radius, y + radius, radius, btnColor);
            drawRoundedRectOutline(x, y, radius * 2, radius * 2, radius, RGBA(255, 255, 255, 180), 1);
            // Letra central
            drawChar(x + radius - 4, y + radius - 8, label[0], Colors::TEXT_WHITE, 1);
        }

        // Badge Pílula de Tecla: (+), (-)
        void drawPillButtonBadge(int x, int y, const char* label) {
            fillRoundedRect(x, y, 22, 18, 5, Colors::JOYCON_GRAY);
            drawText(x + 7, y + 1, label, Colors::TEXT_WHITE, 1);
        }
    };
}


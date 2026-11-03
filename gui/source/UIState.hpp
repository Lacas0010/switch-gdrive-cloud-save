#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace UI {

    enum class Screen {
        Main,           // Tela Principal (2 Colunas, 4 Ações de Rotina)
        Settings,       // Tela de Configurações (Acessível via [-] ou [Y])
        ManageGames,    // Tela de Gerenciamento por Jogo (Seleção / Favoritos)
        DeviceAuth,     // Modal OAuth Device Flow (Código na tela)
        Progress,       // Modal de Barra de Progresso
        Summary         // Modal de Relatório / Conclusão
    };

    struct MainAction {
        enum IconType { Upload, Download, Gamepad, Google };
        IconType icon;
        const char* title;
        const char* subtitle;
        uint32_t accentColor;
    };

    struct SettingsItem {
        enum Type { FolderName, ClientId, ClientSecret, TestConnection };
        Type type;
        const char* title;
        const char* description;
    };

    struct NotificationToast {
        std::string message;
        uint32_t color = 0;
        uint64_t expireTimeNs = 0;

        bool isActive(uint64_t nowNs) const {
            return !message.empty() && (nowNs < expireTimeNs);
        }
    };

    // Controlador de Entrada com Repetição Suave no padrão Nintendo
    class InputRepeater {
    private:
        uint64_t m_holdStartTime = 0;
        uint64_t m_lastRepeatTime = 0;
        uint64_t m_lastButtonMask = 0;

        static constexpr uint64_t INITIAL_DELAY_NS = 350'000'000ULL; // 350ms
        static constexpr uint64_t REPEAT_RATE_NS   = 100'000'000ULL; // 100ms

    public:
        uint64_t process(uint64_t kDown, uint64_t kHeld, uint64_t nowNs) {
            uint64_t triggered = kDown;

            if (kHeld == 0) {
                m_holdStartTime  = 0;
                m_lastRepeatTime = 0;
                m_lastButtonMask = 0;
                return triggered;
            }

            if (kHeld != m_lastButtonMask) {
                m_holdStartTime  = nowNs;
                m_lastRepeatTime = nowNs;
                m_lastButtonMask = kHeld;
            } else if (nowNs - m_holdStartTime >= INITIAL_DELAY_NS) {
                if (nowNs - m_lastRepeatTime >= REPEAT_RATE_NS) {
                    triggered |= kHeld;
                    m_lastRepeatTime = nowNs;
                }
            }

            return triggered;
        }
    };

} // namespace UI

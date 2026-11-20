#include "common.hpp"
#include "core/state_machine.hpp"
#include "core/sync_engine.hpp"
#include "network/drive_client.hpp"
#include "storage/save_manager.hpp"
#include "ui/ui_manager.hpp"
#include "utils/log.hpp"
#include <cstdio>

#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // Inicializa subsistemas básicos com RAII estrito
    Utils::Logger::init();
    Utils::Logger::log("MAIN", "Iniciando Google Drive Cloud Save Suite (gdrive-sync)");

#ifdef __SWITCH__
    // Serviços de idioma do sistema e rede (essenciais para NACP e DNS/libcurl)
    Result setRc = setInitialize();
    auto setGuard = Core::makeScopeGuard([setRc]() {
        if (R_SUCCEEDED(setRc)) setExit();
    });

    Result nifmRc = nifmInitialize(NifmServiceType_User);
    auto nifmGuard = Core::makeScopeGuard([nifmRc]() {
        if (R_SUCCEEDED(nifmRc)) nifmExit();
    });

    // Rede via sockets para libcurl/mbedTLS
    Result sockRc = socketInitializeDefault();
    if (R_FAILED(sockRc)) {
        Utils::Logger::log("MAIN", "Falha ao inicializar socket libnx");
    }
    auto sockGuard = Core::makeScopeGuard([sockRc]() {
        if (R_SUCCEEDED(sockRc)) socketExit();
    });

    // Configuração de entrada dos controles (Pad) e tela sensível ao toque
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
#endif

    {
        // Instanciação das camadas arquiteturais
        Core::StateMachine stateMachine;
        Network::DriveClient driveClient;
        Storage::SaveManager saveManager;
        Core::SyncEngine syncEngine(driveClient, saveManager, stateMachine);
        UI::UIManager uiManager(stateMachine, syncEngine, driveClient, saveManager);

        if (!uiManager.init()) {
            Utils::Logger::log("MAIN", "Falha crítica ao inicializar renderizador gráfico.");
            return 1;
        }

        Utils::Logger::log("MAIN", "Loop principal iniciado a 60 FPS contínuos (Controles + Touch).");

        // Loop de eventos Horizon OS (Garantia Anti-Congelamento)
#ifdef __SWITCH__
        int frameCount = 0;
        bool wasTouching = false;
        int lastTouchX = 0;
        int lastTouchY = 0;

        while (appletMainLoop()) {
            frameCount++;
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);

            // Debounce do botão (+): ignora eventuais transições no carregamento do hbmenu
            if (frameCount > 15 && (kDown & HidNpadButton_Plus)) {
                Utils::Logger::log("MAIN", "Saída solicitada pelo botão (+).");
                break;
            }

            // Leitura da tela sensível ao toque (dispara ao soltar o dedo para evitar duplo-toque)
            UI::TouchEvent touchEvent{};
            HidTouchScreenState touchState{};
            if (hidGetTouchScreenStates(&touchState, 1) && touchState.count > 0) {
                int rawX = static_cast<int>(touchState.touches[0].x);
                int rawY = static_cast<int>(touchState.touches[0].y);
                int dispW = static_cast<int>(uiManager.getDisplayWidth());
                int dispH = static_cast<int>(uiManager.getDisplayHeight());
                lastTouchX = (rawX * dispW) / 1280;
                lastTouchY = (rawY * dispH) / 720;
                wasTouching = true;
            } else if (wasTouching) {
                wasTouching = false;
                if (frameCount > 15) {
                    touchEvent.tapped = true;
                    touchEvent.x = lastTouchX;
                    touchEvent.y = lastTouchY;
                }
            }

            uiManager.handleInput(kDown);
            if (touchEvent.tapped) {
                if (uiManager.handleTouch(touchEvent)) {
                    Utils::Logger::log("MAIN", "Saída solicitada via toque no botão (+).");
                    break;
                }
            }

            uiManager.update();
            uiManager.render();

            svcSleepThread(16'000'000ULL); // 16ms = ~60 FPS e yield para o scheduler do Horizon OS
        }

        char endBuf[64];
        std::snprintf(endBuf, sizeof(endBuf), "Loop principal concluído após %d frames.", frameCount);
        Utils::Logger::log("MAIN", endBuf);
#else
        // Mock loop de teste de desktop
        for (int frame = 0; frame < 5; ++frame) {
            uiManager.update();
            uiManager.render();
        }
#endif

        Utils::Logger::log("MAIN", "Encerrando aplicação de forma graciosa.");
    }

    return 0;
}

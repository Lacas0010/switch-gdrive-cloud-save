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
    // Rede via sockets para libcurl/mbedTLS
    SocketInitConfig sockConfig = {
        .bsdsockets_version = 1,
        .tcp_tx_buf_size = 0x8000,
        .tcp_rx_buf_size = 0x10000,
        .tcp_tx_buf_max_size = 0x40000,
        .tcp_rx_buf_max_size = 0x40000,
        .udp_tx_buf_size = 0x2400,
        .udp_rx_buf_size = 0xA500,
        .sb_efficiency = 2,
    };
    Result sockRc = socketInitialize(&sockConfig);
    if (R_FAILED(sockRc)) {
        Utils::Logger::log("MAIN", "Falha ao inicializar socket libnx");
    }
    auto sockGuard = Core::makeScopeGuard([]() {
        socketExit();
    });

    // Configuração de entrada dos controles (Pad)
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
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

        Utils::Logger::log("MAIN", "Loop principal iniciado a 60 FPS contínuos.");

        // Loop de eventos Horizon OS (Garantia Anti-Congelamento)
#ifdef __SWITCH__
        int frameCount = 0;
        while (appletMainLoop()) {
            frameCount++;
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);

            // Debounce do botão (+): ignora eventuais transições no carregamento do hbmenu
            if (frameCount > 15 && (kDown & HidNpadButton_Plus)) {
                Utils::Logger::log("MAIN", "Saída solicitada pelo botão (+).");
                break;
            }

            uiManager.handleInput(kDown);
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

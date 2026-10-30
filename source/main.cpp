#include <switch.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "Config.hpp"
#include "ProcessMonitor.hpp"
#include "SaveManager.hpp"
#include "ZipHelper.hpp"
#include "GDriveClient.hpp"

// 1. Desativa subsistema grafico e de interface da libnx
extern "C" {
    u32 __nx_applet_type = AppletType_None;

    // 2. Heap restrito a exatamente 3MB para garantir compatibilidade com jogos pesados
    #define INNER_HEAP_SIZE 0x300000 // 3 MiB
    
    void __libnx_initheap(void) {
        void* addr = nullptr;
        Result rc = svcSetHeapSize(&addr, INNER_HEAP_SIZE);
        if (R_FAILED(rc)) {
            return;
        }

        extern void* fake_heap_start;
        extern void* fake_heap_end;
        fake_heap_start = addr;
        fake_heap_end   = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + INNER_HEAP_SIZE);
    }
}

// 3. Pool customizado BSD Socket para economizar memoria RAM
static const SocketInitConfig s_socketInitConfig = {
    .tcp_tx_buf_size     = 0x4000, // 16KB TX
    .tcp_rx_buf_size     = 0x4000, // 16KB RX
    .tcp_tx_buf_max_size = 0x8000, // 32KB Max
    .tcp_rx_buf_max_size = 0x8000, // 32KB Max
    .udp_tx_buf_size     = 0x1000, // 4KB UDP
    .udp_rx_buf_size     = 0x1000,
    .sb_efficiency       = 1,
    .num_bsd_sessions    = 2,
    .bsd_service_type    = BsdServiceType_Auto,
};

static bool s_running = true;

static void cleanupServices() {
    socketExit();
    nifmExit();
    pminfoExit();
    pmdmntExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

static bool isInternetConnected() {
    NifmInternetConnectionType connType;
    u32 wifiState = 0;
    NifmInternetConnectionStatus connStatus;

    Result rc = nifmGetInternetConnectionStatus(&connType, &wifiState, &connStatus);
    return (R_SUCCEEDED(rc) && connStatus == NifmInternetConnectionStatus_Connected);
}

int main(int argc, char* argv[]) {
    // Inicializacao dos servicos essenciais do Horizon OS
    Result rc = smInitialize();
    if (R_FAILED(rc)) return 1;

    rc = fsInitialize();
    if (R_FAILED(rc)) { smExit(); return 1; }

    // Registra montagem do cartao SD no devoptab para funcoes de arquivo padrao (fopen, stat, etc)
    rc = fsdevMountSdmc();
    if (R_FAILED(rc)) { fsExit(); smExit(); return 1; }

    rc = pmdmntInitialize();
    if (R_FAILED(rc)) { fsdevUnmountAll(); fsExit(); smExit(); return 1; }

    rc = pminfoInitialize();
    if (R_FAILED(rc)) { pmdmntExit(); fsdevUnmountAll(); fsExit(); smExit(); return 1; }

    rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) { pminfoExit(); pmdmntExit(); fsdevUnmountAll(); fsExit(); smExit(); return 1; }

    rc = socketInitialize(&s_socketInitConfig);
    if (R_FAILED(rc)) { nifmExit(); pminfoExit(); pmdmntExit(); fsdevUnmountAll(); fsExit(); smExit(); return 1; }

    // Garante que a estrutura de pastas no SD exista
    Config::ensureDirectories();

    ProcessMonitor monitor;
    SaveManager saveManager;
    GDriveClient gdrive;

    bool deviceCodeRequested = false;

    // Loop de monitoramento em segundo plano
    while (s_running) {
        // 1. Gerenciamento de Autenticacao OAuth 2.0 (Device Flow)
        if (!gdrive.isAuthorized()) {
            gdrive.loadConfig();

            if (!gdrive.isAuthorized() && isInternetConnected()) {
                if (!deviceCodeRequested) {
                    if (gdrive.requestDeviceCode()) {
                        deviceCodeRequested = true;
                    }
                } else {
                    if (gdrive.pollAuthorization()) {
                        deviceCodeRequested = false;
                        gdrive.saveConfig();
                    }
                }
            }
        }

        // 2. Monitoramento de Jogos
        if (monitor.scanProcesses()) {
            u64 closedTitleId = monitor.getRecentlyClosedTitle();
            if (closedTitleId != 0) {
                // Checa alteracoes no save do jogo encerrado
                if (saveManager.hasSaveChanged(closedTitleId)) {
                    std::string zipPath = Config::TEMP_DIR + "/save_" + std::to_string(closedTitleId) + ".zip";

                    // Compacta os dados salvos em stream
                    if (ZipHelper::compressSaveData(closedTitleId, zipPath)) {
                        // Envia para o Google Drive se estiver conectado e autorizado
                        if (isInternetConnected() && gdrive.isAuthorized()) {
                            std::string filename = "save_" + std::to_string(closedTitleId) + ".zip";
                            if (gdrive.uploadSaveFile(zipPath, filename)) {
                                saveManager.updateCommittedHash(closedTitleId);
                            }
                        }
                    }

                    // Remove arquivo temporario
                    unlink(zipPath.c_str());
                }
            }
        }

        // Intervalo de ciclo em background: 5 segundos
        svcSleepThread(5'000'000'000ULL);
    }

    cleanupServices();
    return 0;
}

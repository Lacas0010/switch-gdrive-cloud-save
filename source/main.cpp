#include <switch.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "Config.hpp"
#include "ProcessMonitor.hpp"
#include "SaveManager.hpp"
#include "ZipHelper.hpp"
#include "GDriveClient.hpp"

// 1. Subsistema e ciclo de vida canonico de Sysmodule para libnx / Atmosphere
extern "C" {
    u32 __nx_applet_type = AppletType_None;
    u32 __nx_fs_num_sessions = 1;

    // 2. Heap estatico de 2MB no .bss (padrao canonico para sysmodules do Atmosphere)
    #define INNER_HEAP_SIZE 0x200000 // 2 MiB

    void __libnx_initheap(void) {
        static u8 inner_heap[INNER_HEAP_SIZE];
        extern void* fake_heap_start;
        extern void* fake_heap_end;

        fake_heap_start = inner_heap;
        fake_heap_end   = inner_heap + sizeof(inner_heap);
    }

    void __appInit(void) {
        Result rc = smInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

        rc = setsysInitialize();
        if (R_SUCCEEDED(rc)) {
            SetSysFirmwareVersion fw;
            rc = setsysGetFirmwareVersion(&fw);
            if (R_SUCCEEDED(rc))
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
            setsysExit();
        }

        rc = fsInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

        fsdevMountSdmc();
    }

    void __appExit(void) {
        fsdevUnmountAll();
        fsExit();
        smExit();
    }
}

// 3. Pool customizado BSD Socket para economizar memoria RAM no System Pool
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
static bool s_pmdmntReady = false;
static bool s_pminfoReady = false;
static bool s_nifmReady   = false;
static bool s_socketReady = false;

static void logSysmodule(const char* fmt, ...) {
    FILE* f = fopen("sdmc:/config/gdrive-sync/sysmodule.log", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    va_end(args);
    fclose(f);
}

static void cleanupServices() {
    if (s_socketReady) { socketExit(); s_socketReady = false; }
    if (s_nifmReady)   { nifmExit();   s_nifmReady   = false; }
    if (s_pminfoReady) { pminfoExit(); s_pminfoReady = false; }
    if (s_pmdmntReady) { pmdmntExit(); s_pmdmntReady = false; }
}

static bool ensureNetworkServices() {
    if (!s_nifmReady) {
        if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
            s_nifmReady = true;
        }
    }
    if (!s_socketReady) {
        if (R_SUCCEEDED(socketInitialize(&s_socketInitConfig))) {
            s_socketReady = true;
        }
    }
    return s_nifmReady && s_socketReady;
}

static bool isInternetConnected() {
    if (!ensureNetworkServices()) return false;

    NifmInternetConnectionType connType;
    u32 wifiState = 0;
    NifmInternetConnectionStatus connStatus;

    Result rc = nifmGetInternetConnectionStatus(&connType, &wifiState, &connStatus);
    return (R_SUCCEEDED(rc) && connStatus == NifmInternetConnectionStatus_Connected);
}

int main(int argc, char* argv[]) {
    // Garante que a estrutura de pastas no SD exista
    Config::ensureDirectories();
    logSysmodule("[BOOT] Sysmodule CloudSave iniciado!");

    // Inicializa pmdmnt e pminfo com resiliencia para o boot2
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (!s_pmdmntReady && R_SUCCEEDED(pmdmntInitialize())) {
            s_pmdmntReady = true;
        }
        if (!s_pminfoReady && R_SUCCEEDED(pminfoInitialize())) {
            s_pminfoReady = true;
        }
        if (s_pmdmntReady && s_pminfoReady) break;
        svcSleepThread(500'000'000ULL); // 500ms
    }

    // Aguarda qlaunch (Home Menu - 0x0100000000001000) caso estejamos subindo no boot2
    if (s_pmdmntReady) {
        u64 qlaunchPid = 0;
        for (int i = 0; i < 60; ++i) {
            if (R_SUCCEEDED(pmdmntGetProcessId(&qlaunchPid, 0x0100000000001000ULL)) && qlaunchPid != 0) {
                break;
            }
            svcSleepThread(500'000'000ULL);
        }
    }

    ensureNetworkServices();
    logSysmodule("[READY] Servicos prontos (pmdmnt=%d, pminfo=%d, nifm=%d, socket=%d)",
                 s_pmdmntReady, s_pminfoReady, s_nifmReady, s_socketReady);

    ProcessMonitor monitor;
    SaveManager saveManager;
    GDriveClient gdrive;

    bool deviceCodeRequested = false;

    // Loop de monitoramento em segundo plano
    while (s_running) {
        // 1. Gerenciamento de Autenticacao OAuth 2.0 (Device Flow)
        if (!gdrive.isAuthorized()) {
            gdrive.loadConfig();
        }

        if (gdrive.isConfigured() && !gdrive.isAuthorized()) {
            if (isInternetConnected()) {
                if (!deviceCodeRequested) {
                    if (gdrive.requestDeviceCode()) {
                        deviceCodeRequested = true;
                        logSysmodule("[AUTH] Device code solicitado com sucesso.");
                    }
                } else {
                    // Tenta polling para verificar se o usuario digitou o codigo no celular/PC
                    if (gdrive.pollAuthorization()) {
                        deviceCodeRequested = false;
                        logSysmodule("[AUTH] Autorizacao concluida com sucesso!");
                    }
                }
            }
        }

        // 2. Monitoramento de Jogos e Backup de Saves
        u64 closedTitleId = 0;
        if (s_pmdmntReady && s_pminfoReady && monitor.hasGameJustExited(closedTitleId)) {
            logSysmodule("[EVENT] Jogo fechado detectado: 0x%016llX", (unsigned long long)closedTitleId);

            // Verifica se o jogo esta autorizado no modo whitelist
            if (!gdrive.isTitleTracked(closedTitleId)) {
                logSysmodule("[FILTER] Jogo 0x%016llX ignorado pelo modo whitelist.", (unsigned long long)closedTitleId);
            } else {
                // Um jogo foi fechado. Verifica se ha conta de usuario ativa
                AccountUid uid{};
                if (saveManager.getActiveAccount(uid)) {
                    std::string zipPath;
                    bool changed = false;

                    // Processa e compacta o save
                    if (saveManager.processGameSave(closedTitleId, uid, zipPath, changed)) {
                        if (changed && isInternetConnected() && gdrive.isAuthorized()) {
                            // Calcula CRC para persistir apos o upload
                            u32 newCrc = 0;
                            ZipHelper::calculateFileCrc32(zipPath, newCrc);

                            // Envia para o Google Drive
                            if (gdrive.uploadSaveArchive(closedTitleId, zipPath)) {
                                saveManager.markSaveUploaded(closedTitleId, newCrc);
                                logSysmodule("[SYNC] Save 0x%016llX enviado ao Google Drive (CRC=0x%08X)",
                                             (unsigned long long)closedTitleId, newCrc);
                            }
                        }
                    }
                }
            }
        }

        // Libera CPU dormindo por 3 segundos
        svcSleepThread(3'000'000'000ULL);
    }

    cleanupServices();
    return 0;
}

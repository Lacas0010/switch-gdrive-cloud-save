#include <switch.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <dirent.h>
#include <vector>
#include "Config.hpp"
#include "ProcessMonitor.hpp"
#include "SaveManager.hpp"
#include "ZipHelper.hpp"
#include "GDriveClient.hpp"
#include "Logger.hpp"

// ==============================================================================
// 1. INICIALIZACAO ENXUTA DE MEMORIA E CICLO DE VIDA PARA SYSMODULE (ATMOSPHERE)
// ==============================================================================
extern "C" {
    u32 __nx_applet_type = AppletType_None;
    u32 __nx_fs_num_sessions = 1;

    // Heap estatico de 3.5MB no .bss (economiza RAM do System Pool do Horizon OS mantendo espaco para TLS/SSL/Sockets)
    #define INNER_HEAP_SIZE 0x380000 // 3.5 MiB

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

        for (int attempt = 0; attempt < 20; ++attempt) {
            if (R_SUCCEEDED(fsdevMountSdmc())) {
                break;
            }
            svcSleepThread(250'000'000ULL);
        }
    }

    void __appExit(void) {
        fsdevUnmountAll();
        fsExit();
        smExit();
    }
}

// ==============================================================================
// 2. CONFIGURACAO RESILIENTE DE BSD SOCKET (POOL MINIMO)
// ==============================================================================
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

// Recupera snapshots orfaos legados deixados em temp/ e os converte em jobs pendentes
static void recoverLegacyTempSnapshots(SaveManager& saveManager) {
    DIR* dir = opendir(Config::TEMP_DIR.c_str());
    if (!dir) return;

    AccountUid defaultUid{};
    saveManager.getActiveAccount(defaultUid);
    std::string uidStr = SaveManager::formatUid(defaultUid);

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            std::string name = entry->d_name;
            // Padrao legado: save_<TitleId>.zip
            if (name.rfind("save_", 0) == 0 && name.size() > 9 && name.substr(name.size() - 4) == ".zip") {
                unsigned long long tid = 0;
                if (sscanf(name.c_str(), "save_%llx.zip", &tid) == 1 && tid != 0) {
                    std::string oldPath = Config::TEMP_DIR + "/" + name;
                    u32 crc = 0;
                    ZipHelper::calculateFileCrc32(oldPath, crc);
                    std::time_t ts = std::time(nullptr);

                    char newSnapName[256];
                    snprintf(newSnapName, sizeof(newSnapName), "%016llX_%s_%lld_%08X.zip",
                             tid, uidStr.c_str(), (long long)ts, crc);
                    std::string newSnapPath = Config::TEMP_DIR + "/" + newSnapName;
                    rename(oldPath.c_str(), newSnapPath.c_str());

                    PendingUploadJob job;
                    char jId[128];
                    snprintf(jId, sizeof(jId), "%016llX_%lld_%08X", tid, (long long)ts, crc);
                    job.jobId = jId;
                    job.titleId = tid;
                    job.uid = defaultUid;
                    job.accountUidStr = uidStr;
                    job.timestampUtc = ts;
                    job.crc32 = crc;
                    job.fileCount = 1;
                    job.zipPath = newSnapPath;

                    saveManager.savePendingJob(job);
                    logSysmodule("[RECOVERY] Snapshot legado convertido para pendencia: %s", newSnapName);
                }
            }
        }
    }
    closedir(dir);
}

// ==============================================================================
// 3. PONTO DE ENTRADA DO SYSMODULE COM MAQUINA DE ESTADOS EXPLÍCITA
// ==============================================================================
int main(int argc, char* argv[]) {
    Config::ensureDirectories();
    logSysmodule("[BOOT] Sysmodule CloudSave iniciado (Arquitetura Append-Only)");

    // Inicializacao dos servicos pmdmnt e pminfo com retentativas para boot2
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (!s_pmdmntReady && R_SUCCEEDED(pmdmntInitialize())) {
            s_pmdmntReady = true;
        }
        if (!s_pminfoReady && R_SUCCEEDED(pminfoInitialize())) {
            s_pminfoReady = true;
        }
        if (s_pmdmntReady && s_pminfoReady) break;
        svcSleepThread(500'000'000ULL);
    }

    // Aguarda Home Menu (qlaunch) inicializar se o console acabou de ligar
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

    // Converte e registra eventuais snapshots offline residuais
    recoverLegacyTempSnapshots(saveManager);

    // ==========================================================================
    // LOOP PRINCIPAL DA MAQUINA DE ESTADOS (100ms por ciclo - CPU amigavel)
    // ==========================================================================
    while (s_running) {
        // Manutencao de Autorizacao OAuth2 (Device Flow)
        if (!gdrive.isAuthorized()) {
            gdrive.loadConfig();
        }
        if (gdrive.isConfigured() && !gdrive.isAuthorized()) {
            if (isInternetConnected()) {
                if (!deviceCodeRequested) {
                    if (gdrive.requestDeviceCode()) {
                        deviceCodeRequested = true;
                        logSysmodule("[AUTH] Device code solicitado.");
                    }
                } else {
                    if (gdrive.pollAuthorization()) {
                        deviceCodeRequested = false;
                        logSysmodule("[AUTH] Autorizacao OAuth2 concluida!");
                    }
                }
            }
        }

        // Execucao da Maquina de Estados
        u64 closedTitleId = 0;
        switch (saveManager.GetState()) {
            case SyncState::IDLE:
                if (s_pmdmntReady && s_pminfoReady && monitor.hasGameJustExited(closedTitleId)) {
                    logSysmodule("[EVENT] Fechamento de jogo detectado: 0x%016llX", (unsigned long long)closedTitleId);
                    saveManager.OnProcessExit(closedTitleId);
                } else {
                    // Se nao ha jogo fechando, tenta enviar snapshots acumulados na fila
                    if (isInternetConnected() && gdrive.isAuthorized()) {
                        saveManager.TryFlushPending(gdrive);
                    }
                }
                break;

            case SyncState::PROCESS_EXIT_DETECTED:
                saveManager.HandleProcessExitDetected(gdrive);
                break;

            case SyncState::WAIT_LOCKS:
                saveManager.HandleWaitLocks();
                break;

            case SyncState::MOUNT_READONLY:
                saveManager.HandleMountReadOnly();
                break;

            case SyncState::SNAPSHOT_PREPARE:
                saveManager.HandleSnapshotPrepare();
                break;

            case SyncState::COMPRESS:
                saveManager.HandleCompress();
                break;

            case SyncState::UPLOAD:
                saveManager.HandleUpload(gdrive);
                break;

            case SyncState::CLEANUP:
                saveManager.HandleCleanup();
                break;

            case SyncState::FAILURE_RECOVERY:
                saveManager.HandleFailureRecovery();
                break;

            case SyncState::DISABLED:
                break;
        }

        // Libera tempo de CPU: 100ms de sono por iteracao
        svcSleepThread(100'000'000ULL);
    }

    saveManager.Shutdown();
    cleanupServices();
    logSysmodule("[SHUTDOWN] Sysmodule finalizado.");
    return 0;
}

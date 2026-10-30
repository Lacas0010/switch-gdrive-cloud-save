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

// 1. Subsistema e ciclo de vida canonico de Sysmodule para libnx / Atmosphere
extern "C" {
    u32 __nx_applet_type = AppletType_None;
    u32 __nx_fs_num_sessions = 1;

    // 2. Heap estatico de 1.5MB no .bss (otimizado para conviver com multiplos sysmodules no System Pool)
    #define INNER_HEAP_SIZE 0x180000 // 1.5 MiB

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

struct PendingSave {
    u64 titleId;
    std::string zipPath;
    u32 crc;
};
static std::vector<PendingSave> s_pendingSaves;

static void recoverPendingSavesFromDisk() {
    DIR* dir = opendir(Config::TEMP_DIR.c_str());
    if (!dir) return;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            std::string name = entry->d_name;
            if (name.rfind("save_", 0) == 0 && name.size() > 9 && name.substr(name.size() - 4) == ".zip") {
                unsigned long long tid = 0;
                if (sscanf(name.c_str(), "save_%llx.zip", &tid) == 1 && tid != 0) {
                    std::string fullPath = Config::TEMP_DIR + "/" + name;
                    u32 crc = 0;
                    ZipHelper::calculateFileCrc32(fullPath, crc);
                    s_pendingSaves.push_back({tid, fullPath, crc});
                    logSysmodule("[RECOVERY] Save pendente recuperado do SD: 0x%016llX (%s)", tid, name.c_str());
                }
            }
        }
    }
    closedir(dir);
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

    // Recupera eventuais saves que ficaram salvos no SD enquanto o console estava offline
    recoverPendingSavesFromDisk();

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

        // 1.1 Processa fila de saves pendentes (ex: jogos fechados offline ou apos desativar LDN)
        if (!s_pendingSaves.empty() && isInternetConnected() && gdrive.isAuthorized()) {
            for (auto it = s_pendingSaves.begin(); it != s_pendingSaves.end(); ) {
                std::string upErr;
                if (gdrive.uploadSaveArchive(it->titleId, it->zipPath, &upErr)) {
                    saveManager.markSaveUploaded(it->titleId, it->crc);
                    logSysmodule("[SYNC] Save pendente 0x%016llX enviado com sucesso ao Drive!",
                                 (unsigned long long)it->titleId);
                    it = s_pendingSaves.erase(it);
                } else {
                    logSysmodule("[ERROR] Falha ao enviar save pendente 0x%016llX: %s",
                                 (unsigned long long)it->titleId, upErr.c_str());
                    ++it;
                }
            }
        }

        // 2. Monitoramento de Jogos e Backup de Saves
        u64 closedTitleId = 0;
        if (s_pmdmntReady && s_pminfoReady && monitor.hasGameJustExited(closedTitleId)) {
            Logger::logSync("[%016llX] Process terminated", (unsigned long long)closedTitleId);
            logSysmodule("[EVENT] Jogo fechado detectado: 0x%016llX", (unsigned long long)closedTitleId);

            // Aguarda 3.5 segundos para que o Horizon OS descarregue o cache e libere travas de arquivos de save
            svcSleepThread(3'500'000'000ULL);

            // Verifica se o jogo esta autorizado no modo whitelist
            if (!gdrive.isTitleTracked(closedTitleId)) {
                Logger::logSync("[%016llX] Game ignored by whitelist.", (unsigned long long)closedTitleId);
                logSysmodule("[FILTER] Jogo 0x%016llX ignorado pelo modo whitelist.", (unsigned long long)closedTitleId);
            } else {
                // Um jogo foi fechado. Verifica se ha conta de usuario ativa
                AccountUid uid{};
                if (!saveManager.getActiveAccount(uid)) {
                    Logger::logSync("[%016llX] FAILED: Could not obtain active user account.", (unsigned long long)closedTitleId);
                    logSysmodule("[ERROR] Nao foi possivel obter usuario ativo para save 0x%016llX", (unsigned long long)closedTitleId);
                } else {
                    logSysmodule("[SAVE] Lendo e compactando save de 0x%016llX...", (unsigned long long)closedTitleId);
                    std::string zipPath;
                    bool changed = false;

                    // Processa e compacta o save (com retry loop interno caso a particao esteja busy)
                    if (!saveManager.processGameSave(closedTitleId, uid, zipPath, changed)) {
                        logSysmodule("[ERROR] Falha ao ler ou compactar save de 0x%016llX", (unsigned long long)closedTitleId);
                    } else if (!changed) {
                        logSysmodule("[INFO] Save 0x%016llX inalterado (mesmo CRC), upload dispensado.", (unsigned long long)closedTitleId);
                    } else {
                        u32 newCrc = 0;
                        ZipHelper::calculateFileCrc32(zipPath, newCrc);
                        logSysmodule("[SAVE] Save compactado com sucesso! CRC=0x%08X", newCrc);

                        // Jogos como Pokemon Sword usam Comunicacao Local (LDN / Y-Comm) que desligam o Wi-Fi e o audio Bluetooth.
                        // Ao fechar o jogo, o Horizon OS precisa de 15 a 45 segundos para restabelecer a conexao com o roteador.
                        logSysmodule("[NETWORK] Aguardando reconexao Wi-Fi (timeout 60s)...");
                        bool connected = false;
                        for (int attempt = 0; attempt < 60; ++attempt) {
                            if (isInternetConnected()) {
                                connected = true;
                                logSysmodule("[NETWORK] Wi-Fi reconectado apos %d segundos!", attempt);
                                break;
                            }
                            svcSleepThread(1'000'000'000ULL); // 1 segundo por tentativa (total 60s / 1 minuto)
                        }

                        if (connected && gdrive.isAuthorized()) {
                            logSysmodule("[UPLOAD] Iniciando upload de 0x%016llX ao Google Drive...", (unsigned long long)closedTitleId);
                            std::string upErr;
                            if (gdrive.uploadSaveArchive(closedTitleId, zipPath, &upErr)) {
                                saveManager.markSaveUploaded(closedTitleId, newCrc);
                                logSysmodule("[SYNC] Save 0x%016llX enviado ao Google Drive (CRC=0x%08X)",
                                             (unsigned long long)closedTitleId, newCrc);
                            } else {
                                logSysmodule("[ERROR] Falha no upload imediato de 0x%016llX: %s. Enfileirando...",
                                             (unsigned long long)closedTitleId, upErr.c_str());
                                s_pendingSaves.push_back({closedTitleId, zipPath, newCrc});
                            }
                        } else {
                            Logger::logSync("[%016llX] Wi-Fi not connected after 60s (LDN/offline). Enqueuing save to pending list.",
                                            (unsigned long long)closedTitleId);
                            logSysmodule("[WARN] Sem Wi-Fi apos fechar 0x%016llX (LDN/offline). Enfileirando para envio posterior...",
                                         (unsigned long long)closedTitleId);
                            s_pendingSaves.push_back({closedTitleId, zipPath, newCrc});
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

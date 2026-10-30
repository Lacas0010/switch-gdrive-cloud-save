#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include "../../source/Config.hpp"
#include "../../source/GDriveClient.hpp"

// Funcao auxiliar para exibir o teclado virtual da libnx
std::string showKeyboard(const char* guideText, const std::string& initialText) {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        return initialText;
    }

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guideText);
    swkbdConfigSetInitialText(&kbd, initialText.c_str());

    char outBuffer[512] = {0};
    rc = swkbdShow(&kbd, outBuffer, sizeof(outBuffer) - 1);
    swkbdClose(&kbd);

    if (R_SUCCEEDED(rc) && outBuffer[0] != '\0') {
        return std::string(outBuffer);
    }
    return initialText;
}

// Checagem se o Sysmodule esta rodando no momento
bool isSysmoduleRunning(u64& outPid) {
    outPid = 0;
    Result rc = pmdmntGetProcessId(&outPid, Config::SYSMODULE_TITLE_ID);
    return (R_SUCCEEDED(rc) && outPid != 0);
}

// Inicia o Sysmodule em segundo plano
bool startSysmodule() {
    NcmProgramLocation loc = {};
    loc.program_id = Config::SYSMODULE_TITLE_ID;
    loc.storage_id = NcmStorageId_None;

    u64 pid = 0;
    Result rc = pmshellLaunchProgram(0, &loc, &pid);
    return R_SUCCEEDED(rc);
}

// Encerra o Sysmodule
bool stopSysmodule() {
    Result rc = pmshellTerminateProgram(Config::SYSMODULE_TITLE_ID);
    return R_SUCCEEDED(rc);
}

// Checa conexao com a internet
bool isInternetConnected() {
    NifmInternetConnectionType connType;
    u32 wifiState = 0;
    NifmInternetConnectionStatus connStatus;

    Result rc = nifmGetInternetConnectionStatus(&connType, &wifiState, &connStatus);
    return (R_SUCCEEDED(rc) && connStatus == NifmInternetConnectionStatus_Connected);
}

// Tela interativa de autenticacao OAuth 2.0 Device Flow
void runDeviceAuthScreen(GDriveClient& client, PadState& pad) {
    printf("\x1b[2J\x1b[H"); // Limpa tela
    printf("\x1b[36;1m================================================================================\x1b[0m\n");
    printf("\x1b[37;1m                      AUTENTICACAO GOOGLE DRIVE (DEVICE FLOW)                   \x1b[0m\n");
    printf("\x1b[36;1m================================================================================\x1b[0m\n\n");

    if (!isInternetConnected()) {
        printf("\x1b[31;1m[ERRO] O Nintendo Switch nao esta conectado a internet!\x1b[0m\n");
        printf("Conecte-se ao Wi-Fi antes de tentar autenticar.\n\n");
        printf("Pressione [B] para voltar ao menu.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_B) break;
            svcSleepThread(50'000'000ULL);
        }
        return;
    }

    if (!client.isConfigured()) {
        printf("\x1b[31;1m[ERRO] Client ID e Client Secret nao foram configurados!\x1b[0m\n");
        printf("Configure-os no menu principal antes de autenticar.\n\n");
        printf("Pressione [B] para voltar ao menu.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_B) break;
            svcSleepThread(50'000'000ULL);
        }
        return;
    }

    printf("\x1b[33mSolicitando codigo de ativacao aos servidores do Google...\x1b[0m\n");
    consoleUpdate(NULL);

    if (!client.requestDeviceCode()) {
        printf("\x1b[31;1m[FALHA] Nao foi possivel obter o codigo de ativacao.\x1b[0m\n");
        printf("Verifique se seu Client ID e Secret estao corretos e se o Google Drive API esta ativo.\n\n");
        printf("Pressione [B] para voltar.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_B) break;
            svcSleepThread(50'000'000ULL);
        }
        return;
    }

    std::string userCode = client.getPendingUserCode();
    std::string verifyUrl = client.getPendingVerifyUrl();

    int animCounter = 0;
    const char* spinner = "|/-\\";

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_B) {
            break;
        }

        // Tenta fazer o polling de confirmacao
        if (client.pollAuthorization()) {
            printf("\x1b[2J\x1b[H");
            printf("\x1b[32;1m================================================================================\x1b[0m\n");
            printf("\x1b[32;1m               SUCESSO! CONTA GOOGLE VINCULADA COM EXITO!                      \x1b[0m\n");
            printf("\x1b[32;1m================================================================================\x1b[0m\n\n");
            printf("Os tokens foram salvos em: %s\n", Config::CONFIG_FILE.c_str());
            printf("O Sysmodule em segundo plano agora podera fazer uploads automaticamente.\n\n");
            printf("Pressione [A] ou [B] para retornar ao menu.\n");
            consoleUpdate(NULL);
            while (appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & (HidNpadButton_A | HidNpadButton_B)) break;
                svcSleepThread(50'000'000ULL);
            }
            break;
        }

        // Renderiza tela de instrucoes
        printf("\x1b[2J\x1b[H");
        printf("\x1b[36;1m================================================================================\x1b[0m\n");
        printf("\x1b[37;1m                      AUTORIZACAO PENDENTE NO CELULAR/PC                        \x1b[0m\n");
        printf("\x1b[36;1m================================================================================\x1b[0m\n\n");
        printf(" Siga os passos abaixo no seu navegador:\n\n");
        printf(" \x1b[33;1m1.\x1b[0m Acesse o link: \x1b[36;1m%s\x1b[0m\n\n", verifyUrl.c_str());
        printf(" \x1b[33;1m2.\x1b[0m Digite o codigo de ativacao:\n\n");
        printf("       \x1b[47;30;1m   >>>   %s   <<<   \x1b[0m\n\n", userCode.c_str());
        printf(" \x1b[33;1m3.\x1b[0m Autorize o aplicativo na sua conta Google.\n\n");
        printf("--------------------------------------------------------------------------------\n");
        printf(" Status: Aguardando confirmacao no Google [%c]\n", spinner[animCounter % 4]);
        printf(" Pressione [B] para cancelar e voltar ao menu.\n");

        animCounter++;
        consoleUpdate(NULL);
        svcSleepThread(1'000'000'000ULL); // 1 segundo
    }
}

int main(int argc, char* argv[]) {
    // Inicializa console grafico ANSI na tela do Switch
    consoleInit(NULL);

    // Servicos
    smInitialize();
    fsInitialize();
    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);
    pmdmntInitialize();
    pmshellInitialize();

    Config::ensureDirectories();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    GDriveClient client;

    int selectedItem = 0;
    const int TOTAL_MENU_ITEMS = 7;
    std::string statusMessage = "";

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) {
            break; // Sai do aplicativo
        }

        // Navegacao
        if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            selectedItem--;
            if (selectedItem < 0) selectedItem = TOTAL_MENU_ITEMS - 1;
        }
        if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            selectedItem++;
            if (selectedItem >= TOTAL_MENU_ITEMS) selectedItem = 0;
        }

        u64 pid = 0;
        bool sysmoduleRunning = isSysmoduleRunning(pid);
        bool bootFlag = Config::isBootFlagPresent();
        bool internet = isInternetConnected();

        // Atalhos rapidos
        if (kDown & HidNpadButton_X) {
            if (sysmoduleRunning) {
                if (stopSysmodule()) statusMessage = "Sysmodule parado com sucesso!";
                else statusMessage = "Falha ao parar o sysmodule.";
            } else {
                if (startSysmodule()) statusMessage = "Sysmodule iniciado em segundo plano!";
                else statusMessage = "Falha ao iniciar o sysmodule.";
            }
        }

        if (kDown & HidNpadButton_Y) {
            Config::setBootFlag(!bootFlag);
            statusMessage = !bootFlag ? "Inicializacao automatica no boot ATIVADA." : "Inicializacao automatica no boot DESATIVADA.";
        }

        // Acao ao pressionar A
        if (kDown & HidNpadButton_A) {
            switch (selectedItem) {
                case 0: // Vincular Conta Google
                    runDeviceAuthScreen(client, pad);
                    client.loadConfig();
                    break;
                case 1: // Iniciar/Parar Sysmodule
                    if (sysmoduleRunning) {
                        if (stopSysmodule()) statusMessage = "Sysmodule interrompido.";
                        else statusMessage = "Erro ao interromper sysmodule.";
                    } else {
                        if (startSysmodule()) statusMessage = "Sysmodule em execucao!";
                        else statusMessage = "Erro ao disparar sysmodule.";
                    }
                    break;
                case 2: // Toggle Boot
                    Config::setBootFlag(!bootFlag);
                    statusMessage = !bootFlag ? "Boot automatico ATIVADO." : "Boot automatico DESATIVADO.";
                    break;
                case 3: { // Editar Client ID
                    std::string newId = showKeyboard("Digite o Google OAuth Client ID", client.getClientId());
                    client.setClientId(newId);
                    client.saveConfig();
                    statusMessage = "Client ID atualizado.";
                    break;
                }
                case 4: { // Editar Client Secret
                    std::string newSec = showKeyboard("Digite o Google OAuth Client Secret", client.getClientSecret());
                    client.setClientSecret(newSec);
                    client.saveConfig();
                    statusMessage = "Client Secret atualizado.";
                    break;
                }
                case 5: { // Editar Nome da Pasta
                    std::string newFolder = showKeyboard("Nome da pasta no Google Drive", client.getFolderName());
                    client.setFolderName(newFolder);
                    client.saveConfig();
                    statusMessage = "Nome da pasta atualizado.";
                    break;
                }
                case 6: { // Validar / Criar Pasta
                    if (!client.isAuthorized()) {
                        statusMessage = "Erro: autorize a conta Google primeiro!";
                    } else if (!internet) {
                        statusMessage = "Erro: conecte o Switch ao Wi-Fi!";
                    } else {
                        statusMessage = "Verificando/Criando pasta no Google Drive...";
                        consoleUpdate(NULL);
                        std::string fid = client.ensureFolderExists(client.getFolderName());
                        if (!fid.empty()) {
                            statusMessage = "Pasta pronta! ID: " + fid;
                        } else {
                            statusMessage = "Falha ao criar pasta no Google Drive.";
                        }
                    }
                    break;
                }
            }
        }

        // Renderizacao da Tela Principal
        printf("\x1b[2J\x1b[H"); // Limpa tela e posiciona no topo
        printf("\x1b[31;1m================================================================================\x1b[0m\n");
        printf("\x1b[37;1m          GOOGLE DRIVE CLOUD SAVE - GERENCIADOR NINTENDO SWITCH v1.1.0          \x1b[0m\n");
        printf("\x1b[31;1m================================================================================\x1b[0m\n\n");

        // Painel de Status
        printf(" \x1b[37;1mPAINEL DE STATUS:\x1b[0m\n");
        printf("   - Sysmodule Daemon:   %s\x1b[0m\n",
               sysmoduleRunning ? ("\x1b[32;1m[ ATIVO (PID: " + std::to_string(pid) + ") ]").c_str() : "\x1b[31;1m[ PARADO ]");
        printf("   - Inicio no Boot:     %s\x1b[0m\n",
               bootFlag ? "\x1b[32;1m[ ATIVADO (boot2.flag) ]" : "\x1b[90m[ DESATIVADO ]");
        printf("   - Internet (Wi-Fi):   %s\x1b[0m\n",
               internet ? "\x1b[32;1m[ CONECTADO ]" : "\x1b[31;1m[ DESCONECTADO ]");
        printf("   - Conta Google:       %s\x1b[0m\n",
               client.isAuthorized() ? "\x1b[32;1m[ VINCULADA & PRONTA ]" : "\x1b[33;1m[ NAO AUTORIZADA ]");
        printf("   - Pasta no Drive:     \x1b[36;1m\"%s\"\x1b[0m (ID: %s)\n\n",
               client.getFolderName().c_str(),
               client.getFolderId().empty() ? "\x1b[90mNao criada\x1b[0m" : client.getFolderId().c_str());

        printf(" \x1b[37;1mMENU DE OPCOES:\x1b[0m\n");
        const char* menuLabels[TOTAL_MENU_ITEMS] = {
            "1. Vincular Conta Google (OAuth 2.0 Device Flow na Tela)",
            "2. Iniciar / Parar Sysmodule em Segundo Plano",
            "3. Alternar Inicializacao no Boot (boot2.flag)",
            "4. Configurar Client ID (Teclado Virtual)",
            "5. Configurar Client Secret (Teclado Virtual)",
            "6. Alterar Nome da Pasta de Destino (Teclado Virtual)",
            "7. Criar / Validar Pasta de Saves no Google Drive Agora"
        };

        for (int i = 0; i < TOTAL_MENU_ITEMS; i++) {
            if (i == selectedItem) {
                printf("  \x1b[47;30;1m > %-72s < \x1b[0m\n", menuLabels[i]);
            } else {
                printf("    \x1b[37m%-74s\x1b[0m\n", menuLabels[i]);
            }
        }

        printf("\n--------------------------------------------------------------------------------\n");
        if (!statusMessage.empty()) {
            printf(" \x1b[33;1mAVISO:\x1b[0m %s\n", statusMessage.c_str());
        } else {
            printf("\n");
        }
        printf("--------------------------------------------------------------------------------\n");
        printf(" \x1b[36;1m[D-Pad]\x1b[0m Navegar   \x1b[36;1m[A]\x1b[0m Selecionar   \x1b[36;1m[X]\x1b[0m Ligar/Desligar   \x1b[36;1m[Y]\x1b[0m Toggle Boot   \x1b[36;1m[+]\x1b[0m Sair\n");

        consoleUpdate(NULL);
        svcSleepThread(50'000'000ULL); // ~20 FPS responsivo para navegacao
    }

    // Limpeza
    pmshellExit();
    pmdmntExit();
    nifmExit();
    socketExit();
    fsExit();
    smExit();
    consoleExit(NULL);

    return 0;
}

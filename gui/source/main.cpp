#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <unistd.h>
#include "../../source/Config.hpp"
#include "../../source/GDriveClient.hpp"

// ==============================================================================
// PALETA DE CORES ANSI PARA TUI MODERNA NO NINTENDO SWITCH
// ==============================================================================
namespace UI {
    // Joy-Con Neon Accent Colors
    constexpr const char* NEON_CYAN    = "\x1b[96;1m"; // Joy-Con Azul Neon
    constexpr const char* NEON_RED     = "\x1b[91;1m"; // Joy-Con Vermelho Neon
    constexpr const char* BRIGHT_WHITE = "\x1b[97;1m";
    constexpr const char* WHITE        = "\x1b[37m";
    constexpr const char* DIM_GRAY     = "\x1b[90m";
    constexpr const char* SUCCESS_GRN  = "\x1b[92;1m";
    constexpr const char* WARNING_YEL  = "\x1b[93;1m";
    constexpr const char* ACCENT_BLUE  = "\x1b[34;1m";
    constexpr const char* RESET        = "\x1b[0m";

    // Fundo invertido para selecao ativa
    constexpr const char* SEL_BG       = "\x1b[47;30;1m";
    constexpr const char* SEL_BG_CYAN  = "\x1b[46;30;1m";
    constexpr const char* BADGE_BG_GRN = "\x1b[42;30;1m";
    constexpr const char* BADGE_BG_RED = "\x1b[41;97;1m";
    constexpr const char* BADGE_BG_YEL = "\x1b[43;30;1m";
    constexpr const char* BADGE_BG_BLU = "\x1b[44;97;1m";

    // Formatacao de linhas com largura fixa de 80 colunas
    void clearScreen() {
        printf("\x1b[2J\x1b[H");
    }

    void cursorHome() {
        printf("\x1b[H");
    }
}

// Funcao para exibir o teclado virtual Horizon nativo
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
    loc.storageID  = NcmStorageId_None;

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

// ==============================================================================
// TELA DE AJUDA & GUIA DO USUARIO
// ==============================================================================
void showHelpScreen(PadState& pad) {
    UI::clearScreen();

    std::stringstream ss;
    ss << UI::NEON_CYAN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
    ss << UI::NEON_CYAN << "| " << UI::BRIGHT_WHITE << "GOOGLE DRIVE CLOUD SAVE  ::  GUIA DO USUARIO & AJUDA" << UI::NEON_CYAN << "                        |" << UI::RESET << "\n";
    ss << UI::NEON_CYAN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n\n";

    ss << UI::WARNING_YEL << " [ COMO FUNCIONA O BACKUP AUTOMATICO ]" << UI::RESET << "\n";
    ss << UI::WHITE << "  - O Sysmodule fica em execucao silenciosa consumindo menos de 3MB de RAM.\n";
    ss << "  - Ao fechar qualquer jogo, o daemon detecta o encerramento do processo,\n";
    ss << "    monta o save do usuario ativo, compacta em formato .ZIP e envia para a\n";
    ss << "    sua conta do Google Drive.\n";
    ss << "  - O sistema substitui versoes anteriores do mesmo jogo automaticamente (PATCH),\n";
    ss << "    evitando arquivos duplicados e economizando espaco na nuvem.\n\n";

    ss << UI::WARNING_YEL << " [ PRIMEIROS PASSOS ]" << UI::RESET << "\n";
    ss << UI::WHITE << "  1. Obtenha seu " << UI::NEON_CYAN << "Client ID" << UI::WHITE << " e " << UI::NEON_CYAN << "Client Secret" << UI::WHITE << " no Google Cloud Console (Device Flow).\n";
    ss << "  2. Insira-os pelas opcoes 5 e 6 do menu usando o teclado virtual.\n";
    ss << "  3. Selecione a opcao 1 para vincular sua conta exibindo o codigo na tela.\n";
    ss << "  4. Inicie o Sysmodule (opcao 2 ou botao [X]) e ative o auto-boot (opcao 3 / [Y]).\n\n";

    ss << UI::WARNING_YEL << " [ REQUISITO DE CERTIFICADOS SSL (HTTPS) ]" << UI::RESET << "\n";
    ss << UI::WHITE << "  - Baixe o arquivo oficial de certificados Mozilla (" << UI::NEON_CYAN << "cacert.pem" << UI::WHITE << ") e salve em:\n";
    ss << "    " << UI::BRIGHT_WHITE << "sdmc:/config/gdrive-sync/cacert.pem" << UI::WHITE << "\n\n";

    ss << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
    ss << UI::NEON_RED << "| " << UI::BRIGHT_WHITE << "Pressione qualquer botao [A, B, X, Y] para retornar ao menu principal..." << UI::NEON_RED << "   |" << UI::RESET << "\n";
    ss << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";

    printf("%s", ss.str().c_str());
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_Plus)) {
            break;
        }
        svcSleepThread(50'000'000ULL);
    }

    UI::clearScreen();
}

// ==============================================================================
// TELA DE AUTENTICACAO OAUTH 2.0 DEVICE FLOW
// ==============================================================================
void runDeviceAuthScreen(GDriveClient& client, PadState& pad) {
    UI::clearScreen();

    if (!isInternetConnected()) {
        std::stringstream ss;
        ss << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
        ss << UI::NEON_RED << "|  " << UI::BADGE_BG_RED << " ERRO DE CONEXAO " << UI::RESET << "  Nao foi possivel detectar conexao com a internet!      " << UI::NEON_RED << "|" << UI::RESET << "\n";
        ss << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n\n";
        ss << UI::WHITE << "  Por favor, certifique-se de que o seu Nintendo Switch esta conectado a\n";
        ss << "  uma rede Wi-Fi antes de iniciar o processo de autorizacao Google.\n\n";
        ss << UI::DIM_GRAY << "  Pressione [B] para retornar ao menu principal.\n" << UI::RESET;
        printf("%s", ss.str().c_str());
        consoleUpdate(NULL);

        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & (HidNpadButton_B | HidNpadButton_A)) break;
            svcSleepThread(50'000'000ULL);
        }
        UI::clearScreen();
        return;
    }

    if (!client.isConfigured()) {
        std::stringstream ss;
        ss << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
        ss << UI::NEON_RED << "|  " << UI::BADGE_BG_RED << " CREDENCIAIS AUSENTES " << UI::RESET << "  Client ID ou Client Secret nao configurados!   " << UI::NEON_RED << "|" << UI::RESET << "\n";
        ss << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n\n";
        ss << UI::WHITE << "  Voce precisa preencher as credenciais OAuth 2.0 nas opcoes [5] e [6] do menu\n";
        ss << "  ou no arquivo " << UI::BRIGHT_WHITE << "config.json" << UI::WHITE << " antes de solicitar a autorizacao.\n\n";
        ss << UI::DIM_GRAY << "  Pressione [B] para retornar ao menu principal.\n" << UI::RESET;
        printf("%s", ss.str().c_str());
        consoleUpdate(NULL);

        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & (HidNpadButton_B | HidNpadButton_A)) break;
            svcSleepThread(50'000'000ULL);
        }
        UI::clearScreen();
        return;
    }

    std::stringstream waitSS;
    waitSS << UI::NEON_CYAN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
    waitSS << UI::NEON_CYAN << "|  " << UI::BADGE_BG_BLU << " GOOGLE API " << UI::RESET << "  Conectando aos servidores do Google Drive...           " << UI::NEON_CYAN << "|" << UI::RESET << "\n";
    waitSS << UI::NEON_CYAN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n\n";
    waitSS << UI::WARNING_YEL << "  Aguarde solicitando codigo de autorizacao Device Flow...\n" << UI::RESET;
    printf("%s", waitSS.str().c_str());
    consoleUpdate(NULL);

    if (!client.requestDeviceCode()) {
        UI::clearScreen();
        std::stringstream failSS;
        failSS << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
        failSS << UI::NEON_RED << "|  " << UI::BADGE_BG_RED << " FALHA NA REQUISICAO " << UI::RESET << "  Google rejeitou o pedido de Device Code!      " << UI::NEON_RED << "|" << UI::RESET << "\n";
        failSS << UI::NEON_RED << "+------------------------------------------------------------------------------+" << UI::RESET << "\n\n";
        failSS << UI::WHITE << "  Motivos mais comuns:\n";
        failSS << "  1. Client ID ou Secret digitados incorretamente.\n";
        failSS << "  2. A 'Google Drive API' nao foi ativada no console do Google Cloud.\n";
        failSS << "  3. O certificado " << UI::NEON_CYAN << "cacert.pem" << UI::WHITE << " nao esta na pasta config.\n\n";
        failSS << UI::DIM_GRAY << "  Pressione [B] para retornar ao menu.\n" << UI::RESET;
        printf("%s", failSS.str().c_str());
        consoleUpdate(NULL);

        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & (HidNpadButton_B | HidNpadButton_A)) break;
            svcSleepThread(50'000'000ULL);
        }
        UI::clearScreen();
        return;
    }

    std::string userCode = client.getPendingUserCode();
    std::string verifyUrl = client.getPendingVerifyUrl();

    int animCounter = 0;
    const char* animFrames[] = {
        "[===                 ]",
        "[======              ]",
        "[=========           ]",
        "[============        ]",
        "[===============     ]",
        "[==================  ]",
        "[====================]"
    };
    constexpr int NUM_FRAMES = 7;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_B) {
            break;
        }

        // Tenta fazer o polling de confirmacao
        if (client.pollAuthorization()) {
            UI::clearScreen();
            std::stringstream succSS;
            succSS << UI::SUCCESS_GRN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
            succSS << UI::SUCCESS_GRN << "| " << UI::BADGE_BG_GRN << " SUCESSO ABSOLUTO " << UI::RESET << "  Conta Google vinculada com exito!                  " << UI::SUCCESS_GRN << "|" << UI::RESET << "\n";
            succSS << UI::SUCCESS_GRN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n\n";
            succSS << UI::WHITE << "  [OK] Os tokens OAuth 2.0 foram salvos com seguranca em:\n";
            succSS << "       " << UI::BRIGHT_WHITE << Config::CONFIG_FILE << UI::WHITE << "\n\n";
            succSS << "  [OK] O Sysmodule de background agora pode realizar o upload dos saves\n";
            succSS << "       de forma totalmente automatica!\n\n";
            succSS << UI::SUCCESS_GRN << "  Pressione [A] ou [B] para retornar ao painel principal.\n" << UI::RESET;
            printf("%s", succSS.str().c_str());
            consoleUpdate(NULL);

            while (appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & (HidNpadButton_A | HidNpadButton_B)) break;
                svcSleepThread(50'000'000ULL);
            }
            break;
        }

        // Renderiza instrucoes na tela
        UI::cursorHome();
        std::stringstream ss;
        ss << UI::NEON_CYAN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n";
        ss << UI::NEON_CYAN << "| " << UI::BRIGHT_WHITE << "VINCULAR CONTA GOOGLE DRIVE  ::  AUTORIZACAO NA TELA" << UI::NEON_CYAN << "                        |" << UI::RESET << "\n";
        ss << UI::NEON_CYAN << "+------------------------------------------------------------------------------+" << UI::RESET << "\n\n";

        ss << UI::WARNING_YEL << " Siga os 3 passos simples no seu Celular ou Computador:" << UI::RESET << "\n\n";
        ss << "  " << UI::NEON_CYAN << "1." << UI::WHITE << " Abra o navegador e acesse:  " << UI::BRIGHT_WHITE << verifyUrl << UI::RESET << "\n\n";
        ss << "  " << UI::NEON_CYAN << "2." << UI::WHITE << " Digite o codigo exibido abaixo:\n\n";

        // Caixa destacada para o codigo de ativacao
        ss << "       " << UI::NEON_CYAN << "+-----------------------------------------------+" << UI::RESET << "\n";
        ss << "       " << UI::NEON_CYAN << "| " << UI::SEL_BG << "             " << userCode << "             " << UI::RESET << UI::NEON_CYAN << " |" << UI::RESET << "\n";
        ss << "       " << UI::NEON_CYAN << "+-----------------------------------------------+" << UI::RESET << "\n\n";

        ss << "  " << UI::NEON_CYAN << "3." << UI::WHITE << " Faca login na sua conta Google e clique em " << UI::SUCCESS_GRN << "Permitir" << UI::WHITE << ".\n\n";
        ss << UI::DIM_GRAY << "--------------------------------------------------------------------------------" << UI::RESET << "\n";
        ss << "  Status: " << UI::WARNING_YEL << "Aguardando confirmacao do Google... " << animFrames[animCounter % NUM_FRAMES] << UI::RESET << "\n";
        ss << "  " << UI::DIM_GRAY << "[B] Cancelar e retornar ao menu" << UI::RESET << "\n";
        ss << UI::DIM_GRAY << "--------------------------------------------------------------------------------" << UI::RESET << "\n";

        printf("%s", ss.str().c_str());
        consoleUpdate(NULL);

        animCounter++;
        svcSleepThread(1'000'000'000ULL); // Polling a cada 1 segundo
    }

    UI::clearScreen();
}

// ==============================================================================
// PONTO DE ENTRADA PRINCIPAL DA GUI
// ==============================================================================
int main(int argc, char* argv[]) {
    // Inicializa console grafico ANSI nativo da libnx
    consoleInit(NULL);

    // Inicializacao dos servicos do Switch
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
    constexpr int TOTAL_MENU_ITEMS = 8;
    std::string statusNotification = "Bem-vindo ao Google Drive Cloud Save Suite!";
    std::string notificationType   = "INFO"; // INFO, SUCCESS, WARNING, ERROR

    UI::clearScreen();

    // Loop principal da aplicacao
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        // Sai do aplicativo
        if (kDown & HidNpadButton_Plus) {
            break;
        }

        // Navegacao com D-Pad ou Stick Esquerdo
        if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            selectedItem--;
            if (selectedItem < 0) selectedItem = TOTAL_MENU_ITEMS - 1;
        }
        if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            selectedItem++;
            if (selectedItem >= TOTAL_MENU_ITEMS) selectedItem = 0;
        }

        // Tela de ajuda
        if (kDown & (HidNpadButton_Minus | HidNpadButton_L)) {
            showHelpScreen(pad);
        }

        u64 pid = 0;
        bool sysmoduleRunning = isSysmoduleRunning(pid);
        bool bootFlag = Config::isBootFlagPresent();
        bool internet = isInternetConnected();

        // Atalho rapido [X]: Iniciar / Parar Sysmodule
        if (kDown & HidNpadButton_X) {
            if (sysmoduleRunning) {
                if (stopSysmodule()) {
                    statusNotification = "Sysmodule de background parado com sucesso!";
                    notificationType   = "SUCCESS";
                } else {
                    statusNotification = "Falha ao interromper o processo do Sysmodule.";
                    notificationType   = "ERROR";
                }
            } else {
                if (startSysmodule()) {
                    statusNotification = "Sysmodule iniciado com sucesso em segundo plano!";
                    notificationType   = "SUCCESS";
                } else {
                    statusNotification = "Falha ao disparar o executavel do Sysmodule.";
                    notificationType   = "ERROR";
                }
            }
        }

        // Atalho rapido [Y]: Alternar Inicializacao no Boot
        if (kDown & HidNpadButton_Y) {
            Config::setBootFlag(!bootFlag);
            if (!bootFlag) {
                statusNotification = "Inicializacao automatica no boot ATIVADA (boot2.flag criada).";
                notificationType   = "SUCCESS";
            } else {
                statusNotification = "Inicializacao automatica no boot DESATIVADA.";
                notificationType   = "WARNING";
            }
        }

        // Acao ao pressionar [A] no item selecionado
        if (kDown & HidNpadButton_A) {
            switch (selectedItem) {
                case 0: // 1. Vincular Conta Google
                    runDeviceAuthScreen(client, pad);
                    client.loadConfig();
                    break;

                case 1: // 2. Ligar / Desligar Sysmodule
                    if (sysmoduleRunning) {
                        if (stopSysmodule()) {
                            statusNotification = "Sysmodule parado com sucesso!";
                            notificationType   = "SUCCESS";
                        } else {
                            statusNotification = "Erro ao interromper o Sysmodule.";
                            notificationType   = "ERROR";
                        }
                    } else {
                        if (startSysmodule()) {
                            statusNotification = "Sysmodule iniciado em background!";
                            notificationType   = "SUCCESS";
                        } else {
                            statusNotification = "Erro ao iniciar o Sysmodule.";
                            notificationType   = "ERROR";
                        }
                    }
                    break;

                case 2: // 3. Alternar Boot
                    Config::setBootFlag(!bootFlag);
                    if (!bootFlag) {
                        statusNotification = "Auto-boot no ligar do Switch ATIVADO.";
                        notificationType   = "SUCCESS";
                    } else {
                        statusNotification = "Auto-boot DESATIVADO.";
                        notificationType   = "WARNING";
                    }
                    break;

                case 3: { // 4. Validar / Criar Pasta de Saves
                    if (!client.isAuthorized()) {
                        statusNotification = "Atencao: Vincule a sua conta Google primeiro (Opcao 1)!";
                        notificationType   = "WARNING";
                    } else if (!internet) {
                        statusNotification = "Erro: Conecte o Switch ao Wi-Fi para acessar o Google Drive!";
                        notificationType   = "ERROR";
                    } else {
                        statusNotification = "Verificando/Criando pasta no Google Drive...";
                        notificationType   = "INFO";
                        consoleUpdate(NULL);

                        std::string fid = client.ensureFolderExists(client.getFolderName());
                        if (!fid.empty()) {
                            statusNotification = "Pasta pronta no Google Drive! ID: " + fid;
                            notificationType   = "SUCCESS";
                        } else {
                            statusNotification = "Falha ao criar/buscar pasta. Verifique a conexao e credenciais.";
                            notificationType   = "ERROR";
                        }
                    }
                    break;
                }

                case 4: { // 5. Editar Client ID
                    std::string newId = showKeyboard("Digite o Google OAuth Client ID", client.getClientId());
                    client.setClientId(newId);
                    client.saveConfig();
                    statusNotification = "Client ID salvo com sucesso!";
                    notificationType   = "SUCCESS";
                    break;
                }

                case 5: { // 6. Editar Client Secret
                    std::string newSec = showKeyboard("Digite o Google OAuth Client Secret", client.getClientSecret());
                    client.setClientSecret(newSec);
                    client.saveConfig();
                    statusNotification = "Client Secret salvo com sucesso!";
                    notificationType   = "SUCCESS";
                    break;
                }

                case 6: { // 7. Editar Nome da Pasta
                    std::string newFolder = showKeyboard("Nome da pasta de saves no Google Drive", client.getFolderName());
                    client.setFolderName(newFolder);
                    client.saveConfig();
                    statusNotification = "Nome da pasta de destino atualizado!";
                    notificationType   = "SUCCESS";
                    break;
                }

                case 7: // 8. Ajuda e Informacoes
                    showHelpScreen(pad);
                    break;
            }
        }

        // ======================================================================
        // RENDERIZACAO FLICKER-FREE DA INTERFACE (SINGLE BUFFER AT CURSOR HOME)
        // ======================================================================
        UI::cursorHome();
        std::stringstream ss;

        // Header Superior com Joy-Con Neon Stripes
        ss << UI::NEON_CYAN << "[■■■] " << UI::BRIGHT_WHITE << "GOOGLE DRIVE CLOUD SAVE SUITE"
           << UI::DIM_GRAY << " | " << UI::NEON_CYAN << "Atmosphere CFW"
           << UI::DIM_GRAY << " | " << UI::WHITE << "v1.2.0"
           << "                 " << UI::NEON_RED << "[■■■]" << UI::RESET << "\n";
        ss << UI::DIM_GRAY << "--------------------------------------------------------------------------------" << UI::RESET << "\n";

        // Painel de Dashboard (Cards de Status)
        ss << " " << UI::NEON_CYAN << "[#] STATUS DO SERVICO & CONEXAO:" << UI::RESET << "\n";

        // Linha 1: Conta Google & Internet Wi-Fi
        ss << "   " << UI::WHITE << "Conta Google:  ";
        if (client.isAuthorized()) {
            ss << UI::SUCCESS_GRN << "[ CONECTADA & PRONTA ]" << UI::RESET;
        } else {
            ss << UI::WARNING_YEL << "[ NAO AUTORIZADA ]    " << UI::RESET;
        }
        ss << "      " << UI::WHITE << "Conexao Wi-Fi: ";
        if (internet) {
            ss << UI::SUCCESS_GRN << "[ ONLINE ]" << UI::RESET << "\n";
        } else {
            ss << UI::NEON_RED << "[ SEM INTERNET ]" << UI::RESET << "\n";
        }

        // Linha 2: Pasta no Google Drive & ID
        ss << "   " << UI::WHITE << "Pasta Drive:   " << UI::NEON_CYAN << "\"" << client.getFolderName() << "\"" << UI::RESET;
        if (!client.getFolderId().empty()) {
            ss << UI::DIM_GRAY << " (ID: " << client.getFolderId().substr(0, 16) << "...)" << UI::RESET;
        } else {
            ss << UI::DIM_GRAY << " (Criacao automatica no 1o upload)" << UI::RESET;
        }
        ss << "\n";

        // Linha 3: Sysmodule Daemon & Inicializacao no Boot
        ss << "   " << UI::WHITE << "Sysmodule:     ";
        if (sysmoduleRunning) {
            ss << UI::SUCCESS_GRN << "[ ATIVO (PID: " << pid << ") ]" << UI::RESET;
        } else {
            ss << UI::NEON_RED << "[ PARADO ]            " << UI::RESET;
        }
        ss << "      " << UI::WHITE << "Auto-Boot:     ";
        if (bootFlag) {
            ss << UI::SUCCESS_GRN << "[ ATIVADO ]" << UI::RESET << "\n";
        } else {
            ss << UI::DIM_GRAY << "[ DESATIVADO ]" << UI::RESET << "\n";
        }

        ss << UI::DIM_GRAY << "--------------------------------------------------------------------------------" << UI::RESET << "\n";

        // Menu de Opcoes
        ss << " " << UI::NEON_CYAN << "[+] ACOES & CONFIGURACOES:" << UI::RESET << "\n";

        struct MenuItem {
            const char* tag;
            const char* title;
            const char* desc;
        };

        const MenuItem menu[TOTAL_MENU_ITEMS] = {
            {"[1]", "Vincular Conta Google (Device Flow na Tela)", "Exibe codigo e link para login no celular/PC"},
            {"[2]", "Iniciar / Parar Sysmodule em Segundo Plano", "Alterna o daemon de monitoramento de saves"},
            {"[3]", "Alternar Inicializacao no Boot (boot2.flag)",  "Inicia automaticamente ao ligar o console"},
            {"[4]", "Validar / Criar Pasta de Saves no Drive",      "Testa permissao e assegura o diretorio de backup"},
            {"[5]", "Configurar Google OAuth Client ID",           "Insira a chave Client ID pelo teclado virtual"},
            {"[6]", "Configurar Google OAuth Client Secret",       "Insira a chave Client Secret pelo teclado virtual"},
            {"[7]", "Alterar Nome da Pasta de Destino",            "Defina o nome da pasta na raiz do Google Drive"},
            {"[8]", "Guia do Usuario & Dicas de Uso",              "Como funciona o backup automatico de saves"}
        };

        for (int i = 0; i < TOTAL_MENU_ITEMS; i++) {
            if (i == selectedItem) {
                ss << "  " << UI::SEL_BG << " >> " << menu[i].tag << " " << menu[i].title;
                // Preenche o restante da linha para visual moderno de selecao
                int currentLen = 8 + strlen(menu[i].tag) + strlen(menu[i].title);
                for (int pad = currentLen; pad < 76; pad++) ss << " ";
                ss << "<< " << UI::RESET << "\n";
                ss << "       " << UI::NEON_CYAN << "-> " << menu[i].desc << UI::RESET << "\n";
            } else {
                ss << "     " << UI::WHITE << menu[i].tag << " " << menu[i].title << UI::RESET << "\n";
            }
        }

        ss << UI::DIM_GRAY << "--------------------------------------------------------------------------------" << UI::RESET << "\n";

        // Notificacao / Feedback Banner
        ss << " ";
        if (notificationType == "SUCCESS") {
            ss << UI::BADGE_BG_GRN << " SUCESSO " << UI::RESET << " " << UI::SUCCESS_GRN << statusNotification;
        } else if (notificationType == "WARNING") {
            ss << UI::BADGE_BG_YEL << " ATENCAO " << UI::RESET << " " << UI::WARNING_YEL << statusNotification;
        } else if (notificationType == "ERROR") {
            ss << UI::BADGE_BG_RED << " ERRO " << UI::RESET << " " << UI::NEON_RED << statusNotification;
        } else {
            ss << UI::BADGE_BG_BLU << " INFO " << UI::RESET << " " << UI::WHITE << statusNotification;
        }
        ss << UI::RESET << "\n";

        ss << UI::DIM_GRAY << "--------------------------------------------------------------------------------" << UI::RESET << "\n";

        // Barra Inferior de Atalhos com botoes estilizados
        ss << " " << UI::NEON_CYAN << "(A)" << UI::WHITE << " Executar  "
           << UI::NEON_CYAN << "(B)" << UI::WHITE << " Voltar  "
           << UI::NEON_CYAN << "(X)" << UI::WHITE << " Ligar/Parar Daemon  "
           << UI::NEON_CYAN << "(Y)" << UI::WHITE << " Auto-Boot  "
           << UI::NEON_CYAN << "(-)" << UI::WHITE << " Ajuda  "
           << UI::NEON_RED  << "(+)" << UI::WHITE << " Sair" << UI::RESET << "\n";

        printf("%s", ss.str().c_str());
        consoleUpdate(NULL);

        svcSleepThread(50'000'000ULL); // ~20 FPS responsivo para input e sem consumo abusivo
    }

    // Limpeza de servicos ao fechar o app
    pmshellExit();
    pmdmntExit();
    nifmExit();
    socketExit();
    fsExit();
    smExit();
    consoleExit(NULL);

    return 0;
}

#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <unistd.h>
#include "Renderer2D.hpp"
#include "../../source/Config.hpp"
#include "../../source/GDriveClient.hpp"

// ==============================================================================
// TECLADO VIRTUAL NATIVO DO HORIZON OS (swkbd)
// ==============================================================================
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

// ==============================================================================
// CONTROLE DO PROCESSO DO SYSMODULE (pmdmnt / pmshell)
// ==============================================================================
bool isSysmoduleRunning(u64& outPid) {
    outPid = 0;
    Result rc = pmdmntGetProcessId(&outPid, Config::SYSMODULE_TITLE_ID);
    return (R_SUCCEEDED(rc) && outPid != 0);
}

bool startSysmodule(std::string& outMsg) {
    NcmProgramLocation loc = {};
    loc.program_id = Config::SYSMODULE_TITLE_ID;
    loc.storageID  = NcmStorageId_None;

    u64 pid = 0;
    Result rc = pmshellLaunchProgram(0, &loc, &pid);
    if (R_FAILED(rc)) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Erro 0x%X (Abra via Title Override [R] ou use Auto-Boot)", rc);
        outMsg = errBuf;
        return false;
    }

    outMsg = "Sysmodule disparado com sucesso! PID: " + std::to_string(pid);
    return true;
}

bool stopSysmodule(std::string& outMsg) {
    Result rc = pmshellTerminateProgram(Config::SYSMODULE_TITLE_ID);
    if (R_FAILED(rc)) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Erro 0x%X ao encerrar sysmodule", rc);
        outMsg = errBuf;
        return false;
    }

    outMsg = "Sysmodule encerrado com sucesso!";
    return true;
}

bool isInternetConnected() {
    NifmInternetConnectionType connType;
    u32 wifiState = 0;
    NifmInternetConnectionStatus connStatus;

    Result rc = nifmGetInternetConnectionStatus(&connType, &wifiState, &connStatus);
    return (R_SUCCEEDED(rc) && connStatus == NifmInternetConnectionStatus_Connected);
}

// ==============================================================================
// MODAL: AUTENTICACAO OAUTH 2.0 DEVICE FLOW
// ==============================================================================
void runDeviceAuthModal(Gfx::Display& display, GDriveClient& client, PadState& pad) {
    if (!isInternetConnected()) {
        return;
    }

    if (!client.isConfigured()) {
        return;
    }

    // Solicita o Device Code da Google API
    if (!client.requestDeviceCode()) {
        return;
    }

    std::string userCode = client.getPendingUserCode();
    std::string verifyUrl = client.getPendingVerifyUrl();

    int animProgress = 0;
    u64 lastPollTime = armTicksToNs(armGetSystemTick());

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        // Cancelar com (B)
        if (kDown & HidNpadButton_B) {
            break;
        }

        // Checagem periódica do token (a cada 2 segundos)
        u64 now = armTicksToNs(armGetSystemTick());
        if (now - lastPollTime >= 2'000'000'000ULL) {
            lastPollTime = now;
            if (client.pollAuthorization()) {
                client.saveConfig();
                break;
            }
        }

        if (display.beginFrame()) {
            // Fundo escurecido semi-transparente
            display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

            // Card Central da Modal (800x480)
            int mx = 240, my = 110, mw = 800, mh = 480;
            display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_CYAN, 2);

            // Header da Modal com Logotipo do Google Drive
            display.drawGoogleDriveIcon(mx + 50, my + 45, 40);
            display.drawText(mx + 90, my + 30, "VINCULAR CONTA GOOGLE DRIVE", Gfx::Colors::TEXT_WHITE, 2, true);
            display.drawText(mx + 90, my + 64, "Autorizacao de Dispositivo (OAuth 2.0 Device Flow)", Gfx::Colors::TEXT_MUTED, 1);

            // Linha divisória
            display.fillRect(mx + 30, my + 95, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);

            // Passos de Instrução
            display.drawText(mx + 50, my + 120, "1. Abra o navegador no seu celular ou PC e acesse:", Gfx::Colors::TEXT_WHITE, 1);
            display.drawText(mx + 70, my + 145, verifyUrl, Gfx::Colors::JOYCON_CYAN, 2, true);

            display.drawText(mx + 50, my + 195, "2. Digite o codigo de seguranca:", Gfx::Colors::TEXT_WHITE, 1);

            // Caixa Destacada do Código
            int codeBoxX = mx + 160, codeBoxY = my + 225, codeBoxW = 480, codeBoxH = 75;
            display.fillRoundedRect(codeBoxX, codeBoxY, codeBoxW, codeBoxH, 12, Gfx::Colors::CARD_HOVER_BG);
            display.drawRoundedRectOutline(codeBoxX, codeBoxY, codeBoxW, codeBoxH, 12, Gfx::Colors::JOYCON_YELLOW, 2);
            
            // Centraliza o código na caixa
            int textW = userCode.length() * 16;
            int codeX = codeBoxX + (codeBoxW - textW) / 2;
            display.drawText(codeX, codeBoxY + 22, userCode, Gfx::Colors::DRIVE_YELLOW, 2, true);

            display.drawText(mx + 50, my + 325, "3. Faca login na sua conta Google e confirme a permissao.", Gfx::Colors::TEXT_WHITE, 1);

            // Barra de Progresso / Aguardando
            int barX = mx + 50, barY = my + 380, barW = mw - 100, barH = 10;
            display.fillRoundedRect(barX, barY, barW, barH, 5, Gfx::Colors::CARD_OUTLINE);
            int fillW = (animProgress % (barW - 20)) + 20;
            display.fillRoundedRect(barX, barY, fillW, barH, 5, Gfx::Colors::STATUS_GREEN);

            display.drawText(mx + 50, my + 405, "Aguardando confirmacao do Google...", Gfx::Colors::TEXT_MUTED, 1);

            // Botão Voltar no Rodapé
            display.drawControllerButtonBadge(mx + mw - 160, my + mh - 45, "B", Gfx::Colors::JOYCON_GRAY);
            display.drawText(mx + mw - 130, my + mh - 42, "Cancelar", Gfx::Colors::TEXT_WHITE, 1);

            display.endFrame();
        }

        animProgress += 4;
        svcSleepThread(16'000'000ULL); // ~60 FPS
    }
}

// ==============================================================================
// MODAL: GUIA RAPIDO E AJUDA
// ==============================================================================
void runHelpModal(Gfx::Display& display, PadState& pad) {
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus | HidNpadButton_Minus)) {
            break;
        }

        if (display.beginFrame()) {
            display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

            int mx = 180, my = 70, mw = 920, mh = 570;
            display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_CYAN, 2);

            display.drawCloudSaveIcon(mx + 50, my + 45, 34, Gfx::Colors::JOYCON_CYAN);
            display.drawText(mx + 90, my + 30, "GUIA DO USUARIO & AJUDA", Gfx::Colors::TEXT_WHITE, 2, true);
            display.drawText(mx + 90, my + 64, "Google Drive Cloud Save Suite para Nintendo Switch", Gfx::Colors::TEXT_MUTED, 1);

            display.fillRect(mx + 30, my + 95, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);

            int ty = my + 115;
            display.drawText(mx + 40, ty, "COMO FUNCIONA O BACKUP AUTOMATICO:", Gfx::Colors::JOYCON_YELLOW, 1, true);
            ty += 24;
            display.drawText(mx + 50, ty, "- O Sysmodule roda em background consumindo menos de 3MB de RAM.", Gfx::Colors::TEXT_WHITE, 1);
            ty += 20;
            display.drawText(mx + 50, ty, "- Ao encerrar qualquer jogo, o processo e detectado automaticamente.", Gfx::Colors::TEXT_WHITE, 1);
            ty += 20;
            display.drawText(mx + 50, ty, "- Os dados do save sao compactados em .zip e enviados ao Google Drive.", Gfx::Colors::TEXT_WHITE, 1);
            ty += 20;
            display.drawText(mx + 50, ty, "- Saves antigos sao atualizados (PATCH), sem arquivos duplicados.", Gfx::Colors::TEXT_WHITE, 1);

            ty += 34;
            display.drawText(mx + 40, ty, "REQUISITOS E DICAS IMPORTANTES:", Gfx::Colors::JOYCON_YELLOW, 1, true);
            ty += 24;
            display.drawText(mx + 50, ty, "1. Configure seu Client ID e Secret nas opcoes [5] e [6].", Gfx::Colors::TEXT_WHITE, 1);
            ty += 20;
            display.drawText(mx + 50, ty, "2. Certificados SSL (cacert.pem) ficam em: sdmc:/config/gdrive-sync/cacert.pem", Gfx::Colors::TEXT_WHITE, 1);
            ty += 20;
            display.drawText(mx + 50, ty, "3. Para iniciar o Sysmodule via interface sem erros, abra o Homebrew Menu", Gfx::Colors::TEXT_WHITE, 1);
            ty += 20;
            display.drawText(mx + 65, ty, "segurando [R] sobre qualquer jogo (modo Title Override / sem limitacao).", Gfx::Colors::JOYCON_CYAN, 1);
            ty += 20;
            display.drawText(mx + 50, ty, "4. Ative o Auto-Boot [Y] para o daemon iniciar sempre com o console.", Gfx::Colors::TEXT_WHITE, 1);

            display.drawControllerButtonBadge(mx + mw - 160, my + mh - 45, "B", Gfx::Colors::JOYCON_GRAY);
            display.drawText(mx + mw - 130, my + mh - 42, "Fechar", Gfx::Colors::TEXT_WHITE, 1);

            display.endFrame();
        }

        svcSleepThread(16'000'000ULL);
    }
}

// ==============================================================================
// PONTO DE ENTRADA PRINCIPAL DA INTERFACE GRAFICA
// ==============================================================================
int main(int argc, char* argv[]) {
    // Inicialização dos serviços essenciais do Horizon OS
    smInitialize();
    fsInitialize();
    fsdevMountSdmc();
    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);
    pmdmntInitialize();
    pmshellInitialize();

    Config::ensureDirectories();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    Gfx::Display display;
    if (!display.init()) {
        return 1;
    }

    GDriveClient client;

    int selectedItem = 0;
    constexpr int TOTAL_MENU_ITEMS = 7;
    std::string toastMessage = "Pronto! Selecione uma opcao para iniciar.";
    u32 toastColor = Gfx::Colors::STATUS_GREEN;

    // Cache de status para renderização 60 FPS
    u64 lastStatusCheck = 0;
    u64 currentPid = 0;
    bool sysmoduleActive = false;
    bool bootFlagActive  = false;
    bool wifiActive      = false;

    // Loop gráfico principal
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        // [+] Sair
        if (kDown & HidNpadButton_Plus) {
            break;
        }

        // Navegação
        if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            selectedItem--;
            if (selectedItem < 0) selectedItem = TOTAL_MENU_ITEMS - 1;
        }
        if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            selectedItem++;
            if (selectedItem >= TOTAL_MENU_ITEMS) selectedItem = 0;
        }

        // Atualização periódica de status (a cada 1 segundo)
        u64 now = armTicksToNs(armGetSystemTick());
        if (now - lastStatusCheck >= 1'000'000'000ULL) {
            lastStatusCheck = now;
            sysmoduleActive = isSysmoduleRunning(currentPid);
            bootFlagActive  = Config::isBootFlagPresent();
            wifiActive      = isInternetConnected();
        }

        // Atalho rápido [X]: Alternar Sysmodule
        if (kDown & HidNpadButton_X) {
            if (sysmoduleActive) {
                if (stopSysmodule(toastMessage)) {
                    toastColor = Gfx::Colors::STATUS_GREEN;
                } else {
                    toastColor = Gfx::Colors::STATUS_RED;
                }
            } else {
                if (startSysmodule(toastMessage)) {
                    toastColor = Gfx::Colors::STATUS_GREEN;
                } else {
                    toastColor = Gfx::Colors::STATUS_RED;
                }
            }
            sysmoduleActive = isSysmoduleRunning(currentPid);
        }

        // Atalho rápido [Y]: Alternar Boot Flag
        if (kDown & HidNpadButton_Y) {
            Config::setBootFlag(!bootFlagActive);
            bootFlagActive = Config::isBootFlagPresent();
            if (bootFlagActive) {
                toastMessage = "Auto-Boot no ligar do Switch ATIVADO (boot2.flag criada).";
                toastColor   = Gfx::Colors::STATUS_GREEN;
            } else {
                toastMessage = "Auto-Boot DESATIVADO (boot2.flag removida).";
                toastColor   = Gfx::Colors::STATUS_YELLOW;
            }
        }

        // Atalho rápido [-]: Ajuda
        if (kDown & (HidNpadButton_Minus | HidNpadButton_L)) {
            runHelpModal(display, pad);
        }

        // Confirmação com [A]
        if (kDown & HidNpadButton_A) {
            switch (selectedItem) {
                case 0: // Vincular Conta Google
                    if (!wifiActive) {
                        toastMessage = "Erro: Conecte o Switch a rede Wi-Fi antes de vincular!";
                        toastColor   = Gfx::Colors::STATUS_RED;
                    } else if (!client.isConfigured()) {
                        toastMessage = "Atencao: Preencha o Client ID e Secret primeiro (opcoes 4 e 5)!";
                        toastColor   = Gfx::Colors::STATUS_YELLOW;
                    } else {
                        runDeviceAuthModal(display, client, pad);
                        client.loadConfig();
                        if (client.isAuthorized()) {
                            toastMessage = "Sucesso! Conta Google vinculada com exito!";
                            toastColor   = Gfx::Colors::STATUS_GREEN;
                        }
                    }
                    break;

                case 1: // Iniciar/Parar Sysmodule
                    if (sysmoduleActive) {
                        if (stopSysmodule(toastMessage)) {
                            toastColor = Gfx::Colors::STATUS_GREEN;
                        } else {
                            toastColor = Gfx::Colors::STATUS_RED;
                        }
                    } else {
                        if (startSysmodule(toastMessage)) {
                            toastColor = Gfx::Colors::STATUS_GREEN;
                        } else {
                            toastColor = Gfx::Colors::STATUS_RED;
                        }
                    }
                    sysmoduleActive = isSysmoduleRunning(currentPid);
                    break;

                case 2: // Toggle Boot
                    Config::setBootFlag(!bootFlagActive);
                    bootFlagActive = Config::isBootFlagPresent();
                    toastMessage = bootFlagActive ? "Auto-boot ATIVADO!" : "Auto-boot DESATIVADO!";
                    toastColor   = bootFlagActive ? Gfx::Colors::STATUS_GREEN : Gfx::Colors::STATUS_YELLOW;
                    break;

                case 3: { // Validar/Criar Pasta
                    if (!client.isAuthorized()) {
                        toastMessage = "Erro: Vincule sua conta Google primeiro (opcao 1)!";
                        toastColor   = Gfx::Colors::STATUS_YELLOW;
                    } else if (!wifiActive) {
                        toastMessage = "Erro: Sem conexao com a internet!";
                        toastColor   = Gfx::Colors::STATUS_RED;
                    } else {
                        toastMessage = "Verificando pasta no Google Drive...";
                        std::string fid = client.ensureFolderExists(client.getFolderName());
                        if (!fid.empty()) {
                            toastMessage = "Pasta pronta no Google Drive! ID: " + fid.substr(0, 16) + "...";
                            toastColor   = Gfx::Colors::STATUS_GREEN;
                        } else {
                            toastMessage = "Falha ao criar/buscar pasta. Verifique a conexao e credenciais.";
                            toastColor   = Gfx::Colors::STATUS_RED;
                        }
                    }
                    break;
                }

                case 4: { // Editar Client ID
                    std::string newId = showKeyboard("Digite o Google OAuth Client ID", client.getClientId());
                    client.setClientId(newId);
                    client.saveConfig();
                    toastMessage = "Client ID salvo com sucesso!";
                    toastColor   = Gfx::Colors::STATUS_GREEN;
                    break;
                }

                case 5: { // Editar Client Secret
                    std::string newSec = showKeyboard("Digite o Google OAuth Client Secret", client.getClientSecret());
                    client.setClientSecret(newSec);
                    client.saveConfig();
                    toastMessage = "Client Secret salvo com sucesso!";
                    toastColor   = Gfx::Colors::STATUS_GREEN;
                    break;
                }

                case 6: { // Editar Nome da Pasta
                    std::string newFolder = showKeyboard("Nome da pasta de saves no Google Drive", client.getFolderName());
                    client.setFolderName(newFolder);
                    client.saveConfig();
                    toastMessage = "Nome da pasta de destino atualizado!";
                    toastColor   = Gfx::Colors::STATUS_GREEN;
                    break;
                }
            }
        }

        // ======================================================================
        // RENDERIZACAO GRAFICA 2D DA INTERFACE (60 FPS NATIVO)
        // ======================================================================
        if (display.beginFrame()) {
            // 1. Fundo Gradiente Escuro
            display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

            // 2. Barra Superior (Header)
            display.fillRect(0, 0, Gfx::SCREEN_W, 70, Gfx::Colors::HEADER_BG);
            display.fillRect(0, 68, Gfx::SCREEN_W, 2, Gfx::Colors::CARD_OUTLINE);

            // Joy-Con Pílulas de Acento nos cantos
            display.fillRoundedRect(24, 18, 6, 34, 3, Gfx::Colors::JOYCON_CYAN);
            display.fillRoundedRect(Gfx::SCREEN_W - 30, 18, 6, 34, 3, Gfx::Colors::JOYCON_RED);

            // Título e Versão
            display.drawText(42, 16, "GOOGLE DRIVE CLOUD SAVE", Gfx::Colors::TEXT_WHITE, 2, true);
            display.drawText(44, 46, "Atmosphere CFW  •  Suite de Sincronizacao v1.2.0", Gfx::Colors::TEXT_MUTED, 1);

            // Badges de Status no Header (Direita)
            // Wi-Fi
            int wifiBadgeX = Gfx::SCREEN_W - 320;
            display.fillRoundedRect(wifiBadgeX, 20, 120, 30, 15, wifiActive ? Gfx::Colors::STATUS_GREEN : Gfx::Colors::STATUS_RED);
            display.drawWifiIcon(wifiBadgeX + 16, 28, 14, Gfx::Colors::TEXT_WHITE);
            display.drawText(wifiBadgeX + 38, 27, wifiActive ? "ONLINE" : "OFFLINE", Gfx::Colors::TEXT_WHITE, 1, true);

            // Google
            int gBadgeX = Gfx::SCREEN_W - 180;
            display.fillRoundedRect(gBadgeX, 20, 135, 30, 15, client.isAuthorized() ? Gfx::Colors::JOYCON_BLUE : Gfx::Colors::STATUS_YELLOW);
            display.drawText(gBadgeX + 16, 27, client.isAuthorized() ? "VINCULADO" : "PENDENTE", Gfx::Colors::TEXT_WHITE, 1, true);

            // 3. Coluna Esquerda: Cards de Status (Dashboard)
            int leftColX = 30, leftColW = 440;

            // Card 1: Google Drive Status
            int card1Y = 90, card1H = 265;
            display.fillRoundedRect(leftColX, card1Y, leftColW, card1H, 14, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(leftColX, card1Y, leftColW, card1H, 14, Gfx::Colors::CARD_OUTLINE, 1);

            display.drawGoogleDriveIcon(leftColX + 30, card1Y + 30, 28);
            display.drawText(leftColX + 56, card1Y + 22, "GOOGLE DRIVE", Gfx::Colors::TEXT_WHITE, 1, true);
            display.fillRect(leftColX + 20, card1Y + 52, leftColW - 40, 1, Gfx::Colors::CARD_OUTLINE);

            int rY = card1Y + 68;
            display.drawText(leftColX + 24, rY, "Status da Conta:", Gfx::Colors::TEXT_MUTED, 1);
            if (client.isAuthorized()) {
                display.drawText(leftColX + 180, rY, "Conectada & Pronta", Gfx::Colors::STATUS_GREEN, 1, true);
            } else {
                display.drawText(leftColX + 180, rY, "Nao Vinculada", Gfx::Colors::STATUS_YELLOW, 1, true);
            }

            rY += 32;
            display.drawText(leftColX + 24, rY, "Pasta de Saves:", Gfx::Colors::TEXT_MUTED, 1);
            display.drawText(leftColX + 180, rY, "\"" + client.getFolderName() + "\"", Gfx::Colors::JOYCON_CYAN, 1, true);

            rY += 32;
            display.drawText(leftColX + 24, rY, "ID da Pasta:", Gfx::Colors::TEXT_MUTED, 1);
            std::string fidStr = client.getFolderId().empty() ? "(Sera criada no 1o upload)" : client.getFolderId().substr(0, 18) + "...";
            display.drawText(leftColX + 180, rY, fidStr, Gfx::Colors::TEXT_WHITE, 1);

            rY += 32;
            display.drawText(leftColX + 24, rY, "Certificados SSL:", Gfx::Colors::TEXT_MUTED, 1);
            display.drawText(leftColX + 180, rY, "cacert.pem Ativo", Gfx::Colors::STATUS_GREEN, 1);

            rY += 32;
            display.drawText(leftColX + 24, rY, "Metodo de Upload:", Gfx::Colors::TEXT_MUTED, 1);
            display.drawText(leftColX + 180, rY, "PATCH (Sem duplicatas)", Gfx::Colors::TEXT_WHITE, 1);

            // Card 2: Background Daemon Status
            int card2Y = 370, card2H = 260;
            display.fillRoundedRect(leftColX, card2Y, leftColW, card2H, 14, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(leftColX, card2Y, leftColW, card2H, 14, Gfx::Colors::CARD_OUTLINE, 1);

            display.drawCloudSaveIcon(leftColX + 32, card2Y + 28, 22, Gfx::Colors::JOYCON_CYAN);
            display.drawText(leftColX + 56, card2Y + 22, "SYSMODULE DAEMON", Gfx::Colors::TEXT_WHITE, 1, true);
            display.fillRect(leftColX + 20, card2Y + 52, leftColW - 40, 1, Gfx::Colors::CARD_OUTLINE);

            rY = card2Y + 68;
            display.drawText(leftColX + 24, rY, "Execucao:", Gfx::Colors::TEXT_MUTED, 1);
            if (sysmoduleActive) {
                std::string pidStr = "ATIVO (PID " + std::to_string(currentPid) + ")";
                display.drawText(leftColX + 180, rY, pidStr, Gfx::Colors::STATUS_GREEN, 1, true);
            } else {
                display.drawText(leftColX + 180, rY, "PARADO", Gfx::Colors::STATUS_RED, 1, true);
            }

            rY += 32;
            display.drawText(leftColX + 24, rY, "Inicio no Boot:", Gfx::Colors::TEXT_MUTED, 1);
            if (bootFlagActive) {
                display.drawText(leftColX + 180, rY, "ATIVADO (boot2.flag)", Gfx::Colors::STATUS_GREEN, 1, true);
            } else {
                display.drawText(leftColX + 180, rY, "DESATIVADO", Gfx::Colors::TEXT_MUTED, 1);
            }

            rY += 32;
            display.drawText(leftColX + 24, rY, "Consumo de RAM:", Gfx::Colors::TEXT_MUTED, 1);
            display.drawText(leftColX + 180, rY, "< 3 MiB (Ultra leve)", Gfx::Colors::TEXT_WHITE, 1);

            rY += 32;
            display.drawText(leftColX + 24, rY, "Monitoramento:", Gfx::Colors::TEXT_MUTED, 1);
            display.drawText(leftColX + 24, rY + 20, "Detecta saves automaticamente ao fechar jogos", Gfx::Colors::JOYCON_CYAN, 1);

            // 4. Coluna Direita: Menu de Ações Interativo
            int rightColX = 495, rightColW = 755;
            int menuBoxY = 90, menuBoxH = 540;
            display.fillRoundedRect(rightColX, menuBoxY, rightColW, menuBoxH, 14, Gfx::Colors::HEADER_BG);
            display.drawRoundedRectOutline(rightColX, menuBoxY, rightColW, menuBoxH, 14, Gfx::Colors::CARD_OUTLINE, 1);

            display.drawText(rightColX + 24, menuBoxY + 18, "ACOES E CONFIGURACOES", Gfx::Colors::TEXT_MUTED, 1, true);

            struct MenuItem {
                const char* title;
                const char* desc;
            };

            const MenuItem menuItems[TOTAL_MENU_ITEMS] = {
                {"Vincular Conta Google (Device Flow na Tela)", "Exibe o codigo de confirmacao para ativar no celular ou PC"},
                {"Iniciar / Parar Sysmodule em Segundo Plano", "Alterna o processo daemon de monitoramento de saves"},
                {"Alternar Inicializacao no Boot (boot2.flag)",  "Inicia automaticamente o daemon toda vez que ligar o Switch"},
                {"Validar / Criar Pasta de Saves no Google Drive", "Testa a conexao com a API e assegura a pasta destino"},
                {"Configurar Google OAuth Client ID",           "Insira a chave Client ID usando o teclado virtual Horizon"},
                {"Configurar Google OAuth Client Secret",       "Insira o Client Secret usando o teclado virtual Horizon"},
                {"Alterar Nome da Pasta de Destino no Drive",    "Altere o nome da pasta principal de backup no Drive"}
            };

            int itemY = menuBoxY + 44;
            int itemH = 58;

            for (int i = 0; i < TOTAL_MENU_ITEMS; i++) {
                bool isSelected = (i == selectedItem);
                int cardX = rightColX + 16;
                int cardW = rightColW - 32;

                if (isSelected) {
                    display.fillRoundedRect(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_HOVER_BG);
                    display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_HOVER_OUT, 2);

                    // Indicador de seleção
                    display.fillRoundedRect(cardX + 4, itemY + 8, 4, itemH - 16, 2, Gfx::Colors::JOYCON_CYAN);

                    // Título e Descrição
                    display.drawText(cardX + 24, itemY + 11, menuItems[i].title, Gfx::Colors::TEXT_WHITE, 1, true);
                    display.drawText(cardX + 24, itemY + 33, menuItems[i].desc, Gfx::Colors::JOYCON_CYAN, 1);
                } else {
                    display.fillRoundedRect(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_BG);
                    display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_OUTLINE, 1);

                    display.drawText(cardX + 24, itemY + 11, menuItems[i].title, Gfx::Colors::TEXT_WHITE, 1);
                    display.drawText(cardX + 24, itemY + 33, menuItems[i].desc, Gfx::Colors::TEXT_MUTED, 1);
                }

                itemY += itemH + 8;
            }

            // Barra de Notificação / Toast no fundo da coluna direita
            int toastY = menuBoxY + menuBoxH - 62;
            display.fillRoundedRect(rightColX + 16, toastY, rightColW - 32, 46, 8, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(rightColX + 16, toastY, rightColW - 32, 46, 8, toastColor, 1);
            display.fillCircle(rightColX + 36, toastY + 23, 6, toastColor);
            display.drawText(rightColX + 54, toastY + 15, toastMessage, Gfx::Colors::TEXT_WHITE, 1, true);

            // 5. Barra Inferior de Legenda de Controles (Footer)
            int footerY = 665;
            display.fillRect(0, footerY - 10, Gfx::SCREEN_W, 65, Gfx::Colors::HEADER_BG);
            display.fillRect(0, footerY - 11, Gfx::SCREEN_W, 1, Gfx::Colors::CARD_OUTLINE);

            int btnX = 30;
            display.drawControllerButtonBadge(btnX, footerY, "A", Gfx::Colors::JOYCON_RED);
            display.drawText(btnX + 30, footerY + 4, "Selecionar", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 150;
            display.drawControllerButtonBadge(btnX, footerY, "B", Gfx::Colors::JOYCON_GRAY);
            display.drawText(btnX + 30, footerY + 4, "Voltar", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 130;
            display.drawControllerButtonBadge(btnX, footerY, "X", Gfx::Colors::JOYCON_BLUE);
            display.drawText(btnX + 30, footerY + 4, "Ligar/Parar Daemon", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 210;
            display.drawControllerButtonBadge(btnX, footerY, "Y", Gfx::Colors::JOYCON_YELLOW);
            display.drawText(btnX + 30, footerY + 4, "Auto-Boot", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 150;
            display.drawPillButtonBadge(btnX, footerY + 2, "-");
            display.drawText(btnX + 30, footerY + 4, "Ajuda", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 120;
            display.drawPillButtonBadge(btnX, footerY + 2, "+");
            display.drawText(btnX + 30, footerY + 4, "Sair", Gfx::Colors::TEXT_WHITE, 1);

            display.endFrame();
        }

        svcSleepThread(16'000'000ULL); // ~60 FPS fluído
    }

    // Finalização e limpeza dos serviços
    display.close();
    pmshellExit();
    pmdmntExit();
    nifmExit();
    socketExit();
    fsdevUnmountAll();
    fsExit();
    smExit();

    return 0;
}

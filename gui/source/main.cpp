#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "TitleManager.hpp"
#include "DriveSyncCore.hpp"
#include "UIManager.hpp"
#include "UIState.hpp"
#include "../../source/Logger.hpp"

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
// PONTO DE ENTRADA PRINCIPAL
// ==============================================================================
int main(int argc, char* argv[]) {
    // Inicialização dos serviços essenciais do Horizon OS
    smInitialize();
    fsInitialize();
    fsdevMountSdmc();
    timeInitialize();
    socketInitializeDefault();
    setInitialize();
    setsysInitialize();
    nifmInitialize(NifmServiceType_User);
    accountInitialize(AccountServiceType_Administrator);

    // Módulos Especializados da Arquitetura
    Assets::TitleManager titleManager;
    titleManager.init();

    Core::DriveSyncCore core;
    core.init();

    UI::UIManager ui;
    if (!ui.init()) {
        titleManager.close();
        core.close();
        return 1;
    }

    // Configuração dos controles com o Hid subsystem da libnx
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    UI::InputRepeater inputRepeater;
    UI::Screen currentScreen = UI::Screen::Main;

    int mainSelectedIndex = 0;
    int settingsSelectedIndex = 0;
    int gamesSelectedIndex = 0;
    int gamesScrollOffset = 0;

    std::vector<Assets::GameEntry> gamesList = core.loadGamesList(titleManager);

    std::string toastMessage = "";
    u32 toastColor = Gfx::Colors::STATUS_GREEN;
    u64 toastExpireNs = 0;

    auto triggerToast = [&](const std::string& msg, u32 color) {
        toastMessage = msg;
        toastColor = color;
        toastExpireNs = armTicksToNs(armGetSystemTick()) + 3'500'000'000ULL;
    };

    // ==========================================================================
    // LOOP PRINCIPAL DE EVENTOS A 60 FPS
    // ==========================================================================
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 rawDown = padGetButtonsDown(&pad);
        u64 rawHeld = padGetButtons(&pad);
        u64 nowNs = armTicksToNs(armGetSystemTick());

        u64 kDown = inputRepeater.process(rawDown, rawHeld, nowNs);

        // Saída global pelo botão (+) na tela principal ou em configurações
        if ((rawDown & HidNpadButton_Plus) && currentScreen == UI::Screen::Main) {
            break;
        }

        // ======================================================================
        // PROCESSAMENTO DE ENTRADAS CONFORME A TELA ATIVA
        // ======================================================================
        if (currentScreen == UI::Screen::Main) {
            if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                if (mainSelectedIndex > 0) mainSelectedIndex--;
            }
            if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                if (mainSelectedIndex < 3) mainSelectedIndex++;
            }

            // Atalho de Configurações: [-] ou [Y]
            if (rawDown & (HidNpadButton_Minus | HidNpadButton_Y)) {
                currentScreen = UI::Screen::Settings;
                settingsSelectedIndex = 0;
            }

            // Botão (B) sai do aplicativo
            if (rawDown & HidNpadButton_B) {
                break;
            }

            // Confirmação com (A)
            if (rawDown & HidNpadButton_A) {
                switch (mainSelectedIndex) {
                    case 0: { // Backup Completo
                        if (!core.isAuthorized()) {
                            triggerToast("Vincule sua conta Google antes de realizar o backup.", Gfx::Colors::STATUS_YELLOW);
                        } else {
                            auto progressCb = [&](const std::string& gameName, const std::string& stage, float progress) {
                                if (ui.beginFrame()) {
                                    ui.renderProgressModal(gameName, stage, progress);
                                    ui.endFrame();
                                }
                            };

                            Core::SyncResult res = core.performFullBackup(titleManager, progressCb);
                            gamesList = core.loadGamesList(titleManager);

                            // Exibe Modal de Relatório Final
                            while (appletMainLoop()) {
                                padUpdate(&pad);
                                u64 sk = padGetButtonsDown(&pad);
                                if (sk & (HidNpadButton_A | HidNpadButton_B)) break;

                                if (ui.beginFrame()) {
                                    ui.renderSummaryModal("Backup Concluido", res.successCount, res.failCount, res.details);
                                    ui.endFrame();
                                }
                                svcSleepThread(16'000'000ULL);
                            }

                            triggerToast("Backup finalizado com sucesso.", Gfx::Colors::STATUS_GREEN);
                        }
                        break;
                    }

                    case 1: { // Restaurar da Nuvem
                        if (!core.isAuthorized()) {
                            triggerToast("Vincule sua conta Google para baixar saves.", Gfx::Colors::STATUS_YELLOW);
                        } else {
                            auto progressCb = [&](const std::string& gameName, const std::string& stage, float progress) {
                                if (ui.beginFrame()) {
                                    ui.renderProgressModal(gameName, stage, progress);
                                    ui.endFrame();
                                }
                            };

                            Core::SyncResult res = core.performRestore(titleManager, progressCb);
                            gamesList = core.loadGamesList(titleManager);

                            while (appletMainLoop()) {
                                padUpdate(&pad);
                                u64 sk = padGetButtonsDown(&pad);
                                if (sk & (HidNpadButton_A | HidNpadButton_B)) break;

                                if (ui.beginFrame()) {
                                    ui.renderSummaryModal("Restauracao Concluida", res.successCount, res.failCount, res.details);
                                    ui.endFrame();
                                }
                                svcSleepThread(16'000'000ULL);
                            }

                            triggerToast("Verificacao de saves concluida.", Gfx::Colors::STATUS_GREEN);
                        }
                        break;
                    }

                    case 2: { // Gerenciar por Jogo
                        gamesList = core.loadGamesList(titleManager);
                        currentScreen = UI::Screen::ManageGames;
                        gamesSelectedIndex = 0;
                        gamesScrollOffset = 0;
                        break;
                    }

                    case 3: { // Vincular Conta Google (Device Flow)
                        if (!core.isNetworkConnected()) {
                            triggerToast("Conecte o Switch a internet antes de vincular.", Gfx::Colors::STATUS_RED);
                        } else if (!core.getClient().isConfigured()) {
                            triggerToast("Configure seu Client ID e Secret nas Configuracoes [-].", Gfx::Colors::STATUS_YELLOW);
                        } else if (core.getClient().requestDeviceCode()) {
                            currentScreen = UI::Screen::DeviceAuth;
                        } else {
                            triggerToast("Falha ao comunicar com o servico OAuth do Google.", Gfx::Colors::STATUS_RED);
                        }
                        break;
                    }
                }
            }
        }
        else if (currentScreen == UI::Screen::Settings) {
            if (rawDown & (HidNpadButton_B | HidNpadButton_Minus)) {
                currentScreen = UI::Screen::Main;
            }

            if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                if (settingsSelectedIndex > 0) settingsSelectedIndex--;
            }
            if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                if (settingsSelectedIndex < 3) settingsSelectedIndex++;
            }

            if (rawDown & HidNpadButton_A) {
                switch (settingsSelectedIndex) {
                    case 0: { // Pasta no Drive
                        std::string current = core.getFolderName();
                        std::string updated = showKeyboard("Nome da pasta de saves no Google Drive", current);
                        if (!updated.empty() && updated != current) {
                            core.setFolderName(updated);
                            core.saveConfig();
                            triggerToast("Nome da pasta salvo com sucesso.", Gfx::Colors::STATUS_GREEN);
                        }
                        break;
                    }
                    case 1: { // Client ID
                        std::string current = core.getClientId();
                        std::string updated = showKeyboard("Google OAuth Client ID", current);
                        if (!updated.empty() && updated != current) {
                            core.setClientId(updated);
                            core.saveConfig();
                            triggerToast("Client ID atualizado.", Gfx::Colors::STATUS_GREEN);
                        }
                        break;
                    }
                    case 2: { // Client Secret
                        std::string current = core.getClientSecret();
                        std::string updated = showKeyboard("Google OAuth Client Secret", current);
                        if (!updated.empty() && updated != current) {
                            core.setClientSecret(updated);
                            core.saveConfig();
                            triggerToast("Client Secret atualizado.", Gfx::Colors::STATUS_GREEN);
                        }
                        break;
                    }
                    case 3: { // Testar Conexao
                        std::string err;
                        if (core.testConnection(&err)) {
                            triggerToast("Conexao ativa e pasta validada com sucesso!", Gfx::Colors::STATUS_GREEN);
                        } else {
                            triggerToast(err.empty() ? "Falha na conexao." : err, Gfx::Colors::STATUS_RED);
                        }
                        break;
                    }
                }
            }
        }
        else if (currentScreen == UI::Screen::ManageGames) {
            if (rawDown & HidNpadButton_B) {
                currentScreen = UI::Screen::Main;
            }

            int totalGames = static_cast<int>(gamesList.size());

            if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                if (gamesSelectedIndex > 0) {
                    gamesSelectedIndex--;
                    if (gamesSelectedIndex < gamesScrollOffset) gamesScrollOffset = gamesSelectedIndex;
                }
            }
            if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                if (gamesSelectedIndex < totalGames - 1) {
                    gamesSelectedIndex++;
                    if (gamesSelectedIndex >= gamesScrollOffset + 6) {
                        gamesScrollOffset = gamesSelectedIndex - 6 + 1;
                    }
                }
            }

            // Alternar seleção com (A)
            if (rawDown & HidNpadButton_A) {
                if (gamesSelectedIndex >= 0 && gamesSelectedIndex < totalGames) {
                    gamesList[gamesSelectedIndex].selected = !gamesList[gamesSelectedIndex].selected;
                }
            }

            // Marcar todos / nenhum com (Y)
            if (rawDown & HidNpadButton_Y) {
                bool anyUnsel = false;
                for (const auto& g : gamesList) {
                    if (!g.selected) { anyUnsel = true; break; }
                }
                for (auto& g : gamesList) g.selected = anyUnsel;
            }

            // Salvar favoritos com (X)
            if (rawDown & HidNpadButton_X) {
                std::vector<std::string> tracked;
                for (auto& g : gamesList) {
                    g.isFavorite = g.selected;
                    if (g.selected) {
                        char tidBuf[32];
                        snprintf(tidBuf, sizeof(tidBuf), "%016llx", (unsigned long long)g.titleId);
                        tracked.push_back(tidBuf);
                    }
                }
                core.getClient().setSyncMode("whitelist");
                core.getClient().setTrackedTitles(tracked);
                core.saveConfig();
                triggerToast("Lista de favoritos atualizada.", Gfx::Colors::STATUS_GREEN);
            }

            // Iniciar backup dos jogos selecionados com (+)
            if (rawDown & HidNpadButton_Plus) {
                std::vector<u64> toUpload;
                for (const auto& g : gamesList) {
                    if (g.selected) toUpload.push_back(g.titleId);
                }

                if (!toUpload.empty()) {
                    auto progressCb = [&](const std::string& gameName, const std::string& stage, float progress) {
                        if (ui.beginFrame()) {
                            ui.renderProgressModal(gameName, stage, progress);
                            ui.endFrame();
                        }
                    };

                    Core::SyncResult res = core.performSelectiveBackup(toUpload, titleManager, progressCb);
                    gamesList = core.loadGamesList(titleManager);

                    while (appletMainLoop()) {
                        padUpdate(&pad);
                        u64 sk = padGetButtonsDown(&pad);
                        if (sk & (HidNpadButton_A | HidNpadButton_B)) break;

                        if (ui.beginFrame()) {
                            ui.renderSummaryModal("Backup Seletivo Concluido", res.successCount, res.failCount, res.details);
                            ui.endFrame();
                        }
                        svcSleepThread(16'000'000ULL);
                    }

                    triggerToast("Upload dos jogos selecionados concluido.", Gfx::Colors::STATUS_GREEN);
                } else {
                    triggerToast("Selecione ao menos um jogo para realizar o backup.", Gfx::Colors::STATUS_YELLOW);
                }
            }
        }
        else if (currentScreen == UI::Screen::DeviceAuth) {
            static u64 lastPollNs = 0;
            static int animTick = 0;

            if (rawDown & HidNpadButton_B) {
                currentScreen = UI::Screen::Main;
            }

            if (nowNs - lastPollNs >= 2'000'000'000ULL) {
                lastPollNs = nowNs;
                std::string pollErr;
                if (core.getClient().pollDeviceToken(&pollErr)) {
                    core.saveConfig();
                    triggerToast("Conta Google vinculada com sucesso!", Gfx::Colors::STATUS_GREEN);
                    currentScreen = UI::Screen::Main;
                }
            }

            animTick += 5;
            if (ui.beginFrame()) {
                ui.renderDeviceAuthModal(core.getClient().getPendingVerifyUrl(),
                                        core.getClient().getPendingUserCode(),
                                        animTick);
                ui.endFrame();
            }
            svcSleepThread(16'000'000ULL);
            continue;
        }

        // ======================================================================
        // RENDERIZACAO DA TELA ATIVA (60 FPS NATIVO)
        // ======================================================================
        if (ui.beginFrame()) {
            if (currentScreen == UI::Screen::Main) {
                ui.renderMainScreen(core, mainSelectedIndex, gamesList.size());
            } else if (currentScreen == UI::Screen::Settings) {
                ui.renderSettingsScreen(core, settingsSelectedIndex);
            } else if (currentScreen == UI::Screen::ManageGames) {
                ui.renderManageGamesScreen(gamesList, gamesSelectedIndex, gamesScrollOffset, titleManager);
            }

            // Notificação Toast
            if (!toastMessage.empty() && nowNs < toastExpireNs) {
                ui.renderToast(toastMessage, toastColor);
            }

            ui.endFrame();
        }

        svcSleepThread(16'000'000ULL);
    }

    // Finalização e limpeza segura dos serviços
    ui.close();
    titleManager.close();
    core.close();

    accountExit();
    nifmExit();
    setsysExit();
    setExit();
    socketExit();
    timeExit();
    fsdevUnmountAll();
    fsExit();
    smExit();

    return 0;
}

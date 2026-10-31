#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include "Renderer2D.hpp"
#include "OAuthManager.hpp"
#include "DriveVersionManager.hpp"
#include "RestoreManager.hpp"
#include "../../source/Config.hpp"
#include "../../source/GDriveClient.hpp"
#include "../../source/SaveManager.hpp"
#include "../../source/ZipHelper.hpp"
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
    if (rc == 0xE801) {
        outMsg = "Sysmodule ja esta ativo em segundo plano!";
        return true;
    }
    if (R_FAILED(rc)) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Erro 0x%X (Abra via [R] no jogo ou reinicie)", rc);
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
// MODAL: RESUMO FINAL DE UPLOAD
// ==============================================================================
void runSummaryModal(Gfx::Display& display, PadState& pad, int successCount, int failCount, const std::vector<std::string>& failDetails) {
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus)) {
            break;
        }

        if (display.beginFrame()) {
            display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

            int mx = 240, my = 120, mw = 800, mh = 460;
            display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
            u32 outlineColor = (failCount == 0) ? Gfx::Colors::STATUS_GREEN : Gfx::Colors::STATUS_YELLOW;
            display.drawRoundedRectOutline(mx, my, mw, mh, 16, outlineColor, 2);

            display.drawCloudSaveIcon(mx + 50, my + 45, 36, outlineColor);
            display.drawText(mx + 90, my + 30, "RELATORIO DE SINCRONIZACAO", Gfx::Colors::TEXT_WHITE, 2, true);
            display.drawText(mx + 90, my + 64, "Envio concluido para o Google Drive", Gfx::Colors::TEXT_MUTED, 1);

            display.fillRect(mx + 30, my + 95, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);

            int ty = my + 130;
            display.drawText(mx + 60, ty, "Saves enviados com sucesso: " + std::to_string(successCount), Gfx::Colors::STATUS_GREEN, 2, true);
            ty += 42;
            display.drawText(mx + 60, ty, "Falhas encontradas: " + std::to_string(failCount), (failCount > 0) ? Gfx::Colors::STATUS_RED : Gfx::Colors::TEXT_MUTED, 2, true);
            ty += 40;

            if (failCount > 0 && !failDetails.empty()) {
                display.drawText(mx + 60, ty, "Detalhes das falhas:", Gfx::Colors::JOYCON_YELLOW, 1, true);
                ty += 24;
                for (size_t i = 0; i < failDetails.size() && i < 4; ++i) {
                    display.drawText(mx + 80, ty, "- " + failDetails[i], Gfx::Colors::TEXT_WHITE, 1);
                    ty += 22;
                }
            } else {
                display.drawText(mx + 60, ty, "Todos os arquivos de save selecionados foram atualizados no Drive!", Gfx::Colors::JOYCON_CYAN, 1);
                ty += 26;
                display.drawText(mx + 60, ty, "Nenhum erro ou duplicata foi detectado.", Gfx::Colors::TEXT_MUTED, 1);
            }

            int btnY = my + mh - 50;
            display.drawControllerButtonBadge(mx + mw - 180, btnY, "A", Gfx::Colors::JOYCON_RED);
            display.drawText(mx + mw - 150, btnY + 4, "Continuar", Gfx::Colors::TEXT_WHITE, 1);

            display.endFrame();
        }
        svcSleepThread(16'000'000ULL);
    }
}

// ==============================================================================
// ROTINA DE UPLOAD EM LOTE COM PROGRESSO NA TELA
// ==============================================================================
void processUploadBatch(Gfx::Display& display,
                        SaveManager& saveManager,
                        GDriveClient& client,
                        AccountUid uid,
                        const std::vector<GameSaveInfo>& targets,
                        PadState& pad) {
    if (targets.empty()) return;

    int total = static_cast<int>(targets.size());
    int successCount = 0;
    int failCount = 0;
    std::vector<std::string> failDetails;

    for (int idx = 0; idx < total; ++idx) {
        const auto& item = targets[idx];
        std::string currentStage = "Compactando save (.zip)...";

        // Renderiza tela de progresso da etapa 1 (Zipping)
        auto renderProgress = [&](const std::string& stage, float fraction) {
            if (display.beginFrame()) {
                display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

                int mx = 200, my = 150, mw = 880, mh = 420;
                display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
                display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_CYAN, 2);

                display.drawCloudSaveIcon(mx + 50, my + 45, 36, Gfx::Colors::JOYCON_CYAN);
                char headerBuf[128];
                snprintf(headerBuf, sizeof(headerBuf), "ENVIANDO SAVE [%d/%d]", idx + 1, total);
                display.drawText(mx + 90, my + 30, headerBuf, Gfx::Colors::TEXT_WHITE, 2, true);
                display.drawText(mx + 90, my + 64, "Processando dados para o Google Drive", Gfx::Colors::TEXT_MUTED, 1);

                display.fillRect(mx + 30, my + 95, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);

                // Nome do Jogo
                int ty = my + 125;
                display.drawText(mx + 50, ty, "Jogo:", Gfx::Colors::TEXT_MUTED, 1);
                display.drawText(mx + 110, ty, item.titleName, Gfx::Colors::JOYCON_CYAN, 2, true);

                ty += 40;
                char tidBuf[64];
                snprintf(tidBuf, sizeof(tidBuf), "Title ID: 0x%016llX", (unsigned long long)item.titleId);
                display.drawText(mx + 50, ty, tidBuf, Gfx::Colors::TEXT_WHITE, 1);

                ty += 30;
                display.drawText(mx + 50, ty, "Status: " + stage, Gfx::Colors::DRIVE_YELLOW, 1, true);

                // Barra de progresso
                int barX = mx + 50, barY = my + 270, barW = mw - 100, barH = 18;
                display.fillRoundedRect(barX, barY, barW, barH, 9, Gfx::Colors::CARD_OUTLINE);
                int fillW = std::clamp(static_cast<int>(barW * fraction), 18, barW);
                display.fillRoundedRect(barX, barY, fillW, barH, 9, Gfx::Colors::STATUS_GREEN);

                char pctBuf[32];
                snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>(fraction * 100.0f));
                display.drawText(barX + barW + 15, barY + 1, pctBuf, Gfx::Colors::TEXT_WHITE, 1);

                display.drawText(mx + 50, my + 340, "Por favor, aguarde e nao desligue o console.", Gfx::Colors::TEXT_MUTED, 1);

                display.endFrame();
            }
        };

        renderProgress("Compactando save (.zip)...", (static_cast<float>(idx) + 0.3f) / total);

        std::string zipPath;
        bool changed = false;
        // forceArchive = true para que o .zip nao seja apagado mesmo se o CRC for identico
        bool procOk = saveManager.processGameSave(item.titleId, uid, zipPath, changed, true);

        if (!procOk) {
            failCount++;
            failDetails.push_back(item.titleName + " (Falha ao ler/montar save)");
            continue;
        }

        renderProgress("Enviando para o Google Drive...", (static_cast<float>(idx) + 0.7f) / total);

        u32 newCrc = 0;
        ZipHelper::calculateFileCrc32(zipPath, newCrc);

        std::string uploadErr;
        bool upOk = client.uploadSaveArchive(item.titleId, zipPath, &uploadErr);
        if (upOk) {
            saveManager.markSaveUploaded(item.titleId, newCrc);
            successCount++;
        } else {
            failCount++;
            std::string errDesc = uploadErr.empty() ? "Falha na transmissao HTTP" : uploadErr;
            failDetails.push_back(item.titleName + " (" + errDesc + ")");
        }
    }

    runSummaryModal(display, pad, successCount, failCount, failDetails);
}

// ==============================================================================
// ROTINA DE DOWNLOAD / RESTAURACAO INTELIGENTE (Drive -> Console)
// Baixa exclusivamente se Timestamp_Drive > Timestamp_Local
// ==============================================================================
void processDownloadBatch(Gfx::Display& display,
                          SaveManager& saveManager,
                          GDriveClient& client,
                          AccountUid uid,
                          PadState& pad) {
    auto formatTimestamp = [](std::time_t ts) -> std::string {
        if (ts <= 0) return "Nao registrado / 0";
        struct tm* tmInfo = std::gmtime(&ts);
        if (!tmInfo) return "Desconhecido";
        char buf[64];
        snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d UTC",
                 tmInfo->tm_mday, tmInfo->tm_mon + 1, tmInfo->tm_year + 1900,
                 tmInfo->tm_hour, tmInfo->tm_min);
        return buf;
    };

    // 1. Validacao de Sanidade do Relogio RTC do Console
    std::time_t currentRtc = 0;
    if (!SaveManager::isConsoleClockSane(&currentRtc)) {
        std::string errText = "O relogio do console esta descalibrado (" + formatTimestamp(currentRtc) + " < 2025).\n\n"
                              "Para evitar sobrescrita indevida de saves locais com a nuvem, a\n"
                              "sincronizacao automatica foi bloqueada por seguranca.\n\n"
                              "Por favor, acerte a data/hora nas configuracoes do console ou conecte a internet.";
        Logger::logSync("[RTC] Clock sanity check FAILED (Current: %lld < %lld). Aborting restore.",
                        (long long)currentRtc, (long long)SaveManager::MIN_SANE_RTC_TIMESTAMP);

        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 k = padGetButtonsDown(&pad);
            if (k & (HidNpadButton_A | HidNpadButton_B)) break;

            if (display.beginFrame()) {
                display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);
                int mx = 180, my = 180, mw = 920, mh = 310;
                display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
                display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_RED, 2);
                display.drawLockIcon(mx + 50, my + 50, 32, Gfx::Colors::JOYCON_RED);
                display.drawText(mx + 95, my + 35, "RELOGIO DO CONSOLE INVALIDO (RTC)", Gfx::Colors::TEXT_WHITE, 2, true);
                display.drawText(mx + 95, my + 68, "Protecao contra perda acidental de saves ativada", Gfx::Colors::JOYCON_YELLOW, 1);
                display.fillRect(mx + 30, my + 105, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);
                display.drawText(mx + 45, my + 130, errText, Gfx::Colors::TEXT_WHITE, 1);
                display.drawControllerButtonBadge(mx + mw - 160, my + mh - 50, "B", Gfx::Colors::JOYCON_GRAY);
                display.drawText(mx + mw - 130, my + mh - 46, "Voltar", Gfx::Colors::TEXT_WHITE, 1);
                display.endFrame();
            }
            svcSleepThread(16'000'000ULL);
        }
        return;
    }

    auto showLoading = [&](const std::string& msg) {
        if (display.beginFrame()) {
            display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);
            int mx = 200, my = 200, mw = 880, mh = 260;
            display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_CYAN, 2);
            display.drawCloudSaveIcon(mx + 50, my + 50, 36, Gfx::Colors::JOYCON_CYAN);
            display.drawText(mx + 95, my + 35, "ATUALIZAR DO GOOGLE DRIVE", Gfx::Colors::TEXT_WHITE, 2, true);
            display.drawText(mx + 95, my + 68, "Buscando versoes mais recentes na nuvem", Gfx::Colors::TEXT_MUTED, 1);
            display.fillRect(mx + 30, my + 105, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);
            display.drawText(mx + 50, my + 145, msg, Gfx::Colors::JOYCON_CYAN, 1, true);
            display.endFrame();
        }
    };

    showLoading("Conectando ao Google Drive e listando arquivos...");

    std::string listErr;
    std::vector<DriveFileInfo> driveFiles = client.listDriveSaves(&listErr);

    if (driveFiles.empty()) {
        std::string errText = listErr.empty() ? "Nenhum save encontrado na pasta do Google Drive." : listErr;
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 k = padGetButtonsDown(&pad);
            if (k & (HidNpadButton_A | HidNpadButton_B)) break;

            if (display.beginFrame()) {
                display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);
                int mx = 250, my = 200, mw = 780, mh = 260;
                display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
                display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_RED, 2);
                display.drawText(mx + 40, my + 40, "NENHUM SAVE NO DRIVE", Gfx::Colors::TEXT_WHITE, 2, true);
                display.drawText(mx + 40, my + 80, errText, Gfx::Colors::TEXT_MUTED, 1);
                display.drawControllerButtonBadge(mx + mw - 160, my + mh - 50, "B", Gfx::Colors::JOYCON_GRAY);
                display.drawText(mx + mw - 130, my + mh - 46, "Voltar", Gfx::Colors::TEXT_WHITE, 1);
                display.endFrame();
            }
            svcSleepThread(16'000'000ULL);
        }
        return;
    }

    std::map<u64, std::string> nameMap;
    std::vector<GameSaveInfo> installed = saveManager.listAvailableSaves(uid);
    for (const auto& item : installed) {
        nameMap[item.titleId] = item.titleName;
    }

    int total = static_cast<int>(driveFiles.size());
    int restoredCount = 0;
    int ignoredCount = 0;
    int errorCount = 0;
    std::vector<std::string> summaryLogs;

    for (int idx = 0; idx < total; ++idx) {
        const auto& file = driveFiles[idx];
        u64 tid = file.titleId;
        std::string gameName;
        auto nit = nameMap.find(tid);
        if (nit != nameMap.end() && !nit->second.empty()) {
            gameName = nit->second;
        } else {
            char tidBuf[32];
            snprintf(tidBuf, sizeof(tidBuf), "0x%016llX", (unsigned long long)tid);
            gameName = tidBuf;
        }

        std::time_t driveTs = file.modifiedTime;
        std::time_t localTs = saveManager.getLocalSaveTimestamp(tid, uid);

        std::string driveTsStr = formatTimestamp(driveTs);
        std::string localTsStr = formatTimestamp(localTs);

        auto renderStep = [&](const std::string& actionMsg, float fraction, u32 actionColor) {
            if (display.beginFrame()) {
                display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

                int mx = 180, my = 120, mw = 920, mh = 470;
                display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
                display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_CYAN, 2);

                display.drawCloudSaveIcon(mx + 45, my + 45, 36, Gfx::Colors::JOYCON_CYAN);
                char headerBuf[128];
                snprintf(headerBuf, sizeof(headerBuf), "ANALISANDO SAVE [%d/%d]", idx + 1, total);
                display.drawText(mx + 90, my + 30, headerBuf, Gfx::Colors::TEXT_WHITE, 2, true);
                display.drawText(mx + 90, my + 64, "Comparando versao do Google Drive com o console", Gfx::Colors::TEXT_MUTED, 1);

                display.fillRect(mx + 30, my + 95, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);

                int ty = my + 120;
                display.drawText(mx + 45, ty, "Jogo:", Gfx::Colors::TEXT_MUTED, 1);
                display.drawText(mx + 105, ty, gameName, Gfx::Colors::JOYCON_CYAN, 2, true);

                ty += 40;
                char tidBuf[64];
                snprintf(tidBuf, sizeof(tidBuf), "Title ID: 0x%016llX", (unsigned long long)tid);
                display.drawText(mx + 45, ty, tidBuf, Gfx::Colors::TEXT_MUTED, 1);

                ty += 30;
                display.drawText(mx + 45, ty, "Data no Drive (Nuvem): " + driveTsStr, Gfx::Colors::TEXT_WHITE, 1);
                ty += 24;
                display.drawText(mx + 45, ty, "Data Local (Switch):   " + localTsStr, Gfx::Colors::TEXT_WHITE, 1);

                ty += 35;
                display.drawText(mx + 45, ty, "Status: " + actionMsg, actionColor, 1, true);

                // Barra de progresso
                int barX = mx + 45, barY = my + 335, barW = mw - 90, barH = 18;
                display.fillRoundedRect(barX, barY, barW, barH, 9, Gfx::Colors::CARD_OUTLINE);
                int fillW = std::clamp(static_cast<int>(barW * fraction), 18, barW);
                display.fillRoundedRect(barX, barY, fillW, barH, 9, Gfx::Colors::STATUS_GREEN);

                char pctBuf[32];
                snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>(fraction * 100.0f));
                display.drawText(barX + barW + 15, barY + 1, pctBuf, Gfx::Colors::TEXT_WHITE, 1);

                display.drawText(mx + 45, my + 395, "Por favor, aguarde e nao desligue o console.", Gfx::Colors::TEXT_MUTED, 1);

                display.endFrame();
            }
        };

        float baseFrac = static_cast<float>(idx) / total;
        float nextFrac = static_cast<float>(idx + 1) / total;

        // Validacao de Sanidade do RTC do save local:
        // Se o save local existir mas o timestamp for anterior a 2025 (RTC resetado para 1970/2000),
        // abortamos a sobrescrita automatica deste jogo por seguranca para evitar perda de dados.
        if (localTs > 0 && !SaveManager::isTimestampSane(localTs)) {
            ignoredCount++;
            summaryLogs.push_back(gameName + ": Bloqueado (RTC local < 2025)");
            Logger::logSync("[%016llX] Local save timestamp is suspicious (%lld < 2025). Skipping auto-restore to avoid data loss.",
                            (unsigned long long)tid, (long long)localTs);
            renderStep("Bloqueado (Data local < 2025 suspeita de perda de RTC).", nextFrac, Gfx::Colors::JOYCON_YELLOW);
            svcSleepThread(150'000'000ULL);
        } else if (driveTs > localTs) {
            renderStep("Baixando versao mais recente: " + gameName + "...", baseFrac + 0.3f / total, Gfx::Colors::DRIVE_YELLOW);

            std::string tempZip = Config::TEMP_DIR + "/restore.zip";
            remove(tempZip.c_str());

            std::string dlErr;
            bool dlOk = client.downloadFile(file.id, tempZip, &dlErr);
            if (!dlOk) {
                errorCount++;
                summaryLogs.push_back(gameName + ": Falha download (" + dlErr + ")");
                continue;
            }

            renderStep("Restaurando save e persistindo no console...", baseFrac + 0.7f / total, Gfx::Colors::JOYCON_CYAN);

            RestoreResult res = RestoreManager::restoreFromLocalZip(tid, uid, tempZip);
            bool resOk = res.success;
            std::string resErr = res.errorMessage;
            remove(tempZip.c_str());

            if (resOk) {
                saveManager.setLocalSaveTimestamp(tid, driveTs);
                restoredCount++;
                summaryLogs.push_back(gameName + ": Restaurado com sucesso!");
            } else {
                errorCount++;
                summaryLogs.push_back(gameName + ": Erro gravacao (" + resErr + ")");
            }
        } else {
            ignoredCount++;
            summaryLogs.push_back(gameName + ": Ignorado (Local atualizado)");
            renderStep("Ignorado (Local ja esta atualizado ou mais recente).", nextFrac, Gfx::Colors::TEXT_MUTED);
            svcSleepThread(100'000'000ULL);
        }
    }

    // Modal de Resumo Final
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & (HidNpadButton_A | HidNpadButton_B)) break;

        if (display.beginFrame()) {
            display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

            int mx = 180, my = 100, mw = 920, mh = 510;
            display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_CYAN, 2);

            display.drawCloudSaveIcon(mx + 45, my + 45, 36, Gfx::Colors::JOYCON_CYAN);
            display.drawText(mx + 90, my + 30, "RESUMO DA SINCRONIZACAO DO DRIVE", Gfx::Colors::TEXT_WHITE, 2, true);
            display.drawText(mx + 90, my + 64, "Relatorio de restauracao e comparacao de versoes", Gfx::Colors::TEXT_MUTED, 1);

            display.fillRect(mx + 30, my + 95, mw - 60, 2, Gfx::Colors::CARD_OUTLINE);

            int ty = my + 115;
            display.drawText(mx + 45, ty, "Total de Jogos no Drive Analisados: " + std::to_string(total), Gfx::Colors::TEXT_WHITE, 1, true);
            ty += 28;
            display.drawText(mx + 45, ty, "Saves Baixados e Restaurados (Nuvem mais recente): " + std::to_string(restoredCount), Gfx::Colors::STATUS_GREEN, 1, true);
            ty += 28;
            display.drawText(mx + 45, ty, "Saves Ignorados (Local ja atualizado ou mais recente): " + std::to_string(ignoredCount), Gfx::Colors::JOYCON_CYAN, 1);
            ty += 28;
            display.drawText(mx + 45, ty, "Erros / Falhas: " + std::to_string(errorCount), (errorCount > 0) ? Gfx::Colors::STATUS_RED : Gfx::Colors::TEXT_MUTED, 1, true);

            ty += 35;
            display.drawText(mx + 45, ty, "Historico por Jogo:", Gfx::Colors::TEXT_MUTED, 1, true);
            ty += 22;
            for (size_t i = 0; i < summaryLogs.size() && i < 6; ++i) {
                display.drawText(mx + 60, ty, "• " + summaryLogs[i], Gfx::Colors::TEXT_WHITE, 1);
                ty += 22;
            }

            int btnY = my + mh - 50;
            display.drawControllerButtonBadge(mx + mw - 180, btnY, "A", Gfx::Colors::JOYCON_RED);
            display.drawText(mx + mw - 150, btnY + 4, "Continuar", Gfx::Colors::TEXT_WHITE, 1);

            display.endFrame();
        }
        svcSleepThread(16'000'000ULL);
    }
}

// ==============================================================================
// MODAL: SELECIONAR JOGOS PARA SINCRONIZAR
// ==============================================================================
void runSelectGamesModal(Gfx::Display& display,
                         SaveManager& saveManager,
                         GDriveClient& client,
                         AccountUid uid,
                         PadState& pad) {
    // 1. Carrega lista de saves disponíveis
    std::vector<GameSaveInfo> saves = saveManager.listAvailableSaves(uid);

    if (saves.empty()) {
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & (HidNpadButton_A | HidNpadButton_B)) break;

            if (display.beginFrame()) {
                display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);
                int mx = 300, my = 200, mw = 680, mh = 280;
                display.fillRoundedRect(mx, my, mw, mh, 16, Gfx::Colors::CARD_BG);
                display.drawRoundedRectOutline(mx, my, mw, mh, 16, Gfx::Colors::JOYCON_CYAN, 2);
                display.drawText(mx + 40, my + 40, "NENHUM SAVE ENCONTRADO", Gfx::Colors::TEXT_WHITE, 2, true);
                display.drawText(mx + 40, my + 80, "Nao ha dados de save para o usuario ativo.", Gfx::Colors::TEXT_MUTED, 1);
                display.drawControllerButtonBadge(mx + mw - 160, my + mh - 50, "B", Gfx::Colors::JOYCON_GRAY);
                display.drawText(mx + mw - 130, my + mh - 46, "Voltar", Gfx::Colors::TEXT_WHITE, 1);
                display.endFrame();
            }
            svcSleepThread(16'000'000ULL);
        }
        return;
    }

    // Marca previamente os jogos de acordo com o tracked_titles salvo se estiver em whitelist
    for (auto& s : saves) {
        if (client.isTitleTracked(s.titleId)) {
            s.selected = true;
        }
    }

    int selectedIdx = 0;
    int scrollOffset = 0;
    constexpr int ITEMS_PER_PAGE = 7;
    int totalItems = static_cast<int>(saves.size());

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        // (B) Voltar
        if (kDown & HidNpadButton_B) {
            break;
        }

        // Navegação Up / Down
        if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            if (selectedIdx > 0) {
                selectedIdx--;
                if (selectedIdx < scrollOffset) {
                    scrollOffset = selectedIdx;
                }
            }
        }
        if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            if (selectedIdx < totalItems - 1) {
                selectedIdx++;
                if (selectedIdx >= scrollOffset + ITEMS_PER_PAGE) {
                    scrollOffset = selectedIdx - ITEMS_PER_PAGE + 1;
                }
            }
        }

        // (A) Marcar / desmarcar item
        if (kDown & HidNpadButton_A) {
            saves[selectedIdx].selected = !saves[selectedIdx].selected;
        }

        // (Y) Marcar todos / Desmarcar todos
        if (kDown & HidNpadButton_Y) {
            bool anyUnselected = false;
            for (const auto& s : saves) {
                if (!s.selected) { anyUnselected = true; break; }
            }
            for (auto& s : saves) {
                s.selected = anyUnselected;
            }
        }

        // (X) Salvar seleção atual como padrão do Sysmodule (whitelist)
        if (kDown & HidNpadButton_X) {
            std::vector<std::string> tracked;
            for (const auto& s : saves) {
                if (s.selected) {
                    char tidBuf[32];
                    snprintf(tidBuf, sizeof(tidBuf), "%016llx", (unsigned long long)s.titleId);
                    tracked.push_back(tidBuf);
                }
            }
            client.setSyncMode("whitelist");
            client.setTrackedTitles(tracked);
            client.saveConfig();
        }

        // (+) Iniciar upload dos itens marcados
        if (kDown & HidNpadButton_Plus) {
            std::vector<GameSaveInfo> toUpload;
            for (const auto& s : saves) {
                if (s.selected) toUpload.push_back(s);
            }
            if (!toUpload.empty()) {
                // Atualiza também tracked titles se o usuario desejar
                std::vector<std::string> tracked;
                for (const auto& s : toUpload) {
                    char tidBuf[32];
                    snprintf(tidBuf, sizeof(tidBuf), "%016llx", (unsigned long long)s.titleId);
                    tracked.push_back(tidBuf);
                }
                client.setSyncMode("whitelist");
                client.setTrackedTitles(tracked);
                client.saveConfig();

                processUploadBatch(display, saveManager, client, uid, toUpload, pad);
                break;
            }
        }

        // Renderização da Tela de Seleção
        if (display.beginFrame()) {
            display.clearScreenGradient(Gfx::Colors::BG_DARK_TOP, Gfx::Colors::BG_DARK_BOT);

            // Header
            display.fillRect(0, 0, Gfx::SCREEN_W, 70, Gfx::Colors::HEADER_BG);
            display.fillRect(0, 68, Gfx::SCREEN_W, 2, Gfx::Colors::CARD_OUTLINE);
            display.fillRoundedRect(24, 18, 6, 34, 3, Gfx::Colors::JOYCON_CYAN);
            display.drawText(42, 16, "SELECIONAR JOGOS PARA SINCRONIZAR", Gfx::Colors::TEXT_WHITE, 2, true);

            int selectedCount = 0;
            for (const auto& s : saves) if (s.selected) selectedCount++;
            char subHeader[128];
            snprintf(subHeader, sizeof(subHeader), "Total de Jogos: %d  •  Selecionados para Upload: %d", totalItems, selectedCount);
            display.drawText(44, 46, subHeader, Gfx::Colors::TEXT_MUTED, 1);

            // Card Principal da Lista
            int listX = 40, listY = 85, listW = 1200, listH = 550;
            display.fillRoundedRect(listX, listY, listW, listH, 14, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(listX, listY, listW, listH, 14, Gfx::Colors::CARD_OUTLINE, 1);

            int itemY = listY + 14;
            int itemH = 64;

            for (int i = 0; i < ITEMS_PER_PAGE; i++) {
                int dataIdx = scrollOffset + i;
                if (dataIdx >= totalItems) break;

                const auto& item = saves[dataIdx];
                bool isFocused = (dataIdx == selectedIdx);

                int cardX = listX + 16;
                int cardW = listW - 32;

                if (isFocused) {
                    display.fillRoundedRect(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_HOVER_BG);
                    display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_HOVER_OUT, 2);
                    display.fillRoundedRect(cardX + 4, itemY + 8, 4, itemH - 16, 2, Gfx::Colors::JOYCON_CYAN);
                } else {
                    display.fillRoundedRect(cardX, itemY, cardW, itemH, 10, Gfx::Colors::HEADER_BG);
                    display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_OUTLINE, 1);
                }

                // Caixa Checkbox [X] ou [ ]
                int chkX = cardX + 22, chkY = itemY + 18, chkW = 28, chkH = 28;
                if (item.selected) {
                    display.fillRoundedRect(chkX, chkY, chkW, chkH, 6, Gfx::Colors::STATUS_GREEN);
                    display.drawText(chkX + 7, chkY + 6, "X", Gfx::Colors::TEXT_WHITE, 1, true);
                } else {
                    display.fillRoundedRect(chkX, chkY, chkW, chkH, 6, Gfx::Colors::CARD_BG);
                    display.drawRoundedRectOutline(chkX, chkY, chkW, chkH, 6, Gfx::Colors::TEXT_MUTED, 2);
                }

                // Nome do Jogo e Informações
                int nameX = cardX + 68;
                display.drawText(nameX, itemY + 14, item.titleName, Gfx::Colors::TEXT_WHITE, 1, true);

                char infoBuf[128];
                float szMb = static_cast<float>(item.saveSize) / (1024.0f * 1024.0f);
                snprintf(infoBuf, sizeof(infoBuf), "Title ID: 0x%016llX   •   Tamanho estimado: %.2f MB",
                         (unsigned long long)item.titleId, szMb);
                display.drawText(nameX, itemY + 38, infoBuf, isFocused ? Gfx::Colors::JOYCON_CYAN : Gfx::Colors::TEXT_MUTED, 1);

                itemY += itemH + 10;
            }

            // Scroll Indicator (se houver mais itens)
            if (totalItems > ITEMS_PER_PAGE) {
                int scrollTrackX = listX + listW - 14;
                int scrollTrackY = listY + 20;
                int scrollTrackH = listH - 40;
                display.fillRect(scrollTrackX, scrollTrackY, 4, scrollTrackH, Gfx::Colors::CARD_OUTLINE);

                float thumbRatio = static_cast<float>(ITEMS_PER_PAGE) / totalItems;
                int thumbH = std::max(20, static_cast<int>(scrollTrackH * thumbRatio));
                float offsetRatio = static_cast<float>(scrollOffset) / (totalItems - ITEMS_PER_PAGE);
                int thumbY = scrollTrackY + static_cast<int>((scrollTrackH - thumbH) * offsetRatio);
                display.fillRect(scrollTrackX - 1, thumbY, 6, thumbH, Gfx::Colors::JOYCON_CYAN);
            }

            // Footer com Legenda
            int footerY = 665;
            display.fillRect(0, footerY - 10, Gfx::SCREEN_W, 65, Gfx::Colors::HEADER_BG);
            display.fillRect(0, footerY - 11, Gfx::SCREEN_W, 1, Gfx::Colors::CARD_OUTLINE);

            int btnX = 30;
            display.drawControllerButtonBadge(btnX, footerY, "A", Gfx::Colors::JOYCON_RED);
            display.drawText(btnX + 30, footerY + 4, "Marcar/Desmarcar", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 190;
            display.drawControllerButtonBadge(btnX, footerY, "Y", Gfx::Colors::JOYCON_YELLOW);
            display.drawText(btnX + 30, footerY + 4, "Todos/Nenhum", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 160;
            display.drawControllerButtonBadge(btnX, footerY, "X", Gfx::Colors::JOYCON_BLUE);
            display.drawText(btnX + 30, footerY + 4, "Salvar como Padrao", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 210;
            display.drawPillButtonBadge(btnX, footerY + 2, "+");
            display.drawText(btnX + 30, footerY + 4, "Iniciar Upload", Gfx::Colors::TEXT_WHITE, 1);

            btnX += 170;
            display.drawControllerButtonBadge(btnX, footerY, "B", Gfx::Colors::JOYCON_GRAY);
            display.drawText(btnX + 30, footerY + 4, "Voltar", Gfx::Colors::TEXT_WHITE, 1);

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
    setInitialize();
    setsysInitialize();
    fsInitialize();
    timeInitialize();
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
    SaveManager saveManager;

    int selectedItem = 0;
    int menuScrollOffset = 0;
    constexpr int TOTAL_MENU_ITEMS = 10;
    constexpr int MENU_ITEMS_PER_VIEW = 6;
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

        // Navegação com scroll automático
        if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            selectedItem--;
            if (selectedItem < 0) {
                selectedItem = TOTAL_MENU_ITEMS - 1;
                menuScrollOffset = TOTAL_MENU_ITEMS - MENU_ITEMS_PER_VIEW;
            }
        }
        if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            selectedItem++;
            if (selectedItem >= TOTAL_MENU_ITEMS) {
                selectedItem = 0;
                menuScrollOffset = 0;
            }
        }

        // Ajuste do scroll offset para manter o item selecionado visível
        if (selectedItem < menuScrollOffset) {
            menuScrollOffset = selectedItem;
        } else if (selectedItem >= menuScrollOffset + MENU_ITEMS_PER_VIEW) {
            menuScrollOffset = selectedItem - MENU_ITEMS_PER_VIEW + 1;
        }
        menuScrollOffset = std::clamp(menuScrollOffset, 0, TOTAL_MENU_ITEMS - MENU_ITEMS_PER_VIEW);

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

                case 3: { // Subir Todos os Saves Agora
                    if (!client.isAuthorized()) {
                        toastMessage = "Erro: Vincule sua conta Google primeiro (opcao 1)!";
                        toastColor   = Gfx::Colors::STATUS_YELLOW;
                    } else if (!wifiActive) {
                        toastMessage = "Erro: Sem conexao com a internet!";
                        toastColor   = Gfx::Colors::STATUS_RED;
                    } else {
                        AccountUid uid{};
                        if (!saveManager.getActiveAccount(uid)) {
                            toastMessage = "Erro: Nenhuma conta de usuario ativa encontrada no Switch.";
                            toastColor   = Gfx::Colors::STATUS_RED;
                        } else {
                            std::vector<GameSaveInfo> allSaves = saveManager.listAvailableSaves(uid);
                            if (allSaves.empty()) {
                                toastMessage = "Nenhum save de jogo encontrado para envio.";
                                toastColor   = Gfx::Colors::STATUS_YELLOW;
                            } else {
                                processUploadBatch(display, saveManager, client, uid, allSaves, pad);
                                toastMessage = "Upload em lote concluido com sucesso!";
                                toastColor   = Gfx::Colors::STATUS_GREEN;
                            }
                        }
                    }
                    break;
                }

                case 4: { // Atualizar do Drive (Baixar se Nuvem for Mais Recente)
                    if (!client.isAuthorized()) {
                        toastMessage = "Erro: Vincule sua conta Google primeiro (opcao 1)!";
                        toastColor   = Gfx::Colors::STATUS_YELLOW;
                    } else if (!wifiActive) {
                        toastMessage = "Erro: Sem conexao com a internet!";
                        toastColor   = Gfx::Colors::STATUS_RED;
                    } else {
                        AccountUid uid{};
                        if (!saveManager.getActiveAccount(uid)) {
                            toastMessage = "Erro: Nenhuma conta de usuario ativa encontrada no Switch.";
                            toastColor   = Gfx::Colors::STATUS_RED;
                        } else {
                            processDownloadBatch(display, saveManager, client, uid, pad);
                            toastMessage = "Verificacao e download do Drive concluidos!";
                            toastColor   = Gfx::Colors::STATUS_GREEN;
                        }
                    }
                    break;
                }

                case 5: { // Selecionar Jogos para Sincronizar
                    if (!client.isAuthorized()) {
                        toastMessage = "Erro: Vincule sua conta Google primeiro (opcao 1)!";
                        toastColor   = Gfx::Colors::STATUS_YELLOW;
                    } else {
                        AccountUid uid{};
                        if (!saveManager.getActiveAccount(uid)) {
                            toastMessage = "Erro: Nenhuma conta de usuario ativa encontrada no Switch.";
                            toastColor   = Gfx::Colors::STATUS_RED;
                        } else {
                            runSelectGamesModal(display, saveManager, client, uid, pad);
                            toastMessage = "Sincronizacao de jogos finalizada.";
                            toastColor   = Gfx::Colors::STATUS_GREEN;
                        }
                    }
                    break;
                }

                case 6: { // Validar/Criar Pasta
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

                case 7: { // Editar Client ID
                    std::string newId = showKeyboard("Digite o Google OAuth Client ID", client.getClientId());
                    client.setClientId(newId);
                    client.saveConfig();
                    toastMessage = "Client ID salvo com sucesso!";
                    toastColor   = Gfx::Colors::STATUS_GREEN;
                    break;
                }

                case 8: { // Editar Client Secret
                    std::string newSec = showKeyboard("Digite o Google OAuth Client Secret", client.getClientSecret());
                    client.setClientSecret(newSec);
                    client.saveConfig();
                    toastMessage = "Client Secret salvo com sucesso!";
                    toastColor   = Gfx::Colors::STATUS_GREEN;
                    break;
                }

                case 9: { // Editar Nome da Pasta
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
            display.drawText(44, 46, "Atmosphere CFW  •  Suite de Sincronizacao v0.0.6", Gfx::Colors::TEXT_MUTED, 1);

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
            int menuBoxY = 80, menuBoxH = 568;
            display.fillRoundedRect(rightColX, menuBoxY, rightColW, menuBoxH, 14, Gfx::Colors::HEADER_BG);
            display.drawRoundedRectOutline(rightColX, menuBoxY, rightColW, menuBoxH, 14, Gfx::Colors::CARD_OUTLINE, 1);

            display.drawText(rightColX + 24, menuBoxY + 12, "ACOES E CONFIGURACOES", Gfx::Colors::TEXT_MUTED, 1, true);

            // Indicador de paginação no cabeçalho
            char pageHint[32];
            snprintf(pageHint, sizeof(pageHint), "%d de %d", selectedItem + 1, TOTAL_MENU_ITEMS);
            display.drawText(rightColX + rightColW - 100, menuBoxY + 12, pageHint, Gfx::Colors::JOYCON_CYAN, 1);

            enum IconKind {
                ICON_GOOGLE,
                ICON_POWER,
                ICON_FLAG,
                ICON_UPLOAD,
                ICON_DOWNLOAD,
                ICON_GAMEPAD,
                ICON_FOLDER,
                ICON_KEY,
                ICON_LOCK,
                ICON_EDIT
            };

            struct MenuItem {
                IconKind icon;
                const char* title;
                const char* desc;
                u32 accentColor;
            };

            const MenuItem menuItems[TOTAL_MENU_ITEMS] = {
                {ICON_GOOGLE,   "Vincular Conta Google (Device Flow na Tela)", "Exibe o codigo de confirmacao para ativar no celular ou PC", Gfx::Colors::DRIVE_YELLOW},
                {ICON_POWER,    "Iniciar / Parar Sysmodule em Segundo Plano", "Alterna o processo daemon de monitoramento de saves",          sysmoduleActive ? Gfx::Colors::STATUS_GREEN : Gfx::Colors::STATUS_RED},
                {ICON_FLAG,     "Alternar Inicializacao no Boot (boot2.flag)",  "Inicia automaticamente o daemon toda vez que ligar o Switch", bootFlagActive ? Gfx::Colors::STATUS_GREEN : Gfx::Colors::TEXT_MUTED},
                {ICON_UPLOAD,   "Subir Todos os Saves Agora (Upload em Lote)",  "Compacta e envia todos os saves do usuario ativo para o Drive", Gfx::Colors::JOYCON_CYAN},
                {ICON_DOWNLOAD, "Atualizar do Drive (Baixar se Mais Recente)", "Verifica e restaura saves da nuvem estritamente mais recentes", Gfx::Colors::STATUS_GREEN},
                {ICON_GAMEPAD,  "Selecionar Jogos para Sincronizar",          "Lista os jogos para marcar quais enviar ou salvar como padrao", Gfx::Colors::JOYCON_BLUE},
                {ICON_FOLDER,   "Validar / Criar Pasta de Saves no Drive",     "Testa a conexao com a API e assegura a pasta destino",          Gfx::Colors::DRIVE_GREEN},
                {ICON_KEY,      "Configurar Google OAuth Client ID",           "Insira a chave Client ID usando o teclado virtual Horizon",     Gfx::Colors::JOYCON_YELLOW},
                {ICON_LOCK,     "Configurar Google OAuth Client Secret",       "Insira o Client Secret usando o teclado virtual Horizon",       Gfx::Colors::JOYCON_RED},
                {ICON_EDIT,     "Alterar Nome da Pasta de Destino no Drive",    "Altere o nome da pasta principal de backup no Drive",           Gfx::Colors::JOYCON_CYAN}
            };

            int startY = menuBoxY + 38;
            int itemH = 62;
            int itemGap = 7;
            int cardX = rightColX + 16;
            int cardW = rightColW - 46; // Espaço reservado para o scrollbar

            for (int v = 0; v < MENU_ITEMS_PER_VIEW; v++) {
                int i = menuScrollOffset + v;
                if (i >= TOTAL_MENU_ITEMS) break;

                bool isSelected = (i == selectedItem);
                int itemY = startY + v * (itemH + itemGap);
                const auto& item = menuItems[i];

                if (isSelected) {
                    display.fillRoundedRect(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_HOVER_BG);
                    display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_HOVER_OUT, 2);

                    // Indicador vertical Joy-Con à esquerda
                    display.fillRoundedRect(cardX + 4, itemY + 8, 4, itemH - 16, 2, Gfx::Colors::JOYCON_CYAN);

                    // Seta de navegação à direita
                    display.drawText(cardX + cardW - 24, itemY + 22, ">", Gfx::Colors::JOYCON_CYAN, 1, true);
                } else {
                    display.fillRoundedRect(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_BG);
                    display.drawRoundedRectOutline(cardX, itemY, cardW, itemH, 10, Gfx::Colors::CARD_OUTLINE, 1);
                }

                // Círculo com Ícone Procedural
                int iconCircleX = cardX + 34;
                int iconCircleY = itemY + itemH / 2;
                int iconRadius = 18;

                display.fillCircle(iconCircleX, iconCircleY, iconRadius, isSelected ? Gfx::Colors::HEADER_BG : Gfx::Colors::CARD_BG);
                display.drawRoundedRectOutline(iconCircleX - iconRadius, iconCircleY - iconRadius, iconRadius * 2, iconRadius * 2, iconRadius, item.accentColor, 1);

                switch (item.icon) {
                    case ICON_GOOGLE:
                        display.drawGoogleDriveIcon(iconCircleX, iconCircleY, 20);
                        break;
                    case ICON_POWER:
                        display.drawPowerIcon(iconCircleX, iconCircleY, 18, item.accentColor);
                        break;
                    case ICON_FLAG:
                        display.drawFlagIcon(iconCircleX, iconCircleY, 18, item.accentColor);
                        break;
                    case ICON_UPLOAD:
                        display.drawCloudSaveIcon(iconCircleX, iconCircleY, 20, item.accentColor);
                        break;
                    case ICON_DOWNLOAD:
                        display.drawCloudDownloadIcon(iconCircleX, iconCircleY, 20, item.accentColor);
                        break;
                    case ICON_GAMEPAD:
                        display.drawGamepadIcon(iconCircleX, iconCircleY, 20, item.accentColor);
                        break;
                    case ICON_FOLDER:
                        display.drawFolderIcon(iconCircleX - 10, iconCircleY - 7, 20, 14, item.accentColor);
                        break;
                    case ICON_KEY:
                        display.drawKeyIcon(iconCircleX, iconCircleY, 18, item.accentColor);
                        break;
                    case ICON_LOCK:
                        display.drawLockIcon(iconCircleX, iconCircleY, 18, item.accentColor);
                        break;
                    case ICON_EDIT:
                        display.drawEditIcon(iconCircleX, iconCircleY, 18, item.accentColor);
                        break;
                }

                // Título e Descrição
                int textX = cardX + 66;
                display.drawText(textX, itemY + 12, item.title, Gfx::Colors::TEXT_WHITE, 1, true);
                display.drawText(textX, itemY + 36, item.desc, isSelected ? Gfx::Colors::JOYCON_CYAN : Gfx::Colors::TEXT_MUTED, 1);
            }

            // Barra de Scroll Lateral
            int scrollTrackX = rightColX + rightColW - 18;
            int scrollTrackY = startY;
            int scrollTrackH = MENU_ITEMS_PER_VIEW * (itemH + itemGap) - itemGap;

            display.fillRoundedRect(scrollTrackX, scrollTrackY, 6, scrollTrackH, 3, Gfx::Colors::CARD_OUTLINE);

            float viewRatio = static_cast<float>(MENU_ITEMS_PER_VIEW) / TOTAL_MENU_ITEMS;
            int thumbH = std::max(24, static_cast<int>(scrollTrackH * viewRatio));
            float scrollRatio = static_cast<float>(menuScrollOffset) / (TOTAL_MENU_ITEMS - MENU_ITEMS_PER_VIEW);
            int thumbY = scrollTrackY + static_cast<int>((scrollTrackH - thumbH) * scrollRatio);

            display.fillRoundedRect(scrollTrackX, thumbY, 6, thumbH, 3, Gfx::Colors::JOYCON_CYAN);

            // Indicadores de seta acima/abaixo do scroll
            if (menuScrollOffset > 0) {
                display.drawText(scrollTrackX - 1, scrollTrackY - 14, "^", Gfx::Colors::JOYCON_CYAN, 1);
            }
            if (menuScrollOffset + MENU_ITEMS_PER_VIEW < TOTAL_MENU_ITEMS) {
                display.drawText(scrollTrackX - 1, scrollTrackY + scrollTrackH + 2, "v", Gfx::Colors::JOYCON_CYAN, 1);
            }

            // Barra de Notificação / Toast no fundo da coluna direita
            int toastY = menuBoxY + menuBoxH - 48;
            display.fillRoundedRect(rightColX + 16, toastY, rightColW - 32, 40, 8, Gfx::Colors::CARD_BG);
            display.drawRoundedRectOutline(rightColX + 16, toastY, rightColW - 32, 40, 8, toastColor, 1);
            display.fillCircle(rightColX + 36, toastY + 20, 6, toastColor);
            display.drawText(rightColX + 54, toastY + 12, toastMessage, Gfx::Colors::TEXT_WHITE, 1, true);

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
    setsysExit();
    setExit();
    socketExit();
    timeExit();
    fsdevUnmountAll();
    fsExit();
    smExit();

    return 0;
}

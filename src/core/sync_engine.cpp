#include "sync_engine.hpp"
#include "../storage/zip_util.hpp"
#include "../utils/log.hpp"
#include "../utils/time_util.hpp"
#include <cstdio>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR_S(p) _mkdir(p)
#else
#define MKDIR_S(p) mkdir(p, 0777)
#endif

namespace Core {

    SyncEngine::SyncEngine(Network::DriveClient& client, Storage::SaveManager& saveManager, StateMachine& stateMachine)
        : m_client(client),
          m_saveManager(saveManager),
          m_stateMachine(stateMachine),
          m_isRunning(false),
          m_cancelRequested(false),
          m_progress(0.0f) {
        Storage::SaveManager::getActiveAccount(m_activeUid);
    }

    SyncEngine::~SyncEngine() {
        requestCancel();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

    void SyncEngine::requestCancel() {
        m_cancelRequested.store(true, std::memory_order_relaxed);
    }

    std::string SyncEngine::getCurrentGame() {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        return m_currentGame;
    }

    std::string SyncEngine::getStatusDetail() {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        return m_statusDetail;
    }

    OperationSummary SyncEngine::getSummary() {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        return m_summary;
    }

    void SyncEngine::poll() {
        if (!m_isRunning.load(std::memory_order_relaxed) && m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

    bool SyncEngine::startFullBackup() {
        if (m_isRunning.load(std::memory_order_relaxed)) return false;
        poll();

        m_cancelRequested.store(false, std::memory_order_relaxed);
        m_progress.store(0.0f, std::memory_order_relaxed);
        m_isRunning.store(true, std::memory_order_relaxed);
        m_stateMachine.setActiveOperation(OperationType::FULL_BACKUP);
        m_stateMachine.setEngineState(EngineState::PROCESSING);
        m_stateMachine.setScreen(Screen::OPERATION_PROGRESS);

        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_currentGame = "Iniciando...";
            m_statusDetail = "Escanendo partição de saves...";
            m_summary = OperationSummary{};
        }

        m_workerThread = std::thread(&SyncEngine::runWorker, this, OperationType::FULL_BACKUP);
        return true;
    }

    bool SyncEngine::startFullRestore() {
        if (m_isRunning.load(std::memory_order_relaxed)) return false;
        poll();

        m_cancelRequested.store(false, std::memory_order_relaxed);
        m_progress.store(0.0f, std::memory_order_relaxed);
        m_isRunning.store(true, std::memory_order_relaxed);
        m_stateMachine.setActiveOperation(OperationType::FULL_RESTORE);
        m_stateMachine.setEngineState(EngineState::PROCESSING);
        m_stateMachine.setScreen(Screen::OPERATION_PROGRESS);

        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_currentGame = "Iniciando...";
            m_statusDetail = "Conectando ao Google Drive...";
            m_summary = OperationSummary{};
        }

        m_workerThread = std::thread(&SyncEngine::runWorker, this, OperationType::FULL_RESTORE);
        return true;
    }

    bool SyncEngine::startTestConnection() {
        if (m_isRunning.load(std::memory_order_relaxed)) return false;
        poll();

        m_cancelRequested.store(false, std::memory_order_relaxed);
        m_progress.store(0.0f, std::memory_order_relaxed);
        m_isRunning.store(true, std::memory_order_relaxed);
        m_stateMachine.setActiveOperation(OperationType::TEST_CONNECTION);
        m_stateMachine.setEngineState(EngineState::PROCESSING);
        m_stateMachine.setScreen(Screen::OPERATION_PROGRESS);

        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_currentGame = "Google Drive API";
            m_statusDetail = "Validando autenticação e conectividade...";
            m_summary = OperationSummary{};
        }

        m_workerThread = std::thread(&SyncEngine::runWorker, this, OperationType::TEST_CONNECTION);
        return true;
    }

    void SyncEngine::runWorker(OperationType op) {
        if (op == OperationType::FULL_BACKUP) {
            doBackupAll();
        } else if (op == OperationType::FULL_RESTORE) {
            doRestoreAll();
        } else if (op == OperationType::TEST_CONNECTION) {
            doTestConnection();
        }

        m_isRunning.store(false, std::memory_order_relaxed);
        bool cancelled = m_cancelRequested.load(std::memory_order_relaxed);
        if (cancelled) {
            m_stateMachine.setEngineState(EngineState::CANCELLED);
        } else {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_stateMachine.setEngineState(m_summary.failCount == 0 ? EngineState::COMPLETED_SUCCESS : EngineState::COMPLETED_ERROR);
        }
    }

    void SyncEngine::doBackupAll() {
        Storage::SaveManager::getActiveAccount(m_activeUid);
        std::vector<Storage::GameSaveInfo> localSaves = m_saveManager.listAvailableSaves(m_activeUid);

        if (localSaves.empty()) {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_summary.finished = true;
            m_summary.success = true;
            m_summary.logMessages.push_back("Nenhum save encontrado no console.");
            return;
        }

        MKDIR_S("sdmc:/config");
        MKDIR_S("sdmc:/config/gdrive-sync");
        MKDIR_S("sdmc:/config/gdrive-sync/temp");

        size_t total = localSaves.size();
        for (size_t i = 0; i < total; ++i) {
            if (m_cancelRequested.load(std::memory_order_relaxed)) break;

            const auto& save = localSaves[i];
            u64 tid = save.titleId;
            char tidHex[32];
            std::snprintf(tidHex, sizeof(tidHex), "%016llX", static_cast<unsigned long long>(tid));

            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_currentGame = "Title " + std::string(tidHex);
                m_statusDetail = "Compactando save local...";
                m_progress.store(static_cast<float>(i) / static_cast<float>(total), std::memory_order_relaxed);
            }

            std::string tempZip = "sdmc:/config/gdrive-sync/temp/backup_" + std::string(tidHex) + ".zip";
            std::remove(tempZip.c_str());

#ifdef __SWITCH__
            Core::ScopedMountedSave mountGuard(tid, m_activeUid, true);
            if (!mountGuard.isMounted()) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_summary.failCount++;
                m_summary.logMessages.push_back("Falha ao montar save: " + std::string(tidHex));
                continue;
            }

            bool zipOk = Storage::ZipUtil::zipDirectory("save:/", tempZip, &m_cancelRequested);
            if (!zipOk) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_summary.failCount++;
                m_summary.logMessages.push_back("Falha na compactação de " + std::string(tidHex));
                continue;
            }
#else
            // Cria zip mock para testes em ambiente de host
            std::string mockDir = "sdmc:/config/gdrive-sync/temp/mock_" + std::string(tidHex);
            MKDIR_S(mockDir.c_str());
            FILE* mf = std::fopen((mockDir + "/data.bin").c_str(), "wb");
            if (mf) { std::fwrite("DATA", 1, 4, mf); std::fclose(mf); }
            Storage::ZipUtil::zipDirectory(mockDir, tempZip, &m_cancelRequested);
#endif

            u32 crc = 0;
            Storage::ZipUtil::calculateFileCrc32(tempZip, crc);
            std::time_t now = Utils::TimeUtil::getNowUtc();

            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_statusDetail = "Enviando para o Google Drive...";
            }

            std::string upErr;
            bool upOk = m_client.uploadSaveLockstep(
                tid,
                Storage::SaveManager::formatUid(m_activeUid),
                tempZip,
                crc,
                now,
                &m_cancelRequested,
                [this, i, total](float p, size_t, size_t) {
                    float overall = (static_cast<float>(i) + p) / static_cast<float>(total);
                    m_progress.store(overall, std::memory_order_relaxed);
                },
                &upErr
            );

            std::remove(tempZip.c_str());

            std::lock_guard<std::mutex> lock(m_statusMutex);
            if (upOk) {
                m_saveManager.setLocalSaveTimestamp(tid, now);
                m_summary.successCount++;
                m_summary.logMessages.push_back("Backup concluído: " + std::string(tidHex));
            } else {
                m_summary.failCount++;
                m_summary.logMessages.push_back("Erro no upload: " + std::string(tidHex) + " (" + upErr + ")");
            }
        }

        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_summary.finished = true;
        m_summary.totalProcessed = static_cast<int>(total);
        m_summary.success = (m_summary.failCount == 0);
        m_progress.store(1.0f, std::memory_order_relaxed);
    }

    void SyncEngine::doRestoreAll() {
        std::string listErr;
        std::vector<Network::DriveFileInfo> driveSaves = m_client.listDriveSaves(&listErr, &m_cancelRequested);

        if (driveSaves.empty()) {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_summary.finished = true;
            m_summary.success = true;
            m_summary.logMessages.push_back("Nenhum save encontrado na pasta do Drive.");
            return;
        }

        MKDIR_S("sdmc:/config/gdrive-sync/temp");
        size_t total = driveSaves.size();

        for (size_t i = 0; i < total; ++i) {
            if (m_cancelRequested.load(std::memory_order_relaxed)) break;

            const auto& file = driveSaves[i];
            u64 tid = file.titleId;
            std::time_t driveTs = file.modifiedTime;
            std::time_t localTs = m_saveManager.getLocalSaveTimestamp(tid, m_activeUid);

            // Trava de Sanidade RTC: se relógio do Switch for menor que 2025, bloqueia restauração automática
            if (!Utils::TimeUtil::isTimestampSane(localTs) && localTs > 0) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_summary.failCount++;
                m_summary.logMessages.push_back("Trava RTC acionada: relógio local descalibrado (< 2025).");
                continue;
            }

            if (driveTs <= localTs && localTs > 0) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_summary.successCount++;
                m_summary.logMessages.push_back("Já sincronizado: " + file.name);
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_currentGame = file.name;
                m_statusDetail = "Baixando snapshot da nuvem...";
                m_progress.store(static_cast<float>(i) / static_cast<float>(total), std::memory_order_relaxed);
            }

            std::string tempZip = "sdmc:/config/gdrive-sync/temp/dl_" + file.id + ".zip";
            std::remove(tempZip.c_str());

            std::string dlErr;
            bool dlOk = m_client.downloadFile(file.id, tempZip, &m_cancelRequested, nullptr, &dlErr);

            if (!dlOk) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_summary.failCount++;
                m_summary.logMessages.push_back("Falha no download: " + file.name + " (" + dlErr + ")");
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_statusDetail = "Restaurando com transação atômica...";
            }

            auto res = Storage::SaveManager::restoreSaveFromZip(tid, m_activeUid, tempZip, &m_cancelRequested);
            std::remove(tempZip.c_str());

            std::lock_guard<std::mutex> lock(m_statusMutex);
            if (res.success) {
                m_saveManager.setLocalSaveTimestamp(tid, driveTs);
                m_summary.successCount++;
                m_summary.logMessages.push_back("Restaurado com sucesso: " + file.name);
            } else {
                m_summary.failCount++;
                m_summary.logMessages.push_back("Erro na restauração: " + file.name + " (" + res.errorMessage + ")");
            }
        }

        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_summary.finished = true;
        m_summary.totalProcessed = static_cast<int>(total);
        m_summary.success = (m_summary.failCount == 0);
        m_progress.store(1.0f, std::memory_order_relaxed);
    }

    void SyncEngine::doTestConnection() {
        std::string err;
        bool ok = m_client.testConnection(&err);

        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_summary.finished = true;
        m_summary.totalProcessed = 1;
        if (ok) {
            m_summary.success = true;
            m_summary.successCount = 1;
            m_summary.logMessages.push_back("Conexão com Google Drive API bem-sucedida!");
        } else {
            m_summary.success = false;
            m_summary.failCount = 1;
            m_summary.logMessages.push_back("Falha na conexão: " + err);
        }
        m_progress.store(1.0f, std::memory_order_relaxed);
    }

} // namespace Core

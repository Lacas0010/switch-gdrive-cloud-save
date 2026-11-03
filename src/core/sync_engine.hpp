#pragma once
#include "../common.hpp"
#include "../network/drive_client.hpp"
#include "../storage/save_manager.hpp"
#include "state_machine.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace Core {

    struct OperationSummary {
        bool finished = false;
        bool success = false;
        int totalProcessed = 0;
        int successCount = 0;
        int failCount = 0;
        std::vector<std::string> logMessages;
    };

    class SyncEngine {
    public:
        SyncEngine(Network::DriveClient& client, Storage::SaveManager& saveManager, StateMachine& stateMachine);
        ~SyncEngine();

        // Operações em segundo plano (não bloqueiam o loop da UI)
        bool startFullBackup();
        bool startFullRestore();
        bool startTestConnection();
        void requestCancel();

        // Checagem de status assíncrono para o loop da UI (60 FPS contínuos)
        bool isRunning() const noexcept { return m_isRunning.load(std::memory_order_relaxed); }
        float getProgress() const noexcept { return m_progress.load(std::memory_order_relaxed); }
        std::string getCurrentGame();
        std::string getStatusDetail();
        OperationSummary getSummary();

        // Atualização e encerramento seguro da worker thread
        void poll();

        AccountUid getActiveUid() const noexcept { return m_activeUid; }

    private:
        void runWorker(OperationType op);
        void doBackupAll();
        void doRestoreAll();
        void doTestConnection();

        Network::DriveClient& m_client;
        Storage::SaveManager& m_saveManager;
        StateMachine& m_stateMachine;

        std::thread m_workerThread;
        std::atomic<bool> m_isRunning;
        std::atomic<bool> m_cancelRequested;
        std::atomic<float> m_progress;

        std::mutex m_statusMutex;
        std::string m_currentGame;
        std::string m_statusDetail;
        OperationSummary m_summary;

        AccountUid m_activeUid;
    };

} // namespace Core

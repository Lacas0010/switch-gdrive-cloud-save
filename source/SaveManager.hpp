#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <map>
#include <ctime>

// Estados canonicos da Maquina de Sincronizacao Append-Only (Sysmodule Produtor)
enum class SyncState {
    IDLE,
    PROCESS_EXIT_DETECTED,
    WAIT_LOCKS,
    MOUNT_READONLY,
    SNAPSHOT_PREPARE,
    COMPRESS,
    UPLOAD,
    CLEANUP,
    FAILURE_RECOVERY,
    DISABLED
};

struct GameSaveInfo {
    u64 titleId = 0;
    std::string titleName;
    u64 saveSize = 0;
    bool selected = false;
};

struct PendingUploadJob {
    std::string jobId;
    u64 titleId = 0;
    AccountUid uid{};
    std::string accountUidStr;
    std::time_t timestampUtc = 0;
    u32 crc32 = 0;
    size_t fileCount = 0;
    std::string zipPath;
    int retryCount = 0;
    std::string lastError;
};

class GDriveClient;

class SaveManager {
public:
    SaveManager();
    ~SaveManager();

    // Controle da Maquina de Estados
    SyncState GetState() const { return m_state; }
    void SetState(SyncState state) { m_state = state; }

    // Encerramento seguro do servico
    void Shutdown();

    // Notificacao de termino de jogo
    void OnProcessExit(u64 titleId);

    // Manipuladores de cada estado no loop principal do sysmodule
    void HandleProcessExitDetected(GDriveClient& gdrive);
    void HandleWaitLocks();
    void HandleMountReadOnly();
    void HandleSnapshotPrepare();
    void HandleCompress();
    void HandleUpload(GDriveClient& gdrive);
    void HandleCleanup();
    void HandleFailureRecovery();

    // Gestao da fila persistente em disco (sdmc:/config/gdrive-sync/pending/<jobId>.json)
    void TryFlushPending(GDriveClient& gdrive);
    bool FlushPendingUploads(GDriveClient& gdrive);

    // Funcoes de descoberta e leitura estritamente READ-ONLY
    bool getActiveAccount(AccountUid& outUid);
    std::vector<GameSaveInfo> listAvailableSaves(AccountUid uid);
    bool processGameSave(u64 titleId, AccountUid uid, std::string& outZipPath, bool& outChanged, bool forceArchive = false);
    void markSaveUploaded(u64 titleId, u32 crc);

    // Gestao de timestamps locais
    std::time_t getLocalSaveTimestamp(u64 titleId, AccountUid uid);
    void setLocalSaveTimestamp(u64 titleId, std::time_t ts);

    // Validacao de sanidade temporal (01/01/2025 00:00:00 UTC = 1735689600)
    static constexpr std::time_t MIN_SANE_RTC_TIMESTAMP = 1735689600;
    static bool isTimestampSane(std::time_t ts);
    static bool isConsoleClockSane(std::time_t* outCurrentTime = nullptr);

    // Conversoes de AccountUid para string hexadecimal
    static std::string formatUid(const AccountUid& uid);
    static AccountUid parseUid(const std::string& str);

    // Persistencia da fila de upload em JSON individual
    bool savePendingJob(const PendingUploadJob& job);
    std::vector<PendingUploadJob> loadPendingJobs();
    bool removePendingJob(const std::string& jobId);

    void loadState();
    void saveState();
    void loadTimestamps();
    void saveTimestamps();

private:
    SyncState m_state = SyncState::IDLE;

    // Variaveis de contexto da operacao atual de snapshot
    u64 m_currentTitleId = 0;
    u64 m_baseTitleId = 0;
    AccountUid m_currentUid{};
    std::string m_currentUidStr;
    bool m_isDeviceSave = false;
    std::time_t m_currentTimestamp = 0;
    u32 m_currentCrc = 0;
    size_t m_currentFileCount = 0;
    std::string m_snapshotZipPath;
    std::string m_currentJobId;

    // Temporizadores e retentativas
    int m_lockWaitTicks = 0;
    int m_mountRetryAttempts = 0;
    int m_recoveryAttempts = 0;
    u64 m_lastFlushTick = 0;

    std::map<u64, u32> m_savedCrcs;
    std::map<u64, std::time_t> m_timestamps;
};

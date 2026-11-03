#include "catch.hpp"
#include "switch_mock.hpp"
#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>

enum class SyncAction {
    SYNCED,
    DOWNLOAD_REQUIRED,
    UPLOAD_REQUIRED,
    CONFLICT_HASH_MISMATCH,
    RTC_SANITY_BLOCKED
};

static constexpr std::time_t MIN_SANE_RTC_TIMESTAMP = 1735689600; // 2025-01-01 00:00:00 UTC

struct SyncMetadata {
    std::time_t localTimestamp = 0;
    std::time_t cloudTimestamp = 0;
    uint32_t localCrc = 0;
    uint32_t cloudCrc = 0;
};

static SyncAction evaluateSyncDecision(const SyncMetadata& meta) {
    // 1. Trava de Sanidade RTC: se relogio local estiver antes de 2025,
    // proibe comparacao cega que causaria perda de saves
    if (meta.localTimestamp < MIN_SANE_RTC_TIMESTAMP) {
        return SyncAction::RTC_SANITY_BLOCKED;
    }

    // 2. Nuvem mais recente que local
    if (meta.cloudTimestamp > meta.localTimestamp) {
        return SyncAction::DOWNLOAD_REQUIRED;
    }

    // 3. Local mais recente que nuvem
    if (meta.localTimestamp > meta.cloudTimestamp) {
        return SyncAction::UPLOAD_REQUIRED;
    }

    // 4. Timestamps identicos: verifica integridade CRC32
    if (meta.localTimestamp == meta.cloudTimestamp) {
        if (meta.localCrc == meta.cloudCrc) {
            return SyncAction::SYNCED;
        } else {
            return SyncAction::CONFLICT_HASH_MISMATCH;
        }
    }

    return SyncAction::SYNCED;
}

// Formatadores de caminho Lockstep Cloud Storage
static std::string formatRootLivePath(u64 titleId) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Switch Saves/save_%016llX.zip", (unsigned long long)titleId);
    return std::string(buf);
}

static std::string formatSnapshotPath(u64 titleId, const AccountUid& uid, std::time_t tsUtc, uint32_t crc32) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Switch Saves/%016llX/%016llX%016llX/%016llX_%016llX%016llX_%lld_%08X.zip",
             (unsigned long long)titleId,
             (unsigned long long)uid.uid[0], (unsigned long long)uid.uid[1],
             (unsigned long long)titleId,
             (unsigned long long)uid.uid[0], (unsigned long long)uid.uid[1],
             (long long)tsUtc,
             crc32);
    return std::string(buf);
}

TEST_CASE("Sync Decision Engine - Decision Matrix & Conflict Detection", "[sync_decision]") {
    const std::time_t BASE_TIME = 1762164930; // 2025+ sane timestamp

    SECTION("Cloud Timestamp > Local Timestamp -> DOWNLOAD_REQUIRED") {
        SyncMetadata meta;
        meta.localTimestamp = BASE_TIME;
        meta.cloudTimestamp = BASE_TIME + 3600; // Nuvem 1 hora a frente
        meta.localCrc = 0x11111111;
        meta.cloudCrc = 0x22222222;

        SyncAction action = evaluateSyncDecision(meta);
        REQUIRE(action == SyncAction::DOWNLOAD_REQUIRED);
    }

    SECTION("Local Timestamp > Cloud Timestamp -> UPLOAD_REQUIRED") {
        SyncMetadata meta;
        meta.localTimestamp = BASE_TIME + 1800; // Local 30 min a frente
        meta.cloudTimestamp = BASE_TIME;
        meta.localCrc = 0x33333333;
        meta.cloudCrc = 0x44444444;

        SyncAction action = evaluateSyncDecision(meta);
        REQUIRE(action == SyncAction::UPLOAD_REQUIRED);
    }

    SECTION("Timestamps Identical and CRCs Identical -> SYNCED (No Transfer Allowed)") {
        SyncMetadata meta;
        meta.localTimestamp = BASE_TIME;
        meta.cloudTimestamp = BASE_TIME;
        meta.localCrc = 0xAABBCCDD;
        meta.cloudCrc = 0xAABBCCDD;

        SyncAction action = evaluateSyncDecision(meta);
        REQUIRE(action == SyncAction::SYNCED);
    }

    SECTION("Timestamps Identical but CRCs Differ -> CONFLICT_HASH_MISMATCH") {
        SyncMetadata meta;
        meta.localTimestamp = BASE_TIME;
        meta.cloudTimestamp = BASE_TIME;
        meta.localCrc = 0x11223344;
        meta.cloudCrc = 0x55667788; // Alteracao externa com mesmo timestamp

        SyncAction action = evaluateSyncDecision(meta);
        REQUIRE(action == SyncAction::CONFLICT_HASH_MISMATCH);
    }

    SECTION("Local Timestamp < MIN_SANE_RTC_TIMESTAMP -> RTC_SANITY_BLOCKED") {
        SyncMetadata meta;
        meta.localTimestamp = 1000; // Ano 1970 (relógio Switch descalibrado)
        meta.cloudTimestamp = BASE_TIME;
        meta.localCrc = 0x1111;
        meta.cloudCrc = 0x2222;

        // Mesmo com a nuvem tendo timestamp maior, a operacao DEVE ser bloqueada
        // para nao sobrescrever o progresso local quando o relogio perdeu bateria.
        SyncAction action = evaluateSyncDecision(meta);
        REQUIRE(action == SyncAction::RTC_SANITY_BLOCKED);
    }
}

TEST_CASE("Lockstep Storage - Path Hierarchy & Snapshot Format", "[lockstep_paths]") {
    u64 titleId = 0x01006F8002326000ULL; // Super Smash Bros Ultimate
    AccountUid uid;
    uid.uid[0] = 0x0123456789ABCDEFULL;
    uid.uid[1] = 0xFEDCBA9876543210ULL;
    std::time_t ts = 1793700930;
    uint32_t crc = 0x4C3A2B10;

    SECTION("Root live path format matches live sync conventions") {
        std::string rootPath = formatRootLivePath(titleId);
        REQUIRE(rootPath == "Switch Saves/save_01006F8002326000.zip");
    }

    SECTION("Snapshot versioned path contains full immutable audit trail") {
        std::string snapPath = formatSnapshotPath(titleId, uid, ts, crc);
        REQUIRE(snapPath == "Switch Saves/01006F8002326000/0123456789ABCDEFFEDCBA9876543210/01006F8002326000_0123456789ABCDEFFEDCBA9876543210_1793700930_4C3A2B10.zip");
    }
}

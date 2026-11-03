#include "catch.hpp"
#include "switch_mock.hpp"
#include "../../source/ZipHelper.hpp"
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0777)
#endif

struct TransactionResult {
    bool success = false;
    std::string errorMessage;
    std::string preventiveBackupPath;
    bool committed = false;
    bool rollbackTriggered = false;
};

// Implementacao do motor transacional de restauracao desacoplado para testes de regressao
static TransactionResult executeTransactionalRestore(
    u64 titleId,
    AccountUid uid,
    const std::string& incomingZipPath,
    const std::string& savePartitionDir,
    const std::string& backupStorageDir
) {
    TransactionResult res;
    auto& fsMock = getFsDevMock();

    // 1. Validacao previa do arquivo zip recebido
    struct stat st;
    if (stat(incomingZipPath.c_str(), &st) != 0 || st.st_size == 0) {
        res.success = false;
        res.errorMessage = "ZIP de restauracao vazio ou inexistente.";
        return res;
    }

    u32 fileCrc = 0;
    if (!ZipHelper::calculateFileCrc32(incomingZipPath, fileCrc)) {
        res.success = false;
        res.errorMessage = "Falha no calculo de CRC32 do arquivo recebido.";
        return res;
    }

    // 2. Monta particao de save
    fsdevUnmountDevice("save");
    Result mountRc = fsdevMountSaveData("save", titleId, uid);
    if (R_FAILED(mountRc)) {
        res.success = false;
        res.errorMessage = "Falha ao montar particao save:/";
        return res;
    }

    // 3. Backup Preventivo: salva dados atuais antes de tocar em qualquer arquivo
    MKDIR(backupStorageDir.c_str());
    std::vector<std::string> currentFiles;
    ZipHelper::listFilesRecursive(savePartitionDir, "", currentFiles);
    bool hadExistingData = !currentFiles.empty();

    char bakName[256];
    std::time_t now = 1762165000; // Timestamp fixo para previsibilidade de teste
    snprintf(bakName, sizeof(bakName), "%s/%016llX_before_restore_%lld.zip",
             backupStorageDir.c_str(), (unsigned long long)titleId, (long long)now);
    std::string bakPath = bakName;
    res.preventiveBackupPath = bakPath;

    if (hadExistingData) {
        if (!ZipHelper::zipDirectory(savePartitionDir, bakPath)) {
            fsdevUnmountDevice("save");
            res.success = false;
            res.errorMessage = "Falha ao gerar backup preventivo local.";
            return res;
        }
    }

    // 4. Limpa particao de save para receber snapshot
    ZipHelper::cleanDirectory(savePartitionDir);

    // 5. Descompacta snapshot recebido
    bool unzipOk = ZipHelper::unzipToDirectory(incomingZipPath, savePartitionDir);
    std::vector<std::string> extractedFiles;
    if (unzipOk) {
        ZipHelper::listFilesRecursive(savePartitionDir, "", extractedFiles);
    }
    bool extractionValid = (unzipOk && !extractedFiles.empty());

    // 6. Validacao e Commit Atômico
    Result commitRc = 0;
    if (extractionValid) {
        commitRc = fsdevCommitDevice("save");
    }

    // 7. Rollback em caso de erro na extracao ou no commit
    if (!extractionValid || R_FAILED(commitRc)) {
        res.rollbackTriggered = true;
        res.committed = false;

        if (hadExistingData) {
            ZipHelper::cleanDirectory(savePartitionDir);
            ZipHelper::unzipToDirectory(bakPath, savePartitionDir);
            fsdevCommitDevice("save"); // Recommit dos dados seguros do backup original
        }

        fsdevUnmountDevice("save");
        res.success = false;
        res.errorMessage = !extractionValid ? "Extracao corrompida. Rollback acionado." : "Falha no commit.";
        return res;
    }

    // Sucesso
    fsdevUnmountDevice("save");
    res.success = true;
    res.committed = true;
    return res;
}

TEST_CASE("Transactional Restore Engine - Preventive Backup & Atomic Rollback", "[rollback]") {
    std::string testRoot = "tests_tmp_rollback";
    MKDIR(testRoot.c_str());

    std::string saveDir = testRoot + "/save_partition";
    std::string backupDir = testRoot + "/sdmc_backups";
    MKDIR(saveDir.c_str());
    MKDIR(backupDir.c_str());

    u64 testTitleId = 0x0100000000010000ULL;
    AccountUid testUid;
    testUid.uid[0] = 0xAAAAAAAAAAAAAAAAULL;
    testUid.uid[1] = 0xBBBBBBBBBBBBBBBBULL;

    auto& fsMock = getFsDevMock();

    SECTION("Scenario 1: Happy Path - Clean Restore & Commit") {
        fsMock.reset();

        // 1. Prepara save local existente
        std::string originalFile = saveDir + "/player.sav";
        FILE* of = fopen(originalFile.c_str(), "wb");
        const char origContent[] = "ORIGINAL_SAVE_BEFORE_ANY_ACTION";
        fwrite(origContent, 1, sizeof(origContent), of);
        fclose(of);

        // 2. Prepara novo save valido vindo da nuvem
        std::string cloudSaveDir = testRoot + "/cloud_source";
        MKDIR(cloudSaveDir.c_str());
        std::string newSaveFile = cloudSaveDir + "/player.sav";
        FILE* nf = fopen(newSaveFile.c_str(), "wb");
        const char newContent[] = "NEW_UPDATED_SAVE_FROM_CLOUD";
        fwrite(newContent, 1, sizeof(newContent), nf);
        fclose(nf);

        std::string validZip = testRoot + "/valid_incoming.zip";
        ZipHelper::zipDirectory(cloudSaveDir, validZip);

        // 3. Executa restauracao transacional
        TransactionResult tr = executeTransactionalRestore(testTitleId, testUid, validZip, saveDir, backupDir);

        REQUIRE(tr.success == true);
        REQUIRE(tr.committed == true);
        REQUIRE_FALSE(tr.rollbackTriggered);
        REQUIRE(fsMock.commitCallCount == 1);

        // Verifica que o backup preventivo foi criado
        FILE* bf = fopen(tr.preventiveBackupPath.c_str(), "rb");
        REQUIRE(bf != nullptr);
        fclose(bf);

        // Verifica que os novos dados estao no save:/
        FILE* rf = fopen(originalFile.c_str(), "rb");
        REQUIRE(rf != nullptr);
        char readBuf[128] = {0};
        fread(readBuf, 1, sizeof(readBuf), rf);
        fclose(rf);
        REQUIRE(std::string(readBuf) == "NEW_UPDATED_SAVE_FROM_CLOUD");
    }

    SECTION("Scenario 2: Corrupted ZIP during restore triggers Rollback & Prevents fsdevCommitDevice") {
        fsMock.reset();

        // 1. Grava save original crítico do usuário
        std::string originalFile = saveDir + "/critical_progress.sav";
        FILE* of = fopen(originalFile.c_str(), "wb");
        const char origContent[] = "CRITICAL_PROGRESS_DO_NOT_LOSE_THIS";
        fwrite(origContent, 1, sizeof(origContent), of);
        fclose(of);

        // 2. Usa o fixture de save propositalmente corrompido
        std::string corruptedZip = "tests/fixtures/corrupted_save.zip";

        // 3. Executa restauracao
        TransactionResult tr = executeTransactionalRestore(testTitleId, testUid, corruptedZip, saveDir, backupDir);

        // 4. Verificacoes estritas de QA e Seguranca:
        // A) Transacao deve falhar
        REQUIRE_FALSE(tr.success);
        REQUIRE(tr.rollbackTriggered == true);

        // B) O commit dos dados corrompidos NUNCA deve ter ocorrido
        REQUIRE(tr.committed == false);

        // C) Os dados originais do usuario em save:/ DEVEM ter sido preservados via rollback
        FILE* rf = fopen(originalFile.c_str(), "rb");
        REQUIRE(rf != nullptr);
        char readBuf[128] = {0};
        fread(readBuf, 1, sizeof(readBuf), rf);
        fclose(rf);
        REQUIRE(std::string(readBuf) == "CRITICAL_PROGRESS_DO_NOT_LOSE_THIS");
    }

    SECTION("Scenario 3: Commit Failure Simulation Triggers Rollback") {
        fsMock.reset();
        fsMock.simulateCommitFailure = true; // Simula falha de gravacao na NAND/SD

        std::string originalFile = saveDir + "/savegame.dat";
        FILE* of = fopen(originalFile.c_str(), "wb");
        const char origContent[] = "MY_ORIGINAL_DATA";
        fwrite(origContent, 1, sizeof(origContent), of);
        fclose(of);

        std::string cloudSaveDir = testRoot + "/cloud_source";
        std::string validZip = testRoot + "/valid_incoming.zip";

        TransactionResult tr = executeTransactionalRestore(testTitleId, testUid, validZip, saveDir, backupDir);

        REQUIRE_FALSE(tr.success);
        REQUIRE(tr.rollbackTriggered == true);
    }
}

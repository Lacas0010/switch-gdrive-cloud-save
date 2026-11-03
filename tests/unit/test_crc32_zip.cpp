#include "catch.hpp"
#include "../../source/ZipHelper.hpp"
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <zlib.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0777)
#endif

#pragma pack(push, 1)
struct ZipLocalHeaderTest {
    uint32_t signature;       // 0x04034b50
    uint16_t version_needed;  // 20
    uint16_t flags;
    uint16_t method;          // 8 (Deflate)
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
};

struct ZipEOCDTest {
    uint32_t signature;       // 0x06054b50
    uint16_t disk_num;
    uint16_t cd_start_disk;
    uint16_t records_on_disk;
    uint16_t total_records;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
};
#pragma pack(pop)

TEST_CASE("Zip and CRC32 Engine - Integrity & Streaming Compression", "[zip_crc32]") {
    std::string testWorkDir = "tests_tmp_zip";
    MKDIR(testWorkDir.c_str());

    SECTION("Pre-Upload Validation: Abort if save directory is empty (0 files)") {
        std::string emptyDir = testWorkDir + "/empty_save";
        MKDIR(emptyDir.c_str());
        std::string outZip = testWorkDir + "/should_not_create.zip";
        remove(outZip.c_str());

        // zipDirectory deve retornar false ao tentar empacotar diretorio sem arquivos
        bool zipResult = ZipHelper::zipDirectory(emptyDir, outZip);
        REQUIRE_FALSE(zipResult);

        // O arquivo zip nao deve existir ou ser gerado vazio
        FILE* f = fopen(outZip.c_str(), "rb");
        REQUIRE(f == nullptr);
    }

    SECTION("Streaming 32KB Multi-Chunk Packaging & CRC32 Calculation") {
        std::string payloadDir = testWorkDir + "/payload_save";
        MKDIR(payloadDir.c_str());

        // Cria arquivo de 96 KB (3 blocos de 32 KB) para testar o streaming zlib em chunks
        std::string multiChunkFile = payloadDir + "/data_chunked.bin";
        std::vector<unsigned char> data(96 * 1024);
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<unsigned char>((i * 7 + 13) % 256);
        }

        FILE* fp = fopen(multiChunkFile.c_str(), "wb");
        REQUIRE(fp != nullptr);
        fwrite(data.data(), 1, data.size(), fp);
        fclose(fp);

        // Calcula CRC32 de referencia diretamente com zlib
        uLong refCrc = crc32(0L, data.data(), data.size());

        // Testa calculo via ZipHelper::calculateFileCrc32
        u32 calculatedCrc = 0;
        bool crcOk = ZipHelper::calculateFileCrc32(multiChunkFile, calculatedCrc);
        REQUIRE(crcOk);
        REQUIRE(calculatedCrc == static_cast<u32>(refCrc));

        // Empacota em ZIP
        std::string outZip = testWorkDir + "/test_output.zip";
        remove(outZip.c_str());
        bool zipOk = ZipHelper::zipDirectory(payloadDir, outZip);
        REQUIRE(zipOk);

        // Valida cabeçalhos binários do ZIP
        FILE* zf = fopen(outZip.c_str(), "rb");
        REQUIRE(zf != nullptr);

        ZipLocalHeaderTest lh{};
        REQUIRE(fread(&lh, sizeof(ZipLocalHeaderTest), 1, zf) == 1);
        REQUIRE(lh.signature == 0x04034b50); // Assinatura padrao PK0304
        REQUIRE(lh.method == 8);            // Metodo Deflate
        REQUIRE(lh.crc32 == calculatedCrc); // CRC32 gravado no cabecalho bate com payload
        REQUIRE(lh.uncompressed_size == data.size());

        fclose(zf);

        // Testa extracao e comparacao byte-a-byte
        std::string extractDir = testWorkDir + "/extracted";
        MKDIR(extractDir.c_str());
        bool unzipOk = ZipHelper::unzipToDirectory(outZip, extractDir);
        REQUIRE(unzipOk);

        std::string extractedFile = extractDir + "/data_chunked.bin";
        FILE* ef = fopen(extractedFile.c_str(), "rb");
        REQUIRE(ef != nullptr);
        std::vector<unsigned char> extractedData(data.size());
        size_t readBytes = fread(extractedData.data(), 1, data.size(), ef);
        fclose(ef);

        REQUIRE(readBytes == data.size());
        REQUIRE(extractedData == data);
    }

    SECTION("Corrupted ZIP File Handling") {
        // Testa tentativa de descompactar o fixture com payload corrompido propositalmente
        std::string corruptedZipPath = "tests/fixtures/corrupted_save.zip";
        std::string targetExtract = testWorkDir + "/corrupt_extract";
        MKDIR(targetExtract.c_str());

        // unzipToDirectory deve falhar ao encontrar assinatura invalida ou stream corrompido
        bool unzipRes = ZipHelper::unzipToDirectory(corruptedZipPath, targetExtract);
        REQUIRE_FALSE(unzipRes);
    }
}

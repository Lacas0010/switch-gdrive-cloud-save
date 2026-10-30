#include "ZipHelper.hpp"
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>

namespace {
#pragma pack(push, 1)
    struct LocalHeader {
        uint32_t signature;       // 0x04034b50
        uint16_t version_needed;  // 20
        uint16_t flags;           // 0
        uint16_t method;          // 8 (Deflate)
        uint16_t mod_time;
        uint16_t mod_date;
        uint32_t crc32;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint16_t filename_len;
        uint16_t extra_len;
    };

    struct CentralHeader {
        uint32_t signature;       // 0x02014b50
        uint16_t version_made_by; // 20
        uint16_t version_needed;  // 20
        uint16_t flags;
        uint16_t method;
        uint16_t mod_time;
        uint16_t mod_date;
        uint32_t crc32;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint16_t filename_len;
        uint16_t extra_len;
        uint16_t comment_len;
        uint16_t disk_start;
        uint16_t internal_attr;
        uint32_t external_attr;
        uint32_t local_header_offset;
    };

    struct EndOfCentralDir {
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

    struct FileEntryMeta {
        std::string relPath;
        uint32_t crc32;
        uint32_t compressedSize;
        uint32_t uncompressedSize;
        uint32_t localOffset;
    };

    constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024; // Buffer fixo de 64KB

    void listFilesRecursive(const std::string& baseDir, const std::string& subDir, std::vector<std::string>& fileList) {
        std::string fullDir = baseDir;
        if (!subDir.empty()) {
            if (fullDir.back() != '/') fullDir += '/';
            fullDir += subDir;
        }

        DIR* dir = opendir(fullDir.c_str());
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            std::string entryRel = subDir.empty() ? entry->d_name : (subDir + "/" + entry->d_name);
            std::string entryFull = fullDir + "/" + entry->d_name;

            struct stat st;
            if (stat(entryFull.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    listFilesRecursive(baseDir, entryRel, fileList);
                } else if (S_ISREG(st.st_mode)) {
                    fileList.push_back(entryRel);
                }
            }
        }
        closedir(dir);
    }
}

bool ZipHelper::calculateFileCrc32(const std::string& filePath, u32& outCrc) {
    FILE* f = fopen(filePath.c_str(), "rb");
    if (!f) return false;

    std::vector<unsigned char> buffer(STREAM_BUFFER_SIZE);
    uLong crc = crc32(0L, Z_NULL, 0);

    size_t bytesRead = 0;
    while ((bytesRead = fread(buffer.data(), 1, STREAM_BUFFER_SIZE, f)) > 0) {
        crc = crc32(crc, buffer.data(), bytesRead);
    }

    fclose(f);
    outCrc = static_cast<u32>(crc);
    return true;
}

bool ZipHelper::zipDirectory(const std::string& sourceDir, const std::string& outZipPath) {
    std::vector<std::string> fileList;
    listFilesRecursive(sourceDir, "", fileList);

    if (fileList.empty()) {
        // Nada para zipar
        return false;
    }

    FILE* outZip = fopen(outZipPath.c_str(), "wb");
    if (!outZip) return false;

    std::vector<unsigned char> inBuffer(STREAM_BUFFER_SIZE);
    std::vector<unsigned char> outBuffer(STREAM_BUFFER_SIZE);
    std::vector<FileEntryMeta> entries;

    for (const auto& relPath : fileList) {
        std::string fullPath = sourceDir;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += relPath;

        FILE* inFile = fopen(fullPath.c_str(), "rb");
        if (!inFile) continue;

        // Calcula CRC32 e tamanho descomprimido em stream
        uLong crcVal = crc32(0L, Z_NULL, 0);
        uint32_t uncompressedSize = 0;
        size_t r = 0;
        while ((r = fread(inBuffer.data(), 1, STREAM_BUFFER_SIZE, inFile)) > 0) {
            crcVal = crc32(crcVal, inBuffer.data(), r);
            uncompressedSize += r;
        }
        fseek(inFile, 0, SEEK_SET);

        uint32_t localOffset = ftell(outZip);

        // Preenche LocalHeader temporario
        LocalHeader lh{};
        lh.signature = 0x04034b50;
        lh.version_needed = 20;
        lh.flags = 0;
        lh.method = 8; // Deflate
        lh.crc32 = static_cast<uint32_t>(crcVal);
        lh.uncompressed_size = uncompressedSize;
        lh.filename_len = static_cast<uint16_t>(relPath.size());
        lh.extra_len = 0;

        // Escreve header temporario (compressed_size sera atualizado depois)
        fwrite(&lh, sizeof(LocalHeader), 1, outZip);
        fwrite(relPath.data(), 1, relPath.size(), outZip);

        uint32_t compressedStart = ftell(outZip);

        // Comprime com zlib em blocos
        z_stream strm{};
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            fclose(inFile);
            continue;
        }

        while (true) {
            size_t bytesRead = fread(inBuffer.data(), 1, STREAM_BUFFER_SIZE, inFile);
            strm.avail_in = bytesRead;
            strm.next_in = inBuffer.data();
            int flush = (bytesRead < STREAM_BUFFER_SIZE) ? Z_FINISH : Z_NO_FLUSH;

            int ret = Z_OK;
            do {
                strm.avail_out = STREAM_BUFFER_SIZE;
                strm.next_out = outBuffer.data();
                ret = deflate(&strm, flush);
                size_t have = STREAM_BUFFER_SIZE - strm.avail_out;
                if (have > 0) {
                    fwrite(outBuffer.data(), 1, have, outZip);
                }
            } while (strm.avail_out == 0 && ret != Z_STREAM_END);

            if (flush == Z_FINISH || ret == Z_STREAM_END) {
                break;
            }
        }

        deflateEnd(&strm);
        fclose(inFile);

        uint32_t compressedEnd = ftell(outZip);
        uint32_t compressedSize = compressedEnd - compressedStart;

        // Volta e corrige o compressed_size no LocalHeader
        fseek(outZip, localOffset + offsetof(LocalHeader, compressed_size), SEEK_SET);
        fwrite(&compressedSize, sizeof(uint32_t), 1, outZip);
        fseek(outZip, compressedEnd, SEEK_SET);

        FileEntryMeta meta;
        meta.relPath = relPath;
        meta.crc32 = static_cast<uint32_t>(crcVal);
        meta.compressedSize = compressedSize;
        meta.uncompressedSize = uncompressedSize;
        meta.localOffset = localOffset;
        entries.push_back(meta);
    }

    // Escreve Central Directory
    uint32_t cdOffset = ftell(outZip);
    for (const auto& meta : entries) {
        CentralHeader ch{};
        ch.signature = 0x02014b50;
        ch.version_made_by = 20;
        ch.version_needed = 20;
        ch.flags = 0;
        ch.method = 8;
        ch.crc32 = meta.crc32;
        ch.compressed_size = meta.compressedSize;
        ch.uncompressed_size = meta.uncompressedSize;
        ch.filename_len = static_cast<uint16_t>(meta.relPath.size());
        ch.local_header_offset = meta.localOffset;

        fwrite(&ch, sizeof(CentralHeader), 1, outZip);
        fwrite(meta.relPath.data(), 1, meta.relPath.size(), outZip);
    }

    uint32_t cdEnd = ftell(outZip);
    uint32_t cdSize = cdEnd - cdOffset;

    // End of Central Directory
    EndOfCentralDir eocd{};
    eocd.signature = 0x06054b50;
    eocd.records_on_disk = static_cast<uint16_t>(entries.size());
    eocd.total_records = static_cast<uint16_t>(entries.size());
    eocd.cd_size = cdSize;
    eocd.cd_offset = cdOffset;

    fwrite(&eocd, sizeof(EndOfCentralDir), 1, outZip);
    fclose(outZip);

    return true;
}

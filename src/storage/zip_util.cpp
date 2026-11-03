#include "zip_util.hpp"
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR_SYS(p) _mkdir(p)
#else
#define MKDIR_SYS(p) mkdir(p, 0777)
#endif

namespace Storage {

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

    constexpr size_t STREAM_BUFFER_SIZE = 32 * 1024; // 32 KB blocks

    static void mkdirs(const std::string& path) {
        std::string cur;
        for (size_t i = 0; i < path.size(); ++i) {
            cur += path[i];
            if (path[i] == '/' || i == path.size() - 1) {
                MKDIR_SYS(cur.c_str());
            }
        }
    }
}

void ZipUtil::listFilesRecursive(
    std::string_view baseDir,
    std::string_view subDir,
    std::vector<std::string>& fileList
) {
    std::string fullDir(baseDir);
    if (!subDir.empty()) {
        if (!fullDir.empty() && fullDir.back() != '/') fullDir += '/';
        fullDir.append(subDir);
    }

    DIR* dir = opendir(fullDir.c_str());
    if (!dir) return;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;

        std::string entryRel = subDir.empty() ? entry->d_name : (std::string(subDir) + "/" + entry->d_name);
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

bool ZipUtil::cleanDirectory(std::string_view dirPath) {
    std::string base(dirPath);
    if (base.empty()) return false;
    if (base.back() != '/') base += '/';

    DIR* dir = opendir(base.c_str());
    if (!dir) return false;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;

        std::string fullPath = base + entry->d_name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                cleanDirectory(fullPath);
                rmdir(fullPath.c_str());
            } else {
                std::remove(fullPath.c_str());
            }
        }
    }
    closedir(dir);
    return true;
}

bool ZipUtil::calculateFileCrc32(std::string_view filePath, u32& outCrc) {
    std::string pathStr(filePath);
    auto fp = Core::openFile(pathStr.c_str(), "rb");
    if (!fp) return false;

    std::vector<unsigned char> buffer(STREAM_BUFFER_SIZE);
    uLong crc = crc32(0L, Z_NULL, 0);

    size_t bytesRead = 0;
    while ((bytesRead = std::fread(buffer.data(), 1, STREAM_BUFFER_SIZE, fp.get())) > 0) {
        crc = crc32(crc, buffer.data(), static_cast<uInt>(bytesRead));
    }

    outCrc = static_cast<u32>(crc);
    return true;
}

bool ZipUtil::zipDirectory(
    std::string_view sourceDir,
    std::string_view outZipPath,
    const std::atomic<bool>* cancelToken,
    ProgressCallback onProgress
) {
    std::vector<std::string> fileList;
    listFilesRecursive(sourceDir, "", fileList);

    if (fileList.empty()) {
        return false;
    }

    std::string outZipStr(outZipPath);
    auto outZip = Core::openFile(outZipStr.c_str(), "wb");
    if (!outZip) return false;

    std::vector<unsigned char> inBuffer(STREAM_BUFFER_SIZE);
    std::vector<unsigned char> outBuffer(STREAM_BUFFER_SIZE);
    std::vector<FileEntryMeta> entries;

    const size_t totalFiles = fileList.size();

    for (size_t fIdx = 0; fIdx < totalFiles; ++fIdx) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            outZip.reset();
            std::remove(outZipStr.c_str());
            return false;
        }

        const auto& relPath = fileList[fIdx];
        if (onProgress) {
            onProgress(relPath, static_cast<float>(fIdx) / static_cast<float>(totalFiles));
        }

        std::string fullPath(sourceDir);
        if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
        fullPath += relPath;

        auto inFile = Core::openFile(fullPath.c_str(), "rb");
        if (!inFile) continue;

        uLong crcVal = crc32(0L, Z_NULL, 0);
        uint32_t uncompressedSize = 0;
        size_t r = 0;
        while ((r = std::fread(inBuffer.data(), 1, STREAM_BUFFER_SIZE, inFile.get())) > 0) {
            crcVal = crc32(crcVal, inBuffer.data(), static_cast<uInt>(r));
            uncompressedSize += static_cast<uint32_t>(r);
        }
        std::fseek(inFile.get(), 0, SEEK_SET);

        uint32_t localOffset = static_cast<uint32_t>(std::ftell(outZip.get()));

        LocalHeader lh{};
        lh.signature = 0x04034b50;
        lh.version_needed = 20;
        lh.flags = 0;
        lh.method = 8; // Deflate
        lh.crc32 = static_cast<uint32_t>(crcVal);
        lh.uncompressed_size = uncompressedSize;
        lh.filename_len = static_cast<uint16_t>(relPath.size());
        lh.extra_len = 0;

        std::fwrite(&lh, sizeof(LocalHeader), 1, outZip.get());
        std::fwrite(relPath.data(), 1, relPath.size(), outZip.get());

        uint32_t compressedStart = static_cast<uint32_t>(std::ftell(outZip.get()));

        z_stream strm{};
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            continue;
        }

        while (true) {
            if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
                deflateEnd(&strm);
                outZip.reset();
                std::remove(outZipStr.c_str());
                return false;
            }

            size_t bytesRead = std::fread(inBuffer.data(), 1, STREAM_BUFFER_SIZE, inFile.get());
            strm.avail_in = static_cast<uInt>(bytesRead);
            strm.next_in = inBuffer.data();
            int flush = (bytesRead < STREAM_BUFFER_SIZE) ? Z_FINISH : Z_NO_FLUSH;

            int ret = Z_OK;
            do {
                strm.avail_out = static_cast<uInt>(STREAM_BUFFER_SIZE);
                strm.next_out = outBuffer.data();
                ret = deflate(&strm, flush);
                size_t have = STREAM_BUFFER_SIZE - strm.avail_out;
                if (have > 0) {
                    std::fwrite(outBuffer.data(), 1, have, outZip.get());
                }
            } while (strm.avail_out == 0 && ret != Z_STREAM_END);

            if (flush == Z_FINISH || ret == Z_STREAM_END) {
                break;
            }
        }

        deflateEnd(&strm);
        inFile.reset();

        uint32_t compressedEnd = static_cast<uint32_t>(std::ftell(outZip.get()));
        uint32_t compressedSize = compressedEnd - compressedStart;

        std::fseek(outZip.get(), localOffset + offsetof(LocalHeader, compressed_size), SEEK_SET);
        std::fwrite(&compressedSize, sizeof(uint32_t), 1, outZip.get());
        std::fseek(outZip.get(), compressedEnd, SEEK_SET);

        FileEntryMeta meta;
        meta.relPath = relPath;
        meta.crc32 = static_cast<uint32_t>(crcVal);
        meta.compressedSize = compressedSize;
        meta.uncompressedSize = uncompressedSize;
        meta.localOffset = localOffset;
        entries.push_back(meta);
    }

    uint32_t cdOffset = static_cast<uint32_t>(std::ftell(outZip.get()));

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

        std::fwrite(&ch, sizeof(CentralHeader), 1, outZip.get());
        std::fwrite(meta.relPath.data(), 1, meta.relPath.size(), outZip.get());
    }

    uint32_t cdEnd = static_cast<uint32_t>(std::ftell(outZip.get()));
    uint32_t cdSize = cdEnd - cdOffset;

    EndOfCentralDir eocd{};
    eocd.signature = 0x06054b50;
    eocd.records_on_disk = static_cast<uint16_t>(entries.size());
    eocd.total_records = static_cast<uint16_t>(entries.size());
    eocd.cd_size = cdSize;
    eocd.cd_offset = cdOffset;

    std::fwrite(&eocd, sizeof(EndOfCentralDir), 1, outZip.get());
    return true;
}

bool ZipUtil::unzipToDirectory(
    std::string_view zipPath,
    std::string_view targetDir,
    const std::atomic<bool>* cancelToken
) {
    std::string zipPathStr(zipPath);
    auto fp = Core::openFile(zipPathStr.c_str(), "rb");
    if (!fp) return false;

    std::fseek(fp.get(), 0, SEEK_END);
    long fileSize = std::ftell(fp.get());
    if (fileSize < static_cast<long>(sizeof(EndOfCentralDir))) {
        return false;
    }

    long searchLen = std::min(fileSize, 65536L + static_cast<long>(sizeof(EndOfCentralDir)));
    long searchStart = fileSize - searchLen;
    std::fseek(fp.get(), searchStart, SEEK_SET);

    std::vector<unsigned char> searchBuf(static_cast<size_t>(searchLen));
    if (std::fread(searchBuf.data(), 1, static_cast<size_t>(searchLen), fp.get()) != static_cast<size_t>(searchLen)) {
        return false;
    }

    long eocdOffset = -1;
    for (long i = searchLen - static_cast<long>(sizeof(EndOfCentralDir)); i >= 0; --i) {
        uint32_t sig = *reinterpret_cast<uint32_t*>(&searchBuf[static_cast<size_t>(i)]);
        if (sig == 0x06054b50) {
            eocdOffset = searchStart + i;
            break;
        }
    }

    if (eocdOffset < 0) {
        return false;
    }

    std::fseek(fp.get(), eocdOffset, SEEK_SET);
    EndOfCentralDir eocd{};
    if (std::fread(&eocd, sizeof(EndOfCentralDir), 1, fp.get()) != 1) {
        return false;
    }

    std::string baseOut(targetDir);
    if (baseOut.empty()) baseOut = "./";
    if (baseOut.back() != '/') baseOut += '/';

    std::fseek(fp.get(), static_cast<long>(eocd.cd_offset), SEEK_SET);
    std::vector<unsigned char> inBuffer(STREAM_BUFFER_SIZE);
    std::vector<unsigned char> outBuffer(STREAM_BUFFER_SIZE);

    for (uint16_t i = 0; i < eocd.total_records; ++i) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            return false;
        }

        CentralHeader ch{};
        if (std::fread(&ch, sizeof(CentralHeader), 1, fp.get()) != 1 || ch.signature != 0x02014b50) {
            return false;
        }

        std::string filename(ch.filename_len, '\0');
        if (std::fread(&filename[0], 1, ch.filename_len, fp.get()) != ch.filename_len) {
            return false;
        }

        if (ch.extra_len > 0) std::fseek(fp.get(), ch.extra_len, SEEK_CUR);
        if (ch.comment_len > 0) std::fseek(fp.get(), ch.comment_len, SEEK_CUR);

        long nextCdPos = std::ftell(fp.get());
        std::string fullOut = baseOut + filename;

        if (filename.back() == '/') {
            mkdirs(fullOut);
            std::fseek(fp.get(), nextCdPos, SEEK_SET);
            continue;
        }

        size_t lastSlash = fullOut.rfind('/');
        if (lastSlash != std::string::npos) {
            mkdirs(fullOut.substr(0, lastSlash));
        }

        std::fseek(fp.get(), static_cast<long>(ch.local_header_offset), SEEK_SET);
        LocalHeader lh{};
        if (std::fread(&lh, sizeof(LocalHeader), 1, fp.get()) != 1 || lh.signature != 0x04034b50) {
            return false;
        }

        std::fseek(fp.get(), lh.filename_len + lh.extra_len, SEEK_CUR);

        auto outFp = Core::openFile(fullOut.c_str(), "wb");
        if (!outFp) {
            std::fseek(fp.get(), nextCdPos, SEEK_SET);
            continue;
        }

        if (ch.method == 0) { // Store
            size_t remaining = ch.uncompressed_size;
            while (remaining > 0) {
                size_t toRead = std::min(remaining, STREAM_BUFFER_SIZE);
                size_t r = std::fread(inBuffer.data(), 1, toRead, fp.get());
                if (r == 0) break;
                std::fwrite(inBuffer.data(), 1, r, outFp.get());
                remaining -= r;
            }
        } else if (ch.method == 8) { // Deflate
            z_stream strm{};
            if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
                return false;
            }

            size_t remainingComp = ch.compressed_size;
            int ret = Z_OK;
            while (remainingComp > 0 && ret != Z_STREAM_END) {
                size_t toRead = std::min(remainingComp, STREAM_BUFFER_SIZE);
                size_t r = std::fread(inBuffer.data(), 1, toRead, fp.get());
                if (r == 0) break;
                remainingComp -= r;

                strm.avail_in = static_cast<uInt>(r);
                strm.next_in = inBuffer.data();

                do {
                    strm.avail_out = static_cast<uInt>(STREAM_BUFFER_SIZE);
                    strm.next_out = outBuffer.data();
                    ret = inflate(&strm, Z_NO_FLUSH);
                    size_t have = STREAM_BUFFER_SIZE - strm.avail_out;
                    if (have > 0) {
                        std::fwrite(outBuffer.data(), 1, have, outFp.get());
                    }
                } while (strm.avail_out == 0 && ret != Z_STREAM_END);
            }
            inflateEnd(&strm);

            if (ret != Z_STREAM_END && ret != Z_OK) {
                return false; // Falha de descompressao / stream truncado
            }
        }

        outFp.reset();
        std::fseek(fp.get(), nextCdPos, SEEK_SET);
    }

    return true;
}

} // namespace Storage

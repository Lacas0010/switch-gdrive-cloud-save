#pragma once
#ifdef __SWITCH__
#include <switch.h>
#else
#include <cstdint>
typedef uint32_t u32;
typedef uint64_t u64;
#endif
#include <string>
#include <vector>

class ZipHelper {
public:
    static bool zipDirectory(const std::string& sourceDir, const std::string& outZipPath);
    static bool unzipToDirectory(const std::string& zipPath, const std::string& targetDir);
    static bool cleanDirectory(const std::string& dirPath);
    static void listFilesRecursive(const std::string& baseDir, const std::string& subDir, std::vector<std::string>& fileList);
    static bool calculateFileCrc32(const std::string& filePath, u32& outCrc);
};

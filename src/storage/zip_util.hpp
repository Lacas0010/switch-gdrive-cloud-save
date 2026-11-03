#pragma once
#include "../common.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <functional>

namespace Storage {

    class ZipUtil {
    public:
        // Callback para relatar progresso da compactação e permitir cancelamento gracioso
        using ProgressCallback = std::function<void(std::string_view currentFile, float progress)>;

        // Compacta recursivamente o conteudo de sourceDir para outZipPath
        static bool zipDirectory(
            std::string_view sourceDir,
            std::string_view outZipPath,
            const std::atomic<bool>* cancelToken = nullptr,
            ProgressCallback onProgress = nullptr
        );

        // Descompacta um arquivo ZIP para targetDir
        static bool unzipToDirectory(
            std::string_view zipPath,
            std::string_view targetDir,
            const std::atomic<bool>* cancelToken = nullptr
        );

        // Apaga recursivamente todos os arquivos e subpastas dentro de dirPath
        static bool cleanDirectory(std::string_view dirPath);

        // Lista arquivos recursivamente
        static void listFilesRecursive(
            std::string_view baseDir,
            std::string_view subDir,
            std::vector<std::string>& fileList
        );

        // Calcula CRC32 do arquivo em blocos de 32 KB
        static bool calculateFileCrc32(std::string_view filePath, u32& outCrc);
    };

} // namespace Storage

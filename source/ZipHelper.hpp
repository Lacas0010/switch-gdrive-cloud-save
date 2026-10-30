#pragma once
#include <switch.h>
#include <string>
#include <vector>

class ZipHelper {
public:
    // Compacta recursivamente o conteudo de sourceDir (ex: "save:/") para outZipPath
    static bool zipDirectory(const std::string& sourceDir, const std::string& outZipPath);

    // Descompacta um arquivo ZIP para targetDir (ex: "save:/")
    static bool unzipToDirectory(const std::string& zipPath, const std::string& targetDir);

    // Apaga recursivamente todos os arquivos e subpastas dentro de dirPath
    static bool cleanDirectory(const std::string& dirPath);

    // Lista arquivos recursivamente
    static void listFilesRecursive(const std::string& baseDir, const std::string& subDir, std::vector<std::string>& fileList);

    // Calcula CRC32 do arquivo para checagem rapida de alteracoes
    static bool calculateFileCrc32(const std::string& filePath, u32& outCrc);
};

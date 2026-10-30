#pragma once
#include <switch.h>
#include <string>
#include <vector>

class ZipHelper {
public:
    // Compacta recursivamente o conteudo de sourceDir (ex: "save:/") para outZipPath
    // Utiliza buffer de stream de 64KB para nao impactar o Heap do Sysmodule
    static bool zipDirectory(const std::string& sourceDir, const std::string& outZipPath);

    // Calcula CRC32 do arquivo para checagem rapida de alteracoes
    static bool calculateFileCrc32(const std::string& filePath, u32& outCrc);
};

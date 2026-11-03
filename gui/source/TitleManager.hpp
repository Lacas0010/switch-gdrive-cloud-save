#pragma once
#include <switch.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <ctime>

namespace Assets {

    struct TextureHandle {
        int width  = 0;
        int height = 0;
        std::vector<u32> pixels; // RGBA8888
        bool valid = false;
    };

    struct GameEntry {
        u64 titleId = 0;
        std::string titleName;
        u64 saveSize = 0;
        std::time_t lastModified = 0;
        bool selected = false;
        bool isFavorite = false;
    };

    class TitleManager {
    private:
        bool m_nsInitialized = false;
        std::unordered_map<u64, TextureHandle> m_iconCache;
        std::unordered_map<u64, std::string>   m_nameCache;

        TextureHandle decodeJpeg(const void* buffer, size_t size);
        TextureHandle createFallbackIcon(u64 titleId, const std::string& name);

    public:
        TitleManager();
        ~TitleManager();

        bool init();
        void close();

        // Consulta e decodifica o ícone oficial JPEG via ns:app (com cache permanente)
        const TextureHandle* getTitleIcon(u64 titleId, const std::string& nameFallback = "");

        // Obtém o nome oficial do jogo gravado no NACP do aplicativo
        std::string getTitleName(u64 titleId, const std::string& fallback = "");

        // Limpa cache se necessário
        void clearCache();
    };

} // namespace Assets

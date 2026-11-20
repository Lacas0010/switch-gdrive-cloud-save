#include "texture_cache.hpp"
#include "../utils/log.hpp"
#include <cstdio>
#include <cstring>

#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace UI {

    namespace {
        // Verifica se uma string UTF-8 contém caracteres latinos/ASCII legíveis (evita fallback CJK indesejado)
        static bool hasReadableLatin(std::string_view s) {
            if (s.empty()) return false;
            int asciiLetters = 0;
            for (unsigned char c : s) {
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    asciiLetters++;
                }
            }
            return asciiLetters >= 2;
        }

        // Sanitiza string NACP: converte bytes Latin-1/Windows-1252 inválidos para UTF-8 e remove caracteres de controle
        static std::string sanitizeNacpString(const char* raw, size_t maxLen) {
            if (!raw || maxLen == 0) return "";
            size_t len = 0;
            while (len < maxLen && raw[len] != '\0') ++len;

            std::string out;
            out.reserve(len);

            size_t i = 0;
            while (i < len) {
                unsigned char c = static_cast<unsigned char>(raw[i]);
                if (c < 0x20) {
                    if (c == '\n' || c == '\r' || c == '\t') {
                        if (!out.empty() && out.back() != ' ') out.push_back(' ');
                    }
                    ++i;
                    continue;
                }
                if (c < 0x80) {
                    out.push_back(static_cast<char>(c));
                    ++i;
                    continue;
                }
                // Valida sequência UTF-8 de 2 bytes
                if ((c & 0xE0) == 0xC0 && i + 1 < len) {
                    unsigned char c2 = static_cast<unsigned char>(raw[i + 1]);
                    if ((c2 & 0xC0) == 0x80) {
                        out.push_back(static_cast<char>(c));
                        out.push_back(static_cast<char>(c2));
                        i += 2;
                        continue;
                    }
                }
                // Valida sequência UTF-8 de 3 bytes
                if ((c & 0xF0) == 0xE0 && i + 2 < len) {
                    unsigned char c2 = static_cast<unsigned char>(raw[i + 1]);
                    unsigned char c3 = static_cast<unsigned char>(raw[i + 2]);
                    if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                        uint32_t cp = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                        // Ignora símbolos de marca registrada (™ U+2122) ou substitui traços/aspas especiais
                        if (cp == 0x2122) {
                            i += 3;
                            continue;
                        }
                        out.push_back(static_cast<char>(c));
                        out.push_back(static_cast<char>(c2));
                        out.push_back(static_cast<char>(c3));
                        i += 3;
                        continue;
                    }
                }
                // Valida sequência UTF-8 de 4 bytes
                if ((c & 0xF8) == 0xF0 && i + 3 < len) {
                    unsigned char c2 = static_cast<unsigned char>(raw[i + 1]);
                    unsigned char c3 = static_cast<unsigned char>(raw[i + 2]);
                    unsigned char c4 = static_cast<unsigned char>(raw[i + 3]);
                    if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80 && (c4 & 0xC0) == 0x80) {
                        out.push_back(static_cast<char>(c));
                        out.push_back(static_cast<char>(c2));
                        out.push_back(static_cast<char>(c3));
                        out.push_back(static_cast<char>(c4));
                        i += 4;
                        continue;
                    }
                }
                // Byte Latin-1/ISO-8859-1 isolado (comum em traduções/mods BR de NACP): converte para UTF-8
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                ++i;
            }
            return out;
        }
    } // namespace

    TextureCache::TextureCache()
        : m_initialized(false) {
        m_defaultMeta.name = "Jogo Desconhecido";
        m_defaultMeta.author = "Nintendo";
        m_defaultMeta.version = "v1.0.0";
    }

    TextureCache::~TextureCache() {
        cleanup();
    }

    void TextureCache::init() {
#ifdef __SWITCH__
        if (!m_initialized) {
            Result rc = nsInitialize();
            m_initialized = R_SUCCEEDED(rc);
        }
#else
        m_initialized = true;
#endif
    }

    void TextureCache::cleanup() {
#ifdef __SWITCH__
        if (m_initialized) {
            nsExit();
            m_initialized = false;
        }
#endif
        m_cache.clear();
    }

    bool TextureCache::loadControlData(u64 titleId, GameMetadata& outMeta) {
        outMeta.titleId = titleId;
        char hexBuf[32];
        std::snprintf(hexBuf, sizeof(hexBuf), "%016llX", static_cast<unsigned long long>(titleId));
        outMeta.name = "Jogo 0x" + std::string(hexBuf);
        outMeta.author = "Nintendo Switch";
        outMeta.version = "1.0.0";

#ifdef __SWITCH__
        if (!m_initialized) init();
        if (!m_initialized) return false;

        auto controlData = std::make_unique<NsApplicationControlData>();
        std::memset(controlData.get(), 0, sizeof(NsApplicationControlData));
        u64 actualSize = 0;

        Result rc = nsGetApplicationControlData(
            NsApplicationControlSource_Storage,
            titleId,
            controlData.get(),
            sizeof(NsApplicationControlData),
            &actualSize
        );

        if (R_SUCCEEDED(rc) && actualSize >= sizeof(controlData->nacp)) {
            std::string chosenName;
            std::string chosenAuthor;

            // 1. Tenta idioma do sistema via libnx
            NacpLanguageEntry* langEntry = nullptr;
            Result nrc = nacpGetLanguageEntry(&controlData->nacp, &langEntry);
            if (R_SUCCEEDED(nrc) && langEntry && langEntry->name[0] != '\0') {
                std::string candidate = sanitizeNacpString(langEntry->name, sizeof(langEntry->name));
                if (hasReadableLatin(candidate)) {
                    chosenName = candidate;
                    chosenAuthor = sanitizeNacpString(langEntry->author, sizeof(langEntry->author));
                }
            }

            // 2. Fallback inteligente priorizando PT-BR, PT, EN-US, EN-GB, ES (evita fallback CJK/japonês)
            if (chosenName.empty()) {
                static constexpr int kPreferredSlots[] = {15, 10, 0, 1, 5, 6, 3, 4, 7, 8, 9, 2, 11, 12, 13, 14};
                for (int slot : kPreferredSlots) {
                    const NacpLanguageEntry& entry = controlData->nacp.lang[slot];
                    if (entry.name[0] != '\0') {
                        std::string candidate = sanitizeNacpString(entry.name, sizeof(entry.name));
                        if (!candidate.empty()) {
                            chosenName = candidate;
                            chosenAuthor = sanitizeNacpString(entry.author, sizeof(entry.author));
                            if (hasReadableLatin(candidate)) {
                                break;
                            }
                        }
                    }
                }
            }

            if (!chosenName.empty()) {
                outMeta.name = chosenName;
            }
            if (!chosenAuthor.empty()) {
                outMeta.author = chosenAuthor;
            }
            if (controlData->nacp.display_version[0] != '\0') {
                outMeta.version = sanitizeNacpString(controlData->nacp.display_version, sizeof(controlData->nacp.display_version));
            }

            // Decodifica ícone JPEG oficial
            if (actualSize > sizeof(controlData->nacp)) {
                size_t iconBytesSize = actualSize - sizeof(controlData->nacp);
                if (iconBytesSize > 0 && iconBytesSize <= sizeof(controlData->icon)) {
                    int w = 0, h = 0, channels = 0;
                    unsigned char* decoded = stbi_load_from_memory(
                        controlData->icon,
                        static_cast<int>(iconBytesSize),
                        &w, &h, &channels, 4
                    );

                    if (decoded && w > 0 && h > 0) {
                        outMeta.icon.width = w;
                        outMeta.icon.height = h;
                        outMeta.icon.pixels.resize(static_cast<size_t>(w * h));
                        std::memcpy(outMeta.icon.pixels.data(), decoded, static_cast<size_t>(w * h * 4));
                        outMeta.icon.valid = true;
                        stbi_image_free(decoded);
                    }
                }
            }
            return true;
        }
#endif
        return false;
    }

    const GameMetadata& TextureCache::getGameMetadata(u64 titleId) {
        auto it = m_cache.find(titleId);
        if (it != m_cache.end()) {
            return it->second;
        }

        GameMetadata meta;
        loadControlData(titleId, meta);
        auto insertRes = m_cache.emplace(titleId, std::move(meta));
        return insertRes.first->second;
    }

    std::string TextureCache::getTitleName(u64 titleId) {
        return getGameMetadata(titleId).name;
    }

} // namespace UI

#include "texture_cache.hpp"
#include "../utils/log.hpp"
#include <cstdio>
#include <cstring>
#include <zlib.h>

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
                if ((c & 0xE0) == 0xC0 && i + 1 < len) {
                    unsigned char c2 = static_cast<unsigned char>(raw[i + 1]);
                    if ((c2 & 0xC0) == 0x80) {
                        out.push_back(static_cast<char>(c));
                        out.push_back(static_cast<char>(c2));
                        i += 2;
                        continue;
                    }
                }
                if ((c & 0xF0) == 0xE0 && i + 2 < len) {
                    unsigned char c2 = static_cast<unsigned char>(raw[i + 1]);
                    unsigned char c3 = static_cast<unsigned char>(raw[i + 2]);
                    if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                        uint32_t cp = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                        if (cp == 0x2122) { // Remove símbolo ™ (U+2122)
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
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                ++i;
            }
            return out;
        }

#ifdef __SWITCH__
        // Descomprime NACP comprimido (Horizon OS 19.x/20.x+, titles_data_format == 1 em offset 0x3215)
        // Bloco 0x0000..0x2FFF: u16 compSize (little-endian) + payload raw DEFLATE -> 0x6000 bytes (32 x NacpLanguageEntry)
        static bool decompressNacpLangEntries(const uint8_t* nacpRaw, std::vector<NacpLanguageEntry>& outEntries) {
            uint16_t compSize = static_cast<uint16_t>(nacpRaw[0]) | (static_cast<uint16_t>(nacpRaw[1]) << 8);
            if (compSize == 0 || compSize > (0x3000 - 2)) {
                return false;
            }

            outEntries.assign(32, NacpLanguageEntry{});
            const size_t expectedOutBytes = 32 * sizeof(NacpLanguageEntry); // 0x6000

            auto tryInflate = [&](int windowBits) -> bool {
                std::memset(outEntries.data(), 0, expectedOutBytes);
                z_stream zs{};
                zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(nacpRaw + 2));
                zs.avail_in = compSize;
                zs.next_out = reinterpret_cast<Bytef*>(outEntries.data());
                zs.avail_out = static_cast<uInt>(expectedOutBytes);

                if (inflateInit2(&zs, windowBits) != Z_OK) {
                    return false;
                }
                int ret = inflate(&zs, Z_FINISH);
                size_t produced = zs.total_out;
                inflateEnd(&zs);
                return (ret == Z_STREAM_END || (ret == Z_OK && produced >= sizeof(NacpLanguageEntry)));
            };

            // Tenta raw DEFLATE (-MAX_WBITS = -15) conforme especificação HOS 19+/20+, depois zlib padrão
            if (tryInflate(-MAX_WBITS)) return true;
            if (tryInflate(MAX_WBITS)) return true;
            return false;
        }

        static void selectBestLangEntry(
            const NacpLanguageEntry* entries,
            size_t count,
            std::string& outName,
            std::string& outAuthor
        ) {
            // Prioridade: PT-BR (15), PT (10), EN-US (0), EN-GB (1), ES-419 (5), ES (6), demais latinos, depois restantes
            static constexpr size_t kPreferredSlots[] = {
                15, 10, 0, 1, 5, 6, 3, 4, 7, 8, 9, 2, 11, 12, 13, 14
            };
            for (size_t slot : kPreferredSlots) {
                if (slot >= count) continue;
                if (entries[slot].name[0] != '\0') {
                    std::string candidate = sanitizeNacpString(entries[slot].name, sizeof(entries[slot].name));
                    if (!candidate.empty()) {
                        outName = candidate;
                        outAuthor = sanitizeNacpString(entries[slot].author, sizeof(entries[slot].author));
                        if (hasReadableLatin(candidate)) {
                            return;
                        }
                    }
                }
            }
            for (size_t slot = 16; slot < count; ++slot) {
                if (entries[slot].name[0] != '\0') {
                    std::string candidate = sanitizeNacpString(entries[slot].name, sizeof(entries[slot].name));
                    if (!candidate.empty() && hasReadableLatin(candidate)) {
                        outName = candidate;
                        outAuthor = sanitizeNacpString(entries[slot].author, sizeof(entries[slot].author));
                        return;
                    }
                }
            }
        }
#endif
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

            const uint8_t* nacpRaw = reinterpret_cast<const uint8_t*>(&controlData->nacp);
            uint8_t titlesDataFormat = nacpRaw[0x3215];

            if (titlesDataFormat == 1) {
                // Formato NACP comprimido (HOS 19+/20+: Zelda BotW 1.9.0+, Mario Wonder atualizado, etc.)
                std::vector<NacpLanguageEntry> decompressedEntries;
                if (decompressNacpLangEntries(nacpRaw, decompressedEntries)) {
                    selectBestLangEntry(decompressedEntries.data(), decompressedEntries.size(), chosenName, chosenAuthor);
                }
            } else {
                // Formato NACP clássico (descomprimido, 16 slots em 0x0000..0x2FFF)
                NacpLanguageEntry* langEntry = nullptr;
                Result nrc = nacpGetLanguageEntry(&controlData->nacp, &langEntry);
                if (R_SUCCEEDED(nrc) && langEntry && langEntry->name[0] != '\0') {
                    std::string candidate = sanitizeNacpString(langEntry->name, sizeof(langEntry->name));
                    if (hasReadableLatin(candidate)) {
                        chosenName = candidate;
                        chosenAuthor = sanitizeNacpString(langEntry->author, sizeof(langEntry->author));
                    }
                }
                if (chosenName.empty()) {
                    selectBestLangEntry(controlData->nacp.lang, 16, chosenName, chosenAuthor);
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

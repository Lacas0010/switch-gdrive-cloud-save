#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <map>
#include <ctime>

struct GameSaveInfo {
    u64 titleId = 0;
    std::string titleName;
    u64 saveSize = 0;
    bool selected = false;
};

class SaveManager {
public:
    SaveManager();
    ~SaveManager();

    // Obtem o AccountUid do usuario ativo/recente no console
    bool getActiveAccount(AccountUid& outUid);

    // Lista todos os jogos que possuem save data para o usuario especificado (ou ativo)
    std::vector<GameSaveInfo> listAvailableSaves(AccountUid uid);

    // Processa o save: monta "save:/", compacta em .zip temporario no SD e verifica alteracao via CRC32
    // Se forceArchive = true, preserva o arquivo .zip mesmo se o CRC for identico ao anterior (util para uploads manuais)
    bool processGameSave(u64 titleId, AccountUid uid, std::string& outZipPath, bool& outChanged, bool forceArchive = false);

    // Atualiza o estado salvo apos o upload com sucesso
    void markSaveUploaded(u64 titleId, u32 crc);

    // Obtem o timestamp Unix UTC da versao local do save (via timestamps.json ou stat de save:/)
    std::time_t getLocalSaveTimestamp(u64 titleId, AccountUid uid);

    // Define o timestamp local para o jogo e persiste em timestamps.json
    void setLocalSaveTimestamp(u64 titleId, std::time_t ts);

    // Restaura o save a partir de um .zip baixado:
    // Faz backup de seguranca do save local atual para sdmc:/config/gdrive-sync/backups/<TitleID>_backup.zip,
    // limpa save:/, descompacta o .zip baixado, executa commit obrigatorio e desmonta save:/.
    bool restoreGameSave(u64 titleId, AccountUid uid, const std::string& zipPath, std::string* outError = nullptr);

    // Gerenciamento de timestamps locais
    void loadTimestamps();
    void saveTimestamps();

private:
    std::map<u64, u32> m_savedCrcs;
    std::map<u64, std::time_t> m_timestamps;

    void loadState();
    void saveState();
};

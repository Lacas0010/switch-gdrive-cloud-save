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

    // Constante de sanidade temporal: 01/01/2025 00:00:00 UTC (1735689600)
    // Protege contra descalibracao do RTC (1970/2000) por perda de bateria ou modo aviao
    static constexpr std::time_t MIN_SANE_RTC_TIMESTAMP = 1735689600;

    // Valida se um determinado timestamp e razoavel (>= 01/01/2025)
    static bool isTimestampSane(std::time_t ts);

    // Valida se o relogio RTC atual do console esta calibrado e e posterior a 01/01/2025
    static bool isConsoleClockSane(std::time_t* outCurrentTime = nullptr);

    // Restaura o save a partir de um .zip baixado com seguranca transacional e rollback:
    // 1. Cria backup de seguranca <TitleID>_save_backup.bak
    // 2. Descompacta e valida conteudo extraido em save:/
    // 3. Executa commit obrigatorio no Horizon OS
    // 4. Em caso de qualquer erro, executa rollback restaurando o .bak automaticamente
    // 5. Em caso de 100% de sucesso, remove o .bak temporario
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

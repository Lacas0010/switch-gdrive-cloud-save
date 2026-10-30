#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <map>

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
    // Retorna true caso haja um novo save pronto para upload e que seja diferente do backup anterior
    bool processGameSave(u64 titleId, AccountUid uid, std::string& outZipPath, bool& outChanged);

    // Atualiza o estado salvo apos o upload com sucesso
    void markSaveUploaded(u64 titleId, u32 crc);

private:
    std::map<u64, u32> m_savedCrcs;

    void loadState();
    void saveState();
};

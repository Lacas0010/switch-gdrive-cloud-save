#pragma once
#include <switch.h>
#include <string>
#include <map>

class SaveManager {
public:
    SaveManager();
    ~SaveManager();

    // Obtem o AccountUid do usuario ativo/recente no console
    bool getActiveAccount(AccountUid& outUid);

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

#pragma once
#include <switch.h>

class ProcessMonitor {
public:
    ProcessMonitor();
    ~ProcessMonitor();

    // Retorna true exatamente na iteracao em que um jogo foi fechado pelo usuario
    bool hasGameJustExited(u64& outExitedTitleId);

    // Retorna se ha algum jogo rodando no momento
    bool isGameRunning(u64& outRunningTitleId);

private:
    u64  m_lastTitleId;
    bool m_wasRunning;
};

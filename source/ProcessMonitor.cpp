#include "ProcessMonitor.hpp"

ProcessMonitor::ProcessMonitor()
    : m_lastTitleId(0), m_wasRunning(false) {}

ProcessMonitor::~ProcessMonitor() = default;

bool ProcessMonitor::isGameRunning(u64& outRunningTitleId) {
    u64 pid = 0;
    Result rc = pmdmntGetApplicationProcessId(&pid);

    if (R_SUCCEEDED(rc) && pid != 0) {
        u64 titleId = 0;
        if (R_SUCCEEDED(pminfoGetProgramId(&titleId, pid))) {
            outRunningTitleId = titleId;
            return true;
        }
    }

    outRunningTitleId = 0;
    return false;
}

bool ProcessMonitor::hasGameJustExited(u64& outExitedTitleId) {
    u64 currentTitleId = 0;
    bool running = isGameRunning(currentTitleId);

    if (running) {
        m_lastTitleId = currentTitleId;
        m_wasRunning = true;
        return false;
    }

    // O jogo acabou de fechar se estava rodando na checagem anterior e agora parou
    if (m_wasRunning) {
        outExitedTitleId = m_lastTitleId;
        m_wasRunning = false;
        m_lastTitleId = 0;
        return true;
    }

    return false;
}

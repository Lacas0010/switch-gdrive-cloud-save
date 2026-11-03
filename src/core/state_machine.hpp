#pragma once
#include <atomic>

namespace Core {

    enum class Screen {
        MAIN,
        GAME_SELECT,
        SETTINGS,
        OAUTH_MODAL,
        OPERATION_PROGRESS
    };

    enum class OperationType {
        NONE,
        FULL_BACKUP,
        FULL_RESTORE,
        SINGLE_GAME_BACKUP,
        SINGLE_GAME_RESTORE,
        TEST_CONNECTION
    };

    enum class EngineState {
        IDLE,
        INITIALIZING,
        SCANNING,
        PROCESSING,
        COMPLETED_SUCCESS,
        COMPLETED_ERROR,
        CANCELLED
    };

    class StateMachine {
    public:
        StateMachine()
            : m_currentScreen(Screen::MAIN),
              m_previousScreen(Screen::MAIN),
              m_activeOperation(OperationType::NONE),
              m_engineState(EngineState::IDLE) {}

        Screen getScreen() const noexcept { return m_currentScreen.load(std::memory_order_relaxed); }
        void setScreen(Screen screen) noexcept {
            m_previousScreen.store(m_currentScreen.load(std::memory_order_relaxed), std::memory_order_relaxed);
            m_currentScreen.store(screen, std::memory_order_relaxed);
        }

        Screen getPreviousScreen() const noexcept { return m_previousScreen.load(std::memory_order_relaxed); }

        OperationType getActiveOperation() const noexcept { return m_activeOperation.load(std::memory_order_relaxed); }
        void setActiveOperation(OperationType op) noexcept { m_activeOperation.store(op, std::memory_order_relaxed); }

        EngineState getEngineState() const noexcept { return m_engineState.load(std::memory_order_relaxed); }
        void setEngineState(EngineState state) noexcept { m_engineState.store(state, std::memory_order_relaxed); }

    private:
        std::atomic<Screen> m_currentScreen;
        std::atomic<Screen> m_previousScreen;
        std::atomic<OperationType> m_activeOperation;
        std::atomic<EngineState> m_engineState;
    };

} // namespace Core

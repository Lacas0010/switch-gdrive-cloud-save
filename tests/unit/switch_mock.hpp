#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Libnx primitive types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint32_t Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res)    ((res) != 0)

struct AccountUid {
    u64 uid[2];
    bool operator==(const AccountUid& o) const { return uid[0] == o.uid[0] && uid[1] == o.uid[1]; }
    bool operator!=(const AccountUid& o) const { return !(*this == o); }
};

// State tracker for mocked Horizon OS fsdev services
struct FsDevMockState {
    bool saveMounted = false;
    bool writeMode = false;
    u64 mountedTitleId = 0;
    AccountUid mountedUid{};
    int commitCallCount = 0;
    int unmountCallCount = 0;
    bool simulateCommitFailure = false;
    bool simulateMountFailure = false;

    void reset() {
        saveMounted = false;
        writeMode = false;
        mountedTitleId = 0;
        mountedUid = {};
        commitCallCount = 0;
        unmountCallCount = 0;
        simulateCommitFailure = false;
        simulateMountFailure = false;
    }
};

inline FsDevMockState& getFsDevMock() {
    static FsDevMockState s_state;
    return s_state;
}

inline Result fsdevMountSaveData(const char* name, u64 titleId, AccountUid uid) {
    auto& mock = getFsDevMock();
    if (mock.simulateMountFailure) return 0x202;
    mock.saveMounted = true;
    mock.writeMode = true;
    mock.mountedTitleId = titleId;
    mock.mountedUid = uid;
    return 0;
}

inline Result fsdevMountDeviceSaveData(const char* name, u64 titleId) {
    auto& mock = getFsDevMock();
    if (mock.simulateMountFailure) return 0x202;
    mock.saveMounted = true;
    mock.writeMode = true;
    mock.mountedTitleId = titleId;
    return 0;
}

inline Result fsdevCommitDevice(const char* name) {
    auto& mock = getFsDevMock();
    mock.commitCallCount++;
    if (mock.simulateCommitFailure) return 0x404;
    return 0;
}

inline Result fsdevUnmountDevice(const char* name) {
    auto& mock = getFsDevMock();
    mock.unmountCallCount++;
    mock.saveMounted = false;
    return 0;
}

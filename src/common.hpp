#pragma once

#ifdef __SWITCH__
#include <switch.h>
#else
#include <cstdint>
#include <cstring>
#include <string>

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
#endif

#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <curl/curl.h>

namespace Core {

    // RAII para ponteiros FILE*
    struct FileCloser {
        void operator()(FILE* fp) const noexcept {
            if (fp) {
                std::fclose(fp);
            }
        }
    };
    using UniqueFile = std::unique_ptr<FILE, FileCloser>;

    inline UniqueFile openFile(const char* path, const char* mode) {
        return UniqueFile(std::fopen(path, mode));
    }

    // RAII para instâncias CURL* da libcurl
    struct CurlEasyDeleter {
        void operator()(CURL* handle) const noexcept {
            if (handle) {
                curl_easy_cleanup(handle);
            }
        }
    };
    using UniqueCurl = std::unique_ptr<CURL, CurlEasyDeleter>;

    // RAII para listas de cabeçalhos curl_slist*
    struct CurlSlistDeleter {
        void operator()(curl_slist* list) const noexcept {
            if (list) {
                curl_slist_free_all(list);
            }
        }
    };
    using UniqueCurlSlist = std::unique_ptr<curl_slist, CurlSlistDeleter>;

    // RAII generico para saida antecipada segura (Scope Exit Guard)
    template <typename F>
    class ScopeGuard {
    public:
        explicit ScopeGuard(F&& func) : m_func(std::forward<F>(func)), m_active(true) {}
        ~ScopeGuard() {
            if (m_active) {
                m_func();
            }
        }
        void dismiss() noexcept { m_active = false; }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
        ScopeGuard(ScopeGuard&& other) noexcept : m_func(std::move(other.m_func)), m_active(other.m_active) {
            other.m_active = false;
        }

    private:
        F m_func;
        bool m_active;
    };

    template <typename F>
    inline ScopeGuard<F> makeScopeGuard(F&& func) {
        return ScopeGuard<F>(std::forward<F>(func));
    }

#ifdef __SWITCH__
    // RAII estrito para particao save:/ montada via Horizon OS
    class ScopedMountedSave {
    public:
        ScopedMountedSave(u64 titleId, AccountUid uid, bool readOnly = false)
            : m_mounted(false), m_committed(false) {
            fsdevUnmountDevice("save");
            Result rc = fsdevMountSaveData("save", titleId, uid);
            if (R_FAILED(rc)) {
                rc = fsdevMountDeviceSaveData("save", titleId);
            }
            m_mounted = R_SUCCEEDED(rc);
            (void)readOnly;
        }

        ~ScopedMountedSave() {
            if (m_mounted) {
                fsdevUnmountDevice("save");
            }
        }

        bool isMounted() const noexcept { return m_mounted; }

        bool commit() noexcept {
            if (!m_mounted) return false;
            Result rc = fsdevCommitDevice("save");
            m_committed = R_SUCCEEDED(rc);
            return m_committed;
        }

        bool wasCommitted() const noexcept { return m_committed; }

        ScopedMountedSave(const ScopedMountedSave&) = delete;
        ScopedMountedSave& operator=(const ScopedMountedSave&) = delete;

    private:
        bool m_mounted;
        bool m_committed;
    };
#endif

} // namespace Core

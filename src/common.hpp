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
#if __has_include(<curl/curl.h>)
#include <curl/curl.h>
#define HAS_LIBCURL_HEADER 1
#endif

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

#ifdef HAS_LIBCURL_HEADER
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
#endif

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
            : m_mounted(false), m_committed(false), m_lastError(0) {
            fsdevUnmountDevice("save");

            // 1. Tenta montar save da conta ativa
            Result rc = fsdevMountSaveData("save", titleId, uid);

            // 2. Fallback: save de dispositivo (ex: Animal Crossing)
            if (R_FAILED(rc)) {
                Result devRc = fsdevMountDeviceSaveData("save", titleId);
                if (R_SUCCEEDED(devRc)) {
                    rc = devRc;
                }
            }

            // 3. Fallback: procura save existente do mesmo TitleID em outro perfil do console
            if (R_FAILED(rc)) {
                FsSaveDataInfoReader reader;
                if (R_SUCCEEDED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
                    FsSaveDataInfo buf[16];
                    s64 count = 0;
                    while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, buf, 16, &count)) && count > 0) {
                        for (s64 i = 0; i < count; ++i) {
                            if (buf[i].application_id == titleId && buf[i].save_data_type == FsSaveDataType_Account) {
                                Result altRc = fsdevMountSaveData("save", titleId, buf[i].uid);
                                if (R_SUCCEEDED(altRc)) {
                                    rc = altRc;
                                    break;
                                }
                            }
                        }
                        if (R_SUCCEEDED(rc)) break;
                    }
                    fsSaveDataInfoReaderClose(&reader);
                }
            }

            // 4. Se estamos restaurando (!readOnly) e o save ainda não existe no Switch, cria a partição automaticamente
            if (R_FAILED(rc) && !readOnly) {
                s64 saveSize = 32 * 1024 * 1024;
                s64 journalSize = 32 * 1024 * 1024;
                u64 ownerId = titleId;

                auto ctrl = std::make_unique<NsApplicationControlData>();
                if (ctrl) {
                    std::memset(ctrl.get(), 0, sizeof(NsApplicationControlData));
                    u64 outSz = 0;
                    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, ctrl.get(), sizeof(NsApplicationControlData), &outSz)) &&
                        outSz >= sizeof(NacpStruct)) {
                        if (ctrl->nacp.user_account_save_data_size > 0) {
                            saveSize = ctrl->nacp.user_account_save_data_size;
                        }
                        if (ctrl->nacp.user_account_save_data_journal_size > 0) {
                            journalSize = ctrl->nacp.user_account_save_data_journal_size;
                        }
                        if (ctrl->nacp.save_data_owner_id != 0) {
                            ownerId = ctrl->nacp.save_data_owner_id;
                        }
                    }
                }

                FsSaveDataAttribute attr{};
                attr.application_id = titleId;
                attr.uid = uid;
                attr.save_data_type = FsSaveDataType_Account;

                FsSaveDataCreationInfo create{};
                create.save_data_size = saveSize;
                create.journal_size = journalSize;
                create.available_size = 0x4000;
                create.owner_id = ownerId;
                create.flags = 0;
                create.save_data_space_id = FsSaveDataSpaceId_User;

                FsSaveDataMetaInfo meta{};
                meta.size = 0x40060;
                meta.type = FsSaveDataMetaType_Thumbnail;

                Result createRc = fsCreateSaveDataFileSystem(&attr, &create, &meta);
                if (R_SUCCEEDED(createRc)) {
                    rc = fsdevMountSaveData("save", titleId, uid);
                }
            }

            m_lastError = rc;
            m_mounted = R_SUCCEEDED(rc);
        }

        ~ScopedMountedSave() {
            if (m_mounted) {
                fsdevUnmountDevice("save");
            }
        }

        bool isMounted() const noexcept { return m_mounted; }
        Result getLastError() const noexcept { return m_lastError; }

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
        Result m_lastError;
    };
#endif

} // namespace Core

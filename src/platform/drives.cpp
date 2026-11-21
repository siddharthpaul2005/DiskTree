#include "disktree/platform/drives.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>       // SHGetKnownFolderPath
#include <knownfolders.h>
#else
#include <cstdlib>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#include <string>

namespace disktree::platform {

#ifdef _WIN32

static std::string narrow(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::vector<DriveInfo> list_drives() {
    std::vector<DriveInfo> drives;
    DWORD mask = GetLogicalDrives();

    // Don't pop up "insert a disk" dialogs for empty removable drives
    UINT old_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[] = {wchar_t(L'A' + i), L':', L'\\', 0};

        DriveInfo d;
        d.root = narrow(root);
        switch (GetDriveTypeW(root)) {
            case DRIVE_FIXED:     d.type = "fixed";     break;
            case DRIVE_REMOVABLE: d.type = "removable"; break;
            case DRIVE_REMOTE:    d.type = "network";   break;
            case DRIVE_CDROM:     d.type = "cdrom";     break;
            case DRIVE_RAMDISK:   d.type = "ram";       break;
            default:              d.type = "unknown";   break;
        }

        wchar_t label[MAX_PATH + 1] = {};
        wchar_t fs[MAX_PATH + 1]    = {};
        if (GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr,
                                  fs, MAX_PATH)) {
            d.label      = narrow(label);
            d.filesystem = narrow(fs);
        }

        ULARGE_INTEGER avail{}, total{}, free_all{};
        if (GetDiskFreeSpaceExW(root, &avail, &total, &free_all)) {
            d.total_bytes = total.QuadPart;
            d.free_bytes  = avail.QuadPart;
            d.ready       = true;
        }
        drives.push_back(std::move(d));
    }

    SetErrorMode(old_mode);
    return drives;
}

std::vector<Place> list_places() {
    struct Known { const KNOWNFOLDERID* id; const char* key; const char* name; };
    static const Known known[] = {
        {&FOLDERID_Desktop,   "desktop",   "Desktop"},
        {&FOLDERID_Downloads, "downloads", "Downloads"},
        {&FOLDERID_Documents, "documents", "Documents"},
        {&FOLDERID_Pictures,  "pictures",  "Pictures"},
        {&FOLDERID_Videos,    "videos",    "Videos"},
        {&FOLDERID_Music,     "music",     "Music"},
        {&FOLDERID_Profile,   "profile",   "Your user folder"},
    };
    std::vector<Place> out;
    for (const auto& k : known) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*k.id, KF_FLAG_DEFAULT, nullptr, &p)) && p) {
            DWORD attr = GetFileAttributesW(p);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                out.push_back({k.key, k.name, narrow(p)});
        }
        if (p) CoTaskMemFree(p);
    }
    return out;
}

AccessLevel enable_full_read_access() {
    AccessLevel level;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token))
        return level;

    TOKEN_ELEVATION elevation{};
    DWORD len = 0;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &len))
        level.elevated = elevation.TokenIsElevated != 0;

    if (level.elevated) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(nullptr, L"SeBackupPrivilege", &tp.Privileges[0].Luid) &&
            AdjustTokenPrivileges(token, FALSE, &tp, sizeof tp, nullptr, nullptr))
            level.full_read = GetLastError() == ERROR_SUCCESS;   // not ERROR_NOT_ALL_ASSIGNED
    }
    CloseHandle(token);
    return level;
}

std::optional<uint64_t> file_table_bytes(const std::string& root) {
    // "C:\" -> "\\.\C:"
    if (root.size() < 2 || root[1] != ':') return std::nullopt;
    std::wstring dev = L"\\\\.\\";
    dev += wchar_t(root[0]);
    dev += L':';
    HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    NTFS_VOLUME_DATA_BUFFER data{};
    DWORD got = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                              &data, sizeof data, &got, nullptr);
    CloseHandle(h);
    if (!ok) return std::nullopt;
    return uint64_t(data.MftValidDataLength.QuadPart);
}

#else

AccessLevel enable_full_read_access() {
    AccessLevel level;
    level.elevated  = geteuid() == 0;
    level.full_read = level.elevated;
    return level;
}

std::optional<uint64_t> file_table_bytes(const std::string&) {
    return std::nullopt;
}

std::vector<Place> list_places() {
    std::vector<Place> out;
    if (const char* home = std::getenv("HOME")) {
        out.push_back({"profile", "Your user folder", home});
        for (const char* sub : {"Desktop", "Downloads", "Documents", "Pictures", "Videos", "Music"}) {
            std::string p = std::string(home) + "/" + sub;
            struct stat st{};
            if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                out.push_back({sub, sub, p});
        }
    }
    return out;
}

std::vector<DriveInfo> list_drives() {
    DriveInfo d;
    d.root = "/";
    d.type = "fixed";
    struct statvfs s{};
    if (statvfs("/", &s) == 0) {
        d.total_bytes = uint64_t(s.f_blocks) * s.f_frsize;
        d.free_bytes  = uint64_t(s.f_bavail) * s.f_frsize;
        d.ready       = true;
    }
    return {d};
}

#endif

} // namespace disktree::platform

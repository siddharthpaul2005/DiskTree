#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace disktree::platform {

// One mounted volume, e.g. "C:\".
struct DriveInfo {
    std::string root;         // "C:\"
    std::string label;        // volume label, may be empty
    std::string filesystem;   // "NTFS", "exFAT", ...
    std::string type;         // "fixed", "removable", "network", "cdrom", "ram", "unknown"
    uint64_t    total_bytes = 0;
    uint64_t    free_bytes  = 0;   // free space available to this user
    bool        ready       = false; // false for empty card readers / DVD drives
};

// All drives currently visible, sorted by root. On non-Windows
// platforms this returns the root filesystem only.
std::vector<DriveInfo> list_drives();

// A well-known user folder, e.g. { "downloads", "Downloads", "C:\Users\me\Downloads" }.
struct Place {
    std::string id;
    std::string name;
    std::string path;
};

// The current user's Desktop, Downloads, Documents, Pictures, Videos,
// Music and profile folder, in that order. Missing folders are skipped.
std::vector<Place> list_places();

// Whether this process runs with administrator rights. When it does, this
// also switches on the backup privilege, which lets directory listings
// (opened with FILE_FLAG_BACKUP_SEMANTICS) read folders whose permissions
// would otherwise shut out even administrators. Read-only: nothing is
// ever written with it. Call once at start-up.
struct AccessLevel {
    bool elevated    = false;   // running as administrator
    bool full_read   = false;   // backup privilege is on
};
AccessLevel enable_full_read_access();

// Bytes used by the NTFS file table (the $MFT) of a drive such as "C:\".
// Needs administrator rights; nullopt when unavailable or not NTFS.
std::optional<uint64_t> file_table_bytes(const std::string& root);

} // namespace disktree::platform

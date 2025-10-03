#include "stdafx.h"

using namespace std; using namespace ATL; namespace fs = filesystem; using fs::path;

const char * APP_NAME = "zipfs";
const char * APP_VERSION = "0.1.0";

const size_t DEFAULT_CACHE_SIZE = 1024;

struct zipmount_options {
    optional<string> root_directory {"x:\\zipfs"}; optional<string> mount_point {"z:\\"}; optional<string> acp {"default"};
};

STRUCTOPT(zipmount_options, root_directory, mount_point);

static fs::path root_directory, mount_point;

static string acp;

// a cache which evicts the least recently used item when it is full
template<class Key, class Value>
class lru_cache {
public:
    typedef Key key_type;
    typedef Value value_type;
    typedef std::list<key_type> list_type;
    typedef struct { value_type first; typename list_type::iterator second; } xvalue_type;
    typedef std::map<key_type, xvalue_type> map_type;

    lru_cache(size_t capacity) : m_capacity(capacity) {}

    ~lru_cache() {}

    size_t size() const { return m_map.size(); }

    size_t capacity() const { return m_capacity; }

    bool empty() const { return m_map.empty(); }

    bool contains(const key_type & key) { return m_map.find(key) != m_map.end(); }

    template<typename K, typename V>
    void insert(K && key, V && value) {
        typename map_type::iterator i = m_map.find(key); if(i == m_map.end()) {
            // insert item into the cache, but first check if it is full
            if(size() >= m_capacity) {
                // cache is full, evict the least recently used item
                evict();
            }

            // insert the new item
            m_list.push_front(std::forward<K>(key));
            
            // Create the xvalue_type with the value and list iterator
            xvalue_type xvalue;
            xvalue.first = std::forward<V>(value);
            xvalue.second = m_list.begin();
            
            // Insert into the map
            m_map.emplace(std::forward<K>(key), std::move(xvalue));
        }
    }

    value_type * get(const key_type & key) {
        // lookup value in the cache
        typename map_type::iterator i = m_map.find(key);

        if(i == m_map.end()) return nullptr;

        // return the value, but first update its place in the most recently used list
        typename list_type::iterator j = i->second.second; if(j != m_list.begin()) {
            // move item to the front of the most recently used list
            m_list.erase(j); m_list.push_front(key);

            // update iterator in map
            j = m_list.begin();
            
            // We need to create a new xvalue_type with the same value but updated iterator
            // Since zip_file_info is move-only, we need to move it out of the map temporarily
            value_type temp_value(std::move(i->second.first));
            
            // Create new xvalue_type with the moved value and updated list iterator
            xvalue_type xvalue;
            xvalue.first = std::move(temp_value);
            xvalue.second = j;
            
            // Update the map entry
            m_map[key] = std::move(xvalue);
            
            // Return a pointer to the value in the map
            return &m_map[key].first;
        }
        else {
            // the item is already at the front of the most recently
            // used list so just return it
            return &i->second.first;
        }
    }

    void clear() { m_map.clear(); m_list.clear(); }

private:
    void evict() {
        // evict item from the end of most recently used list
        typename list_type::iterator i = --m_list.end(); {
            m_map.erase(*i); m_list.erase(i);
        }
    }

private:
    map_type m_map; list_type m_list; size_t m_capacity;
};

static struct ok_type {
    bool epilogue {false};

    void failed(int rc = 1) { if(epilogue) { print("failed\n"); epilogue = false; } exit(rc); }

    void succeeded() { if(epilogue) { print("\n"); epilogue = false; } }

    ok_type & operator=(int rc) {
        if(rc) failed(rc); else succeeded(); ; return *this;
    }

    template<typename T, std::enable_if_t<std::is_same_v<T, bool>, int> = 0>
    ok_type & operator=(T b) {
        if(!b) failed(); else succeeded(); ; return *this;
    }

    template<typename T>
    ok_type & operator()(T && s) {
        auto now = std::chrono::system_clock::now();
        auto time_point = std::chrono::floor<std::chrono::seconds>(now);
        auto time_of_day = std::chrono::hh_mm_ss {time_point - std::chrono::floor<std::chrono::days>(time_point)};

        epilogue = true; print("[{:%T}] {}", time_of_day, s); return *this;
    }
} ok;

template<typename T>
bool fatal(T && s, int rc = 1) { ok(s) = rc; return true; }

std::map<wstring, path> $archive_map;

struct archive_path {
    fs::path archive; fs::path path;

    archive_path(LPCWSTR FileName) {
        USES_CONVERSION; if(lstrcmpW(FileName, L"\\") != 0) {
            wstring ws = FileName + 1;

            auto pos = ws.find(L'\\'); if(pos == wstring::npos) {
                archive = $archive_map[ws];
            }
            else {
                archive = $archive_map[ws.substr(0, pos)]; path = ws.substr(pos + 1);
            }
        }
    }

    bool is_root() const { return archive.empty(); }

    operator bool() const { return !is_root(); }
};

struct zipfs_archive {
    enum { NONE, FILE, DIR };

    struct entry_t {
        int type {0}; int index {0};

        operator bool() const { return !type; }

        bool is_file() const { return type == FILE; }

        bool is_dir() const { return type == DIR; }
    };

    struct stat_t {
        string fpath; size_t size; int64_t mtime; int type;

        bool is_file() const { return type == FILE; }

        bool is_dir() const { return type == DIR; }
    };

    // New implementation using zip.h
    ::zip_archive archive; 
    size_t size {0}; 
    lru_cache<int, ::zip_file_info> cache {DEFAULT_CACHE_SIZE}; 
    CAtlFileMappingBase fmapping;

    zipfs_archive() = default;

    operator bool() const { return fmapping.GetData() != nullptr; }

    int open(string const & fname) {
        CAtlFile f; {
            if(f.Create(fname.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL) != S_OK) {
                return -1;
            }

            if(fmapping.MapFile(f) != S_OK) {
                return -1;
            }
        }

        try {
            archive.open(static_cast<uint8_t*>(fmapping.GetData()), fmapping.GetMappingSize());
            size = archive.size();
            
            // ::FILE * fp = fopen("r:/zipfs.txt", "w");

            // for(int idx = 0; idx < size; idx++) {
            //     auto st = stat(idx);
                
            //     fwrite(st.fpath.c_str(), 1, st.fpath.size(), fp);
            //     fwrite("\n", 1, 1, fp);
            // }
            
            // fclose(fp);
            return 0;
        } catch(const std::runtime_error& e) {
            return -1;
        }
    }

    stat_t stat(int findex) {
        if(findex < 0 || findex >= size) {
            stat_t r;
            r.fpath = ""; r.size = 0; r.mtime = 0; r.type = zipfs_archive::NONE;
            return r;
        }

        auto info = this->archive.get_file_info(findex);
        
        stat_t r;
        r.fpath = std::string(info.filename);
        r.size = info.uncompressed_size;
        r.mtime = info.mod_time; // Use the new mod_time field
        r.type = info.is_directory ? zipfs_archive::DIR : zipfs_archive::FILE;
        
        return r;
    }

    entry_t locate(string const & fname) {
        if(fname.empty() || fname == "/") return {zipfs_archive::DIR, -1};

        // Find the entry by name
        size_t index = archive.find_entry_index(fname.c_str(), fname.size());
        
        if(index == archive.size()) {
            // Try with trailing slash for directories
            string dname = fname + '/';
            index = archive.find_entry_index(dname.c_str(), dname.size());
            
            if(index != archive.size()) {
                return {zipfs_archive::DIR, static_cast<int>(index)};
            }
            
            // Try to find any entries that start with this directory name
            for(size_t i = 0; i < archive.size(); i++) {
                auto filename = archive.get_filename(i);
                if(filename.size() > dname.size() && 
                   std::string_view(filename).substr(0, dname.size()) == dname) {
                    return {zipfs_archive::DIR, static_cast<int>(i)};
                }
            }
            
            return {};
        }
        
        auto info = archive.get_file_info(index);
        return {info.is_directory ? zipfs_archive::DIR : zipfs_archive::FILE, static_cast<int>(index)};
    }

    std::string_view read(int findex) {
        // Try cache first
        auto cached_info = this->cache.get(findex);
        if(cached_info) {
            // Get the decompressed data from cached zip_file_info
            // Handle non-const data() method
            const uint8_t* data = cached_info->data();
            if(!data) {
                return {}; // Return empty string_view
            }
            
            // Create a string_view from the data
            return std::string_view(reinterpret_cast<const char*>(data), cached_info->uncompressed_size);
        }
        
        // Not in cache, get the file info
        auto info = archive.get_file_info(findex);
        if(!info.raw_ptr) {
            return {}; // Return empty string_view
        }
        
        // Add to cache
        this->cache.insert(findex, std::move(info));
        
        // Try again with the cached entry
        return read(findex);
    }

    template<typename F>
    void each(string const & fname, F && f) {
        auto ent = locate(fname); if(ent.is_dir()) {
            // Convert fname to a format suitable for the new implementation
            std::string dir_path = fname;
            if(!dir_path.empty() && dir_path.back() != '/') {
                dir_path += '/';
            }
            
            // Use the for_each_entry method from the new implementation
            archive.for_each_entry(dir_path, [&](const ::zip_dir_entry* entry) {
                if(!entry) return;
                
                // Get the filename
                std::string_view filename(reinterpret_cast<const char*>(entry->file_name), entry->filename_length);
                
                // Skip the directory itself
                if(filename == dir_path) return;
                
                // Get the relative path from the directory
                std::string_view relative_path;
                if(!dir_path.empty()) {
                    relative_path = filename.substr(dir_path.size());
                } else {
                    relative_path = filename;
                }
                
                // Check if this is a direct child or a deeper descendant
                size_t slash_pos = relative_path.find('/');
                
                if(slash_pos == std::string_view::npos) {
                    // This is a file directly in the directory
                    stat_t st;
                    st.fpath = std::string(relative_path);
                    st.size = entry->uncompressed_size;
                    st.mtime = 0; // No modification time in new implementation
                    st.type = zipfs_archive::FILE;
                    f(st);
                } else if(slash_pos == relative_path.size() - 1) {
                    // This is a directory directly in the parent
                    stat_t st;
                    st.fpath = std::string(relative_path.substr(0, slash_pos));
                    st.size = 0;
                    st.mtime = 0;
                    st.type = zipfs_archive::DIR;
                    f(st);
                }
                // Skip deeper descendants
            });
        }
    }
};

typedef std::map<fs::path, zipfs_archive *> archives_type;

archives_type archives;

static zipfs_archive & $archive(fs::path const & fname) {
    archives_type::iterator it = archives.find(fname); if(it != archives.end()) {
        return *it->second;
    }

    auto ar = new zipfs_archive(); {
        ar->open(fname.string());
    }

    archives[fname] = ar; return *ar;
}

// fs callbacks
static NTSTATUS DOKAN_CALLBACK zmCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext, ACCESS_MASK DesiredAccess, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo) {
    archive_path ap(FileName); if(ap.is_root()) {
        DokanFileInfo->IsDirectory = TRUE; return STATUS_SUCCESS;
    }

    DWORD creationDisposition, fileAttributesAndFlags; ACCESS_MASK genericDesiredAccess; {
        DokanMapKernelToUserCreateFileFlags(
            DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
            &genericDesiredAccess, &fileAttributesAndFlags, &creationDisposition);
    }

    auto & ar = $archive(ap.archive); if(!ar) {
        return DokanNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
    }

    USES_CONVERSION;

    string fpath = W2A(ap.path.generic_wstring().c_str());

    auto [ftype, findex] = ar.locate(fpath); if(!ftype) {
        if((creationDisposition == CREATE_NEW) || (creationDisposition == OPEN_ALWAYS)) {
            return DokanNtStatusFromWin32(ERROR_ACCESS_DENIED);
        }

        return DokanNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
    }

    if(creationDisposition == CREATE_NEW) {
        return DokanNtStatusFromWin32(ERROR_FILE_EXISTS);
    }

    DokanFileInfo->Context = findex;

    bool is_dir = (ftype == 2); if(is_dir) {
        DokanFileInfo->IsDirectory = TRUE;

        if(creationDisposition == OPEN_ALWAYS) return STATUS_OBJECT_NAME_COLLISION;

        return STATUS_SUCCESS;
    }

    DokanFileInfo->IsDirectory = FALSE;

    return STATUS_SUCCESS;
}

static void DOKAN_CALLBACK zmCloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {}

static NTSTATUS DOKAN_CALLBACK zmReadFile(LPCWSTR FileName, LPVOID Buffer, DWORD BufferLength, LPDWORD ReadLength, LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo) {
    int findex = DokanFileInfo->Context;

    auto & ar = $archive(archive_path(FileName).archive);

    auto s = ar.read(findex);
    if (s.empty()) {
        *ReadLength = 0;
        return STATUS_UNSUCCESSFUL;
    }

    auto size = s.size();
    auto toread = std::min(size - Offset, (size_t)BufferLength);

    memcpy(Buffer, s.data() + Offset, toread);

    *ReadLength = toread;

    return STATUS_SUCCESS;
}

// Convert DOS time format to FILETIME
FILETIME dos_time_to_filetime(uint32_t dos_time) {
    FILETIME ft;
    SYSTEMTIME st = {0};
    
    // Extract date and time components from DOS time format
    st.wYear = ((dos_time >> 25) & 0x7F) + 1980;
    st.wMonth = (dos_time >> 21) & 0x0F;
    st.wDay = (dos_time >> 16) & 0x1F;
    st.wHour = (dos_time >> 11) & 0x1F;
    st.wMinute = (dos_time >> 5) & 0x3F;
    st.wSecond = (dos_time & 0x1F) * 2;
    
    // Convert to FILETIME
    SystemTimeToFileTime(&st, &ft);
    return ft;
}

FILETIME time64_to_filetime(__time64_t t) {
    ULARGE_INTEGER time_value; FILETIME ft;
    // FILETIME represents time in 100-nanosecond intervals since January 1, 1601 (UTC).
    // _time64_t represents seconds since January 1, 1970 (UTC).
    // The difference in seconds is 11644473600.
    // Multiply by 10,000,000 to convert seconds to 100-nanosecond intervals.
    time_value.QuadPart = (t * 10000000LL) + 116444736000000000LL;

    ft.dwLowDateTime = time_value.LowPart; ft.dwHighDateTime = time_value.HighPart;

    return ft;
}

static NTSTATUS DOKAN_CALLBACK zmGetFileInformation(LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation, PDOKAN_FILE_INFO DokanFileInfo) {
    if(DokanFileInfo->IsDirectory) {
        HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY; return STATUS_SUCCESS;
    }

    int findex = DokanFileInfo->Context;

    auto & ar = $archive(archive_path(FileName).archive);

    auto stat = ar.stat(findex); {
        FILETIME mtime = dos_time_to_filetime(stat.mtime);

        HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        HandleFileInformation->nFileSizeLow = stat.size;
        HandleFileInformation->nFileSizeHigh = stat.size >> 32;
        HandleFileInformation->ftCreationTime = mtime;
        HandleFileInformation->ftLastWriteTime = mtime;
        HandleFileInformation->ftLastAccessTime = mtime;
        HandleFileInformation->nNumberOfLinks = 0;
        HandleFileInformation->nFileIndexHigh = 0;
        HandleFileInformation->nFileIndexLow = 0;
        HandleFileInformation->dwVolumeSerialNumber = 0;
    }

    return STATUS_SUCCESS;
}

static wstring shortcut_target(wstring const & shortcut_fname) {
    wstring result;

    CComPtr<IShellLinkW> shellLink; {
        ok = (shellLink.CoCreateInstance(CLSID_ShellLink) == S_OK);
    }

    CComPtr<IPersistFile> persistFile; {
        ok = (shellLink->QueryInterface(IID_PPV_ARGS(&persistFile)) == S_OK);
    }

    ok = (persistFile->Load(shortcut_fname.c_str(), STGM_READ));

    LPITEMIDLIST itemIdList {NULL}; {
        ok = (shellLink->GetIDList(&itemIdList) == S_OK);
    }

    wchar_t target_path[MAX_PATH]; {
        if(SHGetPathFromIDListW(itemIdList, target_path)) result = target_path;
    }

    CoTaskMemFree(itemIdList);

    return result;
}

static NTSTATUS DOKAN_CALLBACK zmFindFiles(LPCWSTR FileName, PFillFindData FillFindData, PDOKAN_FILE_INFO DokanFileInfo) {
    USES_CONVERSION;

    if(lstrcmpW(FileName, L"\\") == 0) {
        // read file list using std::filesystem
        for(auto & x : fs::directory_iterator(root_directory)) {
            path p = x.path(); __a: auto ext = p.extension(); if(ext == ".zip") {
                WIN32_FIND_DATAW find_data {0}; {
                    auto fname = p.stem().wstring(); {
                        $archive_map[fname] = p;
                    }

                    find_data.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY; wcscpy(find_data.cFileName, fname.c_str());
                }

                FillFindData(&find_data, DokanFileInfo);
            }
            else if(ext == ".lnk") {
                auto target = shortcut_target(p.wstring()); {
                    if(target.empty()) continue;
                }

                p = target; goto __a;
            }
        }

        return STATUS_SUCCESS;
    }

    archive_path ap(FileName); auto dname = W2A(ap.path.generic_wstring().c_str());

    auto & ar = $archive(ap.archive);

    ar.each(dname, [&](auto const & stat) {
        WIN32_FIND_DATAW find_data {0}; if(stat.is_dir()) {
            find_data.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        }
        else {
            auto ftime = time64_to_filetime(stat.mtime);

            find_data.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            find_data.nFileSizeLow = stat.size;
            find_data.nFileSizeHigh = stat.size >> 32;
            find_data.ftCreationTime = ftime;
            find_data.ftLastWriteTime = ftime;
            find_data.ftLastAccessTime = ftime;
        }

        wcscpy(find_data.cFileName, A2W(stat.fpath.c_str()));

        FillFindData(&find_data, DokanFileInfo);
    });

    return STATUS_SUCCESS;
}

int main(int argc, char ** argv) {
    ok = (CoInitializeEx(0, COINIT_MULTITHREADED) == S_OK);

    USES_CONVERSION; try {
        // Line of code that does all the work:
        auto options = structopt::app(APP_NAME, APP_VERSION).parse<zipmount_options>(argc, argv);

        root_directory = A2W(options.root_directory.value().c_str());
        mount_point = A2W(options.mount_point.value().c_str());
        
        acp = options.acp.value(); if(acp != "default") {
            LCID lcid = atoi(acp.c_str()); SetThreadLocale(lcid);
        }

        ok(format("check existance of {}", options.root_directory.value())) =
            fs::exists(root_directory);

        SetConsoleCtrlHandler([](DWORD type) {
            switch(type) {
                case CTRL_C_EVENT:
                case CTRL_BREAK_EVENT:
                case CTRL_CLOSE_EVENT:
                case CTRL_LOGOFF_EVENT:
                case CTRL_SHUTDOWN_EVENT: {
                    DokanRemoveMountPoint(mount_point.c_str()); exit(0);
                }
            }

            return FALSE;
        }, true);

        DOKAN_OPTIONS dokanOptions {0}; {
            dokanOptions.Version = DOKAN_VERSION;
            dokanOptions.SingleThread = TRUE;
            dokanOptions.Timeout = 3000 * 1000;
            // dokanOptions.Timeout = 3 * 1000;
            dokanOptions.MountPoint = mount_point.c_str();
            dokanOptions.Options =
                DOKAN_OPTION_REMOVABLE | DOKAN_OPTION_WRITE_PROTECT | DOKAN_OPTION_MOUNT_MANAGER;
            // dokanOptions.UNCName = unc_name.c_str();
            // dokanOptions.Options = DOKAN_OPTION_NETWORK | DOKAN_OPTION_ENABLE_UNMOUNT_NETWORK_DRIVE;
        }

        DOKAN_OPERATIONS dokanOperations {0}; {
            dokanOperations.ZwCreateFile = zmCreateFile;
            dokanOperations.CloseFile = zmCloseFile;
            dokanOperations.ReadFile = zmReadFile;
            dokanOperations.GetFileInformation = zmGetFileInformation;
            dokanOperations.FindFiles = zmFindFiles;
        }

        DokanInit(); ok("(CTRL + C) to quit");

        auto rc = DokanMain(&dokanOptions, &dokanOperations); switch(rc) {
            case DOKAN_SUCCESS: break;
            case DOKAN_ERROR: println("Error"); break;
            case DOKAN_DRIVE_LETTER_ERROR: println("Bad Drive letter"); break;
            case DOKAN_DRIVER_INSTALL_ERROR: println("Can't install driver"); break;
            case DOKAN_START_ERROR: println("Driver something wrong"); break;
            case DOKAN_MOUNT_ERROR: println("Can't assign a drive letter"); break;
            case DOKAN_MOUNT_POINT_ERROR: println("Mount point error"); break;
            case DOKAN_VERSION_ERROR: println("Version error"); break;
            default: println("Unknown error: {}", rc); break;
        }

        DokanShutdown();
    }
    catch(structopt::exception & e) {
        println("{}", e.what()); println("{}", e.help());
    }

    return 0;
}
#ifndef zip_5238585a_2153_4421_a0b1_7323edf7e7ad
#define zip_5238585a_2153_4421_a0b1_7323edf7e7ad

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>
#include <utility>

#include "zlib.h"

// ZIP file format constants
struct zip_constants {
    static constexpr uint16_t SIGNATURE_LOCAL_FILE = 0x0403;         // PK\x03\x04
    static constexpr uint16_t SIGNATURE_CENTRAL_DIR = 0x0201;        // PK\x01\x02
    static constexpr uint16_t SIGNATURE_END_OF_CENTRAL_DIR = 0x0605; // PK\x05\x06
    static constexpr uint16_t SIGNATURE_DATA_DESCRIPTOR = 0x0807;    // PK\x07\x08

    // ZIP sigatures
    static constexpr uint32_t SIGNATURE_ZIP = 0x04034b50;
    static constexpr uint32_t SIGNATURE_ZIP_END_OF_CENTRAL_DIR = 0x02014b50;

    // ZIP64 signatures
    static constexpr uint32_t SIGNATURE_ZIP64_END_OF_CENTRAL_DIR = 0x06064b50;         // PK\x06\x06
    static constexpr uint32_t SIGNATURE_ZIP64_END_OF_CENTRAL_DIR_LOCATOR = 0x07064b50; // PK\x06\x07
    static constexpr uint16_t SIGNATURE_ZIP64_EXTENDED_INFO = 0x0001;                  // ZIP64 extended info field
};

// Compression methods
enum class zip_compression_method : uint16_t {
    NONE = 0,
    SHRUNK = 1,
    REDUCED_1 = 2,
    REDUCED_2 = 3,
    REDUCED_3 = 4,
    REDUCED_4 = 5,
    IMPLODED = 6,
    DEFLATED = 8,
    ENHANCED_DEFLATED = 9,
    PKWARE_DCL_IMPLODED = 10,
    BZIP2 = 12,
    LZMA = 14,
    IBM_TERSE = 18,
    IBM_LZ77_Z = 19,
    ZSTANDARD = 93,
    MP3 = 94,
    XZ = 95,
    JPEG = 96,
    WAVPACK = 97,
    PPMD = 98,
    AEX_ENCRYPTION_MARKER = 99
};

// General purpose bit flags
struct zip_gp_flags {
    uint16_t raw_flags;

    bool is_encrypted() const { return (raw_flags & 0x0001) != 0; }
    bool has_data_descriptor() const { return (raw_flags & 0x0008) != 0; }
    bool is_compressed_patched() const { return (raw_flags & 0x0020) != 0; }
    bool is_strongly_encrypted() const { return (raw_flags & 0x0040) != 0; }
    bool uses_utf8() const { return (raw_flags & 0x0800) != 0; }
};

// Packed structures for direct casting from ZIP file data
#pragma pack(push, 1)
// Local file header structure (packed for direct casting)
struct zip_file_header {
    uint16_t version;
    uint16_t raw_flags;
    uint16_t compression;
    uint32_t dos_time;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
    // Marker for the start of variable-length data
    // The actual filename follows this header in the ZIP file
    char file_name[0];
};

// Central directory entry structure (packed for direct casting)
struct zip_dir_entry {
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t raw_flags;
    uint16_t compression;
    uint32_t dos_time;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
    uint16_t comment_length;
    uint16_t disk_number_start;
    uint16_t internal_file_attributes;
    uint32_t external_file_attributes;
    uint32_t local_header_offset;
    // Marker for the start of variable-length data
    // The actual filename follows this header in the ZIP file
    char file_name[0];
};

struct zip_fake_dir_entry : zip_dir_entry {
    char xfile_name[1024];
};

// End of central directory record structure (packed for direct casting)
struct zip_end_of_central_dir {
    uint16_t disk_number;
    uint16_t central_dir_disk_number;
    uint16_t num_entries_on_disk;
    uint16_t num_entries_total;
    uint32_t central_dir_size;
    uint32_t central_dir_offset;
    uint16_t comment_length;
};

// ZIP64 end of central directory locator structure (packed for direct casting)
struct zip64_end_of_central_dir_locator {
    uint32_t disk_number_with_zip64_end;
    uint64_t relative_offset_of_zip64_end;
    uint32_t total_number_of_disks;
};

// ZIP64 end of central directory record structure (packed for direct casting)
struct zip64_end_of_central_dir {
    uint64_t size_of_record;
    uint16_t version_made_by;
    uint16_t version_needed;
    uint32_t disk_number;
    uint32_t central_dir_disk_number;
    uint64_t num_entries_on_disk;
    uint64_t num_entries_total;
    uint64_t central_dir_size;
    uint64_t central_dir_offset;
};

// File info structure for retrieving information about files in the archive
#pragma pack(pop)

// File info structure for retrieving information about files in the archive
struct zip_file_info {
    // Direct reference to the filename in the original data
    std::string_view filename;

    bool is_directory {false};

    size_t compressed_size {0};
    size_t uncompressed_size {0};

    // Modification time (DOS format time)
    uint32_t mod_time {0};

    // Compression method
    zip_compression_method compression {zip_compression_method::NONE};

    // Pointer to compressed data
    const uint8_t * raw_ptr {nullptr};

    // Additional pointer to data
    const uint8_t * data_ptr {nullptr};

    // Default constructor
    zip_file_info() = default;

    // Disable copy constructor
    zip_file_info(const zip_file_info &) = delete;

    // Disable copy assignment
    zip_file_info & operator=(const zip_file_info &) = delete;

    // Move constructor
    zip_file_info(zip_file_info && other) noexcept {
        move_from(std::move(other));
    }

    // Move assignment
    zip_file_info & operator=(zip_file_info && other) noexcept {
        if(this != &other) {
            free_resources();
            move_from(std::move(other));
        }
        return *this;
    }

    // Destructor to free allocated memory
    ~zip_file_info() {
        free_resources();
    }

    // Helper method to free resources
    void free_resources() {
        // Only free data_ptr if it's not null and different from raw_ptr
        if(data_ptr != nullptr && data_ptr != raw_ptr) {
            free(const_cast<uint8_t *>(data_ptr));
            data_ptr = nullptr;
        }
    }

    // Helper method to move resources from another instance
    void move_from(zip_file_info && other) {
        filename = std::move(other.filename);
        is_directory = other.is_directory;
        compressed_size = other.compressed_size;
        uncompressed_size = other.uncompressed_size;
        mod_time = other.mod_time;
        compression = other.compression;
        raw_ptr = other.raw_ptr;
        data_ptr = other.data_ptr;

        // Reset other's pointers
        other.raw_ptr = nullptr;
        other.data_ptr = nullptr;
    }

    // Returns decompressed data or raw data if no compression
    uint8_t * data() {
        // If data_ptr is already set, return it
        if(data_ptr) {
            return const_cast<uint8_t *>(data_ptr);
        }

        // If no raw data or this is a directory, return nullptr
        if(!raw_ptr || is_directory) {
            return nullptr;
        }

        // If no compression, just set data_ptr to raw_ptr and return it
        if(compression == zip_compression_method::NONE) {
            data_ptr = raw_ptr;
            return const_cast<uint8_t *>(data_ptr);
        }

        // Need to decompress the data
        if(compression == zip_compression_method::DEFLATED) {
            // Allocate memory for decompressed data
            uint8_t * decompressed = static_cast<uint8_t *>(malloc(uncompressed_size));

            // Set up zlib stream
            z_stream strm; {
                strm.zalloc = Z_NULL;
                strm.zfree = Z_NULL;
                strm.opaque = Z_NULL;
                strm.avail_in = static_cast<uInt>(compressed_size);
                strm.next_in = const_cast<Bytef *>(raw_ptr);
                strm.avail_out = static_cast<uInt>(uncompressed_size);
                strm.next_out = decompressed;
            }

            // Initialize with negative windowBits to indicate raw deflate data (no zlib header)
            inflateInit2(&strm, -MAX_WBITS);

            // Decompress
            int ret = inflate(&strm, Z_FINISH);
            inflateEnd(&strm);

            if(ret != Z_STREAM_END) {
                // Decompression failed
                free(decompressed);
                return nullptr;
            }

            // Store the decompressed data pointer
            data_ptr = decompressed;
            return const_cast<uint8_t *>(data_ptr);
        }

        // Unsupported compression method
        return nullptr;
    }
};

struct zip_archive {
    // Data members
    uint8_t * m_data = nullptr;   // Pointer to the beginning of the buffer
    size_t m_size = 0;            // Size of the entire buffer
    size_t m_zip_base_offset = 0; // Offset to the beginning of the ZIP archive within the buffer
    size_t m_zip_central_dir_offset = 0;

    // Central directory information
    const uint8_t * m_central_dir = nullptr; // Pointer to the central directory
    size_t m_num_entries = 0;                // Number of entries in the central directory
    std::vector<size_t> m_entry_offsets;     // Offsets of each entry in the central directory

    // ZIP64 support
    bool m_is_zip64 = false;                                 // Whether this is a ZIP64 archive

    zip_archive() = default;

    zip_archive(const void * data, size_t size) { open((uint8_t *)data, size); }

    zip_archive(std::string_view const & x) { open((uint8_t *)x.data(), x.size()); }

    // Find the position of the end of central directory record
    // Returns the position of the signature or SIZE_MAX if not found
    static size_t find_end_of_central_dir(uint8_t * data, size_t size) {
        // ZIP end of central directory signature is 'PK\x05\x06'
        // It must be followed by a variable-length comment, so we need to search for it
        // from the end of the file

        // Minimum size of end of central directory record is 22 bytes
        // 4 bytes signature + 18 bytes data + variable length comment
        if(size < 22) return SIZE_MAX;

        // Search for the signature from the end of the file
        // The maximum size of the comment is 65535 bytes, so we don't need to search
        // more than that distance from the end
        size_t search_size = std::min(size, size_t(65557)); // 65535 + 22

        for(size_t i = 0; i < search_size - 4; ++i) {
            size_t pos = size - 4 - i;
            if(data[pos] == 'P' && data[pos + 1] == 'K' &&
                data[pos + 2] == 0x05 && data[pos + 3] == 0x06) {
                return pos;
            }
        }

        return SIZE_MAX;
    }

    // Find ZIP64 end of central directory locator
    // Returns the position of the signature or SIZE_MAX if not found
    static size_t find_zip64_end_of_central_dir_locator(uint8_t * data, size_t size, size_t eocd_pos) {
        // ZIP64 end of central directory locator is 20 bytes and precedes the EOCD
        if(eocd_pos < 20) return SIZE_MAX;

        size_t locator_pos = eocd_pos - 20;

        // Check for ZIP64 end of central directory locator signature
        if(data[locator_pos] == 'P' && data[locator_pos + 1] == 'K' &&
            data[locator_pos + 2] == 0x06 && data[locator_pos + 3] == 0x07) {
            return locator_pos;
        }

        return SIZE_MAX;
    }

    // Find ZIP64 end of central directory record
    // Returns the position of the record or SIZE_MAX if not found
    static size_t find_zip64_end_of_central_dir(uint8_t * data, size_t size, uint64_t offset) {
        if(offset + 56 > size) return SIZE_MAX;

        // Check for ZIP64 end of central directory signature
        if(data[offset] == 'P' && data[offset + 1] == 'K' &&
            data[offset + 2] == 0x06 && data[offset + 3] == 0x06) {
            return offset;
        }

        return SIZE_MAX;
    }

    static bool is_valid(uint8_t * data, size_t size) {
        return find_end_of_central_dir(data, size) != SIZE_MAX;
    }

    void open(uint8_t * data, size_t size) {
        // Find the end of central directory record
        size_t eocd_pos = find_end_of_central_dir(data, size); if(eocd_pos == SIZE_MAX) {
            throw std::runtime_error("Not a valid ZIP file");
        }

        // Store the data pointer and size for later use
        m_data = data; m_size = size;

        // Parse the end of central directory record
        // Cast the data to the packed structure
        const zip_end_of_central_dir * eocd_record = reinterpret_cast<const zip_end_of_central_dir *>(m_data + eocd_pos + 4);

        // Check for ZIP64 format
        check_zip64_support(eocd_pos);

        // Determine the base offset of the ZIP archive
        if((m_zip_base_offset = find_zip_base_offset(eocd_record)) == SIZE_MAX) {
            throw std::runtime_error("Invalid ZIP file");
        }

        // Parse the central directory entries
        parse_central_directory(eocd_record);
    }

    // Get the number of files in the archive
    size_t size() const { return m_num_entries; }

    // Check if this is a ZIP64 archive
    bool is_zip64() const { return m_is_zip64; }

    // Parse ZIP64 extended information from extra field
    bool parse_zip64_extended_info(const uint8_t * extra_field, uint16_t extra_field_length,
        uint64_t & uncompressed_size, uint64_t & compressed_size,
        uint64_t & local_header_offset) const {
        if(!extra_field || extra_field_length == 0) return false;

        size_t pos = 0;
        while(pos + 4 <= extra_field_length) {
            uint16_t header_id = *reinterpret_cast<const uint16_t *>(extra_field + pos);
            uint16_t data_size = *reinterpret_cast<const uint16_t *>(extra_field + pos + 2);

            if(header_id == zip_constants::SIGNATURE_ZIP64_EXTENDED_INFO) {
                const uint8_t * data = extra_field + pos + 4;
                size_t data_pos = 0;

                // Parse ZIP64 extended information
                if(data_pos + 8 <= data_size) {
                    uncompressed_size = *reinterpret_cast<const uint64_t *>(data + data_pos);
                    data_pos += 8;
                }
                if(data_pos + 8 <= data_size) {
                    compressed_size = *reinterpret_cast<const uint64_t *>(data + data_pos);
                    data_pos += 8;
                }
                if(data_pos + 8 <= data_size) {
                    local_header_offset = *reinterpret_cast<const uint64_t *>(data + data_pos);
                    data_pos += 8;
                }

                return true;
            }

            pos += 4 + data_size;
        }

        return false;
    }

    // Find a central directory entry by index
    // Returns the entry pointer, or nullptr if not found
    const zip_dir_entry * find_entry_by_index(size_t index) const {
        if(!m_central_dir || index >= m_num_entries) return nullptr;

        // Use the entry offset table for direct access by index
        if(index < m_entry_offsets.size()) {
            size_t pos = m_entry_offsets[index];

            // Get the entry data
            const uint8_t * entry_data = m_central_dir + pos + 4;

            // Return the entry
            return reinterpret_cast<const zip_dir_entry *>(entry_data);
        }

        return nullptr;
    }

    // Find an entry by name using binary search on the sorted entry offset table
    // Returns the entry pointer, or nullptr if not found
    const zip_dir_entry * find_entry_by_name(const char * name, size_t len) const {
        size_t index = find_entry_index(name, len);
        if(index == m_num_entries) return nullptr;
        return find_entry_by_index(index);
    }

    // Check for ZIP64 support in the archive
    void check_zip64_support(size_t eocd_pos) {
        // First check if we need ZIP64 due to large file sizes or entry counts
        const zip_end_of_central_dir * eocd_record = reinterpret_cast<const zip_end_of_central_dir *>(m_data + eocd_pos + 4);

        // Check for ZIP64 end of central directory locator
        size_t locator_pos = find_zip64_end_of_central_dir_locator(m_data, m_size, eocd_pos);
        if(locator_pos != SIZE_MAX) {
            const zip64_end_of_central_dir_locator * locator =
                reinterpret_cast<const zip64_end_of_central_dir_locator *>(m_data + locator_pos + 4);

            // Find ZIP64 end of central directory record
            size_t zip64_eocd_pos = find_zip64_end_of_central_dir(m_data, m_size, locator->relative_offset_of_zip64_end);

            if(zip64_eocd_pos != SIZE_MAX) {
                // Validate ZIP64 record
                const zip64_end_of_central_dir * zip64_eocd = reinterpret_cast<const zip64_end_of_central_dir *>(m_data + zip64_eocd_pos + 4);
                if(zip64_eocd->size_of_record >= 44) { // Minimum size minus signature and size field
                    m_is_zip64 = true;
                }
            }
        }

        // Also check if we need ZIP64 due to 32-bit overflow
        // if(!m_is_zip64 && eocd_record) {
        //     if(eocd_record->num_entries_total == 0xFFFF ||
        //         eocd_record->central_dir_size == 0xFFFFFFFF ||
        //         eocd_record->central_dir_offset == 0xFFFFFFFF) {
        //         // These values indicate ZIP64 is needed but we don't have the ZIP64 structures
        //         throw std::runtime_error("ZIP64 required but ZIP64 structures not found");
        //     }
        // }
    }

    // Find an entry index by name using binary search on the sorted entry offset table
    // Returns the index of the entry, or total_entries if not found
    size_t find_entry_index(const char * name, size_t len) const {
        if(!m_central_dir || !name || len == 0) return m_num_entries;

        // Use binary search to find the entry
        size_t left = 0;
        size_t right = m_num_entries;

        while(left < right) {
            size_t mid = left + (right - left) / 2;

            const zip_dir_entry * entry = find_entry_by_index(mid);
            if(!entry) {
                // If we can't get the entry, try linear search from here
                for(size_t i = left; i < right; ++i) {
                    entry = find_entry_by_index(i);
                    if(!entry) continue;

                    // Check if the filename matches
                    if(entry->filename_length == len &&
                        memcmp(entry->file_name, name, len) == 0) {
                        return i;
                    }
                }
                return m_num_entries;
            }

            // Compare filenames
            int cmp;
            if(entry->filename_length == len) {
                cmp = memcmp(entry->file_name, name, len);
            } else {
                // Compare up to the shorter length first
                size_t min_len = std::min<size_t>(entry->filename_length, len);
                cmp = memcmp(entry->file_name, name, min_len);

                // If they match up to the shorter length, the shorter one comes first
                if(cmp == 0) {
                    cmp = (entry->filename_length < len) ? -1 : 1;
                }
            }

            if(cmp == 0) {
                return mid; // Found exact match
            } else if(cmp < 0) {
                left = mid + 1; // Search in the right half
            } else {
                right = mid; // Search in the left half
            }
        }

        return m_num_entries; // Not found
    }

    // Get a filename by index
    std::string_view get_filename(size_t index) const {
        const zip_dir_entry * entry = find_entry_by_index(index); {
            if(!entry) return {};
        }

        return std::string_view(reinterpret_cast<const char *>(entry->file_name), entry->filename_length);
    }

    // Get a pointer to the file data and its size
    std::pair<const uint8_t *, size_t> get_file_data(size_t index) const {
        const zip_dir_entry * entry = find_entry_by_index(index);
        if(!entry) return {nullptr, 0};

        // Check for ZIP64 extended information in central directory entry
        uint64_t uncompressed_size = entry->uncompressed_size;
        uint64_t compressed_size = entry->compressed_size;
        uint64_t local_header_offset = entry->local_header_offset;

        if(m_is_zip64 && entry->extra_field_length > 0) {
            const uint8_t * extra_field = reinterpret_cast<const uint8_t *>(entry->file_name) + entry->filename_length;
            parse_zip64_extended_info(extra_field, entry->extra_field_length,
                uncompressed_size, compressed_size, local_header_offset);
        }

        // The local header offset is relative to the start of the ZIP archive
        size_t absolute_header_offset = m_zip_base_offset + local_header_offset;

        // Find the local file header
        if(absolute_header_offset + 30 > m_size) return {nullptr, 0};

        const uint8_t * local_header_ptr = m_data + absolute_header_offset;

        // Verify local header signature
        if(local_header_ptr[0] != 'P' || local_header_ptr[1] != 'K' ||
            local_header_ptr[2] != 0x03 || local_header_ptr[3] != 0x04) {
            absolute_header_offset = m_zip_base_offset + (local_header_offset | 0x0100000000ll);
            
            if(absolute_header_offset + 30 > m_size) return {nullptr, 0};
            
            local_header_ptr = m_data + absolute_header_offset;

            if(local_header_ptr[0] != 'P' || local_header_ptr[1] != 'K' ||
                local_header_ptr[2] != 0x03 || local_header_ptr[3] != 0x04) {
                return {nullptr, 0};
            }
        }

        // Get direct access to the local header
        const zip_file_header * local_header = reinterpret_cast<const zip_file_header *>(local_header_ptr + 4);

        // Calculate pointers to the various parts of the local file header
        const uint8_t * local_filename_ptr = reinterpret_cast<const uint8_t *>(&local_header->file_name);
        const uint8_t * local_extra_field_ptr = local_filename_ptr + local_header->filename_length;
        const uint8_t * file_data_ptr = local_extra_field_ptr + local_header->extra_field_length;

        // Verify we have enough data
        size_t file_data_offset = file_data_ptr - m_data;
        if(file_data_offset + static_cast<size_t>(compressed_size) > m_size) return {nullptr, 0};

        // Return a pointer to the file data and its size
        return {file_data_ptr, static_cast<size_t>(compressed_size)};
    }

    // Get file info by index
    zip_file_info get_file_info(size_t index) const {
        const zip_dir_entry * entry = find_entry_by_index(index); {
            if(!entry) return {};
        }

        // Get the compressed data pointer
        auto [data_ptr, data_size] = get_file_data(index);

        // Check if the file is a directory (ends with '/')
        bool is_directory = false; {
            if(entry->filename_length > 0) {
                is_directory = (reinterpret_cast<const char *>(entry->file_name)[entry->filename_length - 1] == '/');
            }
        }

        // Check for ZIP64 extended information in central directory entry
        uint64_t uncompressed_size = entry->uncompressed_size;
        uint64_t compressed_size = entry->compressed_size;

        if(m_is_zip64 && entry->extra_field_length > 0) {
            const uint8_t * extra_field = reinterpret_cast<const uint8_t *>(entry->file_name) + entry->filename_length;
            uint64_t dummy_offset; // We don't need the offset for file info
            parse_zip64_extended_info(extra_field, entry->extra_field_length,
                uncompressed_size, compressed_size, dummy_offset);
        }

        // Populate the info structure
        zip_file_info info; {
            info.filename = std::string_view(reinterpret_cast<const char *>(entry->file_name), entry->filename_length);
            info.compressed_size = static_cast<size_t>(compressed_size);
            info.uncompressed_size = static_cast<size_t>(uncompressed_size);
            info.mod_time = entry->dos_time;
            info.compression = static_cast<zip_compression_method>(entry->compression);
            info.raw_ptr = data_ptr;
            info.is_directory = is_directory;
        }

        return info;
    }

    // Find the base offset of the ZIP archive using EOCD record
    // This algorithm searches from the end of the file to find the EOCD signature,
    // then uses the central directory offset to calculate the ZIP archive start position
    size_t find_zip_base_offset(const zip_end_of_central_dir * eocd_record) {
        const size_t max_entry_size = 4096;
        size_t offset = m_size - 4;
        size_t last_offset = offset;

        size_t c = 0; while(true) {
            if(offset > last_offset) break;
            if((last_offset - offset) > max_entry_size) break;

            const uint8_t * central_dir = m_data + offset;

            // Check for central directory signature (PK\x01\x02)
            if(central_dir[0] == 'P' && central_dir[1] == 'K' &&
                central_dir[2] == 0x01 && central_dir[3] == 0x02) {
                ++c; last_offset = offset;
            }

            offset--;
        }

        if(c > 0) {
            m_num_entries = c; m_central_dir = m_data + last_offset;
        }

        c = 0; offset = 0; while(offset < m_size - 4) {
            if(*(reinterpret_cast<const uint32_t *>(m_data + offset)) == zip_constants::SIGNATURE_ZIP) {
                return offset;
                // printf("%zx, %zx\n", c++, offset);
            }

            ++offset;
        }

        return 0;
    }

    // Parse the end of central directory record
    // Store the central directory information
    void parse_central_directory(const zip_end_of_central_dir * eocd_record) {
        if(m_num_entries == 0) return;

        // Build the entry offset table for fast access by index
        m_entry_offsets.clear(); m_entry_offsets.reserve(m_num_entries);

        size_t pos = 0; for(size_t i = 0; i < m_num_entries; ++i) {
            // Verify central directory entry signature
            if(m_central_dir[pos] != 'P' || m_central_dir[pos + 1] != 'K' ||
                m_central_dir[pos + 2] != 0x01 || m_central_dir[pos + 3] != 0x02) {
                break;
            }

            // Store the offset to this entry
            m_entry_offsets.push_back(static_cast<uint32_t>(pos));

            // Get the entry data
            const zip_dir_entry * current_entry = reinterpret_cast<const zip_dir_entry *>(m_central_dir + pos + 4);

            // Move to the next entry
            pos += 46 + current_entry->filename_length + current_entry->extra_field_length + current_entry->comment_length;
        }

        // Sort the offset table by filename for binary search
        std::sort(m_entry_offsets.begin(), m_entry_offsets.end(),
            [this](uint32_t a, uint32_t b) {
                const zip_dir_entry * entry_a = reinterpret_cast<const zip_dir_entry *>(m_central_dir + a + 4);
                const zip_dir_entry * entry_b = reinterpret_cast<const zip_dir_entry *>(m_central_dir + b + 4);

                // Compare filenames
                size_t min_len = std::min<size_t>(entry_a->filename_length, entry_b->filename_length);
                int cmp = memcmp(entry_a->file_name, entry_b->file_name, min_len);

                // If they match up to the shorter length, the shorter one comes first
                if(cmp == 0) {
                    return entry_a->filename_length < entry_b->filename_length;
                }

                return cmp < 0;
            });
    }

    template<typename F>
    void for_each_entry(F && f) {
        for(size_t i = 0; i < m_num_entries; ++i) {
            if constexpr(std::is_void_v<decltype(f(std::declval<const zip_dir_entry *>()))>) {
                // Function doesn't return a value, just call it
                f(find_entry_by_index(i));
            } else {
                // Function returns a value, check if it's true to break
                if(f(find_entry_by_index(i))) {
                    break;
                }
            }
        }
    }

    template<typename F>
    void for_each_entry(std::string_view const & parent, F && f) {
        // parent must be empty or ends with '/'
        if(!(parent.empty() || parent.back() == '/')) return;

        // Use binary search to find the first entry that starts with parent
        size_t start_index = 0; if(!parent.empty()) {
            // Binary search for the first entry that starts with parent
            size_t left = 0;
            size_t right = m_num_entries;

            while(left < right) {
                size_t mid = left + (right - left) / 2;

                const zip_dir_entry * entry = find_entry_by_index(mid); {
                    if(!entry) return;
                }

                std::string_view filename(reinterpret_cast<const char *>(entry->file_name), entry->filename_length);

                // Compare with parent
                if(filename.size() >= parent.size() &&
                    memcmp(filename.data(), parent.data(), parent.size()) >= 0) {
                    right = mid;
                } else {
                    left = mid + 1;
                }
            }

            start_index = left;
        }

        std::string dir_name;

        // Iterate through entries starting from the found index
        for(size_t i = start_index; i < m_num_entries; ++i) {
            const zip_dir_entry * entry = find_entry_by_index(i);
            if(!entry) continue;

            // Get the filename as string_view
            std::string_view filename(reinterpret_cast<const char *>(entry->file_name), entry->filename_length);

            // If we've moved past entries that start with parent, we can stop
            if(!parent.empty() && (filename.size() < parent.size() ||
                                      filename.substr(0, parent.size()) != parent)) {
                if(i > start_index) {
                    // We've moved past the parent section
                    break;
                }
                continue;
            }

            // Skip the parent directory itself
            if(filename == parent) {
                continue;
            }

            // For any parent (including root), only include direct children
            // Get the relative path from parent
            std::string_view relative_path;
            if(!parent.empty()) {
                relative_path = filename.substr(parent.size());
            } else {
                relative_path = filename;
            }

            // Skip if not a direct child (contains additional slashes beyond the first one)
            size_t slash_pos = relative_path.find('/');
            if(slash_pos == std::string_view::npos) {
                // No slash - this is a file directly in the parent directory
            } else if(slash_pos == relative_path.size() - 1) {
                // Slash at the end only - this is a directory directly in the parent
                dir_name = relative_path.substr(0, slash_pos + 1);
            } else {
                // Slash in the middle - this is in a subdirectory, create virtual directory entry
                std::string_view dname = relative_path.substr(0, slash_pos + 1); // Include the trailing slash

                if(dname == dir_name) continue;

                dir_name = dname;

                std::string dir_path = (std::filesystem::path(parent) / dname).string();

                // Create a virtual directory entry on the stack
                zip_fake_dir_entry virtual_entry = {};

                // Set DIR marker and filename
                virtual_entry.external_file_attributes = 0x10; // Directory attribute
                virtual_entry.filename_length = static_cast<uint16_t>(dir_path.size());

                // Copy the directory name to the virtual entry's filename buffer
                if(dir_path.size() < sizeof(virtual_entry.xfile_name)) {
                    memcpy(virtual_entry.xfile_name, dir_path.data(), dir_path.size());
                }

                // Return the directory name to the caller
                if constexpr(std::is_void_v<decltype(f(std::declval<const zip_dir_entry *>()))>) {
                    // Function doesn't return a value, just call it with the virtual entry
                    f(&virtual_entry);
                } else {
                    // Function returns a value, check if it's true to break
                    if(f(&virtual_entry)) {
                        break;
                    }
                }

                continue;
            }

            // Call the function with the entry
            if constexpr(std::is_void_v<decltype(f(std::declval<const zip_dir_entry *>()))>) {
                // Function doesn't return a value, just call it
                f(entry);
            } else {
                // Function returns a value, check if it's true to break
                if(f(entry)) {
                    break;
                }
            }
        }
    }
};

#endif // zip_5238585a_2153_4421_a0b1_7323edf7e7ad

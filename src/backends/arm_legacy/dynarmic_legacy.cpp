#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <chrono>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <thread>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_set>
#include <type_traits>
#include <climits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <direct.h>
#endif

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"

extern "C" {
#include "zlib.h"
#include "storage_win.h"
#include "audio_win.h"
#include "net_compat_win.h"
#include "runtime_settings.h"
#include "song_http_win.h"
#include "build_info.h"
}

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

#ifdef _WIN32
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001u;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> callback) : callback_(std::move(callback)) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() { if (callback_) callback_(); }
private:
    std::function<void()> callback_;
};

namespace {

constexpr u32 kGameBase = 0x10000000u;
constexpr u32 kSmokeBase = 0x0F000000u;
constexpr u32 kObjectBase = 0x20000000u;
constexpr u32 kObjectRegionSize = 0x00100000u;
constexpr u32 kImportBase = 0x21000000u;
constexpr u32 kImportRegionSize = 0x00100000u;
constexpr u32 kControlBase = 0x22000000u;
constexpr u32 kControlRegionSize = 0x00100000u;
constexpr u32 kHeapBase = 0x30000000u;
constexpr u32 kHeapSize = 0x10000000u;
constexpr u32 kStackBase = 0x70000000u;
constexpr u32 kStackSize = 0x01000000u;
constexpr u32 kPageSize = 0x1000u;
constexpr u32 kReturnStub = kControlBase + 0x0000u;
constexpr u32 kVmObject = kControlBase + 0x1000u;
constexpr u32 kVmTable = kControlBase + 0x2000u;
constexpr u32 kVmStubs = kControlBase + 0x3000u;
constexpr u32 kEnvObject = kControlBase + 0x10000u;
constexpr u32 kEnvTable = kControlBase + 0x11000u;
constexpr u32 kEnvStubs = kControlBase + 0x12000u;
constexpr u32 kFakeRefBase = kControlBase + 0x30000u;
constexpr u32 kGuestFileObjectSize = 0x100u;
constexpr u32 kBionicFileFlagsOffset = 12u;
constexpr u32 kBionicFileDescriptorOffset = 14u;
constexpr u16 kBionicFileReadFlag = 0x0004u;
constexpr u16 kBionicFileWriteFlag = 0x0008u;
constexpr u16 kBionicFileReadWriteFlag = 0x0080u;
constexpr u32 kSvcReturn = 0x00FFFFFEu;
constexpr u32 kSvcVmBase = 0x00F00000u;
constexpr u32 kSvcJniBase = 0x00E00000u;
constexpr u32 kJniVersion14 = 0x00010004u;
constexpr u32 kGuestCallTickBudget = 250000000u;
constexpr std::size_t kJniTableSize = 232;
constexpr Dynarmic::HaltReason kCallbackHalt = Dynarmic::HaltReason::UserDefined1;

constexpr u32 kPtLoad = 1;
constexpr u16 kEtDyn = 3;
constexpr u16 kEmArm = 40;
constexpr u32 kShtDynsym = 11;
constexpr u32 kShtRel = 9;
constexpr u32 kShtRela = 4;
constexpr u32 kShtInitArray = 14;
constexpr u16 kShnUndef = 0;
constexpr u8 kSttObject = 1;
constexpr u32 kRArmNone = 0;
constexpr u32 kRArmAbs32 = 2;
constexpr u32 kRArmGlobDat = 21;
constexpr u32 kRArmJumpSlot = 22;
constexpr u32 kRArmRelative = 23;

#pragma pack(push, 1)
struct Elf32Ehdr {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u32 entry;
    u32 phoff;
    u32 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
};
struct Elf32Phdr {
    u32 type;
    u32 offset;
    u32 vaddr;
    u32 paddr;
    u32 filesz;
    u32 memsz;
    u32 flags;
    u32 align;
};
struct Elf32Shdr {
    u32 name;
    u32 type;
    u32 flags;
    u32 addr;
    u32 offset;
    u32 size;
    u32 link;
    u32 info;
    u32 addralign;
    u32 entsize;
};
struct Elf32Sym {
    u32 name;
    u32 value;
    u32 size;
    u8 info;
    u8 other;
    u16 shndx;
};
struct Elf32Rel {
    u32 offset;
    u32 info;
};
#pragma pack(pop)

static u32 AlignDown(u32 value, u32 alignment) {
    return value & ~(alignment - 1u);
}
static u32 AlignUp(u32 value, u32 alignment) {
    if (value > std::numeric_limits<u32>::max() - (alignment - 1u)) {
        throw std::runtime_error("32-bit address overflow while aligning ELF image");
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}
template <typename T>
static T ReadPod(const std::vector<u8>& bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        throw std::runtime_error("truncated binary structure");
    }
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}
static u16 ReadLe16(const std::vector<u8>& bytes, std::size_t offset) {
    return ReadPod<u16>(bytes, offset);
}
static u32 ReadLe32(const std::vector<u8>& bytes, std::size_t offset) {
    return ReadPod<u32>(bytes, offset);
}
static std::vector<u8> ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("could not open " + path);
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("invalid file length for " + path);
    }
    file.seekg(0, std::ios::beg);
    std::vector<u8> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("could not read " + path);
    }
    return bytes;
}
static std::vector<u8> InflateRaw(const u8* source, std::size_t source_size, std::size_t output_size) {
    std::vector<u8> output(output_size);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(source));
    stream.avail_in = static_cast<uInt>(source_size);
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) throw std::runtime_error("inflateInit2 failed");
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END || stream.total_out != output_size) {
        throw std::runtime_error("raw deflate stream did not produce the expected size");
    }
    return output;
}
static std::vector<u8> ExtractZipMember(const std::vector<u8>& zip, std::string_view requested_name) {
    constexpr u32 kEocdSignature = 0x06054B50u;
    constexpr u32 kCentralSignature = 0x02014B50u;
    constexpr u32 kLocalSignature = 0x04034B50u;
    const std::size_t search_start = zip.size() > (0xFFFFu + 22u) ? zip.size() - (0xFFFFu + 22u) : 0;
    std::optional<std::size_t> eocd;
    if (zip.size() >= 22) {
        for (std::size_t pos = zip.size() - 22;; --pos) {
            if (ReadLe32(zip, pos) == kEocdSignature) { eocd = pos; break; }
            if (pos == search_start) break;
        }
    }
    if (!eocd) throw std::runtime_error("APK end-of-central-directory record not found");
    const u16 entry_count = ReadLe16(zip, *eocd + 10);
    const u32 central_size = ReadLe32(zip, *eocd + 12);
    const u32 central_offset = ReadLe32(zip, *eocd + 16);
    if (static_cast<u64>(central_offset) + central_size > zip.size()) {
        throw std::runtime_error("APK central directory is outside the file");
    }
    std::size_t pos = central_offset;
    for (u16 index = 0; index < entry_count; ++index) {
        if (pos + 46 > zip.size() || ReadLe32(zip, pos) != kCentralSignature) {
            throw std::runtime_error("invalid APK central-directory entry");
        }
        const u16 method = ReadLe16(zip, pos + 10);
        const u32 expected_crc = ReadLe32(zip, pos + 16);
        const u32 compressed_size = ReadLe32(zip, pos + 20);
        const u32 uncompressed_size = ReadLe32(zip, pos + 24);
        const u16 name_length = ReadLe16(zip, pos + 28);
        const u16 extra_length = ReadLe16(zip, pos + 30);
        const u16 comment_length = ReadLe16(zip, pos + 32);
        const u32 local_offset = ReadLe32(zip, pos + 42);
        const std::size_t next = pos + 46ull + name_length + extra_length + comment_length;
        if (next > zip.size()) throw std::runtime_error("truncated APK central-directory name");
        const std::string_view name(reinterpret_cast<const char*>(zip.data() + pos + 46), name_length);
        if (name == requested_name) {
            if (static_cast<u64>(local_offset) + 30 > zip.size() || ReadLe32(zip, local_offset) != kLocalSignature) {
                throw std::runtime_error("invalid APK local member header");
            }
            const u16 local_name_length = ReadLe16(zip, local_offset + 26);
            const u16 local_extra_length = ReadLe16(zip, local_offset + 28);
            const std::size_t data_offset = static_cast<std::size_t>(local_offset) + 30u + local_name_length + local_extra_length;
            if (data_offset > zip.size() || compressed_size > zip.size() - data_offset) {
                throw std::runtime_error("APK member data is truncated");
            }
            std::vector<u8> output;
            if (method == 0) {
                output.assign(zip.begin() + static_cast<std::ptrdiff_t>(data_offset),
                              zip.begin() + static_cast<std::ptrdiff_t>(data_offset + compressed_size));
                if (output.size() != uncompressed_size) throw std::runtime_error("stored APK member size mismatch");
            } else if (method == 8) {
                output = InflateRaw(zip.data() + data_offset, compressed_size, uncompressed_size);
            } else {
                throw std::runtime_error("unsupported APK compression method " + std::to_string(method));
            }
            const u32 actual_crc = static_cast<u32>(crc32(0, reinterpret_cast<const Bytef*>(output.data()), static_cast<uInt>(output.size())));
            if (actual_crc != expected_crc) throw std::runtime_error("APK member CRC mismatch");
            return output;
        }
        pos = next;
    }
    throw std::runtime_error("APK member not found: " + std::string(requested_name));
}

struct ZipEntryRecord {
    std::string name;
    u16 method = 0;
    u32 crc = 0;
    u32 compressed_size = 0;
    u32 uncompressed_size = 0;
    u32 local_offset = 0;
};

class ApkMemberCache {
public:
    void Initialize(const std::vector<u8>& image, const std::string& writable_path, std::ostream& log) {
        image_ = &image;
        log_ = &log;
        BuildIndex();
        const u32 apk_crc = static_cast<u32>(crc32(
            0, reinterpret_cast<const Bytef*>(image.data()), static_cast<uInt>(image.size())));
        std::ostringstream fingerprint;
        fingerprint << std::hex << std::setw(8) << std::setfill('0') << apk_crc
                    << '-' << std::dec << image.size();
        disk_root_ = std::filesystem::path(writable_path) / "apk-member-cache" / fingerprint.str();
        std::error_code error;
        std::filesystem::create_directories(disk_root_, error);
        log << "RESULT: DYNARMIC_APK_MEMBER_INDEX_READY entries=" << entries_.size()
            << " persistent=" << disk_root_.string() << '\n';
    }

    bool Exists(std::string requested) const {
        return FindEntry(NormalizeName(std::move(requested))) != nullptr;
    }

    std::shared_ptr<const std::vector<u8>> Load(std::string requested) {
        requested = NormalizeName(std::move(requested));
        const ZipEntryRecord* entry = FindEntry(requested);
        if (!entry) return {};
        const auto cached = memory_cache_.find(entry->name);
        if (cached != memory_cache_.end()) {
            ++memory_hits_;
            return cached->second;
        }
        ++memory_misses_;
        std::shared_ptr<std::vector<u8>> bytes = LoadPersistent(*entry);
        if (bytes) ++disk_hits_;
        else {
            ++disk_misses_;
            bytes = std::make_shared<std::vector<u8>>(Extract(*entry));
            SavePersistent(*entry, *bytes);
        }
        cached_bytes_ += bytes->size();
        memory_cache_[entry->name] = bytes;
        if (log_ && first_load_logs_++ < 48u) {
            *log_ << "[host] APK member cache "
                  << (disk_hits_ + disk_misses_ == 1 && disk_hits_ ? "disk-hit" : "ready")
                  << ": " << entry->name << " bytes=" << bytes->size() << '\n';
        }
        return bytes;
    }

    std::pair<std::size_t, u64> PrecacheBackgroundMusic(
        const std::filesystem::path& writable_path) {
        std::vector<std::string> music_names;
        for (const auto& [name, entry] : entries_) {
            (void)entry;
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
            if (lower.starts_with("assets/") && lower.ends_with(".mp3"))
                music_names.push_back(name);
        }
        std::sort(music_names.begin(), music_names.end());
        const std::filesystem::path output_directory =
            writable_path / "audio-cache";
        std::error_code error;
        std::filesystem::create_directories(output_directory, error);
        std::size_t ready = 0;
        u64 bytes_ready = 0;
        for (const std::string& name : music_names) {
            const std::shared_ptr<const std::vector<u8>> bytes = Load(name);
            if (!bytes) continue;
            const std::filesystem::path destination =
                output_directory / std::filesystem::path(name).filename();
            error.clear();
            if (std::filesystem::is_regular_file(destination, error) &&
                std::filesystem::file_size(destination, error) == bytes->size()) {
                ++ready;
                bytes_ready += bytes->size();
                continue;
            }
            const std::filesystem::path temporary =
                destination.string() + ".wrapper.tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) continue;
            if (!bytes->empty())
                output.write(reinterpret_cast<const char*>(bytes->data()),
                             static_cast<std::streamsize>(bytes->size()));
            output.close();
            if (!output) {
                std::filesystem::remove(temporary, error);
                continue;
            }
            std::filesystem::remove(destination, error);
            error.clear();
            std::filesystem::rename(temporary, destination, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                continue;
            }
            ++ready;
            bytes_ready += bytes->size();
        }
        return {ready, bytes_ready};
    }

    const std::vector<std::string>& OrderedNames() const { return ordered_names_; }

    const ZipEntryRecord* EntryAt(std::size_t index) const {
        if (index >= ordered_names_.size()) return nullptr;
        return FindEntry(ordered_names_[index]);
    }

    std::shared_ptr<const std::vector<u8>> LoadAt(std::size_t index) {
        const ZipEntryRecord* entry = EntryAt(index);
        return entry ? Load(entry->name) : std::shared_ptr<const std::vector<u8>>{};
    }

    std::optional<std::size_t> LocateIndex(std::string requested,
                                           int case_sensitivity) const {
        requested = NormalizeName(std::move(requested));
        const auto compare = [case_sensitivity](const std::string& left,
                                                const std::string& right) {
            if (case_sensitivity == 1) return left == right;
            if (left.size() != right.size()) return false;
            for (std::size_t i = 0; i < left.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(left[i])) !=
                    std::tolower(static_cast<unsigned char>(right[i])))
                    return false;
            }
            return true;
        };
        for (std::size_t index = 0; index < ordered_names_.size(); ++index) {
            if (compare(ordered_names_[index], requested) ||
                (!requested.starts_with("assets/") &&
                 compare(ordered_names_[index], "assets/" + requested)))
                return index;
        }
        return std::nullopt;
    }

    void Report() const {
        if (!log_) return;
        *log_ << "Dynarmic APK member cache totals: entries=" << entries_.size()
              << " memory_hits=" << memory_hits_ << " memory_misses=" << memory_misses_
              << " disk_hits=" << disk_hits_ << " disk_misses=" << disk_misses_
              << " cached_bytes=" << cached_bytes_ << '\n';
    }

private:
    static std::string NormalizeName(std::string name) {
        std::replace(name.begin(), name.end(), '\\', '/');
        while (!name.empty() && name.front() == '/') name.erase(name.begin());
        while (name.starts_with("./")) name.erase(0, 2);
        return name;
    }

    const ZipEntryRecord* FindEntry(const std::string& name) const {
        auto found = entries_.find(name);
        if (found != entries_.end()) return &found->second;
        if (!name.starts_with("assets/")) {
            found = entries_.find("assets/" + name);
            if (found != entries_.end()) return &found->second;
        }
        return nullptr;
    }

    void BuildIndex() {
        if (!image_) throw std::runtime_error("APK cache has no image");
        const auto& zip = *image_;
        constexpr u32 kEocdSignature = 0x06054B50u;
        constexpr u32 kCentralSignature = 0x02014B50u;
        const std::size_t search_start = zip.size() > (0xFFFFu + 22u) ? zip.size() - (0xFFFFu + 22u) : 0;
        std::optional<std::size_t> eocd;
        if (zip.size() >= 22u) {
            for (std::size_t pos = zip.size() - 22u;; --pos) {
                if (ReadLe32(zip, pos) == kEocdSignature) { eocd = pos; break; }
                if (pos == search_start) break;
            }
        }
        if (!eocd) throw std::runtime_error("APK end-of-central-directory record not found");
        const u16 count = ReadLe16(zip, *eocd + 10u);
        std::size_t pos = ReadLe32(zip, *eocd + 16u);
        for (u16 index = 0; index < count; ++index) {
            if (pos + 46u > zip.size() || ReadLe32(zip, pos) != kCentralSignature)
                throw std::runtime_error("invalid APK central-directory entry");
            ZipEntryRecord entry;
            entry.method = ReadLe16(zip, pos + 10u);
            entry.crc = ReadLe32(zip, pos + 16u);
            entry.compressed_size = ReadLe32(zip, pos + 20u);
            entry.uncompressed_size = ReadLe32(zip, pos + 24u);
            const u16 name_length = ReadLe16(zip, pos + 28u);
            const u16 extra_length = ReadLe16(zip, pos + 30u);
            const u16 comment_length = ReadLe16(zip, pos + 32u);
            entry.local_offset = ReadLe32(zip, pos + 42u);
            const std::size_t next = pos + 46ull + name_length + extra_length + comment_length;
            if (next > zip.size()) throw std::runtime_error("truncated APK central-directory entry");
            entry.name.assign(reinterpret_cast<const char*>(zip.data() + pos + 46u), name_length);
            entry.name = NormalizeName(entry.name);
            ordered_names_.push_back(entry.name);
            entries_[entry.name] = std::move(entry);
            pos = next;
        }
    }

    std::vector<u8> Extract(const ZipEntryRecord& entry) const {
        constexpr u32 kLocalSignature = 0x04034B50u;
        const auto& zip = *image_;
        if (static_cast<u64>(entry.local_offset) + 30u > zip.size() ||
            ReadLe32(zip, entry.local_offset) != kLocalSignature)
            throw std::runtime_error("invalid APK local member header for " + entry.name);
        const u16 name_length = ReadLe16(zip, entry.local_offset + 26u);
        const u16 extra_length = ReadLe16(zip, entry.local_offset + 28u);
        const std::size_t data_offset = static_cast<std::size_t>(entry.local_offset) + 30u + name_length + extra_length;
        if (data_offset > zip.size() || entry.compressed_size > zip.size() - data_offset)
            throw std::runtime_error("truncated APK member " + entry.name);
        std::vector<u8> output;
        if (entry.method == 0u) {
            output.assign(zip.begin() + static_cast<std::ptrdiff_t>(data_offset),
                          zip.begin() + static_cast<std::ptrdiff_t>(data_offset + entry.compressed_size));
        } else if (entry.method == 8u) {
            output = InflateRaw(zip.data() + data_offset, entry.compressed_size, entry.uncompressed_size);
        } else {
            throw std::runtime_error("unsupported APK method " + std::to_string(entry.method));
        }
        if (output.size() != entry.uncompressed_size) throw std::runtime_error("APK member size mismatch");
        const u32 actual_crc = static_cast<u32>(crc32(
            0, reinterpret_cast<const Bytef*>(output.data()), static_cast<uInt>(output.size())));
        if (actual_crc != entry.crc) throw std::runtime_error("APK member CRC mismatch for " + entry.name);
        return output;
    }

    std::filesystem::path PersistentPath(const ZipEntryRecord& entry) const {
        std::ostringstream name;
        name << std::hex << std::setw(8) << std::setfill('0') << entry.crc
             << '-' << std::dec << entry.uncompressed_size << ".bin";
        return disk_root_ / name.str();
    }

    std::shared_ptr<std::vector<u8>> LoadPersistent(const ZipEntryRecord& entry) const {
        std::error_code error;
        const auto path = PersistentPath(entry);
        if (!std::filesystem::is_regular_file(path, error) ||
            std::filesystem::file_size(path, error) != entry.uncompressed_size) return {};
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};
        auto output = std::make_shared<std::vector<u8>>(entry.uncompressed_size);
        if (!output->empty() && !file.read(reinterpret_cast<char*>(output->data()), output->size())) return {};
        const u32 actual_crc = static_cast<u32>(crc32(
            0, reinterpret_cast<const Bytef*>(output->data()), static_cast<uInt>(output->size())));
        return actual_crc == entry.crc ? output : std::shared_ptr<std::vector<u8>>{};
    }

    void SavePersistent(const ZipEntryRecord& entry, const std::vector<u8>& bytes) const {
        std::error_code error;
        std::filesystem::create_directories(disk_root_, error);
        const auto path = PersistentPath(entry);
        const auto temporary = path.string() + ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return;
        if (!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        file.close();
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) std::filesystem::remove(temporary, error);
    }

    const std::vector<u8>* image_ = nullptr;
    std::ostream* log_ = nullptr;
    std::filesystem::path disk_root_;
    std::unordered_map<std::string, ZipEntryRecord> entries_;
    std::vector<std::string> ordered_names_;
    std::unordered_map<std::string, std::shared_ptr<std::vector<u8>>> memory_cache_;
    u64 memory_hits_ = 0;
    u64 memory_misses_ = 0;
    u64 disk_hits_ = 0;
    u64 disk_misses_ = 0;
    u64 cached_bytes_ = 0;
    u64 first_load_logs_ = 0;
};

struct MemoryRegion {
    u32 base = 0;
    std::vector<u8> data;
    bool executable = false;
};

class ProbeEnvironment final : public Dynarmic::A32::UserCallbacks {
public:
    ProbeEnvironment() : page_regions_(kGuestPageCount, kUnmappedPage) {}

    u64 ticks_left = 0;
    bool invalid_access = false;
    bool interpreter_fallback = false;
    bool exception_seen = false;
    bool svc_pending = false;
    u32 pending_svc = 0;
    u32 fault_address = 0;
    u32 fallback_pc = 0;
    std::size_t fallback_count = 0;
    u32 exception_pc = 0;

    void Map(u32 base, std::size_t size, bool executable) {
        if (size == 0 || size > std::numeric_limits<u32>::max())
            throw std::runtime_error("invalid guest mapping size");
        const u64 end = static_cast<u64>(base) + size;
        if (end > 0x100000000ull)
            throw std::runtime_error("guest mapping exceeds 32-bit address space");
        for (const auto& region : regions_) {
            const u64 existing_end = static_cast<u64>(region.base) + region.data.size();
            if (!(end <= region.base || base >= existing_end))
                throw std::runtime_error("overlapping guest mapping");
        }
        regions_.push_back(MemoryRegion{base, std::vector<u8>(size), executable});
        std::sort(regions_.begin(), regions_.end(),
                  [](const MemoryRegion& lhs, const MemoryRegion& rhs) {
                      return lhs.base < rhs.base;
                  });
        RebuildPageLookup();
    }

    void CopyIn(u32 address, const u8* source, std::size_t size) {
        MemoryRegion* region = FindMutable(address, size);
        if (!region) throw std::runtime_error("CopyIn outside mapped guest memory");
        std::memcpy(region->data.data() + (address - region->base), source, size);
    }

    bool ReadBytes(u32 address, void* output, std::size_t size) const {
        if (size == 0) return true;
        const MemoryRegion* region = Find(address, size);
        if (!region) return false;
        std::memcpy(output, region->data.data() + (address - region->base), size);
        return true;
    }

    bool WriteBytes(u32 address, const void* source, std::size_t size) {
        if (size == 0) return true;
        MemoryRegion* region = FindMutable(address, size);
        if (!region) return false;
        std::memcpy(region->data.data() + (address - region->base), source, size);
        return true;
    }

    bool ReadCString(u32 address, std::string& output,
                     std::size_t maximum = 1u << 20) const {
        output.clear();
        while (output.size() < maximum) {
            const u64 current64 = static_cast<u64>(address) +
                                  output.size();
            if (current64 > std::numeric_limits<u32>::max()) return false;
            std::size_t available = 0;
            const u8* data = HostPointerToRegionEnd(
                static_cast<u32>(current64), available);
            if (!data || available == 0) return false;
            const std::size_t remaining = maximum - output.size();
            const std::size_t chunk = std::min(available, remaining);
            const void* terminator = std::memchr(data, 0, chunk);
            const std::size_t length = terminator
                ? static_cast<const u8*>(terminator) - data
                : chunk;
            output.append(reinterpret_cast<const char*>(data), length);
            if (terminator) return true;
            if (chunk == 0) break;
        }
        return false;
    }

    void* HostPointer(u32 address, std::size_t size) {
        MemoryRegion* region = FindMutable(address, size);
        return region ? static_cast<void*>(region->data.data() +
                                           (address - region->base))
                      : nullptr;
    }

    const void* HostPointer(u32 address, std::size_t size) const {
        const MemoryRegion* region = Find(address, size);
        return region ? static_cast<const void*>(region->data.data() +
                                                 (address - region->base))
                      : nullptr;
    }

    const u8* HostPointerToRegionEnd(u32 address,
                                     std::size_t& available) const {
        const MemoryRegion* region = FindContaining(address);
        if (!region) {
            available = 0;
            return nullptr;
        }
        const u64 region_end = static_cast<u64>(region->base) + region->data.size();
        available = static_cast<std::size_t>(region_end - address);
        return region->data.data() + (address - region->base);
    }

    u8* HostPointerToRegionEnd(u32 address, std::size_t& available) {
        MemoryRegion* region = FindContainingMutable(address);
        if (!region) {
            available = 0;
            return nullptr;
        }
        const u64 region_end = static_cast<u64>(region->base) + region->data.size();
        available = static_cast<std::size_t>(region_end - address);
        return region->data.data() + (address - region->base);
    }

    bool IsMapped(u32 address, std::size_t size = 1) const {
        return Find(address, size) != nullptr;
    }

    void AttachCpu(Dynarmic::A32::Jit* cpu) { attached_cpu_ = cpu; }

    void ResetStopState() {
        invalid_access = false;
        interpreter_fallback = false;
        exception_seen = false;
        svc_pending = false;
        pending_svc = 0;
        fault_address = 0;
        fallback_pc = 0;
        fallback_count = 0;
        exception_pc = 0;
    }

    u8 MemoryRead8(u32 vaddr) override {
        const MemoryRegion* region = Find(vaddr, sizeof(u8));
        if (!region) return ReadFault<u8>(vaddr);
        return region->data[vaddr - region->base];
    }

    u16 MemoryRead16(u32 vaddr) override {
        return ReadTyped<u16>(vaddr);
    }

    u32 MemoryRead32(u32 vaddr) override {
        return ReadTyped<u32>(vaddr);
    }

    u64 MemoryRead64(u32 vaddr) override {
        return ReadTyped<u64>(vaddr);
    }

    void MemoryWrite8(u32 vaddr, u8 value) override {
        MemoryRegion* region = FindMutable(vaddr, sizeof(value));
        if (!region) {
            WriteFault(vaddr);
            return;
        }
        region->data[vaddr - region->base] = value;
    }

    void MemoryWrite16(u32 vaddr, u16 value) override {
        WriteTyped(vaddr, value);
    }

    void MemoryWrite32(u32 vaddr, u32 value) override {
        WriteTyped(vaddr, value);
    }

    void MemoryWrite64(u32 vaddr, u64 value) override {
        WriteTyped(vaddr, value);
    }

    void InterpreterFallback(u32 pc, std::size_t count) override {
        interpreter_fallback = true;
        fallback_pc = pc;
        fallback_count = count;
        RequestHalt();
    }

    void CallSVC(u32 swi) override {
        svc_pending = true;
        pending_svc = swi;
        RequestHalt();
    }

    void ExceptionRaised(u32 pc, Dynarmic::A32::Exception) override {
        exception_seen = true;
        exception_pc = pc;
        RequestHalt();
    }

    void AddTicks(u64 ticks) override {
        ticks_left = ticks > ticks_left ? 0 : ticks_left - ticks;
    }

    u64 GetTicksRemaining() override { return ticks_left; }

private:
    static constexpr u32 kGuestPageShift = 12u;
    static constexpr std::size_t kGuestPageCount =
        static_cast<std::size_t>(1u) << (32u - kGuestPageShift);
    static constexpr std::int16_t kUnmappedPage = -1;

    void RequestHalt() {
        if (attached_cpu_) attached_cpu_->HaltExecution(kCallbackHalt);
        ticks_left = 0;
    }

    void RebuildPageLookup() {
        std::fill(page_regions_.begin(), page_regions_.end(), kUnmappedPage);
        if (regions_.size() > static_cast<std::size_t>(
                                  std::numeric_limits<std::int16_t>::max()))
            throw std::runtime_error("too many guest memory regions");
        for (std::size_t index = 0; index < regions_.size(); ++index) {
            const MemoryRegion& region = regions_[index];
            const u64 begin_page = static_cast<u64>(region.base) >>
                                   kGuestPageShift;
            const u64 end_address = static_cast<u64>(region.base) +
                                    region.data.size() - 1u;
            const u64 end_page = end_address >> kGuestPageShift;
            if (end_page >= page_regions_.size())
                throw std::runtime_error("guest page lookup overflow");
            for (u64 page = begin_page; page <= end_page; ++page)
                page_regions_[static_cast<std::size_t>(page)] =
                    static_cast<std::int16_t>(index);
        }
    }

    const MemoryRegion* FindContaining(u32 address) const {
        const std::int16_t index =
            page_regions_[static_cast<std::size_t>(address >> kGuestPageShift)];
        if (index < 0) return nullptr;
        const MemoryRegion& region = regions_[static_cast<std::size_t>(index)];
        const u64 end = static_cast<u64>(region.base) + region.data.size();
        return address >= region.base && static_cast<u64>(address) < end
            ? &region
            : nullptr;
    }

    MemoryRegion* FindContainingMutable(u32 address) {
        const std::int16_t index =
            page_regions_[static_cast<std::size_t>(address >> kGuestPageShift)];
        if (index < 0) return nullptr;
        MemoryRegion& region = regions_[static_cast<std::size_t>(index)];
        const u64 end = static_cast<u64>(region.base) + region.data.size();
        return address >= region.base && static_cast<u64>(address) < end
            ? &region
            : nullptr;
    }

    const MemoryRegion* Find(u32 address, std::size_t size) const {
        if (size == 0) return FindContaining(address);
        const u64 end = static_cast<u64>(address) + size;
        if (end > 0x100000000ull) return nullptr;
        const MemoryRegion* region = FindContaining(address);
        if (!region) return nullptr;
        const u64 region_end = static_cast<u64>(region->base) +
                               region->data.size();
        return end <= region_end ? region : nullptr;
    }

    MemoryRegion* FindMutable(u32 address, std::size_t size) {
        if (size == 0) return FindContainingMutable(address);
        const u64 end = static_cast<u64>(address) + size;
        if (end > 0x100000000ull) return nullptr;
        MemoryRegion* region = FindContainingMutable(address);
        if (!region) return nullptr;
        const u64 region_end = static_cast<u64>(region->base) +
                               region->data.size();
        return end <= region_end ? region : nullptr;
    }

    template <typename T>
    T ReadFault(u32 address) {
        invalid_access = true;
        fault_address = address;
        RequestHalt();
        return T{};
    }

    void WriteFault(u32 address) {
        invalid_access = true;
        fault_address = address;
        RequestHalt();
    }

    template <typename T>
    T ReadTyped(u32 address) {
        const MemoryRegion* region = Find(address, sizeof(T));
        if (!region) return ReadFault<T>(address);
        T value{};
        std::memcpy(&value, region->data.data() + (address - region->base),
                    sizeof(value));
        return value;
    }

    template <typename T>
    void WriteTyped(u32 address, T value) {
        MemoryRegion* region = FindMutable(address, sizeof(T));
        if (!region) {
            WriteFault(address);
            return;
        }
        std::memcpy(region->data.data() + (address - region->base), &value,
                    sizeof(value));
    }

    std::vector<MemoryRegion> regions_;
    std::vector<std::int16_t> page_regions_;
    Dynarmic::A32::Jit* attached_cpu_ = nullptr;
};

struct ImportRecord {
    std::string name;
    u32 address = 0;
    u32 svc = 0;
    u64 calls = 0;
    bool warned = false;
    bool is_gl = false;
    void* resolved_host_function = nullptr;
    int gl_argument_count = -1;
    u32 inline_resume_address = 0;
    u64 sampled_host_nanoseconds = 0;
    u64 sampled_host_calls = 0;
};

struct ProfilerCounters {
    u64 import_calls = 0;
    u64 jni_svc_calls = 0;
    u64 gl_calls = 0;
    u64 draw_calls = 0;
    u64 draw_vertices = 0;
    u64 buffer_upload_bytes = 0;
    u64 texture_upload_bytes = 0;
    u64 allocation_calls = 0;
    u64 free_calls = 0;
    u64 reallocation_calls = 0;
    u64 apk_read_calls = 0;
    u64 apk_read_bytes = 0;
    u64 live_heap_bytes = 0;
    u64 peak_heap_bytes = 0;
};

struct GuestCallMetrics {
    std::string label;
    double elapsed_ms = 0.0;
    u64 estimated_ticks = 0;
    u64 jit_runs = 0;
    u64 svc_calls = 0;
    ProfilerCounters before;
    ProfilerCounters after;
};
struct ObjectRecord {
    std::string name;
    u32 address = 0;
};
struct SymbolRecord {
    std::string name;
    u32 address = 0;
    u32 size = 0;
};
struct ElfRuntime {
    u32 image_min = 0;
    u32 image_max = 0;
    u32 entry = 0;
    std::size_t load_segments = 0;
    std::size_t executable_segments = 0;
    std::size_t dynsym_count = 0;
    std::size_t undefined_symbols = 0;
    std::size_t relocation_count = 0;
    std::size_t relative_relocations = 0;
    std::size_t imported_relocations = 0;
    u32 jni_onload = 0;
    u32 native_set_paths = 0;
    u32 native_init = 0;
    u32 native_render = 0;
    u32 native_touch_begin = 0;
    u32 native_touch_end = 0;
    u32 native_touch_move = 0;
    u32 native_key_down = 0;
    u32 native_insert_text = 0;
    u32 native_delete_backward = 0;
    u32 native_pause = 0;
    u32 native_resume = 0;
    std::vector<u32> constructors;
    std::vector<ImportRecord> imports;
    std::vector<ObjectRecord> objects;
    std::vector<SymbolRecord> symbols;
};

static std::size_t BoundedStringLength(const char* text, std::size_t maximum) {
    std::size_t length = 0;
    while (length < maximum && text[length] != '\0') ++length;
    return length;
}
static std::string SectionName(const std::vector<u8>& elf, const Elf32Shdr& shstr, u32 offset) {
    if (offset >= shstr.size || static_cast<u64>(shstr.offset) + offset >= elf.size()) return {};
    const char* begin = reinterpret_cast<const char*>(elf.data() + shstr.offset + offset);
    const std::size_t maximum = std::min<std::size_t>(shstr.size - offset, elf.size() - shstr.offset - offset);
    return std::string(begin, BoundedStringLength(begin, maximum));
}
static std::string StringFromTable(const std::vector<u8>& elf, const Elf32Shdr& strings, u32 offset) {
    if (offset >= strings.size || static_cast<u64>(strings.offset) + offset >= elf.size()) return {};
    const char* begin = reinterpret_cast<const char*>(elf.data() + strings.offset + offset);
    const std::size_t maximum = std::min<std::size_t>(strings.size - offset, elf.size() - strings.offset - offset);
    return std::string(begin, BoundedStringLength(begin, maximum));
}
static void WriteArmSvcStub(ProbeEnvironment& env, u32 address, u32 svc) {
    env.MemoryWrite32(address, 0xEF000000u | (svc & 0x00FFFFFFu));
    env.MemoryWrite32(address + 4u, 0xE12FFF1Eu); // bx lr
}
static bool IsImportedObjectName(const std::string& name, u8 symbol_type) {
    if (symbol_type == kSttObject) return true;
    static const std::set<std::string> known = {
        "__sF", "__stack_chk_guard", "_ctype_", "_tolower_tab_", "_toupper_tab_", "optarg", "optind"
    };
    return known.count(name) != 0;
}
static void InitializeBionicCtype(ProbeEnvironment& env, const std::string& name, u32 address) {
    const u32 table_address = address + 4u;
    env.MemoryWrite32(address, table_address);
    if (name == "_ctype_") {
        std::array<u8, 257> table{};
        constexpr u8 upper = 0x01, lower = 0x02, number = 0x04, space = 0x08;
        constexpr u8 punct = 0x10, control = 0x20, hex = 0x40, blank = 0x80;
        for (u32 index = 0; index < 256; ++index) {
            u8 flags = 0;
            if (index <= 0x1f || (index >= 0x7f && index <= 0x9f)) flags |= control;
            if (index >= 0xa0) flags |= punct;
            if (index == ' ') flags |= space | blank;
            else if (index >= '\t' && index <= '\r') flags |= space;
            if (index >= '0' && index <= '9') flags |= number;
            if (index >= 'A' && index <= 'Z') flags |= upper;
            if (index >= 'a' && index <= 'z') flags |= lower;
            if ((index >= 'A' && index <= 'F') || (index >= 'a' && index <= 'f')) flags |= hex;
            if ((index >= 0x21 && index <= 0x2f) || (index >= 0x3a && index <= 0x40) ||
                (index >= 0x5b && index <= 0x60) || (index >= 0x7b && index <= 0x7e)) flags |= punct;
            table[index + 1] = flags;
        }
        if (!env.WriteBytes(table_address, table.data(), table.size())) throw std::runtime_error("failed to initialize _ctype_");
    } else {
        std::array<std::int16_t, 257> table{};
        table[0] = -1;
        const bool lower = name == "_tolower_tab_";
        for (u32 index = 0; index < 256; ++index) {
            u32 value = index;
            if (lower && index >= 'A' && index <= 'Z') value += 'a' - 'A';
            else if (!lower && index >= 'a' && index <= 'z') value -= 'a' - 'A';
            table[index + 1] = static_cast<std::int16_t>(value);
        }
        if (!env.WriteBytes(table_address, table.data(), sizeof(table))) throw std::runtime_error("failed to initialize bionic case table");
    }
}
static u32 EnsureObject(ElfRuntime& runtime, ProbeEnvironment& env, const std::string& name) {
    for (const auto& object : runtime.objects) if (object.name == name) return object.address;
    if (runtime.objects.size() >= kObjectRegionSize / kPageSize) throw std::runtime_error("too many imported objects");
    const u32 address = kObjectBase + static_cast<u32>(runtime.objects.size()) * kPageSize;
    runtime.objects.push_back(ObjectRecord{name, address});
    if (name == "__stack_chk_guard") env.MemoryWrite32(address, 0xA59C71E3u);
    else if (name == "optind") env.MemoryWrite32(address, 1u);
    else if (name == "__sF") {
        env.MemoryWrite32(address + 0u, 0x23000001u);
        env.MemoryWrite32(address + 84u, 0x23000002u);
        env.MemoryWrite32(address + 168u, 0x23000003u);
    }
    else if (name == "_ctype_" || name == "_tolower_tab_" || name == "_toupper_tab_") InitializeBionicCtype(env, name, address);
    return address;
}
static u32 EnsureImport(ElfRuntime& runtime, ProbeEnvironment& env, const std::string& name) {
    for (const auto& import : runtime.imports) if (import.name == name) return import.address;
    const std::size_t index = runtime.imports.size();
    if ((index + 1u) * 8u > kImportRegionSize) throw std::runtime_error("too many imported functions");
    const u32 svc = static_cast<u32>(index + 1u);
    const u32 address = kImportBase + static_cast<u32>(index * 8u);
    ImportRecord record;
    record.name = name;
    record.address = address;
    record.svc = svc;
    record.is_gl = name.rfind("gl", 0) == 0;
    runtime.imports.push_back(std::move(record));
    WriteArmSvcStub(env, address, svc);
    return address;
}

static const SymbolRecord* FindSymbol(const ElfRuntime& runtime, const std::string& name) {
    for (const SymbolRecord& symbol : runtime.symbols) if (symbol.name == name) return &symbol;
    return nullptr;
}
static void InstallThumbAbsoluteImportHookPreservingArguments(
    ProbeEnvironment& env, const ElfRuntime& runtime, const SymbolRecord& symbol,
    u16 expected_first_halfword, u32 destination) {
    if ((symbol.address & 1u) == 0u)
        throw std::runtime_error("ZIP hook symbol is not marked as Thumb: " + symbol.name);
    if (symbol.size < 8u)
        throw std::runtime_error("ZIP hook symbol is too small: " + symbol.name);
    const u32 address = symbol.address & ~1u;
    if ((address & 3u) != 0u || address < runtime.image_min ||
        address > runtime.image_max - 8u)
        throw std::runtime_error("ZIP hook target is outside the executable image: " + symbol.name);
    const u16 original = env.MemoryRead16(address);
    if (original != expected_first_halfword) {
        std::ostringstream error;
        error << "ZIP hook prologue mismatch for " << symbol.name
              << ": expected 0x" << std::hex << expected_first_halfword
              << " got 0x" << original;
        throw std::runtime_error(error.str());
    }

    // R3 is the fourth AAPCS argument and getFileDataFromZip uses it for the
    // output-size pointer. Test9 incorrectly used R3 as the trampoline scratch
    // register, replacing that pointer with the import-stub address and then
    // writing the member size into executable memory. R0 is safe here: both
    // hooked CCFileUtils methods ignore the `this` value in the host handler,
    // and R0 is overwritten by the return value before control reaches the
    // caller. R1-R3 therefore arrive at the host unchanged.
    env.MemoryWrite16(address + 0u, 0x4800u); // ldr r0, [pc, #0]
    env.MemoryWrite16(address + 2u, 0x4700u); // bx r0
    env.MemoryWrite32(address + 4u, destination);
}
static void InstallThumbInlineSvcHookPreservingArguments(
    ProbeEnvironment& env, ElfRuntime& runtime, const SymbolRecord& symbol,
    u16 expected_first_halfword, const std::string& import_name) {
    if ((symbol.address & 1u) == 0u)
        throw std::runtime_error("inline hook symbol is not marked as Thumb: " + symbol.name);
    const u32 address = symbol.address & ~1u;
    if (symbol.size < 12u || (address & 3u) != 0u ||
        address < runtime.image_min || address > runtime.image_max - 12u)
        throw std::runtime_error("inline hook target cannot hold Thumb-to-ARM SVC bridge: " + symbol.name);
    const u16 original = env.MemoryRead16(address);
    if (original != expected_first_halfword) {
        std::ostringstream error;
        error << "inline hook prologue mismatch for " << symbol.name
              << ": expected 0x" << std::hex << expected_first_halfword
              << " got 0x" << original;
        throw std::runtime_error(error.str());
    }
    const u32 import_address = EnsureImport(runtime, env, import_name);
    ImportRecord* record = nullptr;
    for (ImportRecord& candidate : runtime.imports) {
        if (candidate.address == import_address) { record = &candidate; break; }
    }
    if (!record) throw std::runtime_error("inline hook import record disappeared");
    // Thumb `bx pc` enters ARM state at address+4 without modifying R0-R3.
    // The ARM SVC reaches the normal import dispatcher; address+8 is a real
    // ARM `bx lr`, so every AAPCS argument survives intact.
    env.MemoryWrite16(address + 0u, 0x4778u); // bx pc
    env.MemoryWrite16(address + 2u, 0x46C0u); // nop/alignment
    env.MemoryWrite32(address + 4u, 0xEF000000u | (record->svc & 0x00FFFFFFu));
    env.MemoryWrite32(address + 8u, 0xE12FFF1Eu); // bx lr
    record->inline_resume_address = address + 8u;
}

static bool PatchArmFunctionReturnTrue(
    ProbeEnvironment& env, const ElfRuntime& runtime,
    const SymbolRecord& symbol) {
    const u32 address = symbol.address & ~1u;
    if (address < runtime.image_min || address >= runtime.image_max) return false;
    if (symbol.address & 1u) {
        if (symbol.size < 4u || address > runtime.image_max - 4u) return false;
        env.MemoryWrite16(address + 0u, 0x2001u); // movs r0, #1
        env.MemoryWrite16(address + 2u, 0x4770u); // bx lr
        return true;
    }
    if (symbol.size < 8u || address > runtime.image_max - 8u) return false;
    env.MemoryWrite32(address + 0u, 0xE3A00001u); // mov r0, #1
    env.MemoryWrite32(address + 4u, 0xE12FFF1Eu); // bx lr
    return true;
}

static bool PatchArmFunctionTailJump(
    ProbeEnvironment& env, const ElfRuntime& runtime,
    const SymbolRecord& source, const SymbolRecord& destination) {
    const u32 address = source.address & ~1u;
    const u32 target = destination.address;
    if (address < runtime.image_min || address >= runtime.image_max) return false;
    if (source.address & 1u) {
        if ((address & 3u) == 0u) {
            if (source.size < 8u || address > runtime.image_max - 8u) return false;
            env.MemoryWrite16(address + 0u, 0x4B00u); // ldr r3, [pc, #0]
            env.MemoryWrite16(address + 2u, 0x4718u); // bx r3
            env.MemoryWrite32(address + 4u, target);
            return true;
        }
        if (source.size < 10u || address > runtime.image_max - 10u) return false;
        env.MemoryWrite16(address + 0u, 0x4B01u); // ldr r3, [pc, #4]
        env.MemoryWrite16(address + 2u, 0x4718u); // bx r3
        env.MemoryWrite16(address + 4u, 0x46C0u); // nop
        env.MemoryWrite32(address + 6u, target);
        return true;
    }
    if (source.size < 8u || address > runtime.image_max - 8u) return false;
    env.MemoryWrite32(address + 0u, 0xE51FF004u); // ldr pc, [pc, #-4]
    env.MemoryWrite32(address + 4u, target & ~1u);
    return true;
}

static std::size_t InstallConfigurableIconUnlockHooks(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    static constexpr const char* symbols[] = {
        "_ZN11GameManager14isIconUnlockedEi",
        "_ZN11GameManager14isIconUnlockedEi8IconType",
    };
    if (!gd_settings_hack_icons()) return 0u;
    std::size_t patched = 0u;
    for (const char* name : symbols) {
        const SymbolRecord* symbol = FindSymbol(runtime, name);
        if (symbol && PatchArmFunctionReturnTrue(env, runtime, *symbol))
            ++patched;
    }
    return patched;
}

static std::size_t InstallConfigurableCreatorBypass(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    struct Pair { const char* locked; const char* unlocked; };
    static constexpr Pair pairs[] = {
        {"_ZN9MenuLayer13onFullVersionEPN7cocos2d8CCObjectE",
         "_ZN9MenuLayer9onCreatorEPN7cocos2d8CCObjectE"},
        {"_ZN9MenuLayer13onFullVersionEv",
         "_ZN9MenuLayer9onCreatorEv"},
        {"_ZN12CreatorLayer17onOnlyFullVersionEPN7cocos2d8CCObjectE",
         "_ZN12CreatorLayer10onMyLevelsEPN7cocos2d8CCObjectE"},
    };
    static constexpr const char* online_checks[] = {
        "_ZN12CreatorLayer19canPlayOnlineLevelsEv",
    };
    if (!gd_settings_full_bypass()) return 0u;
    std::size_t patched = 0u;
    for (const char* name : online_checks) {
        const SymbolRecord* symbol = FindSymbol(runtime, name);
        if (symbol && PatchArmFunctionReturnTrue(env, runtime, *symbol))
            ++patched;
    }
    for (const Pair& pair : pairs) {
        const SymbolRecord* locked = FindSymbol(runtime, pair.locked);
        const SymbolRecord* unlocked = FindSymbol(runtime, pair.unlocked);
        if (locked && unlocked &&
            PatchArmFunctionTailJump(env, runtime, *locked, *unlocked))
            ++patched;
    }
    return patched;
}

static std::size_t InstallCcFileUtilsZipHooks(ElfRuntime& runtime, ProbeEnvironment& env) {
    struct Hook { const char* symbol; const char* import; u16 prologue; };
    static constexpr Hook hooks[] = {
        {"_ZN7cocos2d11CCFileUtils18getFileDataFromZipEPKcS2_Pm", "__dynarmic_ccfileutils_getFileDataFromZip", 0xB5F0u},
        {"_ZN7cocos2d11CCFileUtils20existFileDataFromZipEPKcS2_", "__dynarmic_ccfileutils_existFileDataFromZip", 0xB570u},
    };
    std::size_t installed = 0;
    for (const Hook& hook : hooks) {
        const SymbolRecord* target = FindSymbol(runtime, hook.symbol);
        if (!target) continue;
        const u32 destination = EnsureImport(runtime, env, hook.import);
        InstallThumbAbsoluteImportHookPreservingArguments(
            env, runtime, *target, hook.prologue, destination);
        ++installed;
    }
    return installed;
}

static std::size_t InstallCcApplicationOpenUrlHook(ElfRuntime& runtime, ProbeEnvironment& env) {
    static constexpr const char* kSymbol = "_ZN7cocos2d13CCApplication7openURLEPKc";
    const SymbolRecord* target = FindSymbol(runtime, kSymbol);
    if (!target) return 0u;
    const u32 destination = EnsureImport(runtime, env, "__dynarmic_ccapplication_openURL");
    // CCApplication::openURL(this, url): R0 is `this`, R1 is the URL. The
    // absolute hook uses R0 as scratch, preserving the URL in R1.
    InstallThumbAbsoluteImportHookPreservingArguments(
        env, runtime, *target, 0xB530u, destination);
    return 1u;
}

static std::size_t InstallSimpleAudioEffectHooks(ElfRuntime& runtime,
                                                 ProbeEnvironment& env) {
    struct Hook { const char* symbol; const char* import; u16 prologue; };
    static constexpr Hook hooks[] = {
        {"_ZN13CocosDenshion17SimpleAudioEngine10playEffectEPKcbfff",
         "__dynarmic_simpleaudio_playEffect", 0xB500u},
        {"_ZN13CocosDenshion17SimpleAudioEngine13preloadEffectEPKc",
         "__dynarmic_simpleaudio_preloadEffect", 0xB510u},
    };
    std::size_t installed = 0;
    for (const Hook& hook : hooks) {
        const SymbolRecord* target = FindSymbol(runtime, hook.symbol);
        if (!target) continue;
        const u32 destination = EnsureImport(runtime, env, hook.import);
        // Both methods use R0 for `this`; R1-R3 and stack arguments contain
        // the actual sound parameters. The common trampoline may therefore
        // safely use R0 as scratch without corrupting the effect call.
        InstallThumbAbsoluteImportHookPreservingArguments(
            env, runtime, *target, hook.prologue, destination);
        ++installed;
    }
    return installed;
}

static std::size_t InstallHostMinizipHooks(ElfRuntime& runtime,
                                              ProbeEnvironment& env) {
    struct Hook { const char* symbol; const char* import_name; u16 prologue; };
    static constexpr Hook hooks[] = {
        {"_ZN7cocos2d18unzGetGlobalInfo64EPvPNS_19unz_global_info64_sE", "__dynarmic_unzGetGlobalInfo64", 0xB510u},
        {"_ZN7cocos2d16unzGetGlobalInfoEPvPNS_17unz_global_info_sE", "__dynarmic_unzGetGlobalInfo", 0x2800u},
        {"_ZN7cocos2d15unzGetFilePos64EPvPNS_16unz64_file_pos_sE", "__dynarmic_unzGetFilePos64", 0xB510u},
        {"_ZN7cocos2d13unzGetFilePosEPvPNS_14unz_file_pos_sE", "__dynarmic_unzGetFilePos", 0xB530u},
        {"_ZN7cocos2d29unzGetCurrentFileZStreamPos64EPv", "__dynarmic_unzGetCurrentFileZStreamPos64", 0xB510u},
        {"_ZN7cocos2d7unztellEPv", "__dynarmic_unztell", 0x2800u},
        {"_ZN7cocos2d9unztell64EPv", "__dynarmic_unztell64", 0xB510u},
        {"_ZN7cocos2d6unzeofEPv", "__dynarmic_unzeof", 0x2800u},
        {"_ZN7cocos2d14unzGetOffset64EPv", "__dynarmic_unzGetOffset64", 0xB510u},
        {"_ZN7cocos2d12unzGetOffsetEPv", "__dynarmic_unzGetOffset", 0xB510u},
        {"_ZN7cocos2d14unzSetOffset64EPvy", "__dynarmic_unzSetOffset64", 0xB530u},
        {"_ZN7cocos2d12unzSetOffsetEPvm", "__dynarmic_unzSetOffset", 0xB510u},
        {"_ZN7cocos2d16unzGoToFilePos64EPvPKNS_16unz64_file_pos_sE", "__dynarmic_unzGoToFilePos64", 0xB530u},
        {"_ZN7cocos2d14unzGoToFilePosEPvPNS_14unz_file_pos_sE", "__dynarmic_unzGoToFilePos", 0xB500u},
        {"_ZN7cocos2d15unzGoToNextFileEPv", "__dynarmic_unzGoToNextFile", 0xB570u},
        {"_ZN7cocos2d16unzGoToFirstFileEPv", "__dynarmic_unzGoToFirstFile", 0xB530u},
        {"_ZN7cocos2d21unzGetCurrentFileInfoEPvPNS_15unz_file_info_sEPcmS0_mS3_m", "__dynarmic_unzGetCurrentFileInfo", 0xB5F0u},
        {"_ZN7cocos2d23unzGetCurrentFileInfo64EPvPNS_17unz_file_info64_sEPcmS0_mS3_m", "__dynarmic_unzGetCurrentFileInfo64", 0xB510u},
        {"_ZN7cocos2d19unzGetGlobalCommentEPvPcm", "__dynarmic_unzGetGlobalComment", 0xB5F0u},
        {"_ZN7cocos2d21unzGetLocalExtrafieldEPvS0_j", "__dynarmic_unzGetLocalExtrafield", 0xB5F0u},
        {"_ZN7cocos2d19unzCloseCurrentFileEPv", "__dynarmic_unzCloseCurrentFile", 0xB5F8u},
        {"_ZN7cocos2d8unzCloseEPv", "__dynarmic_unzClose", 0xB510u},
        {"_ZN7cocos2d18unzReadCurrentFileEPvS0_j", "__dynarmic_unzReadCurrentFile", 0xB5F0u},
        {"_ZN7cocos2d19unzOpenCurrentFile3EPvPiS1_iPKc", "__dynarmic_unzOpenCurrentFile3", 0xB5F0u},
        {"_ZN7cocos2d19unzOpenCurrentFile2EPvPiS1_i", "__dynarmic_unzOpenCurrentFile2", 0xB510u},
        {"_ZN7cocos2d26unzOpenCurrentFilePasswordEPvPKc", "__dynarmic_unzOpenCurrentFilePassword", 0xB500u},
        {"_ZN7cocos2d18unzOpenCurrentFileEPv", "__dynarmic_unzOpenCurrentFile", 0xB500u},
        {"_ZN7cocos2d9unzOpen64EPKv", "__dynarmic_unzOpen64", 0xB510u},
        {"_ZN7cocos2d7unzOpenEPKc", "__dynarmic_unzOpen", 0xB510u},
        {"_ZN7cocos2d11unzOpen2_64EPKvPNS_21zlib_filefunc64_def_sE", "__dynarmic_unzOpen2_64", 0xB570u},
        {"_ZN7cocos2d8unzOpen2EPKcPNS_19zlib_filefunc_def_sE", "__dynarmic_unzOpen2", 0xB530u},
        {"_ZN7cocos2d24unzStringFileNameCompareEPKcS1_i", "__dynarmic_unzStringFileNameCompare", 0xB570u},
        {"_ZN7cocos2d13unzLocateFileEPvPKci", "__dynarmic_unzLocateFile", 0xB5F0u},
    };
    std::size_t installed = 0;
    for (const Hook& hook : hooks) {
        const SymbolRecord* target = FindSymbol(runtime, hook.symbol);
        if (!target) continue;
        InstallThumbInlineSvcHookPreservingArguments(
            env, runtime, *target, hook.prologue, hook.import_name);
        ++installed;
    }
    return installed;
}

static ElfRuntime MapAndRelocateElf(const std::vector<u8>& elf, ProbeEnvironment& env) {
    const Elf32Ehdr header = ReadPod<Elf32Ehdr>(elf, 0);
    if (std::memcmp(header.ident, "\x7F" "ELF", 4) != 0 || header.ident[4] != 1 || header.ident[5] != 1) {
        throw std::runtime_error("libgame.so is not a little-endian ELF32 image");
    }
    if (header.type != kEtDyn || header.machine != kEmArm) throw std::runtime_error("libgame.so is not an ARM ET_DYN shared object");
    if (header.phentsize != sizeof(Elf32Phdr) || header.shentsize != sizeof(Elf32Shdr)) throw std::runtime_error("unexpected ELF table entry sizes");
    if (static_cast<u64>(header.phoff) + static_cast<u64>(header.phnum) * sizeof(Elf32Phdr) > elf.size() ||
        static_cast<u64>(header.shoff) + static_cast<u64>(header.shnum) * sizeof(Elf32Shdr) > elf.size()) {
        throw std::runtime_error("ELF program or section table is truncated");
    }

    ElfRuntime runtime{};
    runtime.entry = kGameBase + header.entry;
    u32 min_vaddr = std::numeric_limits<u32>::max();
    u32 max_vaddr = 0;
    std::vector<Elf32Phdr> phdrs;
    for (u16 i = 0; i < header.phnum; ++i) {
        const Elf32Phdr ph = ReadPod<Elf32Phdr>(elf, header.phoff + static_cast<std::size_t>(i) * sizeof(Elf32Phdr));
        phdrs.push_back(ph);
        if (ph.type != kPtLoad || ph.memsz == 0) continue;
        if (static_cast<u64>(ph.offset) + ph.filesz > elf.size() || ph.filesz > ph.memsz) throw std::runtime_error("invalid PT_LOAD segment");
        ++runtime.load_segments;
        if (ph.flags & 1u) ++runtime.executable_segments;
        min_vaddr = std::min(min_vaddr, AlignDown(ph.vaddr, kPageSize));
        max_vaddr = std::max(max_vaddr, AlignUp(ph.vaddr + ph.memsz, kPageSize));
    }
    if (runtime.load_segments == 0 || max_vaddr <= min_vaddr) throw std::runtime_error("ELF has no loadable image");
    runtime.image_min = kGameBase + min_vaddr;
    runtime.image_max = kGameBase + max_vaddr;
    env.Map(runtime.image_min, static_cast<std::size_t>(runtime.image_max - runtime.image_min), true);
    env.Map(kObjectBase, kObjectRegionSize, false);
    env.Map(kImportBase, kImportRegionSize, true);
    env.Map(kControlBase, kControlRegionSize, true);
    env.Map(kHeapBase, kHeapSize, false);
    env.Map(kStackBase, kStackSize, false);
    for (const Elf32Phdr& ph : phdrs) {
        if (ph.type == kPtLoad && ph.filesz != 0) env.CopyIn(kGameBase + ph.vaddr, elf.data() + ph.offset, ph.filesz);
    }

    std::vector<Elf32Shdr> sections;
    sections.reserve(header.shnum);
    for (u16 i = 0; i < header.shnum; ++i) sections.push_back(ReadPod<Elf32Shdr>(elf, header.shoff + static_cast<std::size_t>(i) * sizeof(Elf32Shdr)));
    if (header.shstrndx >= sections.size()) throw std::runtime_error("invalid ELF section-name table index");
    const Elf32Shdr& shstr = sections[header.shstrndx];

    for (const Elf32Shdr& section : sections) {
        if (section.type != kShtDynsym) continue;
        if (section.entsize != sizeof(Elf32Sym) || section.link >= sections.size()) throw std::runtime_error("invalid .dynsym metadata");
        const Elf32Shdr& strings = sections[section.link];
        runtime.dynsym_count = section.size / sizeof(Elf32Sym);
        for (std::size_t i = 0; i < runtime.dynsym_count; ++i) {
            const Elf32Sym symbol = ReadPod<Elf32Sym>(elf, section.offset + i * sizeof(Elf32Sym));
            if (symbol.shndx == kShnUndef && symbol.name != 0) ++runtime.undefined_symbols;
            if (symbol.shndx == kShnUndef || symbol.value == 0) continue;
            const std::string name = StringFromTable(elf, strings, symbol.name);
            const u32 address = kGameBase + symbol.value;
            if (!name.empty()) runtime.symbols.push_back(SymbolRecord{name, address, symbol.size});
            if (name == "JNI_OnLoad") runtime.jni_onload = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths" ||
                     name == "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath") runtime.native_set_paths = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit") runtime.native_init = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender") runtime.native_render = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin") runtime.native_touch_begin = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd") runtime.native_touch_end = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove") runtime.native_touch_move = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown") runtime.native_key_down = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText") runtime.native_insert_text = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeDeleteBackward") runtime.native_delete_backward = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause") runtime.native_pause = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume") runtime.native_resume = address;
        }
    }

    std::sort(runtime.symbols.begin(), runtime.symbols.end(), [](const SymbolRecord& lhs, const SymbolRecord& rhs) {
        if (lhs.address != rhs.address) return lhs.address < rhs.address;
        return lhs.size > rhs.size;
    });

    for (const Elf32Shdr& section : sections) {
        if (section.type == kShtRela) throw std::runtime_error("ARM image uses unsupported RELA relocations");
        if (section.type != kShtRel) continue;
        if (section.entsize != sizeof(Elf32Rel) || section.link >= sections.size()) throw std::runtime_error("invalid ELF REL metadata");
        const Elf32Shdr& symbols_section = sections[section.link];
        if (symbols_section.entsize != sizeof(Elf32Sym) || symbols_section.link >= sections.size()) throw std::runtime_error("invalid relocation symbol table");
        const Elf32Shdr& strings = sections[symbols_section.link];
        const std::size_t symbol_count = symbols_section.size / sizeof(Elf32Sym);
        const std::size_t count = section.size / sizeof(Elf32Rel);
        runtime.relocation_count += count;
        for (std::size_t rel_index = 0; rel_index < count; ++rel_index) {
            const Elf32Rel rel = ReadPod<Elf32Rel>(elf, section.offset + rel_index * sizeof(Elf32Rel));
            const u32 symbol_index = rel.info >> 8;
            const u32 type = rel.info & 0xFFu;
            const u32 where = kGameBase + rel.offset;
            const u32 addend = env.MemoryRead32(where);
            u32 value = 0;
            std::string name;
            if (symbol_index != 0) {
                if (symbol_index >= symbol_count) throw std::runtime_error("relocation symbol index outside table");
                const Elf32Sym symbol = ReadPod<Elf32Sym>(elf, symbols_section.offset + static_cast<std::size_t>(symbol_index) * sizeof(Elf32Sym));
                name = StringFromTable(elf, strings, symbol.name);
                if (symbol.shndx != kShnUndef) value = kGameBase + symbol.value;
                else if (IsImportedObjectName(name, symbol.info & 0x0Fu)) value = EnsureObject(runtime, env, name);
                else value = EnsureImport(runtime, env, name);
            }
            switch (type) {
            case kRArmNone: break;
            case kRArmAbs32: env.MemoryWrite32(where, value + addend); break;
            case kRArmGlobDat:
            case kRArmJumpSlot: env.MemoryWrite32(where, value); break;
            case kRArmRelative: env.MemoryWrite32(where, kGameBase + addend); ++runtime.relative_relocations; break;
            default: {
                std::ostringstream error;
                error << "unsupported ARM relocation " << type << " at 0x" << std::hex << rel.offset << " (" << name << ")";
                throw std::runtime_error(error.str());
            }
            }
            if (type == kRArmAbs32 || type == kRArmGlobDat || type == kRArmJumpSlot) ++runtime.imported_relocations;
        }
    }

    for (const Elf32Shdr& section : sections) {
        const std::string name = SectionName(elf, shstr, section.name);
        if (section.type != kShtInitArray && name != ".init_array") continue;
        if (section.size % 4u != 0) throw std::runtime_error("invalid .init_array size");
        const std::size_t count = section.size / 4u;
        runtime.constructors.reserve(count);
        for (std::size_t i = 0; i < count; ++i) runtime.constructors.push_back(env.MemoryRead32(kGameBase + section.addr + static_cast<u32>(i * 4u)));
        break;
    }
    if (runtime.jni_onload == 0 || runtime.native_set_paths == 0 || runtime.native_init == 0 ||
        runtime.native_render == 0 || runtime.native_touch_begin == 0 ||
        runtime.native_touch_end == 0 || runtime.native_touch_move == 0 ||
        runtime.native_key_down == 0) {
        throw std::runtime_error("required JNI/render/input exports were not found in libgame.so");
    }
    if (runtime.constructors.empty()) throw std::runtime_error("ARM ELF has no .init_array");
    return runtime;
}

static u64 JoinU64(u32 low, u32 high) { return static_cast<u64>(low) | (static_cast<u64>(high) << 32); }
static double WordsToDouble(u32 low, u32 high) {
    const u64 bits = JoinU64(low, high);
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
static std::pair<u32, u32> DoubleToWords(double value) {
    u64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return {static_cast<u32>(bits), static_cast<u32>(bits >> 32)};
}
static float WordToFloat(u32 bits) { float value = 0; std::memcpy(&value, &bits, sizeof(value)); return value; }
static u32 FloatToWord(float value) { u32 bits = 0; std::memcpy(&bits, &value, sizeof(bits)); return bits; }


enum class RefKind { Class, String, Method, Object, ByteArray, IntArray, FloatArray };
struct GuestRef {
    u32 handle = 0;
    RefKind kind = RefKind::Object;
    u32 data_address = 0;
    u32 length = 0;
    std::string class_name;
    std::string name;
    std::string signature;
    u64 calls = 0;
};

#pragma pack(push, 1)
struct GuestZStreamLayout {
    u32 next_in;
    u32 avail_in;
    u32 total_in;
    u32 next_out;
    u32 avail_out;
    u32 total_out;
    u32 msg;
    u32 state;
    u32 zalloc;
    u32 zfree;
    u32 opaque;
    s32 data_type;
    u32 adler;
    u32 reserved;
};
struct GuestTmLayout {
    s32 tm_sec;
    s32 tm_min;
    s32 tm_hour;
    s32 tm_mday;
    s32 tm_mon;
    s32 tm_year;
    s32 tm_wday;
    s32 tm_yday;
    s32 tm_isdst;
    s32 tm_gmtoff;
    u32 tm_zone;
};
struct GuestTimebLayout {
    s32 time;
    u16 millitm;
    std::int16_t timezone;
    std::int16_t dstflag;
    u16 padding;
};
#pragma pack(pop)
static_assert(sizeof(GuestZStreamLayout) == 56);
static_assert(sizeof(GuestTmLayout) == 44);
static_assert(sizeof(GuestTimebLayout) == 12);

enum class ZStreamKind { Inflate, Deflate };
struct GuestZStream {
    u32 guest_address = 0;
    z_stream host{};
    ZStreamKind kind = ZStreamKind::Inflate;
    bool active = false;
};

struct GuestFile {
    u32 handle = 0;
    std::FILE* stream = nullptr;
    const std::vector<u8>* memory = nullptr;
    std::size_t memory_position = 0;
    bool standard = false;
    std::string path;
    std::string mode;

    bool readable() const { return stream != nullptr || memory != nullptr; }
};

struct GuestAddrInfoAllocation {
    std::vector<u32> blocks;
};

struct GuestPollFd {
    s32 fd;
    std::int16_t events;
    std::int16_t revents;
};
static_assert(sizeof(GuestPollFd) == 8);

struct CooperativeWorkerContext {
    std::array<u32, 16> regs{};
    std::array<u32, 64> ext_regs{};
    u32 cpsr = 0x10u;
    u32 fpscr = 0u;
    bool valid = false;
};

enum class HostEventType {
    TouchBegin,
    TouchMove,
    TouchEnd,
    KeyDown,
    TextInput,
    DeleteBackward,
    Pause,
    Resume
};

struct HostEvent {
    HostEventType type = HostEventType::TouchMove;
    float x = 0.0f;
    float y = 0.0f;
    u32 value = 0;
};

#ifdef _WIN32
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
using GLsizeiptr_ = std::ptrdiff_t;
using GLintptr_ = std::ptrdiff_t;

class WinGlHost {
public:
    ~WinGlHost() { Destroy(); }

    bool Create(int width, int height, std::ostream& log) {
        log_ = &log;
        native_width_ = width;
        native_height_ = height;
        closed_ = false;
        active_ = true;
        instance_ = GetModuleHandleA(nullptr);
        const char* class_name = "GeometryDashUnifiedLegacyArmWindow";
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = &WinGlHost::WindowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = class_name;
        RegisterClassExA(&wc);

        RECT rectangle{0, 0, width, height};
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
        const int window_width = rectangle.right - rectangle.left;
        const int window_height = rectangle.bottom - rectangle.top;
        const int window_x = std::max(0,
            (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2);
        const int window_y = std::max(0,
            (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2);
        window_ = CreateWindowExA(0, class_name, "Geometry Dash - Unified Legacy ARM",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  window_x, window_y, window_width, window_height,
                                  nullptr, nullptr, instance_, this);
        if (!window_) return Fail("CreateWindowExA failed");
        device_ = GetDC(window_);
        if (!device_) return Fail("GetDC failed");

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;
        const int format = ChoosePixelFormat(device_, &pfd);
        if (!format || !SetPixelFormat(device_, format, &pfd)) return Fail("SetPixelFormat failed");
        context_ = wglCreateContext(device_);
        if (!context_ || !wglMakeCurrent(device_, context_)) return Fail("wglCreateContext/wglMakeCurrent failed");
        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        log << "RESULT: DYNARMIC_OPENGL_DEVICE vendor=" << (vendor ? vendor : "<null>")
            << " renderer=" << (renderer ? renderer : "<null>")
            << " version=" << (version ? version : "<null>") << '\n';
        opengl_ = LoadLibraryA("opengl32.dll");
        if (!opengl_) return Fail("opengl32.dll could not be loaded");
        InitializeGpuProfiler();

        using SwapInterval = BOOL (WINAPI*)(int);
        if (auto* swap = reinterpret_cast<SwapInterval>(Resolve("wglSwapIntervalEXT"))) swap(1);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        return true;
    }

    void Destroy() {
        DestroyGpuProfiler();
        if (context_) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context_); context_ = nullptr; }
        if (device_ && window_) { ReleaseDC(window_, device_); device_ = nullptr; }
        if (window_) { DestroyWindow(window_); window_ = nullptr; }
        if (opengl_) { FreeLibrary(opengl_); opengl_ = nullptr; }
        functions_.clear();
        events_.clear();
    }

    void* Resolve(const std::string& name) {
        const auto found = functions_.find(name);
        if (found != functions_.end()) return found->second;
        PROC function = wglGetProcAddress(name.c_str());
        if (function == nullptr || function == reinterpret_cast<PROC>(1) ||
            function == reinterpret_cast<PROC>(2) || function == reinterpret_cast<PROC>(3) ||
            function == reinterpret_cast<PROC>(-1)) {
            function = opengl_ ? GetProcAddress(opengl_, name.c_str()) : nullptr;
        }
        functions_[name] = reinterpret_cast<void*>(function);
        return reinterpret_cast<void*>(function);
    }

    bool PumpMessages() {
        MSG message;
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { closed_ = true; break; }
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        return !closed_;
    }

    std::vector<HostEvent> TakeEvents() {
        std::vector<HostEvent> result;
        result.reserve(events_.size());
        while (!events_.empty()) {
            result.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        return result;
    }

    void Swap() { if (device_) SwapBuffers(device_); }
    void BeginGpuFrame(u64 frame) {
        PollGpuProfiler();
        if (!gpu_profiler_ready_ || active_gpu_query_ >= 0) return;
        const std::size_t index = static_cast<std::size_t>(
            frame % gpu_queries_.size());
        GpuQuerySlot& slot = gpu_queries_[index];
        if (slot.pending || slot.id == 0) return;
        begin_query_(GL_TIME_ELAPSED, slot.id);
        slot.frame = frame;
        active_gpu_query_ = static_cast<int>(index);
    }
    void EndGpuFrame() {
        if (!gpu_profiler_ready_ || active_gpu_query_ < 0) return;
        end_query_(GL_TIME_ELAPSED);
        gpu_queries_[static_cast<std::size_t>(active_gpu_query_)].pending = true;
        active_gpu_query_ = -1;
    }
    std::vector<std::pair<u64, double>> TakeGpuTimings() {
        PollGpuProfiler();
        std::vector<std::pair<u64, double>> results;
        results.reserve(gpu_results_.size());
        while (!gpu_results_.empty()) {
            results.push_back(gpu_results_.front());
            gpu_results_.pop_front();
        }
        return results;
    }
    std::vector<std::pair<u64, double>> FinishGpuTimings() {
        if (gpu_profiler_ready_) {
            if (active_gpu_query_ >= 0) EndGpuFrame();
            glFinish();
            PollGpuProfiler(true);
        }
        return TakeGpuTimings();
    }
    bool Ready() const { return context_ != nullptr; }
    bool Active() const { return active_ && !closed_; }
    void SetTitle(const std::string& title) { if (window_) SetWindowTextA(window_, title.c_str()); }
    void SetTextInputActive(bool active) {
        text_input_active_ = active;
        if (active && keyboard_down_) {
            keyboard_down_ = false;
            Queue(HostEvent{HostEventType::TouchEnd,
                            static_cast<float>(native_width_) * 0.5f,
                            static_cast<float>(native_height_) * 0.5f, 0});
        }
    }
    void RequestClose() {
        closed_ = true;
        events_.clear();
        if (window_) PostMessageA(window_, WM_CLOSE, 0, 0);
        PostQuitMessage(0);
    }

private:
    using GenQueriesFn = void (APIENTRY*)(GLsizei, GLuint*);
    using DeleteQueriesFn = void (APIENTRY*)(GLsizei, const GLuint*);
    using BeginQueryFn = void (APIENTRY*)(GLenum, GLuint);
    using EndQueryFn = void (APIENTRY*)(GLenum);
    using GetQueryObjectivFn = void (APIENTRY*)(GLuint, GLenum, GLint*);
    using GetQueryObjectui64vFn =
        void (APIENTRY*)(GLuint, GLenum, unsigned long long*);

    struct GpuQuerySlot {
        GLuint id = 0;
        u64 frame = 0;
        bool pending = false;
    };

    void InitializeGpuProfiler() {
        gen_queries_ = reinterpret_cast<GenQueriesFn>(Resolve("glGenQueries"));
        delete_queries_ =
            reinterpret_cast<DeleteQueriesFn>(Resolve("glDeleteQueries"));
        begin_query_ = reinterpret_cast<BeginQueryFn>(Resolve("glBeginQuery"));
        end_query_ = reinterpret_cast<EndQueryFn>(Resolve("glEndQuery"));
        get_query_object_iv_ = reinterpret_cast<GetQueryObjectivFn>(
            Resolve("glGetQueryObjectiv"));
        get_query_object_ui64v_ = reinterpret_cast<GetQueryObjectui64vFn>(
            Resolve("glGetQueryObjectui64v"));
        if (!get_query_object_ui64v_)
            get_query_object_ui64v_ =
                reinterpret_cast<GetQueryObjectui64vFn>(
                    Resolve("glGetQueryObjectui64vEXT"));
        gpu_profiler_ready_ = gen_queries_ && delete_queries_ &&
            begin_query_ && end_query_ && get_query_object_iv_ &&
            get_query_object_ui64v_;
        if (!gpu_profiler_ready_) {
            if (log_) *log_ << "RESULT: DYNARMIC_GPU_TIMER_UNAVAILABLE\n";
            return;
        }
        std::array<GLuint, 8> ids{};
        gen_queries_(static_cast<GLsizei>(ids.size()), ids.data());
        for (std::size_t index = 0; index < ids.size(); ++index)
            gpu_queries_[index].id = ids[index];
        if (log_)
            *log_ << "RESULT: DYNARMIC_GPU_TIMER_READY queries="
                  << ids.size() << '\n';
    }

    void PollGpuProfiler(bool force = false) {
        if (!gpu_profiler_ready_) return;
        for (GpuQuerySlot& slot : gpu_queries_) {
            if (!slot.pending || slot.id == 0) continue;
            GLint available = 0;
            if (!force)
                get_query_object_iv_(
                    slot.id, GL_QUERY_RESULT_AVAILABLE, &available);
            if (!force && !available) continue;
            unsigned long long nanoseconds = 0;
            get_query_object_ui64v_(
                slot.id, GL_QUERY_RESULT, &nanoseconds);
            gpu_results_.emplace_back(
                slot.frame,
                static_cast<double>(nanoseconds) / 1000000.0);
            slot.pending = false;
        }
    }

    void DestroyGpuProfiler() {
        if (!gpu_profiler_ready_) return;
        if (active_gpu_query_ >= 0) {
            end_query_(GL_TIME_ELAPSED);
            active_gpu_query_ = -1;
        }
        std::array<GLuint, 8> ids{};
        for (std::size_t index = 0; index < gpu_queries_.size(); ++index)
            ids[index] = gpu_queries_[index].id;
        delete_queries_(static_cast<GLsizei>(ids.size()), ids.data());
        gpu_profiler_ready_ = false;
        gpu_results_.clear();
    }

    void Queue(HostEvent event) {
        if (event.type == HostEventType::TouchMove && !events_.empty() &&
            events_.back().type == HostEventType::TouchMove) {
            events_.back() = event;
            return;
        }
        events_.push_back(std::move(event));
    }

    void ClientPoint(LPARAM lparam, float& x, float& y) {
        const int raw_x = static_cast<int>(static_cast<short>(static_cast<unsigned long>(lparam) & 0xFFFFu));
        const int raw_y = static_cast<int>(static_cast<short>((static_cast<unsigned long>(lparam) >> 16) & 0xFFFFu));
        RECT area{};
        if (!window_ || !GetClientRect(window_, &area) || area.right <= area.left || area.bottom <= area.top) {
            x = static_cast<float>(raw_x);
            y = static_cast<float>(raw_y);
            return;
        }
        const float client_width = static_cast<float>(area.right - area.left);
        const float client_height = static_cast<float>(area.bottom - area.top);
        x = std::clamp(static_cast<float>(raw_x) * static_cast<float>(native_width_) / client_width,
                       0.0f, static_cast<float>(native_width_));
        y = std::clamp(static_cast<float>(raw_y) * static_cast<float>(native_height_) / client_height,
                       0.0f, static_cast<float>(native_height_));
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        WinGlHost* self = reinterpret_cast<WinGlHost*>(GetWindowLongPtrA(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTA*>(lparam);
            self = static_cast<WinGlHost*>(create->lpCreateParams);
            SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcA(window, message, wparam, lparam);

        float x = 0.0f, y = 0.0f;
        switch (message) {
        case WM_CLOSE:
            if (self->mouse_down_) {
                self->mouse_down_ = false;
                self->Queue(HostEvent{HostEventType::TouchEnd, self->last_x_, self->last_y_, 0});
            }
            self->closed_ = true;
            return 0;
        case WM_DESTROY:
            self->closed_ = true;
            PostQuitMessage(0);
            return 0;
        case WM_ACTIVATEAPP: {
            const bool becoming_active = wparam != 0;
            if (self->active_ != becoming_active) {
                self->active_ = becoming_active;
                self->Queue(HostEvent{becoming_active ? HostEventType::Resume : HostEventType::Pause});
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            self->ClientPoint(lparam, x, y);
            self->last_x_ = x; self->last_y_ = y;
            self->mouse_down_ = true;
            SetFocus(window);
            SetCapture(window);
            self->Queue(HostEvent{HostEventType::TouchBegin, x, y, 0});
            return 0;
        case WM_MOUSEMOVE:
            if (self->mouse_down_) {
                self->ClientPoint(lparam, x, y);
                self->last_x_ = x; self->last_y_ = y;
                self->Queue(HostEvent{HostEventType::TouchMove, x, y, 0});
            }
            return 0;
        case WM_LBUTTONUP:
            if (self->mouse_down_) {
                self->ClientPoint(lparam, x, y);
                self->last_x_ = x; self->last_y_ = y;
                self->mouse_down_ = false;
                ReleaseCapture();
                self->Queue(HostEvent{HostEventType::TouchEnd, x, y, 0});
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (self->mouse_down_) {
                self->mouse_down_ = false;
                self->Queue(HostEvent{HostEventType::TouchEnd, self->last_x_, self->last_y_, 0});
            }
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                self->Queue(HostEvent{HostEventType::KeyDown, 0.0f, 0.0f, 4u});
                return 0;
            }
            if ((wparam == VK_SPACE || wparam == VK_UP) &&
                !self->text_input_active_ && !self->keyboard_down_) {
                self->keyboard_down_ = true;
                self->Queue(HostEvent{HostEventType::TouchBegin,
                                      static_cast<float>(self->native_width_) * 0.5f,
                                      static_cast<float>(self->native_height_) * 0.5f, 0});
                return 0;
            }
            break;
        case WM_KEYUP:
            if ((wparam == VK_SPACE || wparam == VK_UP) && self->keyboard_down_) {
                self->keyboard_down_ = false;
                self->Queue(HostEvent{HostEventType::TouchEnd,
                                      static_cast<float>(self->native_width_) * 0.5f,
                                      static_cast<float>(self->native_height_) * 0.5f, 0});
                return 0;
            }
            break;
        case WM_CHAR:
            if (!self->text_input_active_) return 0;
            if (wparam == '\b') self->Queue(HostEvent{HostEventType::DeleteBackward});
            else if (wparam == '\r') self->Queue(HostEvent{HostEventType::TextInput, 0.0f, 0.0f, static_cast<u32>('\n')});
            else if (wparam >= 0x20u) self->Queue(HostEvent{HostEventType::TextInput, 0.0f, 0.0f, static_cast<u32>(wparam)});
            return 0;
        case WM_UNICHAR:
            if (wparam == UNICODE_NOCHAR) return TRUE;
            if (self->text_input_active_ && wparam <= 0x10FFFFu)
                self->Queue(HostEvent{HostEventType::TextInput, 0.0f, 0.0f, static_cast<u32>(wparam)});
            return 0;
        default:
            break;
        }
        return DefWindowProcA(window, message, wparam, lparam);
    }

    bool Fail(const char* message) {
        if (log_) { *log_ << "OpenGL host error: " << message << '\n'; log_->flush(); }
        return false;
    }

    std::ostream* log_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HDC device_ = nullptr;
    HGLRC context_ = nullptr;
    HMODULE opengl_ = nullptr;
    bool closed_ = false;
    bool active_ = true;
    bool mouse_down_ = false;
    bool keyboard_down_ = false;
    bool text_input_active_ = false;
    int native_width_ = 1280;
    int native_height_ = 720;
    float last_x_ = 0.0f;
    float last_y_ = 0.0f;
    std::deque<HostEvent> events_;
    std::unordered_map<std::string, void*> functions_;
    bool gpu_profiler_ready_ = false;
    int active_gpu_query_ = -1;
    std::array<GpuQuerySlot, 8> gpu_queries_{};
    std::deque<std::pair<u64, double>> gpu_results_;
    GenQueriesFn gen_queries_ = nullptr;
    DeleteQueriesFn delete_queries_ = nullptr;
    BeginQueryFn begin_query_ = nullptr;
    EndQueryFn end_query_ = nullptr;
    GetQueryObjectivFn get_query_object_iv_ = nullptr;
    GetQueryObjectui64vFn get_query_object_ui64v_ = nullptr;
};
#else
class WinGlHost {
public:
    bool Create(int, int, std::ostream&) { return false; }
    void Destroy() {}
    void* Resolve(const char*) { return nullptr; }
    bool PumpMessages() { return false; }
    std::vector<HostEvent> TakeEvents() { return {}; }
    void Swap() {}
    void BeginGpuFrame(u64) {}
    void EndGpuFrame() {}
    std::vector<std::pair<u64, double>> TakeGpuTimings() { return {}; }
    std::vector<std::pair<u64, double>> FinishGpuTimings() { return {}; }
    bool Ready() const { return false; }
    bool Active() const { return false; }
    void SetTitle(const std::string&) {}
    void SetTextInputActive(bool) {}
    void RequestClose() {}
};
#endif

class GuestExecutor {
public:
    GuestExecutor(ProbeEnvironment& env, ElfRuntime& runtime, std::ostream& log)
        : env_(env), runtime_(runtime), log_(log), cpu_(MakeConfig(env)) {
        env_.AttachCpu(&cpu_);
        InitializeControlTraps();
        heap_cursor_ = kHeapBase + 0x1000u;
        errno_address_ = kObjectBase + kObjectRegionSize - 0x1000u;
        env_.MemoryWrite32(errno_address_, 0u);
        c_locale_address_ = AllocateString("C");
        tm_address_ = Allocate(sizeof(GuestTmLayout));
        time_zone_address_ = AllocateString("local");
    }

    ~GuestExecutor() {
        for (auto& [handle, file] : files_) {
            (void)handle;
            if (file.stream && !file.standard) std::fclose(file.stream);
        }
        for (auto& [address, stream] : zstreams_) {
            (void)address;
            if (!stream.active) continue;
            if (stream.kind == ZStreamKind::Inflate) inflateEnd(&stream.host);
            else if (stream.kind == ZStreamKind::Deflate) deflateEnd(&stream.host);
        }
        apk_member_cache_.Report();
        if (apk_memory_read_calls_) {
            log_ << "Dynarmic APK memory cache totals: reads=" << apk_memory_read_calls_
                 << " bytes=" << apk_memory_read_bytes_ << '\n';
            log_.flush();
        }
#ifdef _WIN32
        for (const auto& [guest_fd, host_socket] : sockets_) {
            (void)guest_fd;
            closesocket(host_socket);
        }
        sockets_.clear();
        if (winsock_initialized_) { WSACleanup(); winsock_initialized_ = false; }
#endif
        if (audio_initialized_) {
            audio_shutdown();
            audio_initialized_ = false;
        }
        storage_shutdown();
    }

    void ConfigureHost(const std::string& apk_path, const std::string& writable_path,
                       const std::vector<u8>& apk_image) {
        apk_path_ = apk_path;
        writable_path_ = writable_path;
        apk_image_ = &apk_image;
        apk_member_cache_.Initialize(apk_image, writable_path, log_);
        // Text-entry UI lazily requests these assets the first time a level
        // name, description, or search field opens. Pull them into the native
        // member cache during startup so slower disks do not add another
        // first-keyboard hitch on top of the guest-side font construction.
        static constexpr const char* kTextInputWarmAssets[] = {
            "assets/chatFont-hd.fnt",
            "assets/chatFont-hd.png",
            "assets/chatFont.fnt",
            "assets/chatFont.png",
            "assets/bigFont-hd.fnt",
            "assets/bigFont-hd.png",
            "assets/bigFont.fnt",
            "assets/bigFont.png",
            "assets/goldFont-hd.fnt",
            "assets/goldFont-hd.png",
            "assets/loadingCircle-hd.png",
            "assets/loadingCircle.png",
            "assets/square02b_001-hd.png",
            "assets/square02b_001.png",
        };
        std::size_t warmed_text_assets = 0;
        for (const char* member : kTextInputWarmAssets) {
            if (apk_member_cache_.Load(member)) ++warmed_text_assets;
        }
        log_ << "RESULT: DYNARMIC_TEXT_INPUT_ASSET_PREWARM_READY count="
             << warmed_text_assets << '\n';
        const auto music_cache_begin = std::chrono::steady_clock::now();
        const auto [music_cache_count, music_cache_bytes] =
            apk_member_cache_.PrecacheBackgroundMusic(
                std::filesystem::path(writable_path_));
        const double music_cache_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - music_cache_begin).count();
        log_ << "RESULT: DYNARMIC_BACKGROUND_MUSIC_PRECACHE_READY count="
             << music_cache_count << " bytes=" << music_cache_bytes
             << " elapsed_ms=" << std::fixed << std::setprecision(1)
             << music_cache_ms << '\n';
#ifdef _WIN32
        CreateDirectoryA(writable_path_.c_str(), nullptr);
        WSADATA winsock_data{};
        if (WSAStartup(MAKEWORD(2, 2), &winsock_data) == 0) {
            winsock_initialized_ = true;
            log_ << "RESULT: DYNARMIC_WINSOCK_BRIDGE_READY version="
                 << LOBYTE(winsock_data.wVersion) << '.' << HIBYTE(winsock_data.wVersion) << '\n';
        } else {
            log_ << "WARNING: Winsock initialization failed; online features unavailable\n";
        }
#endif
        storage_initialize(writable_path_.c_str());
        const std::filesystem::path executable_directory =
            std::filesystem::path(apk_path_).parent_path();
        const std::string executable_directory_string =
            executable_directory.empty() ? std::string(".") : executable_directory.string();
        audio_initialize(executable_directory_string.c_str());
        audio_set_writable_directory(writable_path_.c_str());
        audio_set_apk_path(apk_path_.c_str());
        audio_initialized_ = true;
        stdin_handle_ = NewGuestFile(stdin, true, "stdin", "rb");
        stdout_handle_ = NewGuestFile(stdout, true, "stdout", "wb");
        stderr_handle_ = NewGuestFile(stderr, true, "stderr", "wb");
        if (!stdin_handle_ || !stdout_handle_ || !stderr_handle_)
            throw std::runtime_error("could not allocate mapped guest stdio objects");
        for (const auto& object : runtime_.objects) {
            if (object.name == "__sF") {
                env_.MemoryWrite32(object.address + 0u, stdin_handle_);
                env_.MemoryWrite32(object.address + 84u, stdout_handle_);
                env_.MemoryWrite32(object.address + 168u, stderr_handle_);
            }
        }
    }

    bool CreateOpenGlWindow(int width, int height) {
        return gl_.Create(width, height, log_);
    }
    bool PumpMessages() { return gl_.PumpMessages(); }
    void SwapBuffersHost() { gl_.Swap(); }
    void BeginGpuFrame(u64 frame) { gl_.BeginGpuFrame(frame); }
    void EndGpuFrame() { gl_.EndGpuFrame(); }
    std::vector<std::pair<u64, double>> TakeGpuTimings() {
        return gl_.TakeGpuTimings();
    }
    std::vector<std::pair<u64, double>> FinishGpuTimings() {
        return gl_.FinishGpuTimings();
    }
    double FrameInterval() const { return frame_interval_; }
    u64 PermissiveStubCalls() const { return permissive_stub_calls_; }
    const std::set<std::string>& PermissiveNames() const { return permissive_names_; }
    const std::string& LastError() const { return last_error_; }
    std::vector<HostEvent> TakeHostEvents() { return gl_.TakeEvents(); }
    bool WindowActive() const { return gl_.Active(); }
    void SetWindowTitle(const std::string& title) { gl_.SetTitle(title); }
    bool TerminationRequested() const { return termination_requested_; }
    void ReportHeapStatus(const char* reason) { LogHeapStatus(reason); }
    void FlushDiagnostics() { log_.flush(); }
    const GuestCallMetrics& LastCallMetrics() const { return last_call_metrics_; }
    const std::string& LastAndroidLog() const { return last_android_log_; }

    ProfilerCounters CaptureProfilerCounters() const {
        ProfilerCounters counters;
        counters.import_calls = total_import_calls_;
        counters.jni_svc_calls = jni_svc_calls_;
        counters.gl_calls = gl_calls_;
        counters.draw_calls = gl_draw_calls_;
        counters.draw_vertices = gl_draw_vertices_;
        counters.buffer_upload_bytes = gl_buffer_upload_bytes_;
        counters.texture_upload_bytes = gl_texture_upload_bytes_;
        counters.allocation_calls = allocation_calls_;
        counters.free_calls = free_calls_;
        counters.reallocation_calls = reallocation_calls_;
        counters.apk_read_calls = apk_memory_read_calls_;
        counters.apk_read_bytes = apk_memory_read_bytes_;
        counters.live_heap_bytes = live_allocation_bytes_;
        counters.peak_heap_bytes = peak_live_allocation_bytes_;
        return counters;
    }

    std::vector<u64> CaptureImportCounts() const {
        std::vector<u64> counts;
        counts.reserve(runtime_.imports.size());
        for (const ImportRecord& import : runtime_.imports)
            counts.push_back(import.calls);
        return counts;
    }

    std::string DescribeTopImportDeltas(
        const std::vector<u64>& before,
        const std::vector<u64>& after,
        std::size_t limit,
        bool gl_only = false) const {
        struct Delta {
            const ImportRecord* import = nullptr;
            u64 calls = 0;
        };
        std::vector<Delta> deltas;
        const std::size_t count =
            std::min({before.size(), after.size(), runtime_.imports.size()});
        deltas.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const ImportRecord& import = runtime_.imports[index];
            if (gl_only && !import.is_gl) continue;
            const u64 delta = after[index] >= before[index]
                ? after[index] - before[index]
                : 0;
            if (delta) deltas.push_back(Delta{&import, delta});
        }
        std::sort(deltas.begin(), deltas.end(),
                  [](const Delta& lhs, const Delta& rhs) {
                      return lhs.calls > rhs.calls;
                  });
        std::ostringstream output;
        const std::size_t shown = std::min(limit, deltas.size());
        for (std::size_t index = 0; index < shown; ++index) {
            if (index) output << '|';
            output << deltas[index].import->name << ':' << deltas[index].calls;
        }
        return output.str();
    }

    std::string DescribeTopImports(std::size_t limit,
                                   bool gl_only = false) const {
        std::vector<u64> zero(runtime_.imports.size(), 0u);
        return DescribeTopImportDeltas(zero, CaptureImportCounts(), limit,
                                       gl_only);
    }

    std::string DescribeTopImportHostSamples(std::size_t limit) const {
        struct Sample {
            const ImportRecord* import = nullptr;
            double estimated_total_ms = 0.0;
            double sampled_average_ns = 0.0;
        };
        std::vector<Sample> samples;
        for (const ImportRecord& import : runtime_.imports) {
            if (!import.sampled_host_calls || !import.calls) continue;
            const double average_ns =
                static_cast<double>(import.sampled_host_nanoseconds) /
                static_cast<double>(import.sampled_host_calls);
            samples.push_back(Sample{
                &import,
                average_ns * static_cast<double>(import.calls) / 1000000.0,
                average_ns});
        }
        std::sort(samples.begin(), samples.end(),
                  [](const Sample& lhs, const Sample& rhs) {
                      return lhs.estimated_total_ms >
                             rhs.estimated_total_ms;
                  });
        std::ostringstream output;
        const std::size_t shown = std::min(limit, samples.size());
        output << std::fixed << std::setprecision(2);
        for (std::size_t index = 0; index < shown; ++index) {
            if (index) output << '|';
            output << samples[index].import->name
                   << ":estimated_ms=" << samples[index].estimated_total_ms
                   << ":sample_avg_ns="
                   << samples[index].sampled_average_ns
                   << ":samples="
                   << samples[index].import->sampled_host_calls;
        }
        return output.str();
    }

    bool SendTouchPoint(u32 function, float x, float y, const std::string& label) {
        if (!function) return true;
        std::ostringstream details;
        details << std::fixed << std::setprecision(2) << "x=" << x << " y=" << y;
        LogHostDispatch(label, function, details.str());
        return RunFunction(function, {kEnvObject, 0u, 0u, FloatToWord(x), FloatToWord(y)},
                           nullptr, label, 0u, std::chrono::milliseconds(10000));
    }

    bool SendTouchMove(u32 function, float x, float y) {
        if (!function) return true;
        if (!touch_ids_) {
            touch_ids_ = NewArrayRef(RefKind::IntArray, 1u, 4u);
            touch_xs_ = NewArrayRef(RefKind::FloatArray, 1u, 4u);
            touch_ys_ = NewArrayRef(RefKind::FloatArray, 1u, 4u);
        }
        GuestRef* ids = FindRef(touch_ids_);
        GuestRef* xs = FindRef(touch_xs_);
        GuestRef* ys = FindRef(touch_ys_);
        if (!ids || !xs || !ys) return Fail("could not allocate touch-move JNI arrays");
        const s32 identifier = 0;
        if (!env_.WriteBytes(ids->data_address, &identifier, sizeof(identifier)) ||
            !env_.WriteBytes(xs->data_address, &x, sizeof(x)) ||
            !env_.WriteBytes(ys->data_address, &y, sizeof(y))) {
            return Fail("could not update touch-move JNI arrays");
        }
        std::ostringstream details;
        details << std::fixed << std::setprecision(2) << "x=" << x << " y=" << y;
        LogHostDispatch("nativeTouchesMove", function, details.str());
        return RunFunction(function, {kEnvObject, 0u, touch_ids_, touch_xs_, touch_ys_},
                           nullptr, "nativeTouchesMove", 0u, std::chrono::milliseconds(10000));
    }

    bool SendKey(u32 function, u32 key_code) {
        if (!function) return true;
        LogHostDispatch("nativeKeyDown", function, "key=" + std::to_string(key_code));
        return RunFunction(function, {kEnvObject, 0u, key_code}, nullptr,
                           "nativeKeyDown", 0u, std::chrono::milliseconds(10000));
    }

    bool SendText(u32 function, const std::string& text) {
        if (!function || text.empty()) return true;
        const u32 text_ref = NewStringRef(text);
        if (!text_ref) return Fail("could not allocate text JNI string");
        LogHostDispatch("nativeInsertText", function,
                        "bytes=" + std::to_string(text.size()) + " text=\"" + SanitizeLogText(text) + "\"");
        return RunFunction(function, {kEnvObject, 0u, text_ref}, nullptr,
                           "nativeInsertText", 0u, std::chrono::milliseconds(10000));
    }

    bool SendDeleteBackward(u32 function) {
        if (!function) return true;
        LogHostDispatch("nativeDeleteBackward", function, {});
        return RunFunction(function, {kEnvObject, 0u}, nullptr,
                           "nativeDeleteBackward", 0u, std::chrono::milliseconds(10000));
    }

    bool SendLifecycle(u32 function, const std::string& label) {
        if (!function) return true;
        LogHostDispatch(label, function, "lifecycle");
        return RunFunction(function, {kEnvObject, 0u}, nullptr,
                           label, 0u, std::chrono::milliseconds(30000));
    }

    u32 NewStringRef(const std::string& value) {
        GuestRef ref;
        ref.handle = kFakeRefBase + static_cast<u32>((refs_.size() + 1u) * 0x20u);
        ref.kind = RefKind::String;
        ref.length = static_cast<u32>(std::min<std::size_t>(value.size(), std::numeric_limits<u32>::max() - 1u));
        ref.data_address = Allocate(ref.length + 1u);
        if (!ref.data_address || !env_.WriteBytes(ref.data_address, value.c_str(), ref.length + 1u)) return 0;
        refs_.push_back(std::move(ref));
        return refs_.back().handle;
    }

    bool RunFunction(u32 address, const std::vector<u32>& arguments, u32* result,
                     const std::string& label, u64 tick_budget = kGuestCallTickBudget,
                     std::chrono::milliseconds wall_budget = std::chrono::milliseconds::zero()) {
        if (call_depth_ >= 16) return Fail("guest call recursion limit reached in " + label);
        ++call_depth_;
        active_calls_.push_back(label);
        ScopeExit call_scope([this] {
            if (!active_calls_.empty()) active_calls_.pop_back();
            if (call_depth_) --call_depth_;
        });
        cpu_.Regs().fill(0);
        cpu_.ExtRegs().fill(0);
        const u32 stack_top = kStackBase + kStackSize - static_cast<u32>(call_depth_) * 0x00080000u;
        const u32 stack_pointer = stack_top - 0x1000u;
        for (std::size_t i = 0; i < arguments.size() && i < 4; ++i) cpu_.Regs()[i] = arguments[i];
        for (std::size_t i = 4; i < arguments.size(); ++i) {
            env_.MemoryWrite32(stack_pointer + static_cast<u32>((i - 4u) * 4u), arguments[i]);
        }
        cpu_.Regs()[13] = stack_pointer;
        cpu_.Regs()[14] = kReturnStub;
        cpu_.Regs()[15] = address & ~1u;
        cpu_.SetCpsr(0x00000010u | ((address & 1u) ? 0x20u : 0u));
        cpu_.SetFpscr(0u);

        const bool unlimited_ticks = tick_budget == 0;
        u64 budget = tick_budget;
        u64 estimated_ticks = 0;
        u64 jit_runs = 0;
        u64 svc_calls = 0;
        bool returned = false;
        const ProfilerCounters counters_before = CaptureProfilerCounters();
        const u64 import_calls_before = total_import_calls_;
        const auto started = std::chrono::steady_clock::now();
        auto next_progress = started + std::chrono::seconds(5);

        while ((unlimited_ticks || budget != 0) && !returned) {
            const auto before_run = std::chrono::steady_clock::now();
            if (wall_budget.count() > 0 && before_run - started >= wall_budget) {
                const std::string diagnostic = BuildExecutionDiagnostic(
                    label + " exceeded wall-clock guard", estimated_ticks, before_run - started);
                return Fail(diagnostic);
            }

            const u64 chunk = unlimited_ticks ? 5000000u : std::min<u64>(budget, 5000000u);
            env_.ResetStopState();
            env_.ticks_left = chunk;
            ++jit_runs;
            const Dynarmic::HaltReason halt_reason = cpu_.Run();
            cpu_.ClearHalt(kCallbackHalt);

            if (env_.invalid_access) {
                std::ostringstream error;
                error << label << " invalid guest memory at 0x" << std::hex << env_.fault_address
                      << " PC=0x" << cpu_.Regs()[15] << " (" << DescribeAddress(cpu_.Regs()[15]) << ')'
                      << " LR=0x" << cpu_.Regs()[14] << " (" << DescribeAddress(cpu_.Regs()[14]) << ')';
                return Fail(error.str());
            }
            if (env_.interpreter_fallback) {
                std::ostringstream error;
                error << label << " interpreter fallback at 0x" << std::hex << env_.fallback_pc
                      << " (" << DescribeAddress(env_.fallback_pc) << ") count=" << std::dec << env_.fallback_count;
                return Fail(error.str());
            }
            if (env_.exception_seen) {
                std::ostringstream error;
                error << label << " exception at 0x" << std::hex << env_.exception_pc
                      << " (" << DescribeAddress(env_.exception_pc) << ')';
                return Fail(error.str());
            }
            if (env_.svc_pending) {
                ++svc_calls;
                estimated_ticks += 1024u;
                if (env_.pending_svc == kSvcReturn) {
                    returned = true;
                    break;
                }
                if (!HandleSvc(env_.pending_svc, label)) {
                    return false;
                }
                if (!unlimited_ticks) budget = budget > 1024 ? budget - 1024 : 0;
            } else if (env_.ticks_left == 0) {
                estimated_ticks += chunk;
                if (!unlimited_ticks) budget = budget > chunk ? budget - chunk : 0;
            } else {
                std::ostringstream error;
                error << label << " stopped without a trap at PC=0x" << std::hex << cpu_.Regs()[15]
                      << " (" << DescribeAddress(cpu_.Regs()[15]) << ')'
                      << " LR=0x" << cpu_.Regs()[14] << " (" << DescribeAddress(cpu_.Regs()[14]) << ')'
                      << " SP=0x" << cpu_.Regs()[13]
                      << " CPSR=0x" << cpu_.Cpsr() << " halt=0x" << static_cast<u64>(halt_reason);
                return Fail(error.str());
            }

            const auto after_run = std::chrono::steady_clock::now();
            if (after_run >= next_progress) {
                log_ << "Dynarmic guest progress: "
                     << BuildExecutionDiagnostic(label, estimated_ticks, after_run - started) << '\n';
                log_.flush();
                next_progress = after_run + std::chrono::seconds(5);
            }
        }
        if (!returned) {
            const auto elapsed = std::chrono::steady_clock::now() - started;
            const std::string diagnostic = BuildExecutionDiagnostic(
                label + " exceeded guest tick budget", estimated_ticks, elapsed);
            return Fail(diagnostic);
        }
        if (result) *result = cpu_.Regs()[0];
        const auto completed_elapsed = std::chrono::steady_clock::now() - started;
        last_call_metrics_.label = label;
        last_call_metrics_.elapsed_ms =
            std::chrono::duration<double, std::milli>(completed_elapsed).count();
        last_call_metrics_.estimated_ticks = estimated_ticks;
        last_call_metrics_.jit_runs = jit_runs;
        last_call_metrics_.svc_calls = svc_calls;
        last_call_metrics_.before = counters_before;
        last_call_metrics_.after = CaptureProfilerCounters();
        if (label.rfind("native", 0) == 0 &&
            completed_elapsed >= std::chrono::milliseconds(250)) {
            log_ << "Dynarmic guest call timing: " << label
                 << " elapsed_ms=" << std::fixed << std::setprecision(1)
                 << std::chrono::duration<double, std::milli>(completed_elapsed).count()
                 << " import_calls=" << (total_import_calls_ - import_calls_before) << '\n';
            log_.flush();
        }
        return true;
    }

private:
    static Dynarmic::A32::UserConfig MakeConfig(ProbeEnvironment& env) {
        Dynarmic::A32::UserConfig config;
        config.callbacks = &env;
        config.arch_version = Dynarmic::A32::ArchVersion::v5TE;
        config.check_halt_on_memory_access = true;
        return config;
    }
    bool Fail(const std::string& message) {
        last_error_ = message;
        log_ << "ERROR: " << message << '\n';
        log_.flush();
        std::cerr << "DYNARMIC EXECUTION ERROR: " << message << '\n';
        return false;
    }
    void RememberEvent(const std::string& event) {
        if (!recent_events_.empty() && recent_events_.back() == event) return;
        recent_events_.push_back(event);
        while (recent_events_.size() > 16u) recent_events_.pop_front();
    }
    std::string DescribeAddress(u32 address) const {
        address &= ~1u;
        std::ostringstream output;
        if (address >= kImportBase && address < kImportBase + kImportRegionSize) {
            const std::size_t index = static_cast<std::size_t>((address - kImportBase) / 8u);
            if (index < runtime_.imports.size()) {
                output << "import:" << runtime_.imports[index].name;
                return output.str();
            }
        }
        if (address >= kEnvStubs && address < kEnvStubs + kJniTableSize * 8u) {
            output << "JNI-slot-" << ((address - kEnvStubs) / 8u);
            return output.str();
        }
        if (address >= kVmStubs && address < kVmStubs + 8u * 8u) {
            output << "JavaVM-slot-" << ((address - kVmStubs) / 8u);
            return output.str();
        }
        if (address == kReturnStub) return "host-return-stub";
        if (address >= runtime_.image_min && address < runtime_.image_max) {
            const auto found = std::upper_bound(
                runtime_.symbols.begin(), runtime_.symbols.end(), address,
                [](u32 value, const SymbolRecord& symbol) { return value < symbol.address; });
            if (found != runtime_.symbols.begin()) {
                const SymbolRecord& symbol = *std::prev(found);
                const u32 offset = address - symbol.address;
                output << symbol.name;
                if (offset) output << "+0x" << std::hex << offset;
                output << " [ELF+0x" << std::hex << (address - kGameBase) << ']';
                return output.str();
            }
            output << "ELF+0x" << std::hex << (address - kGameBase);
            return output.str();
        }
        if (address >= kHeapBase && address < kHeapBase + kHeapSize) {
            output << "guest-heap+0x" << std::hex << (address - kHeapBase);
            return output.str();
        }
        if (address >= kStackBase && address < kStackBase + kStackSize) {
            output << "guest-stack+0x" << std::hex << (address - kStackBase);
            return output.str();
        }
        output << "0x" << std::hex << address;
        return output.str();
    }
    std::string BuildExecutionDiagnostic(
        const std::string& label, u64 estimated_ticks,
        std::chrono::steady_clock::duration elapsed) {
        std::ostringstream output;
        output << label
               << " elapsed_ms=" << std::fixed << std::setprecision(1)
               << std::chrono::duration<double, std::milli>(elapsed).count()
               << " estimated_ticks=" << estimated_ticks
               << " PC=0x" << std::hex << cpu_.Regs()[15]
               << " (" << DescribeAddress(cpu_.Regs()[15]) << ')'
               << " LR=0x" << cpu_.Regs()[14]
               << " (" << DescribeAddress(cpu_.Regs()[14]) << ')'
               << " SP=0x" << cpu_.Regs()[13]
               << " CPSR=0x" << cpu_.Cpsr() << std::dec;
        if (!recent_events_.empty()) {
            output << " recent={";
            bool first = true;
            for (const std::string& event : recent_events_) {
                if (!first) output << " -> ";
                first = false;
                output << event;
            }
            output << '}';
        }
        std::vector<const ImportRecord*> called;
        called.reserve(runtime_.imports.size());
        for (const ImportRecord& import : runtime_.imports) {
            if (import.calls) called.push_back(&import);
        }
        std::sort(called.begin(), called.end(), [](const ImportRecord* lhs, const ImportRecord* rhs) {
            return lhs->calls > rhs->calls;
        });
        if (!called.empty()) {
            output << " top-imports={";
            const std::size_t count = std::min<std::size_t>(8u, called.size());
            for (std::size_t i = 0; i < count; ++i) {
                if (i) output << ',';
                output << called[i]->name << ':' << called[i]->calls;
            }
            output << '}';
        }
        return output.str();
    }
    static std::string SanitizeLogText(const std::string& text) {
        std::ostringstream output;
        const std::size_t limit = std::min<std::size_t>(text.size(), 96u);
        for (std::size_t index = 0; index < limit; ++index) {
            const unsigned char value = static_cast<unsigned char>(text[index]);
            if (value == '\\' || value == '"') output << '\\' << static_cast<char>(value);
            else if (value >= 0x20u && value < 0x7Fu) output << static_cast<char>(value);
            else output << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned>(value) << std::dec;
        }
        if (text.size() > limit) output << "...";
        return output.str();
    }
    void LogHostDispatch(const std::string& label, u32 function, const std::string& details) {
        ++host_event_sequence_;
        const bool noisy_move = label == "nativeTouchesMove";
        const bool print_event = !noisy_move || host_event_sequence_ <= 32u ||
                                 (host_event_sequence_ % 120u) == 0u;
        std::ostringstream event;
        event << "host#" << host_event_sequence_ << ':' << label;
        if (!details.empty()) event << ' ' << details;
        RememberEvent(event.str());
        if (!print_event) return;
        log_ << "Dynarmic host dispatch #" << host_event_sequence_
             << ": " << label << " guest=0x" << std::hex << function
             << " (" << DescribeAddress(function) << ')' << std::dec;
        if (!details.empty()) log_ << ' ' << details;
        log_ << '\n';
    }
    void DumpFatalGuestState(const std::string& import_name) {
        const u32 pc = cpu_.Regs()[15];
        const u32 lr = cpu_.Regs()[14];
        const u32 sp = cpu_.Regs()[13];
        log_ << "===== DYNARMIC TEST8 GUEST FATAL DIAGNOSTIC =====\n";
        log_ << "Fatal import: " << import_name << '\n';
        log_ << "Active guest call depth: " << active_calls_.size() << '\n';
        if (!active_calls_.empty()) {
            log_ << "Active guest calls:";
            for (const std::string& call : active_calls_) log_ << " -> " << call;
            log_ << '\n';
        }
        log_ << "PC=0x" << std::hex << pc << " (" << DescribeAddress(pc) << ") "
             << "LR=0x" << lr << " (" << DescribeAddress(lr) << ") "
             << "SP=0x" << sp << " CPSR=0x" << cpu_.Cpsr() << std::dec << '\n';
        for (unsigned base = 0; base < 13; base += 4) {
            log_ << "Registers:";
            for (unsigned index = base; index < std::min<unsigned>(base + 4, 13); ++index) {
                log_ << " R" << index << "=0x" << std::hex << std::setw(8)
                     << std::setfill('0') << cpu_.Regs()[index] << std::dec;
            }
            log_ << '\n';
        }
        if (!last_assert_title_.empty() || !last_assert_text_.empty()) {
            log_ << "Last guest message box #" << last_assert_sequence_ << ": "
                 << last_assert_title_ << " | " << last_assert_text_ << '\n';
        } else {
            log_ << "Last guest message box: none recorded\n";
        }
        LogHeapStatus("fatal");
        if (!recent_events_.empty()) {
            log_ << "Recent guest/JNI/host events:\n";
            for (const std::string& event : recent_events_) log_ << "  " << event << '\n';
        }

        constexpr u32 words_before = 8u;
        constexpr u32 words_after = 32u;
        const u32 bytes_before = words_before * 4u;
        const u32 dump_base = sp >= bytes_before ? sp - bytes_before : sp;
        std::array<u32, words_before + words_after> words{};
        if (sp && env_.ReadBytes(dump_base, words.data(), sizeof(words))) {
            log_ << "Guest stack window: " << sizeof(words) << " bytes from 0x"
                 << std::hex << dump_base << " through 0x" << (dump_base + sizeof(words))
                 << std::dec << '\n';
            for (std::size_t index = 0; index < words.size(); ++index) {
                const u32 address = dump_base + static_cast<u32>(index * 4u);
                const s64 relative = static_cast<s64>(address) - static_cast<s64>(sp);
                const u32 value = words[index];
                log_ << "  [SP" << (relative < 0 ? "-" : "+") << "0x"
                     << std::hex << static_cast<u64>(relative < 0 ? -relative : relative)
                     << "] 0x" << std::setw(8) << std::setfill('0') << value;
                if ((value >= runtime_.image_min && value < runtime_.image_max) ||
                    (value >= kImportBase && value < kImportBase + kImportRegionSize) ||
                    value == kReturnStub) {
                    log_ << "  (" << DescribeAddress(value) << ')';
                }
                log_ << std::dec << '\n';
            }
        } else {
            log_ << "Guest stack window: unreadable at SP=0x" << std::hex << sp << std::dec << '\n';
        }
        log_ << "===== END DYNARMIC TEST8 GUEST FATAL DIAGNOSTIC =====\n";
        log_.flush();
    }
    bool FatalImport(const std::string& import_name) {
        last_error_ = "guest called fatal import " + import_name;
        DumpFatalGuestState(import_name);
        std::cerr << "DYNARMIC EXECUTION ERROR: " << last_error_ << '\n';
        return false;
    }
    void InitializeControlTraps() {
        WriteArmSvcStub(env_, kReturnStub, kSvcReturn);
        env_.MemoryWrite32(kVmObject, kVmTable);
        for (u32 index = 0; index < 8; ++index) {
            const u32 stub = kVmStubs + index * 8u;
            env_.MemoryWrite32(kVmTable + index * 4u, stub);
            WriteArmSvcStub(env_, stub, kSvcVmBase + index);
        }
        env_.MemoryWrite32(kEnvObject, kEnvTable);
        for (u32 index = 0; index < kJniTableSize; ++index) {
            const u32 stub = kEnvStubs + index * 8u;
            env_.MemoryWrite32(kEnvTable + index * 4u, stub);
            WriteArmSvcStub(env_, stub, kSvcJniBase + index);
        }
    }
    bool HandleSvc(u32 svc, const std::string& label) {
        if (svc >= kSvcVmBase && svc < kSvcVmBase + 8u) {
            RememberEvent("JavaVM-slot-" + std::to_string(svc - kSvcVmBase));
            return HandleVm(svc - kSvcVmBase);
        }
        if (svc >= kSvcJniBase && svc < kSvcJniBase + kJniTableSize) {
            RememberEvent("JNI-slot-" + std::to_string(svc - kSvcJniBase));
            return HandleJni(svc - kSvcJniBase);
        }
        if (svc == 0 || svc > runtime_.imports.size()) {
            std::ostringstream error;
            error << label << " unknown SVC 0x" << std::hex << svc;
            return Fail(error.str());
        }
        ImportRecord& import = runtime_.imports[svc - 1u];
        ++import.calls;
        ++total_import_calls_;
        // Recording a heap-allocated string for every libc trap was itself a
        // major cost during ZIP scans. Sample normal imports, but always retain
        // fatal/exception-related calls for diagnostics.
        const bool diagnostic_import = import.name == "abort" || import.name == "exit" ||
            import.name == "__stack_chk_fail" || import.name == "longjmp" ||
            import.name == "siglongjmp";
        if (diagnostic_import || (total_import_calls_ & 0x0fffu) == 0u)
            RememberEvent("import:" + import.name);

        // Sample one host dispatch out of every 1024 calls per import. This
        // identifies expensive bridges without putting a clock read around
        // every libc/OpenGL trap on low-end systems.
        if ((import.calls & 0x03ffu) == 1u) {
            const auto host_started = std::chrono::steady_clock::now();
            const bool ok = DispatchImport(import);
            const auto host_elapsed =
                std::chrono::steady_clock::now() - host_started;
            import.sampled_host_nanoseconds += static_cast<u64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    host_elapsed).count());
            ++import.sampled_host_calls;
            return ok;
        }
        return DispatchImport(import);
    }
    void ResumeAfterStub(u32 stub_address) {
        cpu_.Regs()[15] = stub_address + 4u;
        cpu_.SetCpsr(cpu_.Cpsr() & ~0x20u);
    }
    bool HandleVm(u32 index) {
        const u32 output = cpu_.Regs()[1];
        const u32 version = cpu_.Regs()[2];
        u32 result = 0;
        if (index == 4 || index == 7) {
            if (output) env_.MemoryWrite32(output, kEnvObject);
        } else if (index == 6) {
            if (version > kJniVersion14) result = static_cast<u32>(-2);
            else if (output) env_.MemoryWrite32(output, kEnvObject);
        }
        cpu_.Regs()[0] = result;
        ResumeAfterStub(kVmStubs + index * 8u);
        return true;
    }

    static bool IsPowerOfTwo(u32 value) {
        return value != 0u && (value & (value - 1u)) == 0u;
    }

    u32 AlignAddress(u32 value, u32 alignment) const {
        if (!IsPowerOfTwo(alignment) || value > std::numeric_limits<u32>::max() - (alignment - 1u)) return 0;
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    void AccountAllocation(u32 size) {
        live_allocation_bytes_ += size;
        peak_live_allocation_bytes_ = std::max(peak_live_allocation_bytes_, live_allocation_bytes_);
        ++allocation_calls_;
    }

    void AccountFree(u32 size) {
        live_allocation_bytes_ = size > live_allocation_bytes_ ? 0u : live_allocation_bytes_ - size;
        ++free_calls_;
    }

    void AddFreeBlock(u32 address, u32 size) {
        if (!size) return;
        auto next = free_blocks_.lower_bound(address);
        if (next != free_blocks_.begin()) {
            auto previous = std::prev(next);
            if (static_cast<u64>(previous->first) + previous->second == address) {
                address = previous->first;
                size += previous->second;
                free_blocks_.erase(previous);
            }
        }
        next = free_blocks_.lower_bound(address);
        if (next != free_blocks_.end() && static_cast<u64>(address) + size == next->first) {
            size += next->second;
            free_blocks_.erase(next);
        }
        free_blocks_[address] = size;

        // Return free blocks at the top of the arena to the bump cursor.
        for (;;) {
            auto upper = free_blocks_.upper_bound(heap_cursor_);
            if (upper == free_blocks_.begin()) break;
            auto top = std::prev(upper);
            if (static_cast<u64>(top->first) + top->second != heap_cursor_) break;
            heap_cursor_ = top->first;
            free_blocks_.erase(top);
        }
    }

    u32 AllocateAligned(u32 requested, u32 alignment) {
        const u32 size = std::max<u32>(requested, 1u);
        alignment = std::max<u32>(alignment, 16u);
        if (!IsPowerOfTwo(alignment) || size > std::numeric_limits<u32>::max() - 15u) return 0;
        const u32 aligned_size = (size + 15u) & ~15u;

        // Best-fit reuse avoids wasting large image buffers on tiny C++ allocations.
        auto best = free_blocks_.end();
        u32 best_address = 0;
        u32 best_waste = std::numeric_limits<u32>::max();
        for (auto it = free_blocks_.begin(); it != free_blocks_.end(); ++it) {
            const u32 candidate = AlignAddress(it->first, alignment);
            if (!candidate || candidate < it->first) continue;
            const u64 prefix = static_cast<u64>(candidate) - it->first;
            const u64 needed = prefix + aligned_size;
            if (needed > it->second) continue;
            const u32 waste = it->second - static_cast<u32>(needed);
            if (waste < best_waste) {
                best = it;
                best_address = candidate;
                best_waste = waste;
                if (waste == 0u && prefix == 0u) break;
            }
        }
        if (best != free_blocks_.end()) {
            const u32 block_address = best->first;
            const u32 block_size = best->second;
            free_blocks_.erase(best);
            const u32 prefix = best_address - block_address;
            const u32 suffix_address = best_address + aligned_size;
            const u32 suffix = block_size - prefix - aligned_size;
            if (prefix) AddFreeBlock(block_address, prefix);
            if (suffix) AddFreeBlock(suffix_address, suffix);
            allocations_[best_address] = aligned_size;
            AccountAllocation(aligned_size);
            return best_address;
        }

        const u32 address = AlignAddress(heap_cursor_, alignment);
        if (!address || address < heap_cursor_) return 0;
        const u32 prefix = address - heap_cursor_;
        const u64 end = static_cast<u64>(address) + aligned_size;
        const u64 heap_end = static_cast<u64>(kHeapBase) + kHeapSize;
        if (end <= heap_end) {
            if (prefix) free_blocks_[heap_cursor_] = prefix;
            heap_cursor_ = static_cast<u32>(end);
            allocations_[address] = aligned_size;
            AccountAllocation(aligned_size);
            return address;
        }

        ++allocation_failures_;
        if (allocation_failures_ <= 16u) {
            u64 free_total = 0;
            u32 largest_free = 0;
            for (const auto& [free_address, free_size] : free_blocks_) {
                (void)free_address;
                free_total += free_size;
                largest_free = std::max(largest_free, free_size);
            }
            log_ << "Dynarmic guest allocation failed: requested=" << requested
                 << " aligned=" << aligned_size
                 << " alignment=" << alignment
                 << " cursor=0x" << std::hex << heap_cursor_ << std::dec
                 << " arena_used=" << (heap_cursor_ - kHeapBase) << '/' << kHeapSize
                 << " live=" << live_allocation_bytes_
                 << " peak_live=" << peak_live_allocation_bytes_
                 << " allocations=" << allocations_.size()
                 << " free_blocks=" << free_blocks_.size()
                 << " free_bytes=" << free_total
                 << " largest_free=" << largest_free << '\n';
            log_.flush();
        }
        return 0;
    }

    u32 Allocate(u32 requested) {
        return AllocateAligned(requested, 16u);
    }

    void Free(u32 address) {
        if (!address) return;
        const auto found = allocations_.find(address);
        if (found == allocations_.end()) {
            ++ignored_free_calls_;
            if (ignored_free_calls_ <= 8u) {
                log_ << "Dynarmic guest free ignored unknown pointer 0x" << std::hex << address << std::dec << '\n';
                log_.flush();
            }
            return;
        }
        const u32 size = found->second;
        allocations_.erase(found);
        AccountFree(size);
        AddFreeBlock(address, size);
    }

    u32 Reallocate(u32 address, u32 requested) {
        ++reallocation_calls_;
        if (!address) return Allocate(requested);
        if (!requested) {
            Free(address);
            return 0;
        }
        const auto found = allocations_.find(address);
        if (found == allocations_.end()) return 0;
        if (requested > std::numeric_limits<u32>::max() - 15u) return 0;
        const u32 old_size = found->second;
        const u32 new_size = (std::max<u32>(requested, 1u) + 15u) & ~15u;
        if (new_size == old_size) return address;

        if (new_size < old_size) {
            found->second = new_size;
            const u32 released = old_size - new_size;
            live_allocation_bytes_ -= released;
            AddFreeBlock(address + new_size, released);
            return address;
        }

        const u32 extra = new_size - old_size;
        const u32 adjacent_address = address + old_size;
        auto adjacent = free_blocks_.find(adjacent_address);
        if (adjacent != free_blocks_.end() && adjacent->second >= extra) {
            const u32 remaining = adjacent->second - extra;
            free_blocks_.erase(adjacent);
            if (remaining) free_blocks_[adjacent_address + extra] = remaining;
            found->second = new_size;
            live_allocation_bytes_ += extra;
            peak_live_allocation_bytes_ = std::max(peak_live_allocation_bytes_, live_allocation_bytes_);
            return address;
        }
        if (adjacent_address == heap_cursor_ &&
            static_cast<u64>(heap_cursor_) + extra <= static_cast<u64>(kHeapBase) + kHeapSize) {
            heap_cursor_ += extra;
            found->second = new_size;
            live_allocation_bytes_ += extra;
            peak_live_allocation_bytes_ = std::max(peak_live_allocation_bytes_, live_allocation_bytes_);
            return address;
        }

        const u32 replacement = Allocate(requested);
        if (!replacement) return 0;
        if (!CopyGuest(replacement, address, std::min(old_size, requested))) {
            Free(replacement);
            return 0;
        }
        Free(address);
        return replacement;
    }

    void LogHeapStatus(const char* reason) {
        u64 free_total = 0;
        u32 largest_free = 0;
        for (const auto& [address, size] : free_blocks_) {
            (void)address;
            free_total += size;
            largest_free = std::max(largest_free, size);
        }
        log_ << "Dynarmic guest heap [" << reason << "]: arena_used="
             << (heap_cursor_ - kHeapBase) << '/' << kHeapSize
             << " live=" << live_allocation_bytes_
             << " peak_live=" << peak_live_allocation_bytes_
             << " allocations=" << allocations_.size()
             << " free_blocks=" << free_blocks_.size()
             << " free_bytes=" << free_total
             << " largest_free=" << largest_free
             << " alloc/free/realloc=" << allocation_calls_ << '/' << free_calls_ << '/' << reallocation_calls_
             << " failures=" << allocation_failures_ << '\n';
        log_.flush();
    }

    u32 AllocateString(const std::string& value) {
        const u32 address = Allocate(static_cast<u32>(value.size() + 1u));
        if (address) env_.WriteBytes(address, value.c_str(), value.size() + 1u);
        return address;
    }
    bool CopyGuest(u32 destination, u32 source, u32 size) {
        if (!size) return true;
        if (const void* input = env_.HostPointer(source, size)) {
            if (void* output = env_.HostPointer(destination, size)) {
                std::memmove(output, input, size);
                return true;
            }
        }
        std::vector<u8> temporary(size);
        return env_.ReadBytes(source, temporary.data(), size) && env_.WriteBytes(destination, temporary.data(), size);
    }
    u32 CStringLength(u32 address) const {
        std::size_t available = 0;
        const u8* bytes = env_.HostPointerToRegionEnd(address, available);
        if (!bytes) return 0;
        const std::size_t maximum = std::min<std::size_t>(available, 1u << 20);
        const void* end = std::memchr(bytes, 0, maximum);
        return end ? static_cast<u32>(static_cast<const u8*>(end) - bytes) : 0;
    }
    std::string ReadCString(u32 address, std::size_t maximum = 1u << 20) const {
        std::string text;
        if (!env_.ReadCString(address, text, maximum)) return {};
        return text;
    }
    u32 ArgWord(unsigned index) {
        if (index < 4u) return cpu_.Regs()[index];
        return env_.MemoryRead32(cpu_.Regs()[13] + (index - 4u) * 4u);
    }
    u64 ArgU64(unsigned index) {
        if (index & 1u) ++index;
        return JoinU64(ArgWord(index), ArgWord(index + 1u));
    }

    GuestRef* FindRef(u32 handle) {
        for (auto& ref : refs_) if (ref.handle == handle) return &ref;
        return nullptr;
    }
    const GuestRef* FindRef(u32 handle) const {
        for (const auto& ref : refs_) if (ref.handle == handle) return &ref;
        return nullptr;
    }
    u32 NewRef(RefKind kind) {
        GuestRef ref;
        ref.handle = kFakeRefBase + static_cast<u32>((refs_.size() + 1u) * 0x20u);
        ref.kind = kind;
        refs_.push_back(std::move(ref));
        return refs_.back().handle;
    }
    u32 NewClassRef(const std::string& name) {
        for (const auto& ref : refs_) {
            if (ref.kind == RefKind::Class && ref.class_name == name) return ref.handle;
        }
        const u32 handle = NewRef(RefKind::Class);
        GuestRef* ref = FindRef(handle);
        if (!ref) return 0;
        ref->class_name = name;
        log_ << "JNI FindClass: " << name << '\n';
        return handle;
    }
    u32 NewMethodRef(u32 class_handle, const std::string& name, const std::string& signature) {
        const GuestRef* class_ref = FindRef(class_handle);
        const std::string class_name = class_ref ? class_ref->class_name : "?";
        for (const auto& ref : refs_) {
            if (ref.kind == RefKind::Method && ref.class_name == class_name && ref.name == name && ref.signature == signature) return ref.handle;
        }
        const u32 handle = NewRef(RefKind::Method);
        GuestRef* ref = FindRef(handle);
        if (!ref) return 0;
        ref->class_name = class_name;
        ref->name = name;
        ref->signature = signature;
        log_ << "JNI method: " << class_name << '.' << name << ' ' << signature << '\n';
        return handle;
    }
    std::string RefString(u32 handle) const {
        const GuestRef* ref = FindRef(handle);
        if (!ref || ref->kind != RefKind::String) return {};
        return ReadCString(ref->data_address, ref->length + 1u);
    }
    void LogFirstMethodCall(GuestRef* method) {
        if (!method) return;
        if (method->calls++ == 0) {
            log_ << "JNI call: " << method->class_name << '.' << method->name << ' ' << method->signature << '\n';
        }
    }
    u32 NewArrayRef(RefKind kind, u32 length, u32 element_size) {
        const u64 bytes = static_cast<u64>(length) * element_size;
        if (bytes > std::numeric_limits<u32>::max()) return 0;
        const u32 handle = NewRef(kind);
        GuestRef* ref = FindRef(handle);
        if (!ref) return 0;
        ref->length = length;
        if (bytes) {
            ref->data_address = Allocate(static_cast<u32>(bytes));
            if (!ref->data_address) return 0;
            std::vector<u8> zeros(static_cast<std::size_t>(bytes));
            env_.WriteBytes(ref->data_address, zeros.data(), zeros.size());
        }
        return handle;
    }

    class ArgCursor {
    public:
        ArgCursor(GuestExecutor& owner, unsigned word_position, u32 direct_address = 0, bool jvalue_array = false)
            : owner_(owner), word_position_(word_position), direct_address_(direct_address), jvalue_array_(jvalue_array) {}
        u32 Word() {
            if (direct_address_) {
                const u32 result = owner_.env_.MemoryRead32(direct_address_);
                direct_address_ += jvalue_array_ ? 8u : 4u;
                return result;
            }
            return owner_.ArgWord(word_position_++);
        }
        u64 U64() {
            if (direct_address_) {
                if (!jvalue_array_) direct_address_ = AlignUp(direct_address_, 8u);
                const u64 result = JoinU64(owner_.env_.MemoryRead32(direct_address_), owner_.env_.MemoryRead32(direct_address_ + 4u));
                direct_address_ += 8u;
                return result;
            }
            if (word_position_ & 1u) ++word_position_;
            const u32 low = Word();
            const u32 high = Word();
            return JoinU64(low, high);
        }
        float FloatArgument() {
            if (jvalue_array_ && direct_address_) {
                const u32 bits = owner_.env_.MemoryRead32(direct_address_);
                direct_address_ += 8u;
                return WordToFloat(bits);
            }
            const u64 bits = U64();
            return static_cast<float>(WordsToDouble(static_cast<u32>(bits), static_cast<u32>(bits >> 32)));
        }
    private:
        GuestExecutor& owner_;
        unsigned word_position_ = 0;
        u32 direct_address_ = 0;
        bool jvalue_array_ = false;
    };

    static bool JniIsVIndex(u32 index) {
        switch (index) {
        case 35: case 38: case 50: case 56: case 59: case 62:
        case 115: case 118: case 130: case 136: case 139: case 142: return true;
        default: return false;
        }
    }
    static bool JniIsAIndex(u32 index) {
        switch (index) {
        case 36: case 39: case 51: case 57: case 60: case 63:
        case 116: case 119: case 131: case 137: case 140: case 143: return true;
        default: return false;
        }
    }

    u32 DispatchJniObject(GuestRef* method, ArgCursor& arguments) {
        if (!method) return 0;
        LogFirstMethodCall(method);
        const std::string& name = method->name;
        if (name == "getCocos2dxPackageName" || name == "getPackageName") return NewStringRef("com.robtopx.geometryjump");
        if (name == "getCocos2dxWritablePath") return NewStringRef("/save");
        if (name == "getCurrentLanguage") return NewStringRef("en");
        if (name == "getDeviceModel") return NewStringRef("Windows x64 Dynarmic");
        if (name == "getUserID") return NewStringRef("57494e41524d3031");
        if (name == "getStringForKey") {
            const std::string key = RefString(arguments.Word());
            const std::string fallback = RefString(arguments.Word());
            char* value = storage_get_string_copy(key.c_str(), fallback.c_str());
            const u32 result = NewStringRef(value ? value : "");
            std::free(value);
            return result;
        }
        if (name == "getStringWithEllipsis") return arguments.Word();
        if (name == "loadAndDecryptFileToString") {
            const std::string path = RefString(arguments.Word());
            char* value = storage_read_game_file(path.c_str(), nullptr);
            const u32 result = NewStringRef(value ? value : "");
            std::free(value);
            return result;
        }
        if (name == "getItem") return NewStringRef("");
        return NewRef(RefKind::Object);
    }
    u32 DispatchJniBoolean(GuestRef* method, ArgCursor& arguments) {
        if (!method) return 0;
        LogFirstMethodCall(method);
        const std::string& name = method->name;
        if (name == "getBoolForKey") {
            const std::string key = RefString(arguments.Word());
            return static_cast<u32>(storage_get_bool(key.c_str(), static_cast<int>(arguments.Word())));
        }
        if (name == "shouldResumeSound" || name == "isNetworkAvailable") return 1;
        if (name == "doesFileExist") {
            const std::string path = RefString(arguments.Word());
            return static_cast<u32>(storage_file_exists(path.c_str()));
        }
        if (name == "isBackgroundMusicPlaying") return audio_is_background_playing() ? 1u : 0u;
        if (name == "isEffectPlaying") return audio_is_effect_playing(arguments.Word()) ? 1u : 0u;
        return 0;
    }
    u32 DispatchJniInt(GuestRef* method, ArgCursor& arguments) {
        if (!method) return 0;
        LogFirstMethodCall(method);
        const std::string& name = method->name;
        if (name == "getDPI") return 96;
        if (name == "getIntegerForKey") {
            const std::string key = RefString(arguments.Word());
            return static_cast<u32>(storage_get_integer(key.c_str(), static_cast<s32>(arguments.Word())));
        }
        if (name == "getFontSizeAccordingHeight") return arguments.Word();
        if (name == "playEffect") {
            const std::string path = RefString(arguments.Word());
            const bool loop = arguments.Word() != 0;
            const float pitch = arguments.FloatArgument();
            const float pan = arguments.FloatArgument();
            const float gain = arguments.FloatArgument();
            (void)pitch;
            (void)pan;
            return audio_play_effect_ex(path.c_str(), loop ? 1 : 0,
                                        pitch, pan, gain);
        }
        return 0;
    }
    u32 DispatchJniFloat(GuestRef* method, ArgCursor& arguments) {
        if (!method) return 0;
        LogFirstMethodCall(method);
        float result = 0.0f;
        if (method->name == "getFloatForKey") {
            const std::string key = RefString(arguments.Word());
            result = storage_get_float(key.c_str(), arguments.FloatArgument());
        } else if (method->name == "getBackgroundMusicVolume") result = audio_get_background_volume();
        else if (method->name == "getEffectsVolume") result = audio_get_effects_volume();
        else if (method->name == "getBackgroundMusicTime") result = audio_get_background_time();
        return FloatToWord(result);
    }
    u64 DispatchJniDouble(GuestRef* method, ArgCursor& arguments) {
        if (!method) return 0;
        LogFirstMethodCall(method);
        double result = 0.0;
        if (method->name == "getDoubleForKey") {
            const std::string key = RefString(arguments.Word());
            const u64 fallback_bits = arguments.U64();
            result = storage_get_double(key.c_str(), WordsToDouble(static_cast<u32>(fallback_bits), static_cast<u32>(fallback_bits >> 32)));
        }
        u64 bits = 0;
        std::memcpy(&bits, &result, sizeof(bits));
        return bits;
    }
#ifdef _WIN32
    bool OpenExternalUrl(const std::string& url) {
        const bool allowed = url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
        if (!allowed) {
            log_ << "[host] Browser open rejected unsupported URL: " << SanitizeLogText(url) << '\n';
            return false;
        }
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                                  url.c_str(), static_cast<int>(url.size()),
                                                  nullptr, 0);
        if (required <= 0) {
            log_ << "[host] Browser open failed UTF-8 conversion url=" << SanitizeLogText(url) << '\n';
            return false;
        }
        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url.c_str(),
                            static_cast<int>(url.size()), wide.data(), required);
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(),
                                               nullptr, nullptr, SW_SHOWNORMAL);
        const auto shell_code = reinterpret_cast<INT_PTR>(result);
        const bool ok = shell_code > 32;
        log_ << "[host] Browser open url=" << SanitizeLogText(url)
             << " result=" << (ok ? "ok" : "failed")
             << " shell_code=" << shell_code << '\n';
        return ok;
    }
#else
    bool OpenExternalUrl(const std::string&) { return false; }
#endif

    void DispatchJniVoid(GuestRef* method, ArgCursor& arguments) {
        if (!method) return;
        LogFirstMethodCall(method);
        const std::string& name = method->name;
        if (name == "setAnimationInterval") {
            const u64 bits = arguments.U64();
            const double interval = WordsToDouble(static_cast<u32>(bits), static_cast<u32>(bits >> 32));
            if (interval > 0.001 && interval < 1.0) frame_interval_ = interval;
        } else if (name == "setStringForKey") {
            const std::string key = RefString(arguments.Word());
            const std::string value = RefString(arguments.Word());
            storage_set_string(key.c_str(), value.c_str());
        } else if (name == "setBoolForKey") {
            const std::string key = RefString(arguments.Word());
            storage_set_bool(key.c_str(), static_cast<int>(arguments.Word()));
        } else if (name == "setIntegerForKey") {
            const std::string key = RefString(arguments.Word());
            storage_set_integer(key.c_str(), static_cast<s32>(arguments.Word()));
        } else if (name == "setFloatForKey") {
            const std::string key = RefString(arguments.Word());
            storage_set_float(key.c_str(), arguments.FloatArgument());
        } else if (name == "setDoubleForKey") {
            const std::string key = RefString(arguments.Word());
            const u64 bits = arguments.U64();
            storage_set_double(key.c_str(), WordsToDouble(static_cast<u32>(bits), static_cast<u32>(bits >> 32)));
        } else if (name == "saveAndEncryptStringToFile") {
            const u32 value_ref = arguments.Word();
            const std::string value = RefString(value_ref);
            const std::string path = RefString(arguments.Word());
            storage_write_game_file(path.c_str(), value.data(), value.size());
        } else if (name == "playBackgroundMusic") {
            const std::string path = RefString(arguments.Word());
            const bool loop = arguments.Word() != 0;
            audio_play_background(path.c_str(), loop ? 1 : 0);
        } else if (name == "stopBackgroundMusic") audio_stop_background();
        else if (name == "pauseBackgroundMusic") audio_pause_background();
        else if (name == "resumeBackgroundMusic") audio_resume_background();
        else if (name == "rewindBackgroundMusic") audio_rewind_background();
        else if (name == "resumeBackgroundMusicFrom") audio_resume_background_from(arguments.FloatArgument());
        else if (name == "setBackgroundMusicTime") audio_set_background_time(arguments.FloatArgument());
        else if (name == "setBackgroundMusicVolume") audio_set_background_volume(arguments.FloatArgument());
        else if (name == "setEffectsVolume") audio_set_effects_volume(arguments.FloatArgument());
        else if (name == "preloadBackgroundMusic") {
            const std::string path = RefString(arguments.Word());
            audio_preload_background(path.c_str());
        } else if (name == "preloadEffect") {
            const std::string path = RefString(arguments.Word());
            audio_preload_effect(path.c_str());
        } else if (name == "unloadEffect") {
            const std::string path = RefString(arguments.Word());
            audio_unload_effect(path.c_str());
        } else if (name == "pauseAllEffects") audio_pause_all_effects();
        else if (name == "resumeAllEffects") audio_resume_all_effects();
        else if (name == "stopAllEffects") audio_stop_all_effects();
        else if (name == "pauseEffect") audio_pause_effect(arguments.Word());
        else if (name == "resumeEffect") audio_resume_effect(arguments.Word());
        else if (name == "stopEffect") audio_stop_effect(arguments.Word());
        else if (name == "setEffectVolume") {
            const u32 identifier = arguments.Word();
            audio_set_effect_volume(identifier, arguments.FloatArgument());
        } else if (name == "openURL") {
            const std::string url = RefString(arguments.Word());
            RememberEvent("JNI:openURL " + url);
            OpenExternalUrl(url);
        } else if (name == "showMessageBox") {
            const std::string title = RefString(arguments.Word());
            const std::string text = RefString(arguments.Word());
            last_assert_title_ = title;
            last_assert_text_ = text;
            last_assert_sequence_ = ++message_box_count_;
            RememberEvent("message-box:" + title + " | " + text);
            log_ << "JNI message box #" << last_assert_sequence_ << ": " << title << " | " << text << '\n';
        } else if (name == "openIMEKeyboard" || name == "showEditTextDialog") {
            text_input_active_ = true;
            gl_.SetTextInputActive(true);
            log_ << "Dynarmic text input active\n";
        } else if (name == "closeIMEKeyboard") {
            text_input_active_ = false;
            gl_.SetTextInputActive(false);
            log_ << "Dynarmic text input inactive\n";
        } else if (name == "setKeyboardState") {
            text_input_active_ = arguments.Word() != 0;
            gl_.SetTextInputActive(text_input_active_);
            log_ << "Dynarmic text input " << (text_input_active_ ? "active" : "inactive") << '\n';
        } else if (name == "terminateProcess") {
            termination_requested_ = true;
            gl_.RequestClose();
            audio_stop_all_effects();
            audio_stop_background();
            RememberEvent("JNI:terminateProcess requested clean shutdown");
            log_ << "Dynarmic clean shutdown requested by guest terminateProcess\n";
        } else {
            // The remaining Android activity calls are safe no-ops for the first-frame milestone.
        }
    }

    bool HandleJni(u32 index) {
        ++jni_svc_calls_;
        const u32 r1 = cpu_.Regs()[1];
        const u32 r2 = cpu_.Regs()[2];
        const u32 r3 = cpu_.Regs()[3];
        const bool v_mode = JniIsVIndex(index);
        const bool a_mode = JniIsAIndex(index);
        ArgCursor arguments(*this, 3u, (v_mode || a_mode) ? r3 : 0u, a_mode);
        if (const GuestRef* event_method = FindRef(r2);
            event_method && event_method->kind == RefKind::Method) {
            RememberEvent("JNI:" + event_method->class_name + "." + event_method->name);
        }
        u32 result = 0;
        switch (index) {
        case 4: result = kJniVersion14; break;
        case 6: result = NewClassRef(ReadCString(r1)); break;
        case 15: case 17: case 228: result = 0; break;
        case 21: case 25: result = r1; break;
        case 22: case 23: result = 0; break;
        case 24: result = r1 == r2; break;
        case 28: case 29: case 30: {
            result = NewRef(RefKind::Object);
            GuestRef* object = FindRef(result);
            const GuestRef* klass = FindRef(r1);
            if (object && klass) object->class_name = klass->class_name;
            break;
        }
        case 31: {
            const GuestRef* object = FindRef(r1);
            result = NewClassRef(object && !object->class_name.empty() ? object->class_name : "java/lang/Object");
            break;
        }
        case 32: result = 1; break;
        case 33: case 113: result = NewMethodRef(r1, ReadCString(r2), ReadCString(r3)); break;
        case 34: case 35: case 36: case 114: case 115: case 116:
            result = DispatchJniObject(FindRef(r2), arguments); break;
        case 37: case 38: case 39: case 117: case 118: case 119:
            result = DispatchJniBoolean(FindRef(r2), arguments); break;
        case 49: case 50: case 51: case 129: case 130: case 131:
            result = DispatchJniInt(FindRef(r2), arguments); break;
        case 55: case 56: case 57: case 135: case 136: case 137:
            result = DispatchJniFloat(FindRef(r2), arguments); break;
        case 58: case 59: case 60: case 138: case 139: case 140: {
            const u64 value = DispatchJniDouble(FindRef(r2), arguments);
            cpu_.Regs()[0] = static_cast<u32>(value);
            cpu_.Regs()[1] = static_cast<u32>(value >> 32);
            ResumeAfterStub(kEnvStubs + index * 8u);
            return true;
        }
        case 61: case 62: case 63: case 141: case 142: case 143:
            DispatchJniVoid(FindRef(r2), arguments); result = 0; break;
        case 167: result = NewStringRef(ReadCString(r1)); break;
        case 168: { const GuestRef* ref = FindRef(r1); result = ref && ref->kind == RefKind::String ? ref->length : 0; break; }
        case 169: {
            const GuestRef* ref = FindRef(r1);
            if (r2) env_.MemoryWrite8(r2, 0);
            result = ref && ref->kind == RefKind::String ? ref->data_address : 0;
            break;
        }
        case 170: result = 0; break;
        case 171: { const GuestRef* ref = FindRef(r1); result = ref ? ref->length : 0; break; }
        case 176: result = NewArrayRef(RefKind::ByteArray, r1, 1); break;
        case 179: result = NewArrayRef(RefKind::IntArray, r1, 4); break;
        case 181: result = NewArrayRef(RefKind::FloatArray, r1, 4); break;
        case 184: case 187: case 189: {
            const GuestRef* ref = FindRef(r1);
            if (r2) env_.MemoryWrite8(r2, 0);
            result = ref ? ref->data_address : 0;
            break;
        }
        case 192: case 195: case 197: result = 0; break;
        case 200: case 203: case 205: {
            const GuestRef* ref = FindRef(r1);
            const u32 element_size = index == 200 ? 1u : 4u;
            const u32 output = env_.MemoryRead32(cpu_.Regs()[13]);
            if (ref && output && r2 <= ref->length && r3 <= ref->length - r2)
                CopyGuest(output, ref->data_address + r2 * element_size, r3 * element_size);
            result = 0;
            break;
        }
        case 208: case 211: case 213: {
            GuestRef* ref = FindRef(r1);
            const u32 element_size = index == 208 ? 1u : 4u;
            const u32 input = env_.MemoryRead32(cpu_.Regs()[13]);
            if (ref && input && r2 <= ref->length && r3 <= ref->length - r2)
                CopyGuest(ref->data_address + r2 * element_size, input, r3 * element_size);
            result = 0;
            break;
        }
        case 215: result = 0; break;
        case 216: result = 0; break;
        case 219: if (r1) env_.MemoryWrite32(r1, kVmObject); result = 0; break;
        default:
            if (unimplemented_jni_slots_.insert(index).second) log_ << "JNI unimplemented table slot " << index << '\n';
            result = 0;
            break;
        }
        cpu_.Regs()[0] = result;
        ResumeAfterStub(kEnvStubs + index * 8u);
        return true;
    }

    int CompareStrings(u32 left, u32 right, u32 maximum, bool limited, bool insensitive) const {
        std::size_t left_available = 0, right_available = 0;
        const u8* a = env_.HostPointerToRegionEnd(left, left_available);
        const u8* b = env_.HostPointerToRegionEnd(right, right_available);
        if (!a || !b) return 0;
        std::size_t limit = std::min(left_available, right_available);
        limit = std::min<std::size_t>(limit, 1u << 20);
        if (limited) limit = std::min<std::size_t>(limit, maximum);
        for (std::size_t index = 0; index < limit; ++index) {
            unsigned char av = a[index], bv = b[index];
            if (insensitive) {
                av = static_cast<unsigned char>(std::tolower(av));
                bv = static_cast<unsigned char>(std::tolower(bv));
            }
            if (av != bv) return av < bv ? -1 : 1;
            if (av == 0) return 0;
        }
        if (limited && limit == maximum) return 0;
        if (left_available == right_available) return 0;
        return left_available < right_available ? -1 : 1;
    }

    bool CallNestedInitializer(u32 control, u32 initializer) {
        if (!initializer) { env_.MemoryWrite32(control, 2u); return true; }
        const auto saved_regs = cpu_.Regs();
        const auto saved_ext = cpu_.ExtRegs();
        const u32 saved_cpsr = cpu_.Cpsr();
        const u32 saved_fpscr = cpu_.Fpscr();
        env_.MemoryWrite32(control, 1u);
        u32 ignored = 0;
        const bool ok = RunFunction(initializer, {}, &ignored, "pthread_once initializer");
        cpu_.Regs() = saved_regs;
        cpu_.ExtRegs() = saved_ext;
        cpu_.SetCpsr(saved_cpsr);
        cpu_.SetFpscr(saved_fpscr);
        if (!ok) return false;
        env_.MemoryWrite32(control, 2u);
        return true;
    }
    void ReturnU64(u64 value) {
        cpu_.Regs()[0] = static_cast<u32>(value);
        cpu_.Regs()[1] = static_cast<u32>(value >> 32);
    }
    void ReturnDouble(double value) {
        const auto words = DoubleToWords(value);
        cpu_.Regs()[0] = words.first;
        cpu_.Regs()[1] = words.second;
    }

    u16 GuestFileFlagsForMode(const std::string& mode) const {
        const bool read = mode.find('r') != std::string::npos ||
                          mode.find('+') != std::string::npos;
        const bool write = mode.find('w') != std::string::npos ||
                           mode.find('a') != std::string::npos ||
                           mode.find('+') != std::string::npos;
        if (read && write) return kBionicFileReadWriteFlag;
        return write ? kBionicFileWriteFlag : kBionicFileReadFlag;
    }
    u32 NewGuestFile(std::FILE* stream, bool standard, const std::string& path,
                     const std::string& mode) {
        const u32 handle = AllocateAligned(kGuestFileObjectSize, 16u);
        if (!handle) {
            if (stream && !standard) std::fclose(stream);
            return 0;
        }
        std::array<u8, kGuestFileObjectSize> layout{};
        env_.WriteBytes(handle, layout.data(), layout.size());
        env_.MemoryWrite16(handle + kBionicFileFlagsOffset, GuestFileFlagsForMode(mode));
        env_.MemoryWrite16(handle + kBionicFileDescriptorOffset,
                           static_cast<u16>(next_file_id_ & 0x7fffu));
        files_[handle] = GuestFile{handle, stream, nullptr, 0u, standard, path, mode};
        ++next_file_id_;
        return handle;
    }
    GuestFile* FindGuestFile(u32 handle) {
        const auto found = files_.find(handle);
        if (found != files_.end()) return &found->second;
        // Bionic's imported __sF entries may arrive as addresses instead of our rewritten handles.
        for (const auto& object : runtime_.objects) {
            if (object.name != "__sF") continue;
            if (handle >= object.address && handle < object.address + kPageSize) {
                const u32 offset = handle - object.address;
                if (offset < 84u) return FindGuestFile(stdin_handle_);
                if (offset < 168u) return FindGuestFile(stdout_handle_);
                return FindGuestFile(stderr_handle_);
            }
        }
        return nullptr;
    }
    std::string TranslatePath(const std::string& input) const {
        if (input.empty()) return input;
        std::string normalized = input;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::string apk_normalized = apk_path_;
        std::replace(apk_normalized.begin(), apk_normalized.end(), '\\', '/');
        if (normalized == apk_normalized || normalized == "game.apk" || normalized.ends_with("/game.apk")) return apk_path_;
        const std::string data_prefix = "/data/data/com.robtopx.geometryjump/";
        if (normalized.starts_with(data_prefix)) return writable_path_ + "\\" + normalized.substr(data_prefix.size());
        if (normalized == "/save" || normalized == "/save/") return writable_path_;
        if (normalized.starts_with("/save/")) return writable_path_ + "\\" + normalized.substr(6);
#ifdef _WIN32
        if (normalized.size() >= 2 && normalized[1] == ':') return input;
#endif
        return input;
    }
    u32 OpenGuestFile(u32 path_address, u32 mode_address) {
        const std::string guest_path = ReadCString(path_address);
        const std::string mode = ReadCString(mode_address, 32);
        if (guest_path.empty() || mode.empty()) return 0;
        const std::string host_path = TranslatePath(guest_path);
#ifdef _WIN32
        if (mode.find('w') != std::string::npos || mode.find('a') != std::string::npos) {
            const std::filesystem::path parent = std::filesystem::path(host_path).parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
        }
#endif
        const bool read_only = mode.find('r') != std::string::npos &&
                               mode.find('w') == std::string::npos &&
                               mode.find('a') == std::string::npos &&
                               mode.find('+') == std::string::npos;
        if (read_only && apk_image_ && host_path == apk_path_) {
            const u32 handle = NewGuestFile(nullptr, false, host_path, mode);
            GuestFile* file = FindGuestFile(handle);
            if (!file) return 0;
            file->memory = apk_image_;
            if (apk_memory_open_logs_++ < 8u)
                log_ << "Dynarmic APK memory open: " << guest_path << " bytes="
                     << apk_image_->size() << '\n';
            return handle;
        }
        std::FILE* stream = std::fopen(host_path.c_str(), mode.c_str());
        if (!stream) {
            env_.MemoryWrite32(errno_address_, static_cast<u32>(errno));
            if (logged_file_failures_.insert(guest_path).second) log_ << "Dynarmic file open failed: " << guest_path << " mode=" << mode << '\n';
            return 0;
        }
        if (file_open_logs_++ < 256u || host_path != apk_path_)
            log_ << "Dynarmic file open: " << guest_path << " -> " << host_path << " mode=" << mode << '\n';
        return NewGuestFile(stream, false, host_path, mode);
    }
    u32 ReadGuestFile(u32 destination, u32 element_size, u32 count, u32 handle) {
        GuestFile* file = FindGuestFile(handle);
        const u64 requested64 = static_cast<u64>(element_size) * count;
        if (!file || !file->readable() || element_size == 0 || count == 0 ||
            requested64 > 512ull * 1024ull * 1024ull) return 0;
        const std::size_t requested = static_cast<std::size_t>(requested64);
        std::size_t guest_available = 0;
        u8* output = env_.HostPointerToRegionEnd(destination, guest_available);
        if (!output || requested > guest_available) return 0;
        if (file->memory) {
            const std::size_t available = file->memory_position < file->memory->size()
                ? file->memory->size() - file->memory_position : 0u;
            const std::size_t elements = std::min<std::size_t>(count, available / element_size);
            const std::size_t bytes = elements * element_size;
            if (bytes) std::memcpy(output, file->memory->data() + file->memory_position, bytes);
            file->memory_position += bytes;
            apk_memory_read_calls_++;
            apk_memory_read_bytes_ += bytes;
            return static_cast<u32>(elements);
        }
        return static_cast<u32>(std::fread(output, element_size, count, file->stream));
    }
    u32 WriteGuestFile(u32 source, u32 element_size, u32 count, u32 handle) {
        GuestFile* file = FindGuestFile(handle);
        const u64 requested64 = static_cast<u64>(element_size) * count;
        if (!file || !file->stream || element_size == 0 || count == 0 ||
            requested64 > 512ull * 1024ull * 1024ull) return 0;
        const std::size_t requested = static_cast<std::size_t>(requested64);
        std::size_t guest_available = 0;
        const u8* input = env_.HostPointerToRegionEnd(source, guest_available);
        if (!input || requested > guest_available) return 0;
        return static_cast<u32>(std::fwrite(input, element_size, count, file->stream));
    }
    s32 SeekGuestFile(u32 handle, s32 offset, int origin) {
        GuestFile* file = FindGuestFile(handle);
        if (!file || !file->readable()) return -1;
        if (file->memory) {
            s64 base = 0;
            if (origin == SEEK_CUR) base = static_cast<s64>(file->memory_position);
            else if (origin == SEEK_END) base = static_cast<s64>(file->memory->size());
            else if (origin != SEEK_SET) return -1;
            const s64 next = base + offset;
            if (next < 0 || static_cast<u64>(next) > file->memory->size()) return -1;
            file->memory_position = static_cast<std::size_t>(next);
            return 0;
        }
        return std::fseek(file->stream, offset, origin);
    }
    s32 TellGuestFile(u32 handle) {
        GuestFile* file = FindGuestFile(handle);
        if (!file || !file->readable()) return -1;
        if (file->memory) return file->memory_position <= static_cast<std::size_t>(INT32_MAX)
            ? static_cast<s32>(file->memory_position) : -1;
        return static_cast<s32>(std::ftell(file->stream));
    }
    u32 CloseGuestFile(u32 handle) {
        auto found = files_.find(handle);
        if (found == files_.end()) {
            log_ << "Dynarmic fclose ignored unknown guest FILE*=0x"
                 << std::hex << handle << std::dec << '\n';
            return static_cast<u32>(-1);
        }
        int close_result = 0;
        if (!found->second.standard && found->second.stream)
            close_result = std::fclose(found->second.stream);
        const bool standard = found->second.standard;
        files_.erase(found);
        if (!standard) Free(handle);
        return static_cast<u32>(close_result);
    }

    struct FormatCursor {
        GuestExecutor& owner;
        unsigned word_position;
        u32 direct_address;
        u32 Word() {
            if (direct_address) {
                const u32 value = owner.env_.MemoryRead32(direct_address);
                direct_address += 4u;
                return value;
            }
            return owner.ArgWord(word_position++);
        }
        u64 U64() {
            if (direct_address) direct_address = AlignUp(direct_address, 8u);
            else if (word_position & 1u) ++word_position;
            const u32 low = Word();
            const u32 high = Word();
            return JoinU64(low, high);
        }
    };

    std::string FormatGuestString(u32 format_address, FormatCursor cursor, std::size_t maximum = 16u * 1024u * 1024u) {
        const std::string format = ReadCString(format_address, maximum);
        std::string output;
        output.reserve(std::min<std::size_t>(format.size() * 2u + 64u, 1u << 20));
        for (std::size_t i = 0; i < format.size();) {
            if (format[i] != '%') { output.push_back(format[i++]); continue; }
            if (i + 1u < format.size() && format[i + 1u] == '%') { output.push_back('%'); i += 2u; continue; }
            const std::size_t token_start = i++;
            std::string flags;
            while (i < format.size() && std::strchr("-+ #0", format[i])) flags.push_back(format[i++]);
            int width = -1;
            if (i < format.size() && format[i] == '*') { width = static_cast<s32>(cursor.Word()); ++i; }
            else if (i < format.size() && std::isdigit(static_cast<unsigned char>(format[i]))) {
                width = 0;
                while (i < format.size() && std::isdigit(static_cast<unsigned char>(format[i]))) width = width * 10 + (format[i++] - '0');
            }
            int precision = -1;
            if (i < format.size() && format[i] == '.') {
                ++i;
                if (i < format.size() && format[i] == '*') { precision = static_cast<s32>(cursor.Word()); ++i; }
                else {
                    precision = 0;
                    while (i < format.size() && std::isdigit(static_cast<unsigned char>(format[i]))) precision = precision * 10 + (format[i++] - '0');
                }
            }
            std::string length;
            if (i < format.size() && std::strchr("hljztL", format[i])) {
                length.push_back(format[i++]);
                if (i < format.size() && (format[i] == length[0]) && (length[0] == 'h' || length[0] == 'l')) length.push_back(format[i++]);
            }
            if (i >= format.size()) { output.append(format.substr(token_start)); break; }
            const char specifier = format[i++];
            std::string token = "%" + flags;
            if (width >= 0) token += std::to_string(width);
            if (precision >= 0) token += "." + std::to_string(std::max(0, precision));
            token += length;
            token.push_back(specifier);
            char temporary[4096]{};
            int length_written = 0;
            switch (specifier) {
            case 's': {
                const std::string value = ReadCString(cursor.Word());
                length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), value.c_str());
                break;
            }
            case 'c': length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), static_cast<int>(cursor.Word())); break;
            case 'd': case 'i':
                if (length == "ll" || length == "j") length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), static_cast<long long>(static_cast<s64>(cursor.U64())));
                else if (length == "l") length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), static_cast<long>(static_cast<s32>(cursor.Word())));
                else length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), static_cast<int>(static_cast<s32>(cursor.Word())));
                break;
            case 'u': case 'o': case 'x': case 'X':
                if (length == "ll" || length == "j") length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), static_cast<unsigned long long>(cursor.U64()));
                else if (length == "l") length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), static_cast<unsigned long>(cursor.Word()));
                else length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), static_cast<unsigned>(cursor.Word()));
                break;
            case 'p': length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), reinterpret_cast<void*>(static_cast<std::uintptr_t>(cursor.Word()))); break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
                const u64 bits = cursor.U64();
                length_written = std::snprintf(temporary, sizeof(temporary), token.c_str(), WordsToDouble(static_cast<u32>(bits), static_cast<u32>(bits >> 32)));
                break;
            }
            case 'n': {
                const u32 destination = cursor.Word();
                if (length == "ll") env_.MemoryWrite64(destination, output.size());
                else env_.MemoryWrite32(destination, static_cast<u32>(output.size()));
                continue;
            }
            default:
                output.append(format.substr(token_start, i - token_start));
                continue;
            }
            if (length_written < 0) continue;
            if (static_cast<std::size_t>(length_written) < sizeof(temporary)) output.append(temporary, static_cast<std::size_t>(length_written));
            else {
                std::vector<char> dynamic(static_cast<std::size_t>(length_written) + 1u);
                // Re-formatting very large values is unnecessary for this game; preserve a bounded diagnostic instead.
                output.append(temporary, std::strlen(temporary));
            }
            if (output.size() > maximum) { output.resize(maximum); break; }
        }
        return output;
    }

    int ScanGuestString(u32 input_address, u32 format_address, FormatCursor cursor) {
        const std::string input = ReadCString(input_address);
        const std::string format = ReadCString(format_address);
        const char* source = input.c_str();
        const char* fmt = format.c_str();
        int assignments = 0;
        while (*fmt) {
            if (std::isspace(static_cast<unsigned char>(*fmt))) {
                while (std::isspace(static_cast<unsigned char>(*fmt))) ++fmt;
                while (std::isspace(static_cast<unsigned char>(*source))) ++source;
                continue;
            }
            if (*fmt != '%') {
                if (*source != *fmt) break;
                ++source; ++fmt;
                continue;
            }
            ++fmt;
            if (*fmt == '%') { if (*source != '%') break; ++source; ++fmt; continue; }
            bool suppress = false;
            if (*fmt == '*') { suppress = true; ++fmt; }
            int width = 0;
            while (std::isdigit(static_cast<unsigned char>(*fmt))) width = width * 10 + (*fmt++ - '0');
            bool long_value = false;
            bool long_long_value = false;
            if (*fmt == 'l') { long_value = true; ++fmt; if (*fmt == 'l') { long_long_value = true; ++fmt; } }
            else if (*fmt == 'h') { ++fmt; if (*fmt == 'h') ++fmt; }
            const char spec = *fmt ? *fmt++ : '\0';
            while (std::isspace(static_cast<unsigned char>(*source)) && spec != 'c' && spec != '[') ++source;
            const char* start = source;
            u32 destination = suppress ? 0 : cursor.Word();
            if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o') {
                int base = 10;
                if (spec == 'i') base = 0;
                else if (spec == 'x' || spec == 'X') base = 16;
                else if (spec == 'o') base = 8;
                char* end = nullptr;
                if (spec == 'd' || spec == 'i') {
                    const long long value = std::strtoll(source, &end, base);
                    if (end == source) break;
                    if (!suppress) {
                        if (long_long_value) env_.MemoryWrite64(destination, static_cast<u64>(value));
                        else env_.MemoryWrite32(destination, static_cast<u32>(value));
                    }
                } else {
                    const unsigned long long value = std::strtoull(source, &end, base);
                    if (end == source) break;
                    if (!suppress) {
                        if (long_long_value) env_.MemoryWrite64(destination, static_cast<u64>(value));
                        else env_.MemoryWrite32(destination, static_cast<u32>(value));
                    }
                }
                source = end;
            } else if (spec == 'f' || spec == 'e' || spec == 'g') {
                char* end = nullptr;
                const double value = std::strtod(source, &end);
                if (end == source) break;
                if (!suppress) {
                    if (long_value) {
                        u64 bits = 0; std::memcpy(&bits, &value, sizeof(bits)); env_.MemoryWrite64(destination, bits);
                    } else env_.MemoryWrite32(destination, FloatToWord(static_cast<float>(value)));
                }
                source = end;
            } else if (spec == 's') {
                const int limit = width > 0 ? width : std::numeric_limits<int>::max();
                int count = 0;
                while (*source && !std::isspace(static_cast<unsigned char>(*source)) && count < limit) { ++source; ++count; }
                if (!count) break;
                if (!suppress) {
                    env_.WriteBytes(destination, start, static_cast<std::size_t>(count));
                    env_.MemoryWrite8(destination + static_cast<u32>(count), 0);
                }
            } else if (spec == 'c') {
                const int count = width > 0 ? width : 1;
                if (std::strlen(source) < static_cast<std::size_t>(count)) break;
                if (!suppress) env_.WriteBytes(destination, source, static_cast<std::size_t>(count));
                source += count;
            } else if (spec == '[') {
                bool invert = false;
                if (*fmt == '^') { invert = true; ++fmt; }
                std::array<bool, 256> allowed{};
                if (*fmt == ']') { allowed[static_cast<unsigned char>(*fmt++)] = true; }
                while (*fmt && *fmt != ']') {
                    const unsigned char first = static_cast<unsigned char>(*fmt++);
                    if (*fmt == '-' && fmt[1] && fmt[1] != ']') {
                        ++fmt; const unsigned char last = static_cast<unsigned char>(*fmt++);
                        for (unsigned value = first; value <= last; ++value) allowed[value] = true;
                    } else allowed[first] = true;
                }
                if (*fmt == ']') ++fmt;
                const int limit = width > 0 ? width : std::numeric_limits<int>::max();
                int count = 0;
                while (*source && count < limit && (allowed[static_cast<unsigned char>(*source)] != invert)) { ++source; ++count; }
                if (!count) break;
                if (!suppress) {
                    env_.WriteBytes(destination, start, static_cast<std::size_t>(count));
                    env_.MemoryWrite8(destination + static_cast<u32>(count), 0);
                }
            } else if (spec == 'n') {
                if (!suppress) env_.MemoryWrite32(destination, static_cast<u32>(source - input.c_str()));
                continue;
            } else break;
            if (!suppress) ++assignments;
        }
        return assignments;
    }

    bool ReadZStreamLayout(u32 address, GuestZStreamLayout& layout) const {
        return address && env_.ReadBytes(address, &layout, sizeof(layout));
    }
    bool WriteZStreamLayout(GuestZStream& stream, GuestZStreamLayout& layout) {
        layout.total_in = static_cast<u32>(stream.host.total_in);
        layout.total_out = static_cast<u32>(stream.host.total_out);
        layout.msg = 0;
        layout.state = stream.active ? stream.guest_address : 0;
        layout.data_type = stream.host.data_type;
        layout.adler = static_cast<u32>(stream.host.adler);
        layout.reserved = static_cast<u32>(stream.host.reserved);
        return env_.WriteBytes(stream.guest_address, &layout, sizeof(layout));
    }
    void ReleaseZStream(GuestZStream& stream) {
        if (!stream.active) return;
        if (stream.kind == ZStreamKind::Inflate) inflateEnd(&stream.host);
        else deflateEnd(&stream.host);
        stream.active = false;
        std::memset(&stream.host, 0, sizeof(stream.host));
    }
    GuestZStream& PrepareZStream(u32 guest_address, ZStreamKind kind) {
        GuestZStream& stream = zstreams_[guest_address];
        ReleaseZStream(stream);
        stream.guest_address = guest_address;
        stream.kind = kind;
        return stream;
    }
    int GuestInflateInit(u32 guest_address, bool use_window_bits, int window_bits) {
        GuestZStreamLayout layout{};
        if (!ReadZStreamLayout(guest_address, layout)) return Z_STREAM_ERROR;
        GuestZStream& stream = PrepareZStream(guest_address, ZStreamKind::Inflate);
        const int result = use_window_bits
            ? inflateInit2_(&stream.host, window_bits, ZLIB_VERSION, static_cast<int>(sizeof(z_stream)))
            : inflateInit_(&stream.host, ZLIB_VERSION, static_cast<int>(sizeof(z_stream)));
        stream.active = result == Z_OK;
        if (!WriteZStreamLayout(stream, layout)) { ReleaseZStream(stream); return Z_STREAM_ERROR; }
        if (result == Z_OK && zlib_init_logs_++ < 16u) log_ << "Dynarmic zlib inflate stream ready: guest=0x" << std::hex << guest_address << std::dec << '\n';
        return result;
    }
    int GuestDeflateInit(u32 guest_address, int level, bool extended, int method, int window_bits, int mem_level, int strategy) {
        GuestZStreamLayout layout{};
        if (!ReadZStreamLayout(guest_address, layout)) return Z_STREAM_ERROR;
        GuestZStream& stream = PrepareZStream(guest_address, ZStreamKind::Deflate);
        const int result = extended
            ? deflateInit2_(&stream.host, level, method, window_bits, mem_level, strategy, ZLIB_VERSION, static_cast<int>(sizeof(z_stream)))
            : deflateInit_(&stream.host, level, ZLIB_VERSION, static_cast<int>(sizeof(z_stream)));
        stream.active = result == Z_OK;
        if (!WriteZStreamLayout(stream, layout)) { ReleaseZStream(stream); return Z_STREAM_ERROR; }
        return result;
    }
    int GuestZStreamProcess(u32 guest_address, ZStreamKind kind, int flush) {
        const auto found = zstreams_.find(guest_address);
        if (found == zstreams_.end() || !found->second.active || found->second.kind != kind) return Z_STREAM_ERROR;
        GuestZStream& stream = found->second;
        GuestZStreamLayout layout{};
        if (!ReadZStreamLayout(guest_address, layout)) return Z_STREAM_ERROR;
        if (layout.avail_in > 256u * 1024u * 1024u || layout.avail_out > 256u * 1024u * 1024u) return Z_MEM_ERROR;
        u8* input = layout.avail_in ? static_cast<u8*>(env_.HostPointer(layout.next_in, layout.avail_in)) : nullptr;
        u8* output = layout.avail_out ? static_cast<u8*>(env_.HostPointer(layout.next_out, layout.avail_out)) : nullptr;
        if ((layout.avail_in && !input) || (layout.avail_out && !output)) return Z_STREAM_ERROR;
        stream.host.next_in = input;
        stream.host.avail_in = layout.avail_in;
        stream.host.next_out = output;
        stream.host.avail_out = layout.avail_out;
        const u32 input_before = layout.avail_in;
        const u32 output_before = layout.avail_out;
        const int result = kind == ZStreamKind::Inflate ? inflate(&stream.host, flush) : deflate(&stream.host, flush);
        const u32 consumed = input_before - stream.host.avail_in;
        const u32 produced = output_before - stream.host.avail_out;
        layout.next_in += consumed;
        layout.avail_in -= consumed;
        layout.next_out += produced;
        layout.avail_out -= produced;
        stream.host.next_in = Z_NULL;
        stream.host.avail_in = 0;
        stream.host.next_out = Z_NULL;
        stream.host.avail_out = 0;
        return WriteZStreamLayout(stream, layout) ? result : Z_STREAM_ERROR;
    }
    int GuestZStreamReset(u32 guest_address, ZStreamKind kind) {
        const auto found = zstreams_.find(guest_address);
        if (found == zstreams_.end() || !found->second.active || found->second.kind != kind) return Z_STREAM_ERROR;
        GuestZStreamLayout layout{};
        if (!ReadZStreamLayout(guest_address, layout)) return Z_STREAM_ERROR;
        const int result = kind == ZStreamKind::Inflate ? inflateReset(&found->second.host) : deflateReset(&found->second.host);
        return WriteZStreamLayout(found->second, layout) ? result : Z_STREAM_ERROR;
    }
    int GuestZStreamEnd(u32 guest_address, ZStreamKind kind) {
        const auto found = zstreams_.find(guest_address);
        if (found == zstreams_.end() || !found->second.active || found->second.kind != kind) return Z_STREAM_ERROR;
        GuestZStreamLayout layout{};
        if (!ReadZStreamLayout(guest_address, layout)) return Z_STREAM_ERROR;
        const int result = kind == ZStreamKind::Inflate ? inflateEnd(&found->second.host) : deflateEnd(&found->second.host);
        found->second.active = false;
        std::memset(&found->second.host, 0, sizeof(found->second.host));
        return WriteZStreamLayout(found->second, layout) ? result : Z_STREAM_ERROR;
    }
    int GuestInflateCopy(u32 destination_address, u32 source_address) {
        const auto source_found = zstreams_.find(source_address);
        if (source_found == zstreams_.end() || !source_found->second.active || source_found->second.kind != ZStreamKind::Inflate) return Z_STREAM_ERROR;
        GuestZStreamLayout layout{};
        if (!ReadZStreamLayout(source_address, layout)) return Z_STREAM_ERROR;
        GuestZStream& destination = PrepareZStream(destination_address, ZStreamKind::Inflate);
        const int result = inflateCopy(&destination.host, &source_found->second.host);
        destination.active = result == Z_OK;
        return WriteZStreamLayout(destination, layout) ? result : Z_STREAM_ERROR;
    }

#ifdef _WIN32
    using GlWord = std::uintptr_t;
    GlWord CallGlRaw(void* function, const std::array<GlWord, 9>& a, unsigned count) {
        if (!function) return 0;
        switch (count) {
        case 0: return reinterpret_cast<GlWord (APIENTRY*)()>(function)();
        case 1: return reinterpret_cast<GlWord (APIENTRY*)(GlWord)>(function)(a[0]);
        case 2: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord)>(function)(a[0],a[1]);
        case 3: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord,GlWord)>(function)(a[0],a[1],a[2]);
        case 4: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord,GlWord,GlWord)>(function)(a[0],a[1],a[2],a[3]);
        case 5: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord,GlWord,GlWord,GlWord)>(function)(a[0],a[1],a[2],a[3],a[4]);
        case 6: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord,GlWord,GlWord,GlWord,GlWord)>(function)(a[0],a[1],a[2],a[3],a[4],a[5]);
        case 7: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord,GlWord,GlWord,GlWord,GlWord,GlWord)>(function)(a[0],a[1],a[2],a[3],a[4],a[5],a[6]);
        case 8: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord,GlWord,GlWord,GlWord,GlWord,GlWord,GlWord)>(function)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
        case 9: return reinterpret_cast<GlWord (APIENTRY*)(GlWord,GlWord,GlWord,GlWord,GlWord,GlWord,GlWord,GlWord,GlWord)>(function)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8]);
        default: return 0;
        }
    }
    static unsigned GlArgumentCount(const std::string& name) {
        static const std::unordered_map<std::string, unsigned> counts = {
            {"glActiveTexture",1},{"glAttachShader",2},{"glBindAttribLocation",3},{"glBindBuffer",2},
            {"glBindFramebuffer",2},{"glBindRenderbuffer",2},{"glBindTexture",2},{"glBlendFunc",2},
            {"glBufferData",4},{"glBufferSubData",4},{"glCheckFramebufferStatus",1},{"glClear",1},
            {"glClearColor",4},{"glClearDepthf",1},{"glClearStencil",1},{"glCompileShader",1},
            {"glCompressedTexImage2D",8},{"glCreateProgram",0},{"glCreateShader",1},{"glDeleteBuffers",2},
            {"glDeleteFramebuffers",2},{"glDeleteProgram",1},{"glDeleteRenderbuffers",2},{"glDeleteShader",1},
            {"glDeleteTextures",2},{"glDepthFunc",1},{"glDisable",1},{"glDisableVertexAttribArray",1},
            {"glDrawArrays",3},{"glDrawElements",4},{"glEnable",1},{"glEnableVertexAttribArray",1},
            {"glFramebufferRenderbuffer",4},{"glFramebufferTexture2D",5},{"glGenBuffers",2},
            {"glGenFramebuffers",2},{"glGenRenderbuffers",2},{"glGenTextures",2},{"glGenerateMipmap",1},
            {"glGetError",0},{"glGetFloatv",2},{"glGetIntegerv",2},{"glGetProgramInfoLog",4},
            {"glGetProgramiv",3},{"glGetShaderInfoLog",4},{"glGetShaderiv",3},{"glGetString",1},
            {"glGetUniformLocation",2},{"glLineWidth",1},{"glLinkProgram",1},{"glPixelStorei",2},
            {"glReadPixels",7},{"glRenderbufferStorage",4},{"glScissor",4},{"glShaderSource",4},
            {"glTexImage2D",9},{"glTexParameteri",3},{"glUniform1f",2},{"glUniform1i",2},
            {"glUniform2f",3},{"glUniform2fv",3},{"glUniform3f",4},{"glUniform3fv",3},
            {"glUniform4f",5},{"glUniform4fv",3},{"glUniformMatrix4fv",4},{"glUseProgram",1},
            {"glVertexAttribPointer",6},{"glViewport",4}
        };
        const auto found = counts.find(name);
        return found == counts.end() ? std::numeric_limits<unsigned>::max() : found->second;
    }
    static std::size_t GlPixelBytes(u32 width, u32 height, u32 format, u32 type) {
        u64 components = 4;
        u64 bytes_per_component = 1;
        if (format == 0x1906u || format == 0x1909u) components = 1;
        else if (format == 0x190Au) components = 2;
        else if (format == 0x1907u) components = 3;
        if (type == 0x1403u || type == 0x1402u || type == 0x8363u || type == 0x8033u || type == 0x8034u) bytes_per_component = 2;
        if (type == 0x1406u || type == 0x1405u || type == 0x1404u) bytes_per_component = 4;
        if (type == 0x8363u || type == 0x8033u || type == 0x8034u) components = 1;
        const u64 total = static_cast<u64>(width) * height * components * bytes_per_component;
        return total <= 512ull * 1024ull * 1024ull ? static_cast<std::size_t>(total) : 0;
    }
    bool DispatchGl(ImportRecord& import) {
        const std::string& name = import.name;
        ++gl_calls_;
        if (name == "glClearDepthf") {
            void* clear_depth = import.resolved_host_function;
            if (!clear_depth) {
                clear_depth = gl_.Resolve("glClearDepth");
                import.resolved_host_function = clear_depth;
            }
            if (!clear_depth) return Fail("OpenGL function unavailable: glClearDepth");
            reinterpret_cast<void (APIENTRY*)(double)>(clear_depth)(
                static_cast<double>(WordToFloat(ArgWord(0))));
            cpu_.Regs()[0] = 0;
            ResumeAfterStub(import.address);
            return true;
        }
        void* function = import.resolved_host_function;
        if (!function) {
            function = gl_.Resolve(name);
            import.resolved_host_function = function;
        }
        if (!function) return Fail("OpenGL function unavailable: " + name);
        std::array<GlWord, 9> arguments{};
        unsigned count = 0;
        if (import.gl_argument_count < 0) {
            const unsigned resolved_count = GlArgumentCount(name);
            if (resolved_count == std::numeric_limits<unsigned>::max())
                return Fail("OpenGL argument descriptor missing: " + name);
            import.gl_argument_count = static_cast<int>(resolved_count);
        }
        count = static_cast<unsigned>(import.gl_argument_count);
        for (unsigned i = 0; i < count; ++i) arguments[i] = ArgWord(i);

        if (name == "glDrawArrays" || name == "glDrawElements") {
            ++gl_draw_calls_;
            gl_draw_vertices_ += static_cast<u64>(arguments[1]);
        } else if (name == "glBufferData") {
            gl_buffer_upload_bytes_ += static_cast<u64>(arguments[1]);
        } else if (name == "glBufferSubData") {
            gl_buffer_upload_bytes_ += static_cast<u64>(arguments[2]);
        } else if (name == "glTexImage2D") {
            gl_texture_upload_bytes_ += GlPixelBytes(
                static_cast<u32>(arguments[3]),
                static_cast<u32>(arguments[4]),
                static_cast<u32>(arguments[6]),
                static_cast<u32>(arguments[7]));
        } else if (name == "glCompressedTexImage2D") {
            gl_texture_upload_bytes_ += static_cast<u64>(arguments[6]);
        }

        GlWord result = 0;

        if (name == "glGetString") {
            using Fn = const GLubyte* (APIENTRY*)(GLenum);
            const char* text = reinterpret_cast<const char*>(reinterpret_cast<Fn>(function)(static_cast<GLenum>(arguments[0])));
            const auto found = gl_string_cache_.find(static_cast<u32>(arguments[0]));
            if (found != gl_string_cache_.end()) result = found->second;
            else {
                const u32 guest = AllocateString(text ? text : "");
                gl_string_cache_[static_cast<u32>(arguments[0])] = guest;
                result = guest;
                log_ << "OpenGL string 0x" << std::hex << arguments[0] << std::dec << ": " << (text ? text : "<null>") << '\n';
            }
        } else if (name == "glBindAttribLocation") {
            using Fn = void (APIENTRY*)(GLuint, GLuint, const char*);
            const std::string text = ReadCString(static_cast<u32>(arguments[2]));
            reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]), static_cast<GLuint>(arguments[1]), text.c_str());
        } else if (name == "glGetUniformLocation") {
            using Fn = GLint (APIENTRY*)(GLuint, const char*);
            const std::string text = ReadCString(static_cast<u32>(arguments[1]));
            result = static_cast<GlWord>(reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]), text.c_str()));
        } else if (name == "glShaderSource") {
            using Fn = void (APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
            const GLsizei source_count = static_cast<GLsizei>(arguments[1]);
            if (source_count < 0 || source_count > 4096) return Fail("glShaderSource count outside limit");
            std::vector<u32> guest_strings(static_cast<std::size_t>(source_count));
            std::vector<GLint> lengths(static_cast<std::size_t>(source_count), -1);
            std::vector<std::string> source_storage(static_cast<std::size_t>(source_count));
            std::vector<const char*> pointers(static_cast<std::size_t>(source_count));
            if (source_count && !env_.ReadBytes(static_cast<u32>(arguments[2]), guest_strings.data(), guest_strings.size() * sizeof(u32))) return Fail("glShaderSource string array invalid");
            if (arguments[3] && source_count) env_.ReadBytes(static_cast<u32>(arguments[3]), lengths.data(), lengths.size() * sizeof(GLint));
            for (GLsizei i = 0; i < source_count; ++i) {
                source_storage[static_cast<std::size_t>(i)] = ReadCString(guest_strings[static_cast<std::size_t>(i)]);
                pointers[static_cast<std::size_t>(i)] = source_storage[static_cast<std::size_t>(i)].c_str();
            }
            reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]), source_count, pointers.data(), arguments[3] ? lengths.data() : nullptr);
        } else if (name == "glBufferData") {
            using Fn = void (APIENTRY*)(GLenum, GLsizeiptr_, const void*, GLenum);
            const std::size_t size = static_cast<std::size_t>(arguments[1]);
            const void* data = arguments[2] ? env_.HostPointer(static_cast<u32>(arguments[2]), size) : nullptr;
            reinterpret_cast<Fn>(function)(static_cast<GLenum>(arguments[0]), static_cast<GLsizeiptr_>(size), data, static_cast<GLenum>(arguments[3]));
        } else if (name == "glBufferSubData") {
            using Fn = void (APIENTRY*)(GLenum, GLintptr_, GLsizeiptr_, const void*);
            const std::size_t size = static_cast<std::size_t>(arguments[2]);
            const void* data = arguments[3] ? env_.HostPointer(static_cast<u32>(arguments[3]), size) : nullptr;
            reinterpret_cast<Fn>(function)(static_cast<GLenum>(arguments[0]), static_cast<GLintptr_>(arguments[1]), static_cast<GLsizeiptr_>(size), data);
        } else if (name == "glTexImage2D") {
            using Fn = void (APIENTRY*)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
            const std::size_t bytes = GlPixelBytes(static_cast<u32>(arguments[3]), static_cast<u32>(arguments[4]), static_cast<u32>(arguments[6]), static_cast<u32>(arguments[7]));
            const void* pixels = arguments[8] ? env_.HostPointer(static_cast<u32>(arguments[8]), bytes) : nullptr;
            reinterpret_cast<Fn>(function)(arguments[0],static_cast<GLint>(arguments[1]),static_cast<GLint>(arguments[2]),static_cast<GLsizei>(arguments[3]),static_cast<GLsizei>(arguments[4]),static_cast<GLint>(arguments[5]),arguments[6],arguments[7],pixels);
        } else if (name == "glCompressedTexImage2D") {
            using Fn = void (APIENTRY*)(GLenum,GLint,GLenum,GLsizei,GLsizei,GLint,GLsizei,const void*);
            const void* pixels = arguments[7] ? env_.HostPointer(static_cast<u32>(arguments[7]), static_cast<std::size_t>(arguments[6])) : nullptr;
            reinterpret_cast<Fn>(function)(arguments[0],static_cast<GLint>(arguments[1]),arguments[2],static_cast<GLsizei>(arguments[3]),static_cast<GLsizei>(arguments[4]),static_cast<GLint>(arguments[5]),static_cast<GLsizei>(arguments[6]),pixels);
        } else if (name == "glGenBuffers" || name == "glGenFramebuffers" || name == "glGenRenderbuffers" || name == "glGenTextures") {
            using Fn = void (APIENTRY*)(GLsizei, GLuint*);
            const std::size_t bytes = static_cast<std::size_t>(arguments[0]) * sizeof(GLuint);
            std::vector<GLuint> values(static_cast<std::size_t>(arguments[0]));
            reinterpret_cast<Fn>(function)(static_cast<GLsizei>(arguments[0]), values.data());
            if (arguments[1] && bytes) env_.WriteBytes(static_cast<u32>(arguments[1]), values.data(), bytes);
        } else if (name == "glDeleteBuffers" || name == "glDeleteFramebuffers" || name == "glDeleteRenderbuffers" || name == "glDeleteTextures") {
            using Fn = void (APIENTRY*)(GLsizei, const GLuint*);
            const std::size_t bytes = static_cast<std::size_t>(arguments[0]) * sizeof(GLuint);
            std::vector<GLuint> values(static_cast<std::size_t>(arguments[0]));
            if (bytes) env_.ReadBytes(static_cast<u32>(arguments[1]), values.data(), bytes);
            reinterpret_cast<Fn>(function)(static_cast<GLsizei>(arguments[0]), values.data());
        } else if (name == "glGetIntegerv") {
            using Fn = void (APIENTRY*)(GLenum, GLint*);
            std::array<GLint, 16> values{};
            reinterpret_cast<Fn>(function)(static_cast<GLenum>(arguments[0]), values.data());
            const std::size_t count = arguments[0] == 0x0BA2u || arguments[0] == 0x0C10u ? 4u : 1u;
            env_.WriteBytes(static_cast<u32>(arguments[1]), values.data(), count * sizeof(GLint));
        } else if (name == "glGetFloatv") {
            using Fn = void (APIENTRY*)(GLenum, GLfloat*);
            std::array<GLfloat, 16> values{};
            reinterpret_cast<Fn>(function)(static_cast<GLenum>(arguments[0]), values.data());
            const std::size_t count = arguments[0] == 0x0BA6u || arguments[0] == 0x0BA7u || arguments[0] == 0x0BA8u ? 16u : 1u;
            env_.WriteBytes(static_cast<u32>(arguments[1]), values.data(), count * sizeof(GLfloat));
        } else if (name == "glGetProgramiv" || name == "glGetShaderiv") {
            using Fn = void (APIENTRY*)(GLuint, GLenum, GLint*);
            GLint value = 0;
            reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]), static_cast<GLenum>(arguments[1]), &value);
            env_.MemoryWrite32(static_cast<u32>(arguments[2]), static_cast<u32>(value));
        } else if (name == "glGetProgramInfoLog" || name == "glGetShaderInfoLog") {
            using Fn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
            const GLsizei capacity = static_cast<GLsizei>(arguments[1]);
            std::vector<char> text(capacity > 0 ? static_cast<std::size_t>(capacity) : 1u);
            GLsizei length = 0;
            reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]), capacity, &length, text.data());
            if (arguments[2]) env_.MemoryWrite32(static_cast<u32>(arguments[2]), static_cast<u32>(length));
            if (arguments[3] && capacity > 0) env_.WriteBytes(static_cast<u32>(arguments[3]), text.data(), text.size());
        } else if (name == "glUniform1f") {
            reinterpret_cast<void (APIENTRY*)(GLint,GLfloat)>(function)(static_cast<GLint>(arguments[0]), WordToFloat(static_cast<u32>(arguments[1])));
        } else if (name == "glUniform2f") {
            reinterpret_cast<void (APIENTRY*)(GLint,GLfloat,GLfloat)>(function)(static_cast<GLint>(arguments[0]),WordToFloat(static_cast<u32>(arguments[1])),WordToFloat(static_cast<u32>(arguments[2])));
        } else if (name == "glUniform3f") {
            reinterpret_cast<void (APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat)>(function)(static_cast<GLint>(arguments[0]),WordToFloat(static_cast<u32>(arguments[1])),WordToFloat(static_cast<u32>(arguments[2])),WordToFloat(static_cast<u32>(arguments[3])));
        } else if (name == "glUniform4f") {
            reinterpret_cast<void (APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat,GLfloat)>(function)(static_cast<GLint>(arguments[0]),WordToFloat(static_cast<u32>(arguments[1])),WordToFloat(static_cast<u32>(arguments[2])),WordToFloat(static_cast<u32>(arguments[3])),WordToFloat(static_cast<u32>(arguments[4])));
        } else if (name == "glClearColor") {
            reinterpret_cast<void (APIENTRY*)(GLfloat,GLfloat,GLfloat,GLfloat)>(function)(WordToFloat(static_cast<u32>(arguments[0])),WordToFloat(static_cast<u32>(arguments[1])),WordToFloat(static_cast<u32>(arguments[2])),WordToFloat(static_cast<u32>(arguments[3])));
        } else if (name == "glLineWidth") {
            reinterpret_cast<void (APIENTRY*)(GLfloat)>(function)(WordToFloat(static_cast<u32>(arguments[0])));
        } else if (name == "glUniform2fv" || name == "glUniform3fv" || name == "glUniform4fv") {
            using Fn = void (APIENTRY*)(GLint, GLsizei, const GLfloat*);
            const unsigned components = name == "glUniform2fv" ? 2u : name == "glUniform3fv" ? 3u : 4u;
            const std::size_t bytes = static_cast<std::size_t>(arguments[1]) * components * sizeof(GLfloat);
            const GLfloat* values = static_cast<const GLfloat*>(env_.HostPointer(static_cast<u32>(arguments[2]), bytes));
            reinterpret_cast<Fn>(function)(static_cast<GLint>(arguments[0]), static_cast<GLsizei>(arguments[1]), values);
        } else if (name == "glUniformMatrix4fv") {
            using Fn = void (APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
            const std::size_t bytes = static_cast<std::size_t>(arguments[1]) * 16u * sizeof(GLfloat);
            const GLfloat* values = static_cast<const GLfloat*>(env_.HostPointer(static_cast<u32>(arguments[3]), bytes));
            reinterpret_cast<Fn>(function)(static_cast<GLint>(arguments[0]), static_cast<GLsizei>(arguments[1]), static_cast<GLboolean>(arguments[2]), values);
        } else if (name == "glBindBuffer") {
            if (arguments[0] == GL_ARRAY_BUFFER) gl_array_buffer_binding_ = static_cast<u32>(arguments[1]);
            if (arguments[0] == GL_ELEMENT_ARRAY_BUFFER) gl_element_buffer_binding_ = static_cast<u32>(arguments[1]);
            result = CallGlRaw(function, arguments, count);
        } else if (name == "glVertexAttribPointer") {
            using Fn = void (APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
            const void* pointer = gl_array_buffer_binding_ ? reinterpret_cast<const void*>(arguments[5]) : env_.HostPointer(static_cast<u32>(arguments[5]), 1);
            reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]),static_cast<GLint>(arguments[1]),static_cast<GLenum>(arguments[2]),static_cast<GLboolean>(arguments[3]),static_cast<GLsizei>(arguments[4]),pointer);
        } else if (name == "glDrawElements") {
            using Fn = void (APIENTRY*)(GLenum, GLsizei, GLenum, const void*);
            std::size_t element_size = arguments[2] == 0x1401u ? 1u : arguments[2] == 0x1403u ? 2u : 4u;
            const void* indices = gl_element_buffer_binding_ ? reinterpret_cast<const void*>(arguments[3]) : env_.HostPointer(static_cast<u32>(arguments[3]), static_cast<std::size_t>(arguments[1]) * element_size);
            reinterpret_cast<Fn>(function)(static_cast<GLenum>(arguments[0]),static_cast<GLsizei>(arguments[1]),static_cast<GLenum>(arguments[2]),indices);
        } else if (name == "glReadPixels") {
            using Fn = void (APIENTRY*)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
            const std::size_t bytes = GlPixelBytes(static_cast<u32>(arguments[2]),static_cast<u32>(arguments[3]),static_cast<u32>(arguments[4]),static_cast<u32>(arguments[5]));
            void* output = env_.HostPointer(static_cast<u32>(arguments[6]), bytes);
            reinterpret_cast<Fn>(function)(static_cast<GLint>(arguments[0]),static_cast<GLint>(arguments[1]),static_cast<GLsizei>(arguments[2]),static_cast<GLsizei>(arguments[3]),static_cast<GLenum>(arguments[4]),static_cast<GLenum>(arguments[5]),output);
        } else {
            result = CallGlRaw(function, arguments, count);
        }
        cpu_.Regs()[0] = static_cast<u32>(result);
        ResumeAfterStub(import.address);
        return true;
    }
#else
    bool DispatchGl(ImportRecord& import) { return Fail("OpenGL host is available only on Windows: " + import.name); }
#endif

    u32 WriteGuestTm(std::time_t value, bool utc) {
        std::tm host{};
#ifdef _WIN32
        const errno_t error = utc ? gmtime_s(&host, &value) : localtime_s(&host, &value);
        if (error != 0) return 0;
#else
        if (!(utc ? gmtime_r(&value, &host) : localtime_r(&value, &host))) return 0;
#endif
        GuestTmLayout guest{};
        guest.tm_sec = host.tm_sec;
        guest.tm_min = host.tm_min;
        guest.tm_hour = host.tm_hour;
        guest.tm_mday = host.tm_mday;
        guest.tm_mon = host.tm_mon;
        guest.tm_year = host.tm_year;
        guest.tm_wday = host.tm_wday;
        guest.tm_yday = host.tm_yday;
        guest.tm_isdst = host.tm_isdst;
        guest.tm_gmtoff = 0;
        guest.tm_zone = time_zone_address_;
        return env_.WriteBytes(tm_address_, &guest, sizeof(guest)) ? tm_address_ : 0;
    }

    bool CallGuestComparator(u32 comparator, u32 left, u32 right, s32& result) {
        const auto saved_regs = cpu_.Regs();
        const auto saved_ext = cpu_.ExtRegs();
        const u32 saved_cpsr = cpu_.Cpsr();
        const u32 saved_fpscr = cpu_.Fpscr();
        u32 raw = 0;
        const bool ok = RunFunction(comparator, {left, right}, &raw, "qsort comparator", 10000000u);
        cpu_.Regs() = saved_regs;
        cpu_.ExtRegs() = saved_ext;
        cpu_.SetCpsr(saved_cpsr);
        cpu_.SetFpscr(saved_fpscr);
        result = static_cast<s32>(raw);
        return ok;
    }

    bool GuestQsort(u32 base, u32 count, u32 size, u32 comparator) {
        if (count < 2 || size == 0) return true;
        if (count > 100000u || size > 1024u * 1024u) return Fail("qsort dimensions outside safety limit");
        std::vector<u8> pivot(size);
        const u32 scratch = Allocate(size);
        if (!scratch) return false;
        ScopeExit release_scratch([&]() { Free(scratch); });
        for (u32 i = 1; i < count; ++i) {
            if (!env_.ReadBytes(base + i * size, pivot.data(), size) ||
                !env_.WriteBytes(scratch, pivot.data(), size)) return false;
            u32 j = i;
            while (j > 0) {
                s32 comparison = 0;
                if (!CallGuestComparator(comparator, base + (j - 1u) * size, scratch, comparison)) return false;
                if (comparison <= 0) break;
                if (!CopyGuest(base + j * size, base + (j - 1u) * size, size)) return false;
                --j;
            }
            if (!env_.WriteBytes(base + j * size, pivot.data(), size)) return false;
        }
        return true;
    }

    u32 GuestBsearch(u32 key, u32 base, u32 count, u32 size, u32 comparator) {
        u32 low = 0, high = count;
        while (low < high) {
            const u32 middle = low + (high - low) / 2u;
            s32 comparison = 0;
            if (!CallGuestComparator(comparator, key, base + middle * size, comparison)) return 0;
            if (comparison == 0) return base + middle * size;
            if (comparison < 0) high = middle;
            else low = middle + 1u;
        }
        return 0;
    }

    void SetGuestErrno(int value) {
        env_.MemoryWrite32(errno_address_, static_cast<u32>(value));
    }
#ifdef _WIN32
    static int GuestAddressFamily(int host_family) {
        return gd_net_host_family_to_android(host_family);
    }
    static int HostAddressFamily(int guest_family) {
        return gd_net_android_family_to_host(guest_family);
    }
    static int MapWsaError(int error) {
        return gd_net_wsa_error_to_android_passthrough(error);
    }
    u32 SocketFailure() {
        SetGuestErrno(MapWsaError(WSAGetLastError()));
        return static_cast<u32>(-1);
    }
    SOCKET FindHostSocket(u32 guest_fd) const {
        const auto found = sockets_.find(guest_fd);
        return found == sockets_.end() ? INVALID_SOCKET : found->second;
    }
    bool IsGuestSocket(u32 guest_fd) const { return sockets_.find(guest_fd) != sockets_.end(); }
    u32 RegisterSocket(SOCKET socket_value) {
        if (socket_value == INVALID_SOCKET) return SocketFailure();
        const u32 guest_fd = next_socket_fd_++;
        sockets_[guest_fd] = socket_value;
        return guest_fd;
    }
    bool ReadGuestSockaddr(u32 address, u32 length, sockaddr_storage& output, int& output_length) {
        if (!address || length < 2u || length > sizeof(sockaddr_storage)) return false;
        std::array<u8, sizeof(sockaddr_storage)> bytes{};
        if (!env_.ReadBytes(address, bytes.data(), length)) return false;
        u16 family = 0;
        std::memcpy(&family, bytes.data(), sizeof(family));
        family = static_cast<u16>(HostAddressFamily(family));
        std::memcpy(bytes.data(), &family, sizeof(family));
        std::memcpy(&output, bytes.data(), length);
        output_length = static_cast<int>(length);
        return true;
    }
    bool WriteGuestSockaddr(u32 address, u32 length_address, const sockaddr* input, int input_length) {
        if (!length_address) return true;
        const u32 capacity = env_.MemoryRead32(length_address);
        env_.MemoryWrite32(length_address, static_cast<u32>(input_length));
        if (!address || capacity == 0u) return true;
        const u32 copy_length = std::min<u32>(capacity, static_cast<u32>(input_length));
        std::array<u8, sizeof(sockaddr_storage)> bytes{};
        std::memcpy(bytes.data(), input, copy_length);
        u16 family = 0;
        std::memcpy(&family, bytes.data(), sizeof(family));
        family = static_cast<u16>(GuestAddressFamily(family));
        std::memcpy(bytes.data(), &family, sizeof(family));
        return env_.WriteBytes(address, bytes.data(), copy_length);
    }
    u32 GuestSocket(u32 family, u32 type, u32 protocol) {
        if (!winsock_initialized_) { SetGuestErrno(100); return static_cast<u32>(-1); }
        return RegisterSocket(::socket(HostAddressFamily(static_cast<int>(family)),
                                       static_cast<int>(type), static_cast<int>(protocol)));
    }
    u32 GuestConnect(u32 guest_fd, u32 address, u32 length) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        sockaddr_storage host_address{}; int host_length = 0;
        if (socket_value == INVALID_SOCKET || !ReadGuestSockaddr(address, length, host_address, host_length)) {
            SetGuestErrno(88); return static_cast<u32>(-1);
        }

        char address_text[INET6_ADDRSTRLEN]{};
        u16 port = 0;
        if (host_address.ss_family == AF_INET) {
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&host_address);
            ::inet_ntop(AF_INET, &ipv4->sin_addr, address_text, sizeof(address_text));
            port = ntohs(ipv4->sin_port);
        } else if (host_address.ss_family == AF_INET6) {
            const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&host_address);
            ::inet_ntop(AF_INET6, &ipv6->sin6_addr, address_text, sizeof(address_text));
            port = ntohs(ipv6->sin6_port);
        }

        const auto started = std::chrono::steady_clock::now();
        const int code = ::connect(socket_value, reinterpret_cast<const sockaddr*>(&host_address), host_length);
        const int initial_error = code == SOCKET_ERROR ? WSAGetLastError() : 0;
        if (code == 0 || initial_error == WSAEISCONN) {
            SetGuestErrno(0);
            if (network_log_count_++ < 96u)
                log_ << "[host] Socket connect fd=" << guest_fd << " target="
                     << (address_text[0] ? address_text : "?") << ':' << port
                     << " status=connected immediate=yes\n";
            return 0u;
        }

        // Bionic/libcurl uses nonblocking sockets. Windows reports
        // WSAEWOULDBLOCK while POSIX connect reports EINPROGRESS. Test9
        // translated it to EAGAIN, making libcurl reject a healthy connection.
        // Complete the pending connect on the host, then return a connected socket.
        if (initial_error == WSAEWOULDBLOCK || initial_error == WSAEINPROGRESS ||
            initial_error == WSAEALREADY) {
            fd_set writable{};
            fd_set exceptional{};
            FD_ZERO(&writable);
            FD_ZERO(&exceptional);
            FD_SET(socket_value, &writable);
            FD_SET(socket_value, &exceptional);
            timeval timeout{};
            timeout.tv_sec = 15;
            timeout.tv_usec = 0;
            const int ready = ::select(0, nullptr, &writable, &exceptional, &timeout);
            if (ready > 0) {
                int socket_error = 0;
                int socket_error_length = sizeof(socket_error);
                if (::getsockopt(socket_value, SOL_SOCKET, SO_ERROR,
                                 reinterpret_cast<char*>(&socket_error), &socket_error_length) == 0 &&
                    socket_error == 0 && FD_ISSET(socket_value, &writable)) {
                    SetGuestErrno(0);
                    if (network_log_count_++ < 96u) {
                        const double elapsed = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                        log_ << "[host] Socket connect fd=" << guest_fd << " target="
                             << (address_text[0] ? address_text : "?") << ':' << port
                             << " status=connected pending=yes wait_ms=" << std::fixed
                             << std::setprecision(1) << elapsed << '\n';
                    }
                    return 0u;
                }
                if (socket_error == 0) socket_error = WSAECONNREFUSED;
                SetGuestErrno(MapWsaError(socket_error));
                if (network_log_count_++ < 96u)
                    log_ << "[host] Socket connect fd=" << guest_fd << " target="
                         << (address_text[0] ? address_text : "?") << ':' << port
                         << " status=failed pending=yes wsa=" << socket_error
                         << " errno=" << MapWsaError(socket_error) << '\n';
                return static_cast<u32>(-1);
            }
            const int wait_error = ready == 0 ? WSAETIMEDOUT : WSAGetLastError();
            SetGuestErrno(MapWsaError(wait_error));
            if (network_log_count_++ < 96u)
                log_ << "[host] Socket connect fd=" << guest_fd << " target="
                     << (address_text[0] ? address_text : "?") << ':' << port
                     << (ready == 0 ? " status=timeout" : " status=select-failed")
                     << " wsa=" << wait_error << " errno=" << MapWsaError(wait_error) << '\n';
            return static_cast<u32>(-1);
        }

        SetGuestErrno(MapWsaError(initial_error));
        if (network_log_count_++ < 96u)
            log_ << "[host] Socket connect fd=" << guest_fd << " target="
                 << (address_text[0] ? address_text : "?") << ':' << port
                 << " status=failed immediate=yes wsa=" << initial_error
                 << " errno=" << MapWsaError(initial_error) << '\n';
        return static_cast<u32>(-1);
    }
    u32 GuestBind(u32 guest_fd, u32 address, u32 length) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        sockaddr_storage host_address{}; int host_length = 0;
        if (socket_value == INVALID_SOCKET || !ReadGuestSockaddr(address, length, host_address, host_length)) return SocketFailure();
        const int code = ::bind(socket_value, reinterpret_cast<const sockaddr*>(&host_address), host_length);
        return code == 0 ? 0u : SocketFailure();
    }
    u32 GuestAccept(u32 guest_fd, u32 address, u32 length_address) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        if (socket_value == INVALID_SOCKET) { SetGuestErrno(88); return static_cast<u32>(-1); }
        sockaddr_storage host_address{}; int host_length = sizeof(host_address);
        const SOCKET accepted = ::accept(socket_value, reinterpret_cast<sockaddr*>(&host_address), &host_length);
        if (accepted == INVALID_SOCKET) return SocketFailure();
        if (!WriteGuestSockaddr(address, length_address, reinterpret_cast<sockaddr*>(&host_address), host_length)) {
            closesocket(accepted); SetGuestErrno(14); return static_cast<u32>(-1);
        }
        return RegisterSocket(accepted);
    }
    u32 GuestListen(u32 guest_fd, u32 backlog) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        if (socket_value == INVALID_SOCKET) { SetGuestErrno(88); return static_cast<u32>(-1); }
        return ::listen(socket_value, static_cast<int>(backlog)) == 0 ? 0u : SocketFailure();
    }
    static int HostMessageFlags(u32 guest_flags) {
        int host_flags = 0;
        if (guest_flags & 0x0001u) host_flags |= MSG_OOB;
        if (guest_flags & 0x0002u) host_flags |= MSG_PEEK;
        if (guest_flags & 0x0004u) host_flags |= MSG_DONTROUTE;
#ifdef MSG_WAITALL
        if (guest_flags & 0x0100u) host_flags |= MSG_WAITALL;
#endif
        return host_flags; // Ignore Android MSG_DONTWAIT/MSG_NOSIGNAL; socket mode handles them.
    }
    u32 GuestSend(u32 guest_fd, u32 buffer, u32 length, u32 flags) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        const char* data = static_cast<const char*>(env_.HostPointer(buffer, length));
        if (socket_value == INVALID_SOCKET || (!data && length)) {
            SetGuestErrno(socket_value == INVALID_SOCKET ? 88 : 14);
            if (network_log_count_++ < 128u)
                log_ << "[host] Socket send rejected fd=" << guest_fd << " buffer=0x"
                     << std::hex << buffer << std::dec << " length=" << length
                     << (socket_value == INVALID_SOCKET ? " invalid-socket" : " invalid-guest-buffer") << '\n';
            return static_cast<u32>(-1);
        }

        unsigned char* song_response = nullptr;
        std::size_t song_response_size = 0u;
        int song_response_code = 0;
        const int song_request = gd_song_http_handle_raw_request(
            data, length, &song_response, &song_response_size,
            &song_response_code);
        if (song_request > 0) {
            SyntheticHttpResponse response;
            response.bytes.assign(song_response,
                                  song_response + song_response_size);
            std::free(song_response);
            synthetic_http_[guest_fd] = std::move(response);
            if (network_log_count_++ < 128u)
                log_ << "[host] Song metadata completed through custom-first/official-fallback transport fd="
                     << guest_fd << " status=" << song_response_code
                     << " response=" << song_response_size << '\n';
            SetGuestErrno(0);
            return length;
        }
        if (song_request < 0) {
            std::free(song_response);
            SetGuestErrno(5);
            if (network_log_count_++ < 128u)
                log_ << "[host] Song metadata custom/official request failed fd="
                     << guest_fd << '\n';
            return static_cast<u32>(-1);
        }

        void* rewritten_buffer = nullptr;
        std::size_t rewritten_size = 0u;
        const int rewritten = gd_settings_rewrite_http_request(
            data, length, &rewritten_buffer, &rewritten_size);
        ScopeExit release_rewrite([&] {
            if (rewritten_buffer) std::free(rewritten_buffer);
        });
        const char* send_data = rewritten > 0
            ? static_cast<const char*>(rewritten_buffer) : data;
        const std::size_t send_size = rewritten > 0 ? rewritten_size : length;
        if (rewritten > 0 && network_log_count_++ < 128u)
            log_ << "[host] GDPS request rewrite server="
                 << gd_settings_server() << " bytes=" << length << "->"
                 << send_size << '\n';

        std::size_t sent = 0u;
        int error = 0;
        while (sent < send_size) {
            const int chunk = static_cast<int>(std::min<std::size_t>(
                send_size - sent, static_cast<std::size_t>(INT_MAX)));
            int code = ::send(socket_value, send_data + sent, chunk,
                              HostMessageFlags(flags));
            if (code > 0) {
                sent += static_cast<std::size_t>(code);
                if (rewritten <= 0) break; // preserve normal POSIX partial-send behavior
                continue;
            }
            error = code == SOCKET_ERROR ? WSAGetLastError() : WSAECONNRESET;
            if (code == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
                fd_set writable{};
                fd_set exceptional{};
                FD_ZERO(&writable);
                FD_ZERO(&exceptional);
                FD_SET(socket_value, &writable);
                FD_SET(socket_value, &exceptional);
                timeval timeout{};
                timeout.tv_sec = 15;
                const int ready = ::select(
                    0, nullptr, &writable, &exceptional, &timeout);
                if (ready > 0 && FD_ISSET(socket_value, &writable) &&
                    !FD_ISSET(socket_value, &exceptional)) {
                    continue;
                }
                if (ready == 0) error = WSAETIMEDOUT;
                else if (ready == SOCKET_ERROR) error = WSAGetLastError();
            }
            SetGuestErrno(MapWsaError(error));
            if (network_log_count_++ < 128u)
                log_ << "[host] Socket send failed fd=" << guest_fd
                     << " wsa=" << error << " errno=" << MapWsaError(error)
                     << '\n';
            return static_cast<u32>(-1);
        }

        if (socket_send_logged_.insert(guest_fd).second)
            log_ << "[host] Socket first send fd=" << guest_fd << " bytes="
                 << (rewritten > 0 ? length : sent) << '\n';
        SetGuestErrno(0);
        return static_cast<u32>(rewritten > 0 ? length : sent);
    }

    u32 GuestReceive(u32 guest_fd, u32 buffer, u32 length, u32 flags) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        char* data = static_cast<char*>(env_.HostPointer(buffer, length));
        if (socket_value == INVALID_SOCKET || (!data && length)) {
            SetGuestErrno(socket_value == INVALID_SOCKET ? 88 : 14);
            if (network_log_count_++ < 128u)
                log_ << "[host] Socket recv rejected fd=" << guest_fd << " buffer=0x"
                     << std::hex << buffer << std::dec << " length=" << length
                     << (socket_value == INVALID_SOCKET ? " invalid-socket" : " invalid-guest-buffer") << '\n';
            return static_cast<u32>(-1);
        }

        const auto synthetic = synthetic_http_.find(guest_fd);
        if (synthetic != synthetic_http_.end()) {
            SyntheticHttpResponse& response = synthetic->second;
            if (response.offset < response.bytes.size()) {
                const std::size_t count = std::min<std::size_t>(
                    length, response.bytes.size() - response.offset);
                if (count) {
                    std::memcpy(data, response.bytes.data() + response.offset,
                                count);
                    response.offset += count;
                }
                SetGuestErrno(0);
                return static_cast<u32>(count);
            }
            synthetic_http_.erase(synthetic);
            SetGuestErrno(0);
            return 0u;
        }

        const int host_length = static_cast<int>(std::min<u32>(length, INT_MAX));
        int code = ::recv(socket_value, data, host_length, HostMessageFlags(flags));
        int error = code == SOCKET_ERROR ? WSAGetLastError() : 0;

        // Test11 connected and sent correctly, but the old ARM libcurl asks for
        // recv immediately. On Windows that commonly returns WSAEWOULDBLOCK
        // before the first response byte arrives. Its legacy poll/error path then
        // turns the harmless race into CURLE_RECV_ERROR. Wait for readability and
        // retry here so the guest sees actual data, EOF, or a real socket error.
        if (code == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
            fd_set readable{};
            fd_set exceptional{};
            FD_ZERO(&readable);
            FD_ZERO(&exceptional);
            FD_SET(socket_value, &readable);
            FD_SET(socket_value, &exceptional);
            timeval timeout{};
            timeout.tv_sec = 15;
            const auto started = std::chrono::steady_clock::now();
            const int ready = ::select(0, &readable, nullptr, &exceptional, &timeout);
            if (ready > 0 && FD_ISSET(socket_value, &readable)) {
                int socket_error = 0;
                int socket_error_length = sizeof(socket_error);
                if (::getsockopt(socket_value, SOL_SOCKET, SO_ERROR,
                                 reinterpret_cast<char*>(&socket_error), &socket_error_length) != 0) {
                    socket_error = WSAGetLastError();
                }
                if (socket_error != 0) {
                    SetGuestErrno(MapWsaError(socket_error));
                    if (network_log_count_++ < 128u)
                        log_ << "[host] Socket recv readiness error fd=" << guest_fd
                             << " wsa=" << socket_error << " errno=" << MapWsaError(socket_error) << '\n';
                    return static_cast<u32>(-1);
                }
                code = ::recv(socket_value, data, host_length, HostMessageFlags(flags));
                error = code == SOCKET_ERROR ? WSAGetLastError() : 0;
                if (network_log_count_++ < 128u) {
                    const double elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started).count();
                    log_ << "[host] Socket recv wait fd=" << guest_fd << " wait_ms="
                         << std::fixed << std::setprecision(1) << elapsed
                         << " result=" << code;
                    if (code == SOCKET_ERROR) log_ << " wsa=" << error;
                    log_ << '\n';
                }
            } else if (ready == 0) {
                // Keep POSIX nonblocking semantics on timeout. Curl can apply its
                // own total timeout instead of receiving a fabricated peer error.
                SetGuestErrno(11);
                if (network_log_count_++ < 128u)
                    log_ << "[host] Socket recv wait timeout fd=" << guest_fd << " errno=11\n";
                return static_cast<u32>(-1);
            } else {
                error = ready == SOCKET_ERROR ? WSAGetLastError() : WSAECONNABORTED;
            }
        }

        if (code >= 0) {
            if (code > 0 && socket_receive_logged_.insert(guest_fd).second)
                log_ << "[host] Socket first recv fd=" << guest_fd << " bytes=" << code << '\n';
            if (code == 0 && network_log_count_++ < 128u)
                log_ << "[host] Socket EOF fd=" << guest_fd << '\n';
            SetGuestErrno(0);
            return static_cast<u32>(code);
        }
        SetGuestErrno(MapWsaError(error));
        if (network_log_count_++ < 128u)
            log_ << "[host] Socket recv failed fd=" << guest_fd << " wsa=" << error
                 << " errno=" << MapWsaError(error) << '\n';
        return static_cast<u32>(-1);
    }
    u32 GuestSendTo(u32 guest_fd, u32 buffer, u32 length, u32 flags, u32 address, u32 address_length) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        const char* data = static_cast<const char*>(env_.HostPointer(buffer, length));
        sockaddr_storage host_address{}; int host_length = 0;
        if (socket_value == INVALID_SOCKET || (!data && length) ||
            !ReadGuestSockaddr(address, address_length, host_address, host_length)) { SetGuestErrno(14); return static_cast<u32>(-1); }
        const int code = ::sendto(socket_value, data, static_cast<int>(std::min<u32>(length, INT_MAX)),
                                  HostMessageFlags(flags), reinterpret_cast<const sockaddr*>(&host_address), host_length);
        return code >= 0 ? static_cast<u32>(code) : SocketFailure();
    }
    u32 GuestReceiveFrom(u32 guest_fd, u32 buffer, u32 length, u32 flags, u32 address, u32 length_address) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        char* data = static_cast<char*>(env_.HostPointer(buffer, length));
        sockaddr_storage host_address{}; int host_length = sizeof(host_address);
        if (socket_value == INVALID_SOCKET || (!data && length)) { SetGuestErrno(14); return static_cast<u32>(-1); }
        const int code = ::recvfrom(socket_value, data, static_cast<int>(std::min<u32>(length, INT_MAX)),
                                    HostMessageFlags(flags), reinterpret_cast<sockaddr*>(&host_address), &host_length);
        if (code < 0) return SocketFailure();
        WriteGuestSockaddr(address, length_address, reinterpret_cast<sockaddr*>(&host_address), host_length);
        return static_cast<u32>(code);
    }
    u32 GuestSocketName(u32 guest_fd, u32 address, u32 length_address, bool peer) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        if (socket_value == INVALID_SOCKET) { SetGuestErrno(88); return static_cast<u32>(-1); }
        sockaddr_storage host_address{}; int host_length = sizeof(host_address);
        const int code = peer ? ::getpeername(socket_value, reinterpret_cast<sockaddr*>(&host_address), &host_length)
                              : ::getsockname(socket_value, reinterpret_cast<sockaddr*>(&host_address), &host_length);
        if (code != 0) return SocketFailure();
        return WriteGuestSockaddr(address, length_address, reinterpret_cast<sockaddr*>(&host_address), host_length) ? 0u : static_cast<u32>(-1);
    }
    static int HostSocketLevel(int guest_level) {
        if (guest_level == 1) return SOL_SOCKET;
        if (guest_level == 6) return IPPROTO_TCP;
        if (guest_level == 0) return IPPROTO_IP;
        if (guest_level == 41) return IPPROTO_IPV6;
        return guest_level;
    }
    static int HostSocketOption(int guest_level, int guest_option) {
        if (guest_level == 1) {
            switch (guest_option) {
            case 2: return SO_REUSEADDR;
            case 3: return SO_TYPE;
            case 4: return SO_ERROR;
            case 7: return SO_SNDBUF;
            case 8: return SO_RCVBUF;
            case 9: return SO_KEEPALIVE;
            case 13: return SO_LINGER;
            case 20: return SO_RCVTIMEO;
            case 21: return SO_SNDTIMEO;
            default: return guest_option;
            }
        }
        if (guest_level == 6 && guest_option == 1) return TCP_NODELAY;
        return guest_option;
    }
    u32 GuestSetSockOpt(u32 guest_fd, u32 level, u32 option, u32 value_address, u32 value_length) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        const char* value = static_cast<const char*>(env_.HostPointer(value_address, value_length));
        if (socket_value == INVALID_SOCKET || (!value && value_length)) { SetGuestErrno(14); return static_cast<u32>(-1); }
        const int host_level = HostSocketLevel(static_cast<int>(level));
        const int host_option = HostSocketOption(static_cast<int>(level), static_cast<int>(option));
        DWORD timeout_ms = 0;
        struct linger host_linger{};
        if (level == 1u && option == 13u && value_length >= 8u) {
            host_linger.l_onoff = static_cast<u_short>(env_.MemoryRead32(value_address) != 0u);
            host_linger.l_linger = static_cast<u_short>(std::min<u32>(env_.MemoryRead32(value_address + 4u), 0xffffu));
            value = reinterpret_cast<const char*>(&host_linger);
            value_length = sizeof(host_linger);
        } else if (level == 1u && (option == 20u || option == 21u) && value_length >= 8u) {
            const s32 seconds = static_cast<s32>(env_.MemoryRead32(value_address));
            const s32 micros = static_cast<s32>(env_.MemoryRead32(value_address + 4u));
            timeout_ms = static_cast<DWORD>(std::max<s64>(0, static_cast<s64>(seconds) * 1000 + micros / 1000));
            value = reinterpret_cast<const char*>(&timeout_ms);
            value_length = sizeof(timeout_ms);
        }
        const int code = ::setsockopt(socket_value, host_level, host_option, value, static_cast<int>(value_length));
        if (code == 0) return 0;
        // Android-only socket options such as SO_NOSIGPIPE are harmless on Windows.
        if (WSAGetLastError() == WSAENOPROTOOPT || WSAGetLastError() == WSAEINVAL) return 0;
        return SocketFailure();
    }
    u32 GuestGetSockOpt(u32 guest_fd, u32 level, u32 option, u32 value_address, u32 length_address) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        if (socket_value == INVALID_SOCKET || !length_address) { SetGuestErrno(14); return static_cast<u32>(-1); }
        u32 capacity = env_.MemoryRead32(length_address);
        char* value = static_cast<char*>(env_.HostPointer(value_address, capacity));
        if (!value && capacity) { SetGuestErrno(14); return static_cast<u32>(-1); }
        int host_length = static_cast<int>(capacity);
        const int host_level = HostSocketLevel(static_cast<int>(level));
        const int host_option = HostSocketOption(static_cast<int>(level), static_cast<int>(option));
        const int code = ::getsockopt(socket_value, host_level, host_option, value, &host_length);
        if (code != 0) return SocketFailure();
        env_.MemoryWrite32(length_address, static_cast<u32>(host_length));
        if (level == 1u && option == 4u && host_length >= static_cast<int>(sizeof(int))) {
            int host_error = 0;
            std::memcpy(&host_error, value, sizeof(host_error));
            const int guest_error = MapWsaError(host_error);
            std::memcpy(value, &guest_error, sizeof(guest_error));
            if (network_log_count_++ < 128u)
                log_ << "[host] Socket SO_ERROR fd=" << guest_fd
                     << " host=" << host_error << " guest=" << guest_error << '\n';
        }
        return 0;
    }
    u32 GuestPoll(u32 pollfds_address, u32 count, s32 timeout) {
        if (count > 4096u) { SetGuestErrno(22); return static_cast<u32>(-1); }
        std::vector<GuestPollFd> guest(count);
        if (count && !env_.ReadBytes(pollfds_address, guest.data(), guest.size() * sizeof(GuestPollFd))) {
            SetGuestErrno(14);
            return static_cast<u32>(-1);
        }

        fd_set readable{};
        fd_set writable{};
        fd_set exceptional{};
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_ZERO(&exceptional);
        std::vector<SOCKET> host_sockets(count, INVALID_SOCKET);
        std::vector<bool> synthetic_ready(count, false);
        u32 invalid_count = 0;
        u32 synthetic_count = 0;
        bool have_valid_socket = false;
        for (u32 index = 0; index < count; ++index) {
            guest[index].revents = 0;
            if (guest[index].fd < 0) continue;
            const u32 guest_fd = static_cast<u32>(guest[index].fd);
            const auto response = synthetic_http_.find(guest_fd);
            if (response != synthetic_http_.end() &&
                (response->second.offset < response->second.bytes.size() ||
                 response->second.eof_pending)) {
                if (guest[index].events & 0x0001) {
                    guest[index].revents |= 0x0001;
                    synthetic_ready[index] = true;
                    ++synthetic_count;
                }
                continue;
            }
            const SOCKET socket_value = FindHostSocket(guest_fd);
            host_sockets[index] = socket_value;
            if (socket_value == INVALID_SOCKET) {
                guest[index].revents = 0x0020; // Linux POLLNVAL
                ++invalid_count;
                continue;
            }
            have_valid_socket = true;
            if (guest[index].events & 0x0001) FD_SET(socket_value, &readable);  // POLLIN
            if (guest[index].events & 0x0002) FD_SET(socket_value, &exceptional); // POLLPRI
            if (guest[index].events & 0x0004) FD_SET(socket_value, &writable);  // POLLOUT
            FD_SET(socket_value, &exceptional);
        }

        timeval timeval_value{};
        timeval* timeval_pointer = nullptr;
        if (synthetic_count) {
            timeval_value.tv_sec = 0;
            timeval_value.tv_usec = 0;
            timeval_pointer = &timeval_value;
        } else if (timeout >= 0) {
            timeval_value.tv_sec = timeout / 1000;
            timeval_value.tv_usec = (timeout % 1000) * 1000;
            timeval_pointer = &timeval_value;
        }

        int selected = 0;
        if (have_valid_socket) {
            selected = ::select(0, &readable, &writable, &exceptional, timeval_pointer);
            if (selected == SOCKET_ERROR) return SocketFailure();
        } else if (!synthetic_count && timeout > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        }

        u32 ready_count = invalid_count + synthetic_count;
        for (u32 index = 0; index < count; ++index) {
            if (synthetic_ready[index]) continue;
            const SOCKET socket_value = host_sockets[index];
            if (socket_value == INVALID_SOCKET) continue;
            std::int16_t revents = 0;
            if (FD_ISSET(socket_value, &readable)) revents |= 0x0001;
            if (FD_ISSET(socket_value, &writable)) revents |= 0x0004;
            if (FD_ISSET(socket_value, &exceptional)) revents |= 0x0008;
            guest[index].revents = revents;
            if (revents) ++ready_count;
            if (revents && network_poll_log_count_++ < 48u) {
                log_ << "[host] Socket poll fd=" << guest[index].fd
                     << " requested=0x" << std::hex << static_cast<u16>(guest[index].events)
                     << " ready=0x" << static_cast<u16>(revents) << std::dec
                     << " timeout_ms=" << timeout << '\n';
            }
        }
        if (count && !env_.WriteBytes(pollfds_address, guest.data(), guest.size() * sizeof(GuestPollFd))) {
            SetGuestErrno(14);
            return static_cast<u32>(-1);
        }
        SetGuestErrno(0);
        return ready_count;
    }
    u32 GuestIoctl(u32 guest_fd, u32 request, u32 argument_address) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        if (socket_value == INVALID_SOCKET) { SetGuestErrno(88); return static_cast<u32>(-1); }
        if (request == 0x5421u || request == static_cast<u32>(FIONBIO)) {
            u_long value = argument_address ? env_.MemoryRead32(argument_address) : 0u;
            const int code = ioctlsocket(socket_value, FIONBIO, &value);
            if (code == 0) {
                if (value) nonblocking_sockets_.insert(guest_fd); else nonblocking_sockets_.erase(guest_fd);
                return 0;
            }
            return SocketFailure();
        }
        SetGuestErrno(22); return static_cast<u32>(-1);
    }
    u32 GuestFcntl(u32 guest_fd, u32 command, u32 argument) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        if (socket_value == INVALID_SOCKET) { SetGuestErrno(88); return static_cast<u32>(-1); }
        constexpr u32 kFGetFl = 3u, kFSetFl = 4u, kONonBlock = 0x800u;
        if (command == kFGetFl) return nonblocking_sockets_.count(guest_fd) ? kONonBlock : 0u;
        if (command == kFSetFl) {
            u_long value = (argument & kONonBlock) ? 1u : 0u;
            if (ioctlsocket(socket_value, FIONBIO, &value) != 0) return SocketFailure();
            if (value) nonblocking_sockets_.insert(guest_fd); else nonblocking_sockets_.erase(guest_fd);
            return 0;
        }
        return 0;
    }
    u32 GuestCloseSocket(u32 guest_fd) {
        const auto found = sockets_.find(guest_fd);
        if (found == sockets_.end()) return static_cast<u32>(-1);
        const int code = closesocket(found->second);
        sockets_.erase(found);
        nonblocking_sockets_.erase(guest_fd);
        socket_send_logged_.erase(guest_fd);
        socket_receive_logged_.erase(guest_fd);
        synthetic_http_.erase(guest_fd);
        return code == 0 ? 0u : SocketFailure();
    }
    u32 GuestGetAddrInfo(u32 node_address, u32 service_address, u32 hints_address, u32 result_address) {
        if (!result_address) return static_cast<u32>(EAI_FAIL);
        env_.MemoryWrite32(result_address, 0u);
        const std::string node = ReadCString(node_address);
        const std::string service = ReadCString(service_address);
        addrinfo host_hints{}; addrinfo* hints = nullptr;
        if (hints_address) {
            const u32 guest_flags = env_.MemoryRead32(hints_address + 0u);
            host_hints.ai_flags = 0;
            if (guest_flags & 0x001u) host_hints.ai_flags |= AI_PASSIVE;
            if (guest_flags & 0x002u) host_hints.ai_flags |= AI_CANONNAME;
            if (guest_flags & 0x004u) host_hints.ai_flags |= AI_NUMERICHOST;
#ifdef AI_NUMERICSERV
            if (guest_flags & 0x400u) host_hints.ai_flags |= AI_NUMERICSERV;
#endif
            host_hints.ai_family = HostAddressFamily(static_cast<int>(env_.MemoryRead32(hints_address + 4u)));
            host_hints.ai_socktype = static_cast<int>(env_.MemoryRead32(hints_address + 8u));
            host_hints.ai_protocol = static_cast<int>(env_.MemoryRead32(hints_address + 12u));
            hints = &host_hints;
        }
        addrinfo* host_results = nullptr;
        char override_host[512]{};
        const char* lookup_node = node.empty() ? nullptr : node.c_str();
        if (lookup_node && service != "443" &&
            gd_settings_override_dns_host(
                lookup_node, override_host, sizeof(override_host))) {
            lookup_node = override_host;
        }
        const int code = ::getaddrinfo(
            lookup_node, service.empty() ? nullptr : service.c_str(), hints,
            &host_results);
        if (network_log_count_++ < 64u) {
            log_ << "[host] DNS getaddrinfo node=" << (node.empty() ? "<null>" : node)
                 << (lookup_node && lookup_node != node.c_str()
                         ? std::string(" override=") + lookup_node
                         : std::string())
                 << " service=" << (service.empty() ? "<null>" : service)
                 << " result=" << code << '\n';
        }
        if (code != 0) return static_cast<u32>(code);
        ScopeExit cleanup([&] { if (host_results) ::freeaddrinfo(host_results); });
        GuestAddrInfoAllocation allocation;
        u32 first = 0, previous = 0;
        for (addrinfo* item = host_results; item; item = item->ai_next) {
            if (item->ai_addrlen > sizeof(sockaddr_storage)) continue;
            const u32 node_block = Allocate(32u);
            const u32 address_block = Allocate(static_cast<u32>(item->ai_addrlen));
            if (!node_block || !address_block) break;
            allocation.blocks.push_back(node_block); allocation.blocks.push_back(address_block);
            std::array<u8, sizeof(sockaddr_storage)> address_bytes{};
            std::memcpy(address_bytes.data(), item->ai_addr, item->ai_addrlen);
            u16 family = static_cast<u16>(GuestAddressFamily(item->ai_family));
            std::memcpy(address_bytes.data(), &family, sizeof(family));
            env_.WriteBytes(address_block, address_bytes.data(), item->ai_addrlen);
            u32 canonical = 0;
            if (item->ai_canonname) { canonical = AllocateString(item->ai_canonname); if (canonical) allocation.blocks.push_back(canonical); }
            env_.MemoryWrite32(node_block + 0u, static_cast<u32>(item->ai_flags));
            env_.MemoryWrite32(node_block + 4u, static_cast<u32>(GuestAddressFamily(item->ai_family)));
            env_.MemoryWrite32(node_block + 8u, static_cast<u32>(item->ai_socktype));
            env_.MemoryWrite32(node_block + 12u, static_cast<u32>(item->ai_protocol));
            env_.MemoryWrite32(node_block + 16u, static_cast<u32>(item->ai_addrlen));
            env_.MemoryWrite32(node_block + 20u, canonical);
            env_.MemoryWrite32(node_block + 24u, address_block);
            env_.MemoryWrite32(node_block + 28u, 0u);
            if (!first) first = node_block;
            if (previous) env_.MemoryWrite32(previous + 28u, node_block);
            previous = node_block;
        }
        if (!first) { for (u32 block : allocation.blocks) Free(block); return static_cast<u32>(EAI_MEMORY); }
        guest_addrinfo_[first] = std::move(allocation);
        env_.MemoryWrite32(result_address, first);
        return 0;
    }
    u32 GuestFreeAddrInfo(u32 address) {
        const auto found = guest_addrinfo_.find(address);
        if (found == guest_addrinfo_.end()) return 0;
        for (u32 block : found->second.blocks) Free(block);
        guest_addrinfo_.erase(found);
        return 0;
    }
    u32 GuestInetPton(u32 family, u32 text_address, u32 output_address) {
        const std::string text = ReadCString(text_address);
        const int host_family = HostAddressFamily(static_cast<int>(family));
        const std::size_t size = host_family == AF_INET ? 4u : host_family == AF_INET6 ? 16u : 0u;
        void* output = size ? env_.HostPointer(output_address, size) : nullptr;
        return output ? static_cast<u32>(::inet_pton(host_family, text.c_str(), output)) : 0u;
    }
    u32 GuestInetNtop(u32 family, u32 input_address, u32 output_address, u32 output_size) {
        const int host_family = HostAddressFamily(static_cast<int>(family));
        const std::size_t input_size = host_family == AF_INET ? 4u : host_family == AF_INET6 ? 16u : 0u;
        const void* input = input_size ? env_.HostPointer(input_address, input_size) : nullptr;
        std::array<char, INET6_ADDRSTRLEN> text{};
        if (!input || !::inet_ntop(host_family, input, text.data(), text.size())) return 0;
        const std::size_t length = std::strlen(text.data()) + 1u;
        if (!output_address || output_size < length || !env_.WriteBytes(output_address, text.data(), length)) return 0;
        return output_address;
    }
#else
    bool IsGuestSocket(u32) const { return false; }
#endif

    bool InitializeCooperativeWorker(u32 thread_address, u32 start_routine, u32 argument) {
        if (!start_routine) return false;
        cooperative_worker_ = {};
        cooperative_worker_.regs[0] = argument;
        cooperative_worker_.regs[13] = kStackBase + 0x00100000u - 0x1000u;
        cooperative_worker_.regs[14] = kReturnStub;
        cooperative_worker_.regs[15] = start_routine & ~1u;
        cooperative_worker_.cpsr = 0x10u | ((start_routine & 1u) ? 0x20u : 0u);
        cooperative_worker_.valid = true;
        if (thread_address) env_.MemoryWrite32(thread_address, next_thread_id_++);
        log_ << "[host] Cooperative guest worker registered at 0x" << std::hex << start_routine
             << " arg=0x" << argument << std::dec << '\n';
        return true;
    }
    bool ResumeCooperativeWorker() {
        if (!cooperative_worker_.valid || running_cooperative_worker_) return true;
        const auto saved_regs = cpu_.Regs();
        const auto saved_ext = cpu_.ExtRegs();
        const u32 saved_cpsr = cpu_.Cpsr();
        const u32 saved_fpscr = cpu_.Fpscr();
        ScopeExit restore([&] {
            cpu_.Regs() = saved_regs; cpu_.ExtRegs() = saved_ext;
            cpu_.SetCpsr(saved_cpsr); cpu_.SetFpscr(saved_fpscr);
            running_cooperative_worker_ = false;
        });
        cpu_.Regs() = cooperative_worker_.regs;
        cpu_.ExtRegs() = cooperative_worker_.ext_regs;
        cpu_.SetCpsr(cooperative_worker_.cpsr);
        cpu_.SetFpscr(cooperative_worker_.fpscr);
        running_cooperative_worker_ = true;
        cooperative_worker_yielded_ = false;
        cooperative_worker_done_ = false;
        const auto started = std::chrono::steady_clock::now();
        while (!cooperative_worker_yielded_ && !cooperative_worker_done_) {
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds(120))
                return Fail("cooperative HTTP worker exceeded 120 second guard");
            env_.ResetStopState(); env_.ticks_left = 5000000u;
            cpu_.Run(); cpu_.ClearHalt(kCallbackHalt);
            if (env_.invalid_access) return Fail("cooperative HTTP worker invalid guest memory");
            if (env_.interpreter_fallback) return Fail("cooperative HTTP worker interpreter fallback");
            if (env_.exception_seen) return Fail("cooperative HTTP worker guest exception");
            if (env_.svc_pending) {
                if (env_.pending_svc == kSvcReturn) { cooperative_worker_done_ = true; break; }
                if (!HandleSvc(env_.pending_svc, "CCHttpClient worker")) return false;
            } else if (env_.ticks_left != 0u) {
                return Fail("cooperative HTTP worker stopped without a trap");
            }
        }
        cooperative_worker_.regs = cpu_.Regs();
        cooperative_worker_.ext_regs = cpu_.ExtRegs();
        cooperative_worker_.cpsr = cpu_.Cpsr();
        cooperative_worker_.fpscr = cpu_.Fpscr();
        if (cooperative_worker_done_) cooperative_worker_.valid = false;
        if (cooperative_worker_yielded_ && network_worker_runs_++ < 32u)
            log_ << "[host] Cooperative HTTP worker idle after processing queued request(s)\n";
        return true;
    }

    u32 HostGetFileDataFromZip(u32 zip_path_address, u32 member_address, u32 size_address) {
        if (size_address) env_.MemoryWrite32(size_address, 0u);
        const std::string zip_path = ReadCString(zip_path_address);
        const std::string member = ReadCString(member_address);
        if (member.empty()) return 0;
        if (!zip_path.empty() && TranslatePath(zip_path) != apk_path_ &&
            zip_path != "game.apk" && !zip_path.ends_with("/game.apk")) return 0;
        const auto bytes = apk_member_cache_.Load(member);
        if (!bytes) return 0;
        const u32 allocation_size = static_cast<u32>(std::max<std::size_t>(bytes->size(), 1u));
        const u32 output = Allocate(allocation_size);
        if (!output) return 0;
        if (!bytes->empty() && !env_.WriteBytes(output, bytes->data(), bytes->size())) {
            Free(output);
            return 0;
        }
        if (size_address) env_.MemoryWrite32(size_address, static_cast<u32>(bytes->size()));
        return output;
    }
    u32 HostExistFileDataFromZip(u32 zip_path_address, u32 member_address) {
        const std::string zip_path = ReadCString(zip_path_address);
        if (!zip_path.empty() && TranslatePath(zip_path) != apk_path_ &&
            zip_path != "game.apk" && !zip_path.ends_with("/game.apk")) return 0;
        return apk_member_cache_.Exists(ReadCString(member_address)) ? 1u : 0u;
    }

    struct HostUnzState {
        std::size_t current_index = 0;
        std::shared_ptr<const std::vector<u8>> current_bytes;
        std::size_t read_offset = 0;
        bool current_open = false;
    };

    bool IsApkZipPath(const std::string& path) const {
        if (path.empty()) return false;
        const std::string translated = TranslatePath(path);
        if (translated == apk_path_) return true;
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return normalized == "game.apk" || normalized.ends_with("/game.apk");
    }

    HostUnzState* FindHostUnz(u32 handle) {
        const auto found = host_unz_.find(handle);
        return found == host_unz_.end() ? nullptr : &found->second;
    }

    bool WriteUnzFileInfo(HostUnzState& state, bool wide64,
                          u32 info_address, u32 filename_address,
                          u32 filename_capacity) {
        const ZipEntryRecord* entry = apk_member_cache_.EntryAt(state.current_index);
        if (!entry) return false;
        if (info_address) {
            const std::size_t size = wide64 ? 88u : 80u;
            std::array<u8, 88> bytes{};
            auto put32 = [&](std::size_t offset, u32 value) {
                std::memcpy(bytes.data() + offset, &value, sizeof(value));
            };
            auto put64 = [&](std::size_t offset, u64 value) {
                std::memcpy(bytes.data() + offset, &value, sizeof(value));
            };
            put32(0, 20u); put32(4, 20u); put32(8, 0u);
            put32(12, entry->method); put32(16, 0u); put32(20, entry->crc);
            if (wide64) {
                put64(24, entry->compressed_size); put64(32, entry->uncompressed_size);
                put32(40, static_cast<u32>(entry->name.size()));
            } else {
                put32(24, entry->compressed_size); put32(28, entry->uncompressed_size);
                put32(32, static_cast<u32>(entry->name.size()));
            }
            if (!env_.WriteBytes(info_address, bytes.data(), size)) return false;
        }
        if (filename_address && filename_capacity) {
            const std::size_t count = std::min<std::size_t>(
                entry->name.size(), static_cast<std::size_t>(filename_capacity - 1u));
            if (count && !env_.WriteBytes(filename_address, entry->name.data(), count)) return false;
            env_.MemoryWrite8(filename_address + static_cast<u32>(count), 0u);
        }
        return true;
    }

    bool DispatchHostMinizip(ImportRecord& import) {
        constexpr s32 kUnzOk = 0;
        constexpr s32 kUnzEndOfList = -100;
        constexpr s32 kUnzParamError = -102;
        const std::string& name = import.name;
        const u32 r0 = cpu_.Regs()[0], r1 = cpu_.Regs()[1], r2 = cpu_.Regs()[2], r3 = cpu_.Regs()[3];
        auto finish = [&](s32 value) {
            cpu_.Regs()[0] = static_cast<u32>(value);
            if (import.inline_resume_address) {
                cpu_.Regs()[15] = import.inline_resume_address;
                cpu_.SetCpsr(cpu_.Cpsr() & ~0x20u);
            } else {
                ResumeAfterStub(import.address);
            }
            return true;
        };
        if (name == "__dynarmic_unzStringFileNameCompare") {
            const std::string left = ReadCString(r0), right = ReadCString(r1);
            int result = 0;
            if (r2 == 1u) result = left.compare(right);
            else {
                const std::size_t count = std::min(left.size(), right.size());
                for (std::size_t i = 0; i < count && result == 0; ++i)
                    result = std::tolower(static_cast<unsigned char>(left[i])) -
                             std::tolower(static_cast<unsigned char>(right[i]));
                if (!result) result = left.size() < right.size() ? -1 : left.size() > right.size() ? 1 : 0;
            }
            return finish(result);
        }
        if (name == "__dynarmic_unzOpen" || name == "__dynarmic_unzOpen64" ||
            name == "__dynarmic_unzOpen2" || name == "__dynarmic_unzOpen2_64") {
            const std::string path = ReadCString(r0);
            if (!IsApkZipPath(path)) return finish(0);
            const u32 handle = Allocate(64u);
            if (!handle || apk_member_cache_.OrderedNames().empty()) return finish(0);
            host_unz_[handle] = HostUnzState{};
            if (import.calls == 1)
                log_ << "RESULT: DYNARMIC_HOST_MINIZIP_ACTIVE entries="
                     << apk_member_cache_.OrderedNames().size() << '\n';
            return finish(static_cast<s32>(handle));
        }
        HostUnzState* state = FindHostUnz(r0);
        if (!state) return finish(kUnzParamError);
        if (name == "__dynarmic_unzClose") {
            host_unz_.erase(r0); Free(r0); return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGoToFirstFile") {
            state->current_index = 0; state->current_open = false; state->current_bytes.reset();
            return finish(apk_member_cache_.OrderedNames().empty() ? kUnzEndOfList : kUnzOk);
        }
        if (name == "__dynarmic_unzGoToNextFile") {
            if (state->current_index + 1u >= apk_member_cache_.OrderedNames().size())
                return finish(kUnzEndOfList);
            ++state->current_index; state->current_open = false; state->current_bytes.reset();
            return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzLocateFile") {
            const auto index = apk_member_cache_.LocateIndex(ReadCString(r1), static_cast<int>(r2));
            if (!index) return finish(kUnzEndOfList);
            state->current_index = *index; state->current_open = false; state->current_bytes.reset();
            return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGetCurrentFileInfo" ||
            name == "__dynarmic_unzGetCurrentFileInfo64") {
            const bool wide64 = name.ends_with("64");
            return finish(WriteUnzFileInfo(*state, wide64, r1, r2, r3) ? kUnzOk : kUnzParamError);
        }
        if (name == "__dynarmic_unzOpenCurrentFile" ||
            name == "__dynarmic_unzOpenCurrentFile2" ||
            name == "__dynarmic_unzOpenCurrentFile3" ||
            name == "__dynarmic_unzOpenCurrentFilePassword") {
            state->current_bytes = apk_member_cache_.LoadAt(state->current_index);
            state->read_offset = 0; state->current_open = static_cast<bool>(state->current_bytes);
            if ((name == "__dynarmic_unzOpenCurrentFile2" || name == "__dynarmic_unzOpenCurrentFile3")) {
                const ZipEntryRecord* entry = apk_member_cache_.EntryAt(state->current_index);
                if (r1) env_.MemoryWrite32(r1, entry ? entry->method : 0u);
                if (r2) env_.MemoryWrite32(r2, 0u);
            }
            return finish(state->current_open ? kUnzOk : kUnzParamError);
        }
        if (name == "__dynarmic_unzReadCurrentFile") {
            if (!state->current_open || !state->current_bytes) return finish(kUnzParamError);
            const std::size_t remaining = state->current_bytes->size() -
                std::min(state->read_offset, state->current_bytes->size());
            const std::size_t count = std::min<std::size_t>(remaining, r2);
            if (count && !env_.WriteBytes(r1, state->current_bytes->data() + state->read_offset, count))
                return finish(kUnzParamError);
            state->read_offset += count;
            return finish(static_cast<s32>(count));
        }
        if (name == "__dynarmic_unzCloseCurrentFile") {
            state->current_open = false; state->current_bytes.reset(); state->read_offset = 0;
            return finish(kUnzOk);
        }
        if (name == "__dynarmic_unztell" || name == "__dynarmic_unztell64" ||
            name == "__dynarmic_unzGetCurrentFileZStreamPos64")
            return finish(static_cast<s32>(state->read_offset));
        if (name == "__dynarmic_unzeof") {
            const bool eof = !state->current_bytes || state->read_offset >= state->current_bytes->size();
            return finish(eof ? 1 : 0);
        }
        if (name == "__dynarmic_unzGetGlobalInfo") {
            if (r1) { env_.MemoryWrite32(r1, static_cast<u32>(apk_member_cache_.OrderedNames().size())); env_.MemoryWrite32(r1 + 4u, 0u); }
            return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGetGlobalInfo64") {
            if (r1) { env_.MemoryWrite64(r1, apk_member_cache_.OrderedNames().size()); env_.MemoryWrite32(r1 + 8u, 0u); }
            return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGetFilePos") {
            if (r1) { env_.MemoryWrite32(r1, static_cast<u32>(state->current_index)); env_.MemoryWrite32(r1 + 4u, static_cast<u32>(state->current_index)); }
            return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGetFilePos64") {
            if (r1) { env_.MemoryWrite64(r1, state->current_index); env_.MemoryWrite64(r1 + 8u, state->current_index); }
            return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGoToFilePos" || name == "__dynarmic_unzGoToFilePos64") {
            if (!r1) return finish(kUnzParamError);
            const u64 index = name.ends_with("64") ? env_.MemoryRead64(r1 + 8u) : env_.MemoryRead32(r1 + 4u);
            if (index >= apk_member_cache_.OrderedNames().size()) return finish(kUnzEndOfList);
            state->current_index = static_cast<std::size_t>(index); return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGetOffset" || name == "__dynarmic_unzGetOffset64")
            return finish(static_cast<s32>(state->current_index));
        if (name == "__dynarmic_unzSetOffset" || name == "__dynarmic_unzSetOffset64") {
            const u64 index = r1;
            if (index >= apk_member_cache_.OrderedNames().size()) return finish(kUnzEndOfList);
            state->current_index = static_cast<std::size_t>(index); return finish(kUnzOk);
        }
        if (name == "__dynarmic_unzGetGlobalComment" || name == "__dynarmic_unzGetLocalExtrafield")
            return finish(0);
        return finish(kUnzOk);
    }

    bool DispatchImport(ImportRecord& import) {
        const std::string& name = import.name;
        const u32 r0 = cpu_.Regs()[0], r1 = cpu_.Regs()[1], r2 = cpu_.Regs()[2], r3 = cpu_.Regs()[3];
        u32 result = 0;
        bool result_set = true;

        if (import.name.rfind("__dynarmic_unz", 0) == 0)
            return DispatchHostMinizip(import);
        if (import.is_gl) return DispatchGl(import);

        auto finish_hot = [&](u32 value) {
            cpu_.Regs()[0] = value;
            ResumeAfterStub(import.address);
            return true;
        };
        // These dominate APK/level loading. Keep them ahead of the large generic
        // import chain and avoid temporary allocations on every SVC crossing.
        if (name == "__dynarmic_ccfileutils_getFileDataFromZip")
            return finish_hot(HostGetFileDataFromZip(r1, r2, r3));
        if (name == "__dynarmic_ccfileutils_existFileDataFromZip")
            return finish_hot(HostExistFileDataFromZip(r1, r2));
        if (name == "__dynarmic_ccapplication_openURL") {
            const std::string url = ReadCString(r1);
            RememberEvent("CCApplication::openURL " + url);
            OpenExternalUrl(url);
            return finish_hot(0u);
        }
        if (name == "__dynarmic_simpleaudio_preloadEffect") {
            const std::string path = ReadCString(r1);
            audio_preload_effect(path.c_str());
            return finish_hot(0u);
        }
        if (name == "__dynarmic_simpleaudio_playEffect") {
            const u32 stack = cpu_.Regs()[13];
            const float pitch = WordToFloat(r3);
            const float pan = WordToFloat(env_.MemoryRead32(stack + 0u));
            const float gain = WordToFloat(env_.MemoryRead32(stack + 4u));
            const std::string path = ReadCString(r1);
            RememberEvent("SimpleAudioEngine::playEffect " + path);
            return finish_hot(audio_play_effect_ex(
                path.c_str(), r2 != 0u ? 1 : 0, pitch, pan, gain));
        }
        if (name == "fread") return finish_hot(ReadGuestFile(r0, r1, r2, r3));
        if (name == "fseek") return finish_hot(static_cast<u32>(SeekGuestFile(r0, static_cast<s32>(r1), static_cast<int>(r2))));
        if (name == "ftell") return finish_hot(static_cast<u32>(TellGuestFile(r0)));
        if (name == "memcpy" || name == "memmove") return finish_hot(CopyGuest(r0, r1, r2) ? r0 : 0);
        if (name == "strlen") return finish_hot(CStringLength(r0));
        if (name == "strcmp" || name == "strcoll") return finish_hot(static_cast<u32>(CompareStrings(r0, r1, 0, false, false)));
        if (name == "memcmp") {
            const void* a = env_.HostPointer(r0, r2);
            const void* b = env_.HostPointer(r1, r2);
            return finish_hot(a && b ? static_cast<u32>(std::memcmp(a, b, r2)) : 0);
        }
        if (name == "memset") {
            void* destination = env_.HostPointer(r0, r2);
            return finish_hot(destination ? (std::memset(destination, static_cast<int>(r1 & 0xffu), r2), r0) : 0);
        }
        if (name == "pthread_mutex_lock" || name == "pthread_mutex_unlock") return finish_hot(0);

        if (name == "malloc") result = Allocate(r0);
        else if (name == "calloc") {
            const u64 total = static_cast<u64>(r0) * r1;
            result = total <= std::numeric_limits<u32>::max() ? Allocate(static_cast<u32>(total)) : 0;
            if (result && total) {
                void* destination = env_.HostPointer(result, static_cast<std::size_t>(total));
                if (!destination) { Free(result); result = 0; }
                else std::memset(destination, 0, static_cast<std::size_t>(total));
            }
        } else if (name == "realloc") result = Reallocate(r0, r1);
        else if (name == "free") { Free(r0); result = 0; }
        else if (name == "__cxa_finalize") result = 0;
        else if (name == "memcpy" || name == "memmove") result = CopyGuest(r0, r1, r2) ? r0 : 0;
        else if (name == "memset") {
            void* destination = env_.HostPointer(r0, r2);
            result = destination ? (std::memset(destination, static_cast<int>(r1 & 0xffu), r2), r0) : 0;
        } else if (name == "memcmp") {
            const void* a = env_.HostPointer(r0, r2);
            const void* b = env_.HostPointer(r1, r2);
            result = a && b ? static_cast<u32>(std::memcmp(a, b, r2)) : 0;
        } else if (name == "memchr" || name == "memrchr") {
            const u8* bytes = static_cast<const u8*>(env_.HostPointer(r0, r2));
            if (bytes) {
                if (name == "memchr") {
                    const void* found = std::memchr(bytes, static_cast<int>(r1), r2);
                    result = found ? r0 + static_cast<u32>(static_cast<const u8*>(found) - bytes) : 0;
                } else {
                    for (u32 index = r2; index > 0; --index) if (bytes[index - 1u] == static_cast<u8>(r1)) { result = r0 + index - 1u; break; }
                }
            }
        } else if (name == "strlen") result = CStringLength(r0);
        else if (name == "wcslen") { u32 length=0; while(env_.IsMapped(r0+length*4u,4u)&&env_.MemoryRead32(r0+length*4u)!=0u)++length; result=length; }
        else if (name == "strcmp" || name == "strcoll") result = static_cast<u32>(CompareStrings(r0, r1, 0, false, false));
        else if (name == "strncmp") result = static_cast<u32>(CompareStrings(r0, r1, r2, true, false));
        else if (name == "strcasecmp") result = static_cast<u32>(CompareStrings(r0, r1, 0, false, true));
        else if (name == "strncasecmp") result = static_cast<u32>(CompareStrings(r0, r1, r2, true, true));
        else if (name == "strcpy" || name == "strncpy" || name == "strcat" || name == "strlcat") {
            std::string source = ReadCString(r1);
            std::string destination = (name == "strcat" || name == "strlcat") ? ReadCString(r0) : std::string{};
            const std::size_t expected = destination.size() + source.size();
            std::string combined = destination + source;
            if (name == "strncpy") {
                std::vector<u8> bytes(r2, 0);
                const std::size_t copy = std::min<std::size_t>(source.size(), r2);
                if (copy) std::memcpy(bytes.data(), source.data(), copy);
                if (r2) env_.WriteBytes(r0, bytes.data(), bytes.size());
            } else {
                if (name == "strlcat" && r2 != 0 && combined.size() >= r2) combined.resize(r2 - 1u);
                env_.WriteBytes(r0, combined.c_str(), combined.size() + 1u);
            }
            result = name == "strlcat" ? static_cast<u32>(expected) : r0;
        } else if (name == "strdup") {
            const std::string text = ReadCString(r0);
            result = AllocateString(text);
        } else if (name == "strchr" || name == "strrchr") {
            const std::string text = ReadCString(r0);
            const char needle = static_cast<char>(r1);
            const std::size_t position = name == "strchr" ? text.find(needle) : text.rfind(needle);
            result = position == std::string::npos ? 0 : r0 + static_cast<u32>(position);
        } else if (name == "strstr") {
            const std::string haystack = ReadCString(r0), needle = ReadCString(r1);
            const std::size_t position = haystack.find(needle);
            result = position == std::string::npos ? 0 : r0 + static_cast<u32>(position);
        } else if (name == "strtok" || name == "strtok_r") {
            u32 state = name == "strtok_r" ? (r2 ? env_.MemoryRead32(r2) : 0) : strtok_state_;
            if (r0) state = r0;
            if (!state) result = 0;
            else {
                const std::string delimiters = ReadCString(r1);
                while (env_.MemoryRead8(state) && delimiters.find(static_cast<char>(env_.MemoryRead8(state))) != std::string::npos) ++state;
                if (!env_.MemoryRead8(state)) result = 0;
                else {
                    result = state;
                    while (env_.MemoryRead8(state) && delimiters.find(static_cast<char>(env_.MemoryRead8(state))) == std::string::npos) ++state;
                    if (env_.MemoryRead8(state)) env_.MemoryWrite8(state++, 0);
                }
            }
            if (name == "strtok_r") { if (r2) env_.MemoryWrite32(r2, state); }
            else strtok_state_ = state;
        } else if (name == "atoi") result = static_cast<u32>(std::strtol(ReadCString(r0).c_str(), nullptr, 10));
        else if (name == "strtol" || name == "strtoul" || name == "strtoll" || name == "strtod") {
            const std::string text = ReadCString(r0);
            char* end = nullptr;
            if (name == "strtod") {
                const double value = std::strtod(text.c_str(), &end);
                if (r1) env_.MemoryWrite32(r1, r0 + static_cast<u32>(end - text.c_str()));
                ReturnDouble(value); result_set = false;
            } else if (name == "strtoll") {
                const long long value = std::strtoll(text.c_str(), &end, static_cast<int>(r2));
                if (r1) env_.MemoryWrite32(r1, r0 + static_cast<u32>(end - text.c_str()));
                ReturnU64(static_cast<u64>(value)); result_set = false;
            } else if (name == "strtoul") {
                const unsigned long value = std::strtoul(text.c_str(), &end, static_cast<int>(r2));
                if (r1) env_.MemoryWrite32(r1, r0 + static_cast<u32>(end - text.c_str()));
                result = static_cast<u32>(value);
            } else {
                const long value = std::strtol(text.c_str(), &end, static_cast<int>(r2));
                if (r1) env_.MemoryWrite32(r1, r0 + static_cast<u32>(end - text.c_str()));
                result = static_cast<u32>(value);
            }
        } else if (name == "sprintf" || name == "snprintf" || name == "vsprintf" || name == "vsnprintf") {
            const bool bounded = name == "snprintf" || name == "vsnprintf";
            const bool va_list = name == "vsprintf" || name == "vsnprintf";
            const u32 destination = r0;
            const u32 capacity = bounded ? r1 : std::numeric_limits<u32>::max();
            const u32 format = bounded ? r2 : r1;
            const u32 first_arguments = bounded ? 3u : 2u;
            const u32 va_address = bounded ? r3 : r2;
            FormatCursor cursor{*this, first_arguments, va_list ? va_address : 0u};
            const std::string text = FormatGuestString(format, cursor);
            const std::size_t copy = capacity == 0 ? 0 : std::min<std::size_t>(text.size(), static_cast<std::size_t>(capacity - 1u));
            if (copy) env_.WriteBytes(destination, text.data(), copy);
            if (capacity) env_.MemoryWrite8(destination + static_cast<u32>(copy), 0);
            result = static_cast<u32>(text.size());
        } else if (name == "printf" || name == "fprintf" || name == "vfprintf") {
            const bool to_file = name != "printf";
            const bool va_list = name == "vfprintf";
            const u32 format = to_file ? r1 : r0;
            FormatCursor cursor{*this, to_file ? 2u : 1u, va_list ? r2 : 0u};
            const std::string text = FormatGuestString(format, cursor);
            if (to_file) {
                GuestFile* file = FindGuestFile(r0);
                if (file && file->stream) std::fwrite(text.data(), 1, text.size(), file->stream);
            } else std::fwrite(text.data(), 1, text.size(), stdout);
            if (logged_guest_stdio_++ < 1000u) log_ << "guest stdio: " << text;
            result = static_cast<u32>(text.size());
        } else if (name == "sscanf") result = static_cast<u32>(ScanGuestString(r0, r1, FormatCursor{*this, 2u, 0u}));
        else if (name == "fopen") result = OpenGuestFile(r0, r1);
        else if (name == "fread") result = ReadGuestFile(r0, r1, r2, r3);
        else if (name == "fwrite") result = WriteGuestFile(r0, r1, r2, r3);
        else if (name == "fclose") result = CloseGuestFile(r0);
        else if (name == "fflush") { GuestFile* file = FindGuestFile(r0); result = file && file->stream ? static_cast<u32>(std::fflush(file->stream)) : 0; }
        else if (name == "fseek") result = static_cast<u32>(SeekGuestFile(r0, static_cast<s32>(r1), static_cast<int>(r2)));
        else if (name == "ftell") result = static_cast<u32>(TellGuestFile(r0));
        else if (name == "fgets") {
            GuestFile* file = FindGuestFile(r2);
            if (!file || !file->stream || r1 == 0) result = 0;
            else {
                std::vector<char> buffer(r1);
                char* line = std::fgets(buffer.data(), static_cast<int>(r1), file->stream);
                result = line && env_.WriteBytes(r0, buffer.data(), std::strlen(buffer.data()) + 1u) ? r0 : 0;
            }
        } else if (name == "fputs") { const std::string text = ReadCString(r0); GuestFile* file = FindGuestFile(r1); result = file && file->stream ? static_cast<u32>(std::fputs(text.c_str(), file->stream)) : static_cast<u32>(-1); }
        else if (name == "fputc" || name == "putc") { GuestFile* file = FindGuestFile(r1); result = file && file->stream ? static_cast<u32>(std::fputc(static_cast<int>(r0), file->stream)) : static_cast<u32>(-1); }
        else if (name == "getc") { GuestFile* file = FindGuestFile(r0); result = file && file->stream ? static_cast<u32>(std::fgetc(file->stream)) : static_cast<u32>(-1); }
        else if (name == "ungetc") { GuestFile* file = FindGuestFile(r1); result = file && file->stream ? static_cast<u32>(std::ungetc(static_cast<int>(r0), file->stream)) : static_cast<u32>(-1); }
        else if (name == "fileno") result = r0;
        else if (name == "fdopen") result = r0;
        else if (name == "open") {
            const std::string path = ReadCString(r0);
            const u32 mode_address = AllocateString((r1 & 0x400u) ? "ab" : (r1 & 0x201u) ? "wb" : "rb");
            const u32 path_address = AllocateString(path);
            result = OpenGuestFile(path_address, mode_address);
        } else if (name == "read") {
#ifdef _WIN32
            result = IsGuestSocket(r0) ? GuestReceive(r0, r1, r2, 0u) : ReadGuestFile(r1, 1, r2, r0);
#else
            result = ReadGuestFile(r1, 1, r2, r0);
#endif
        } else if (name == "write") {
#ifdef _WIN32
            result = IsGuestSocket(r0) ? GuestSend(r0, r1, r2, 0u) : WriteGuestFile(r1, 1, r2, r0);
#else
            result = WriteGuestFile(r1, 1, r2, r0);
#endif
        } else if (name == "lseek") { result = SeekGuestFile(r0, static_cast<s32>(r1), static_cast<int>(r2)) == 0 ? static_cast<u32>(TellGuestFile(r0)) : static_cast<u32>(-1); }
        else if (name == "close") {
#ifdef _WIN32
            result = IsGuestSocket(r0) ? GuestCloseSocket(r0) : CloseGuestFile(r0);
#else
            result = CloseGuestFile(r0);
#endif
        }
        else if (name == "stat" || name == "fstat") { if (r1) { std::array<u8,128> zero{}; env_.WriteBytes(r1, zero.data(), zero.size()); } result = 0; }
        else if (name == "setvbuf") result = 0;
        else if (name == "getcwd") {
            const std::string cwd = std::filesystem::current_path().string();
            if (r0 && r1 > cwd.size()) { env_.WriteBytes(r0, cwd.c_str(), cwd.size() + 1u); result = r0; }
        } else if (name == "basename") {
            const std::string path = ReadCString(r0);
            const std::size_t slash = path.find_last_of("/\\");
            result = slash == std::string::npos ? r0 : r0 + static_cast<u32>(slash + 1u);
        } else if (name == "crc32") {
            const u8* bytes = r1 && r2 ? static_cast<const u8*>(env_.HostPointer(r1, r2)) : Z_NULL;
            result = static_cast<u32>(crc32(r0, bytes, r2));
        } else if (name == "uncompress") {
            u32 output_size = r2 ? env_.MemoryRead32(r2) : 0;
            uLongf host_output_size = output_size;
            Bytef* output = static_cast<Bytef*>(env_.HostPointer(r0, output_size));
            const Bytef* input = static_cast<const Bytef*>(env_.HostPointer(r3, ArgWord(4)));
            const int code = output && input ? uncompress(output, &host_output_size, input, ArgWord(4)) : Z_STREAM_ERROR;
            if (r2) env_.MemoryWrite32(r2, static_cast<u32>(host_output_size));
            result = static_cast<u32>(code);
        } else if (name == "inflateInit_") result = static_cast<u32>(GuestInflateInit(r0, false, 15));
        else if (name == "inflateInit2_") result = static_cast<u32>(GuestInflateInit(r0, true, static_cast<s32>(r1)));
        else if (name == "inflate") result = static_cast<u32>(GuestZStreamProcess(r0, ZStreamKind::Inflate, static_cast<s32>(r1)));
        else if (name == "inflateReset") result = static_cast<u32>(GuestZStreamReset(r0, ZStreamKind::Inflate));
        else if (name == "inflateEnd") result = static_cast<u32>(GuestZStreamEnd(r0, ZStreamKind::Inflate));
        else if (name == "inflateSync") result = Z_OK;
        else if (name == "inflateCopy") result = static_cast<u32>(GuestInflateCopy(r0, r1));
        else if (name == "deflateInit_") result = static_cast<u32>(GuestDeflateInit(r0, static_cast<s32>(r1), false, 0, 0, 0, 0));
        else if (name == "deflateInit2_") result = static_cast<u32>(GuestDeflateInit(r0, static_cast<s32>(r1), true, static_cast<s32>(r2), static_cast<s32>(r3), static_cast<s32>(ArgWord(4)), static_cast<s32>(ArgWord(5))));
        else if (name == "deflate") result = static_cast<u32>(GuestZStreamProcess(r0, ZStreamKind::Deflate, static_cast<s32>(r1)));
        else if (name == "deflateReset") result = static_cast<u32>(GuestZStreamReset(r0, ZStreamKind::Deflate));
        else if (name == "deflateEnd") result = static_cast<u32>(GuestZStreamEnd(r0, ZStreamKind::Deflate));
        else if (name == "deflateParams") result = Z_OK;
        else if (name == "gzopen" || name == "gzread" || name == "gzclose") result = 0;
        else if (name == "__cxa_atexit") result = 0;
        else if (name == "__errno") result = errno_address_;
        else if (name == "__stack_chk_fail" || name == "abort" || name == "exit" || name == "longjmp" || name == "siglongjmp") return FatalImport(name);
        else if (name == "setjmp" || name == "sigsetjmp") result = 0;
        else if (name == "pthread_once") {
            const u32 state = env_.MemoryRead32(r0);
            if (state == 0 && !CallNestedInitializer(r0, r1)) return false;
            result = 0;
        } else if (name == "pthread_key_create") { const u32 key = next_pthread_key_++; if (r0) env_.MemoryWrite32(r0, key); result = 0; }
        else if (name == "pthread_key_delete") { thread_values_.erase(r0); result = 0; }
        else if (name == "pthread_setspecific") { thread_values_[r0] = r1; result = 0; }
        else if (name == "pthread_getspecific") result = thread_values_[r0];
        else if (name == "pthread_create") {
            result = InitializeCooperativeWorker(r0, r2, r3) ? 0u : static_cast<u32>(-1);
        } else if (name == "pthread_exit") {
            if (running_cooperative_worker_) { cooperative_worker_done_ = true; return true; }
            result = 0;
        } else if (name == "sem_init") {
            semaphores_[r0] = r2; result = 0;
        } else if (name == "sem_destroy") {
            semaphores_.erase(r0); result = 0;
        } else if (name == "sem_wait") {
            u32& count = semaphores_[r0];
            if (count) { --count; result = 0; }
            else if (running_cooperative_worker_) {
                cooperative_worker_yielded_ = true;
                return true; // Keep PC on sem_wait; resume when sem_post adds work.
            } else { SetGuestErrno(11); result = static_cast<u32>(-1); }
        } else if (name == "sem_post") {
            ++semaphores_[r0];
            cpu_.Regs()[0] = 0u;
            ResumeAfterStub(import.address);
            return running_cooperative_worker_ ? true : ResumeCooperativeWorker();
        } else if (name == "pthread_detach" || name == "pthread_mutex_init" ||
                   name == "pthread_mutex_destroy" || name == "pthread_mutex_lock" ||
                   name == "pthread_mutex_unlock" || name == "pthread_cond_broadcast" ||
                   name == "pthread_cond_wait") result = 0;
        else if (name == "gettimeofday") {
            const auto now = std::chrono::system_clock::now();
            const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
            const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now - seconds).count();
            if (r0) { env_.MemoryWrite32(r0, static_cast<u32>(seconds.time_since_epoch().count())); env_.MemoryWrite32(r0 + 4u, static_cast<u32>(micros)); }
            result = 0;
        } else if (name == "time") { const u32 value = static_cast<u32>(std::time(nullptr)); if (r0) env_.MemoryWrite32(r0, value); result = value; }
        else if (name == "clock") result = static_cast<u32>(std::clock());
        else if (name == "clock_gettime") {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
            const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - seconds);
            if (r1) { env_.MemoryWrite32(r1, static_cast<u32>(seconds.count())); env_.MemoryWrite32(r1 + 4u, static_cast<u32>(nanos.count())); }
            result = 0;
        } else if (name == "ftime") {
            const auto now = std::chrono::system_clock::now();
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            GuestTimebLayout value{}; value.time = static_cast<s32>(millis / 1000); value.millitm = static_cast<u16>(millis % 1000);
            result = env_.WriteBytes(r0, &value, sizeof(value)) ? 0 : static_cast<u32>(-1);
        } else if (name == "localtime" || name == "gmtime") { const std::time_t value = static_cast<std::time_t>(env_.MemoryRead32(r0)); result = WriteGuestTm(value, name == "gmtime"); }
        else if (name == "gmtime_r") { const std::time_t value = static_cast<std::time_t>(env_.MemoryRead32(r0)); const u32 temp = WriteGuestTm(value, true); if (temp && r1) CopyGuest(r1, temp, sizeof(GuestTmLayout)); result = temp ? r1 : 0; }
        else if (name == "strftime") {
            GuestTmLayout guest{}; env_.ReadBytes(r3, &guest, sizeof(guest));
            std::tm host{}; host.tm_sec=guest.tm_sec; host.tm_min=guest.tm_min; host.tm_hour=guest.tm_hour; host.tm_mday=guest.tm_mday; host.tm_mon=guest.tm_mon; host.tm_year=guest.tm_year; host.tm_wday=guest.tm_wday; host.tm_yday=guest.tm_yday; host.tm_isdst=guest.tm_isdst;
            std::vector<char> output(r1 ? r1 : 1u);
            const std::size_t written = std::strftime(output.data(), output.size(), ReadCString(r2).c_str(), &host);
            if (r0 && r1) env_.WriteBytes(r0, output.data(), output.size());
            result = static_cast<u32>(written);
        } else if (name == "wmemcpy" || name == "wmemmove") result = CopyGuest(r0,r1,r2*4u) ? r0 : 0;
        else if (name == "wmemset") { for(u32 i=0;i<r2;++i) env_.MemoryWrite32(r0+i*4u,r1); result=r0; }
        else if (name == "wmemcmp") { result=0; for(u32 i=0;i<r2;++i){const u32 a=env_.MemoryRead32(r0+i*4u),b=env_.MemoryRead32(r1+i*4u);if(a!=b){result=static_cast<u32>(a<b?-1:1);break;}} }
        else if (name == "wmemchr") { result=0; for(u32 i=0;i<r2;++i)if(env_.MemoryRead32(r0+i*4u)==r1){result=r0+i*4u;break;} }
        else if (name == "wcscoll") { result=0; u32 i=0; for(;;++i){const u32 a=env_.MemoryRead32(r0+i*4u),b=env_.MemoryRead32(r1+i*4u);if(a!=b){result=static_cast<u32>(a<b?-1:1);break;}if(!a)break;} }
        else if (name == "wcsxfrm") { u32 length=0;while(env_.MemoryRead32(r1+length*4u))++length;if(r0&&r2){const u32 copy=std::min<u32>(length,r2-1u);CopyGuest(r0,r1,copy*4u);env_.MemoryWrite32(r0+copy*4u,0);}result=length; }
        else if (name == "wcsftime") { if(r0&&r1)env_.MemoryWrite32(r0,0); result=0; }
        else if (name == "mbrtowc") { if(!r1||!r2)result=0;else{const u8 c=env_.MemoryRead8(r1);if(r0)env_.MemoryWrite32(r0,c);result=c?1u:0u;} }
        else if (name == "wcrtomb") { if(!r0)result=1;else{env_.MemoryWrite8(r0,static_cast<u8>(r1));result=1;} }
        else if (name == "getwc") { GuestFile* file=FindGuestFile(r0);const int c=file&&file->stream?std::fgetc(file->stream):EOF;result=c==EOF?static_cast<u32>(-1):static_cast<u8>(c); }
        else if (name == "putwc") { GuestFile* file=FindGuestFile(r1);result=file&&file->stream?static_cast<u32>(std::fputc(static_cast<u8>(r0),file->stream)):static_cast<u32>(-1); }
        else if (name == "ungetwc") { GuestFile* file=FindGuestFile(r1);result=file&&file->stream?static_cast<u32>(std::ungetc(static_cast<u8>(r0),file->stream)):static_cast<u32>(-1); }
        else if (name == "strxfrm") { const std::string text=ReadCString(r1);if(r0&&r2){const std::size_t n=std::min<std::size_t>(text.size(),r2-1u);env_.WriteBytes(r0,text.data(),n);env_.MemoryWrite8(r0+static_cast<u32>(n),0);}result=static_cast<u32>(text.size()); }
        else if (name == "wctob") result = r0 <= 0xffu ? r0 : static_cast<u32>(-1);
        else if (name == "btowc") result = r0 == static_cast<u32>(-1) ? static_cast<u32>(-1) : static_cast<u8>(r0);
        else if (name == "wctype") {
            const std::string type = ReadCString(r0);
            static const std::array<const char*,12> classes={"alnum","alpha","blank","cntrl","digit","graph","lower","print","punct","space","upper","xdigit"};
            for (u32 i=0;i<classes.size();++i) if(type==classes[i]) { result=i+1u; break; }
        } else if (name == "iswctype") {
            const int c = r0 <= 0xffu ? static_cast<int>(r0) : 0;
            switch (r1) { case 1: result=std::isalnum(c)!=0; break; case 2: result=std::isalpha(c)!=0; break; case 3: result=c==' '||c=='\t'; break; case 4: result=std::iscntrl(c)!=0; break; case 5: result=std::isdigit(c)!=0; break; case 6: result=std::isgraph(c)!=0; break; case 7: result=std::islower(c)!=0; break; case 8: result=std::isprint(c)!=0; break; case 9: result=std::ispunct(c)!=0; break; case 10: result=std::isspace(c)!=0; break; case 11: result=std::isupper(c)!=0; break; case 12: result=std::isxdigit(c)!=0; break; default: result=0; }
        } else if (name == "tolower" || name == "towlower") result = static_cast<u32>(std::tolower(static_cast<unsigned char>(r0)));
        else if (name == "toupper" || name == "towupper") result = static_cast<u32>(std::toupper(static_cast<unsigned char>(r0)));
        else if (name == "setlocale") result = c_locale_address_;
        else if (name == "getenv") result = 0;
        else if (name == "geteuid") result = 1000;
        else if (name == "gethostname") {
#ifdef _WIN32
            std::vector<char> host(std::max<u32>(r1, 1u), 0);
            const int code = ::gethostname(host.data(), static_cast<int>(host.size()));
            if (code == 0 && r0 && r1) env_.WriteBytes(r0, host.data(), host.size());
            result = code == 0 ? 0u : SocketFailure();
#else
            const char host[]="dynarmic-win64"; if (r0 && r1) { const std::size_t n=std::min<std::size_t>(sizeof(host),r1); env_.WriteBytes(r0,host,n); } result=0;
#endif
        }
        else if (name == "getopt") result = static_cast<u32>(-1);
        else if (name == "getpwuid") result = 0;
        else if (name == "strerror" || name == "strerror_r") { const u32 text=AllocateString("host error"); if(name=="strerror_r"&&r1&&r2){const std::string v=ReadCString(text);const std::size_t n=std::min<std::size_t>(v.size(),r2-1u);env_.WriteBytes(r1,v.data(),n);env_.MemoryWrite8(r1+static_cast<u32>(n),0);result=0;}else result=text; }
        else if (name == "arc4random") result = 0x9e3779b9u ^ static_cast<u32>(import.calls * 2654435761u);
        else if (name == "srand48") { random_state_ = r0 ? r0 : 1u; result = 0; }
        else if (name == "lrand48") { random_state_ = random_state_ * 25214903917ull + 11ull; result = static_cast<u32>((random_state_ >> 17) & 0x7fffffffu); }
        else if (name == "sin") { ReturnDouble(std::sin(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "cos") { ReturnDouble(std::cos(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "tan") { ReturnDouble(std::tan(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "asin") { ReturnDouble(std::asin(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "acos") { ReturnDouble(std::acos(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "atan") { ReturnDouble(std::atan(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "atan2") { ReturnDouble(std::atan2(WordsToDouble(r0,r1),WordsToDouble(r2,r3))); result_set=false; }
        else if (name == "sqrt") { ReturnDouble(std::sqrt(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "floor") { ReturnDouble(std::floor(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "ceil") { ReturnDouble(std::ceil(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "round") { ReturnDouble(std::round(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "roundf") result = FloatToWord(std::round(WordToFloat(r0)));
        else if (name == "pow") { ReturnDouble(std::pow(WordsToDouble(r0,r1),WordsToDouble(r2,r3))); result_set=false; }
        else if (name == "fmod") { ReturnDouble(std::fmod(WordsToDouble(r0,r1),WordsToDouble(r2,r3))); result_set=false; }
        else if (name == "exp") { ReturnDouble(std::exp(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "log") { ReturnDouble(std::log(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "log10") { ReturnDouble(std::log10(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "sinh") { ReturnDouble(std::sinh(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "cosh") { ReturnDouble(std::cosh(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "tanh") { ReturnDouble(std::tanh(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "ldexp") { ReturnDouble(std::ldexp(WordsToDouble(r0,r1),static_cast<s32>(r2))); result_set=false; }
        else if (name == "frexp") { int exponent=0; const double value=std::frexp(WordsToDouble(r0,r1),&exponent); if(r2)env_.MemoryWrite32(r2,static_cast<u32>(exponent)); ReturnDouble(value); result_set=false; }
        else if (name == "modf") { double integral=0; const double fraction=std::modf(WordsToDouble(r0,r1),&integral); if(r2){u64 bits=0;std::memcpy(&bits,&integral,sizeof(bits));env_.MemoryWrite64(r2,bits);} ReturnDouble(fraction); result_set=false; }
        else if (name == "__fpclassifyd") result = static_cast<u32>(std::fpclassify(WordsToDouble(r0,r1)));
        else if (name == "qsort") { if (!GuestQsort(r0,r1,r2,r3)) return false; result=0; }
        else if (name == "bsearch") result = GuestBsearch(r0,r1,r2,r3,ArgWord(4));
        else if (name == "__android_log_print") {
            FormatCursor cursor{*this,3u,0u};
            const std::string text=FormatGuestString(r2,cursor);
            last_android_log_ = text.size() <= 160u ? text : text.substr(0, 160u);
            log_ << "android log: " << text << '\n';
            result=static_cast<u32>(text.size());
        } else if (name == "__gnu_Unwind_Find_exidx") { if(r1)env_.MemoryWrite32(r1,0); result=0; }
        else if (name == "dlopen" || name == "dlsym" || name == "dlclose" || name == "dlerror") result=0;
        else if (name == "mmap") result=AllocateAligned(r1, kPageSize);
        else if (name == "munmap") { Free(r0); result=0; }
        else if (name == "socket") {
#ifdef _WIN32
            result = GuestSocket(r0, r1, r2);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "connect") {
#ifdef _WIN32
            result = GuestConnect(r0, r1, r2);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "bind") {
#ifdef _WIN32
            result = GuestBind(r0, r1, r2);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "listen") {
#ifdef _WIN32
            result = GuestListen(r0, r1);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "accept") {
#ifdef _WIN32
            result = GuestAccept(r0, r1, r2);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "send") {
#ifdef _WIN32
            result = GuestSend(r0, r1, r2, r3);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "recv") {
#ifdef _WIN32
            result = GuestReceive(r0, r1, r2, r3);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "sendto") {
#ifdef _WIN32
            result = GuestSendTo(r0, r1, r2, r3, ArgWord(4), ArgWord(5));
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "recvfrom") {
#ifdef _WIN32
            result = GuestReceiveFrom(r0, r1, r2, r3, ArgWord(4), ArgWord(5));
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "setsockopt") {
#ifdef _WIN32
            result = GuestSetSockOpt(r0, r1, r2, r3, ArgWord(4));
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "getsockopt") {
#ifdef _WIN32
            result = GuestGetSockOpt(r0, r1, r2, r3, ArgWord(4));
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "getpeername" || name == "getsockname") {
#ifdef _WIN32
            result = GuestSocketName(r0, r1, r2, name == "getpeername");
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "poll") {
#ifdef _WIN32
            result = GuestPoll(r0, r1, static_cast<s32>(r2));
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "ioctl") {
#ifdef _WIN32
            result = GuestIoctl(r0, r1, r2);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "fcntl") {
#ifdef _WIN32
            result = GuestFcntl(r0, r1, r2);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "getaddrinfo") {
#ifdef _WIN32
            result = GuestGetAddrInfo(r0, r1, r2, r3);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "freeaddrinfo") {
#ifdef _WIN32
            result = GuestFreeAddrInfo(r0);
#else
            result = 0;
#endif
        } else if (name == "inet_ntop") {
#ifdef _WIN32
            result = GuestInetNtop(r0, r1, r2, r3);
#else
            result = 0;
#endif
        } else if (name == "inet_pton") {
#ifdef _WIN32
            result = GuestInetPton(r0, r1, r2);
#else
            result = 0;
#endif
        } else if (name == "alarm" || name == "raise" || name == "sigaction") result = 0;
        else {
            ++permissive_stub_calls_;
            permissive_names_.insert(name);
            if (!import.warned) { import.warned=true; log_ << "Dynarmic permissive runtime stub: " << name << " -> 0\n"; log_.flush(); }
            result=0;
        }
        if (result_set) cpu_.Regs()[0]=result;
        ResumeAfterStub(import.address);
        return true;
    }

    ProbeEnvironment& env_;
    ElfRuntime& runtime_;
    std::ostream& log_;
    Dynarmic::A32::Jit cpu_;
    u32 heap_cursor_=0;
    std::map<u32,u32> allocations_;
    std::map<u32,u32> free_blocks_;
    u64 live_allocation_bytes_=0;
    u64 peak_live_allocation_bytes_=0;
    u64 allocation_calls_=0;
    u64 free_calls_=0;
    u64 reallocation_calls_=0;
    u64 allocation_failures_=0;
    u64 ignored_free_calls_=0;
    u32 errno_address_=0;
    u32 c_locale_address_=0;
    u32 tm_address_=0;
    u32 time_zone_address_=0;
    u32 next_pthread_key_=1;
    u32 next_thread_id_=1;
    std::unordered_map<u32,u32> thread_values_;
    std::unordered_map<u32,u32> semaphores_;
    CooperativeWorkerContext cooperative_worker_;
    bool running_cooperative_worker_=false;
    bool cooperative_worker_yielded_=false;
    bool cooperative_worker_done_=false;
    u64 network_worker_runs_=0;
    u64 network_poll_log_count_=0;
    u64 random_state_=1;
    unsigned call_depth_=0;
    u64 permissive_stub_calls_=0;
    u64 total_import_calls_=0;
    u64 jni_svc_calls_=0;
    u64 gl_calls_=0;
    u64 gl_draw_calls_=0;
    u64 gl_draw_vertices_=0;
    u64 gl_buffer_upload_bytes_=0;
    u64 gl_texture_upload_bytes_=0;
    GuestCallMetrics last_call_metrics_;
    std::set<std::string> permissive_names_;
    std::deque<std::string> recent_events_;
    std::vector<std::string> active_calls_;
    u64 host_event_sequence_=0;
    u64 message_box_count_=0;
    u64 last_assert_sequence_=0;
    std::string last_assert_title_;
    std::string last_assert_text_;
    std::string last_android_log_;
    std::string last_error_;
    std::vector<GuestRef> refs_;
    std::set<u32> unimplemented_jni_slots_;
    std::unordered_map<u32,GuestFile> files_;
    std::unordered_map<u32, HostUnzState> host_unz_;
    u32 next_file_id_=1;
    u32 stdin_handle_=0;
    u32 stdout_handle_=0;
    u32 stderr_handle_=0;
    std::set<std::string> logged_file_failures_;
    u32 strtok_state_=0;
    std::unordered_map<u32,GuestZStream> zstreams_;
    u64 zlib_init_logs_=0;
    std::string apk_path_;
    std::string writable_path_;
#ifdef _WIN32
    bool winsock_initialized_=false;
    u32 next_socket_fd_=0x4000u;
    struct SyntheticHttpResponse {
        std::vector<u8> bytes;
        std::size_t offset = 0u;
        bool eof_pending = true;
    };
    std::unordered_map<u32, SOCKET> sockets_;
    std::unordered_map<u32, SyntheticHttpResponse> synthetic_http_;
    std::unordered_set<u32> nonblocking_sockets_;
    std::unordered_set<u32> socket_send_logged_;
    std::unordered_set<u32> socket_receive_logged_;
    std::unordered_map<u32, GuestAddrInfoAllocation> guest_addrinfo_;
    u64 network_log_count_=0;
#endif
    const std::vector<u8>* apk_image_ = nullptr;
    ApkMemberCache apk_member_cache_;
    u64 apk_memory_open_logs_ = 0;
    u64 file_open_logs_ = 0;
    u64 apk_memory_read_calls_ = 0;
    u64 apk_memory_read_bytes_ = 0;
    bool text_input_active_ = false;
    bool termination_requested_ = false;
    WinGlHost gl_;
    double frame_interval_=1.0/60.0;
    std::unordered_map<u32,u32> gl_string_cache_;
    u32 gl_array_buffer_binding_=0;
    u32 gl_element_buffer_binding_=0;
    u64 logged_guest_stdio_=0;
    bool audio_initialized_=false;
    u32 touch_ids_=0;
    u32 touch_xs_=0;
    u32 touch_ys_=0;
};

struct FrameProfileSample {
    u64 frame = 0;
    double total_ms = 0.0;
    double event_ms = 0.0;
    double render_ms = 0.0;
    double swap_ms = 0.0;
    double gpu_ms = -1.0;
    u64 estimated_ticks = 0;
    u64 jit_runs = 0;
    u64 svc_calls = 0;
    u64 import_calls = 0;
    u64 jni_calls = 0;
    u64 gl_calls = 0;
    u64 draw_calls = 0;
    u64 draw_vertices = 0;
    u64 buffer_upload_bytes = 0;
    u64 texture_upload_bytes = 0;
    u64 allocation_calls = 0;
    u64 free_calls = 0;
    u64 reallocation_calls = 0;
    u64 live_heap_bytes = 0;
    std::string scene_hint;
    std::string top_imports;
    std::string top_gl;
};

static u64 CounterDelta(u64 before, u64 after) {
    return after >= before ? after - before : 0u;
}

static std::string CsvField(const std::string& value) {
    bool quote = false;
    for (const char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            quote = true;
            break;
        }
    }
    if (!quote) return value;
    std::string result;
    result.reserve(value.size() + 2u);
    result.push_back('"');
    for (const char c : value) {
        if (c == '"') result.push_back('"');
        result.push_back(c);
    }
    result.push_back('"');
    return result;
}

static double Percentile(std::vector<double> values, double percentile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(percentile, 0.0, 1.0) *
                            static_cast<double>(values.size() - 1u);
    const std::size_t low = static_cast<std::size_t>(position);
    const std::size_t high = std::min(low + 1u, values.size() - 1u);
    const double fraction = position - static_cast<double>(low);
    return values[low] + (values[high] - values[low]) * fraction;
}

#ifdef _WIN32
static double CurrentProcessCpuMilliseconds() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0.0;
    ULARGE_INTEGER kernel_value{}, user_value{};
    kernel_value.LowPart = kernel.dwLowDateTime;
    kernel_value.HighPart = kernel.dwHighDateTime;
    user_value.LowPart = user.dwLowDateTime;
    user_value.HighPart = user.dwHighDateTime;
    return static_cast<double>(kernel_value.QuadPart + user_value.QuadPart) /
           10000.0;
}

static std::pair<u64, u64> CurrentProcessMemoryBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            static_cast<DWORD>(sizeof(counters))))
        return {0u, 0u};
    return {static_cast<u64>(counters.WorkingSetSize),
            static_cast<u64>(counters.PrivateUsage)};
}

static std::string HostSystemProfile() {
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    std::array<char, 256> processor{};
    const DWORD processor_length = GetEnvironmentVariableA(
        "PROCESSOR_IDENTIFIER", processor.data(),
        static_cast<DWORD>(processor.size()));
    std::ostringstream output;
    output << "logical_cpus=" << system.dwNumberOfProcessors
           << " page_size=" << system.dwPageSize
           << " ram_mb=" << (memory.ullTotalPhys / (1024ull * 1024ull))
           << " cpu=\"";
    if (processor_length > 0 && processor_length < processor.size())
        output << processor.data();
    else
        output << "unknown";
    output << "\"";
    return output.str();
}
#else
static double CurrentProcessCpuMilliseconds() { return 0.0; }
static std::pair<u64, u64> CurrentProcessMemoryBytes() { return {0u, 0u}; }
static std::string HostSystemProfile() { return "host-profile-unavailable"; }
#endif

class FrameProfiler {
public:
    FrameProfiler(std::ostream& log, std::string csv_path,
                  std::string summary_path, double slow_threshold_ms)
        : log_(log),
          csv_path_(std::move(csv_path)),
          summary_path_(std::move(summary_path)),
          slow_threshold_ms_(slow_threshold_ms) {}

    std::size_t SampleCount() const { return samples_.size(); }

    void SetGpuTiming(u64 frame, double gpu_ms) {
        if (frame == 0 || frame > samples_.size()) return;
        samples_[static_cast<std::size_t>(frame - 1u)].gpu_ms = gpu_ms;
    }

    void Add(FrameProfileSample sample) {
        const bool slow = IsSlow(sample);
        if (slow) {
            ++slow_frame_count_;
            sample.top_imports =
                sample.top_imports.empty() ? "-" : sample.top_imports;
            sample.top_gl = sample.top_gl.empty() ? "-" : sample.top_gl;
            log_ << "Dynarmic slow frame #" << sample.frame
                 << " total_ms=" << std::fixed << std::setprecision(2)
                 << sample.total_ms
                 << " event_ms=" << sample.event_ms
                 << " render_ms=" << sample.render_ms
                 << " swap_ms=" << sample.swap_ms
                 << " gpu_ms=" << sample.gpu_ms
                 << " imports=" << sample.import_calls
                 << " jni=" << sample.jni_calls
                 << " gl=" << sample.gl_calls
                 << " draws=" << sample.draw_calls
                 << " vertices=" << sample.draw_vertices
                 << " alloc/free/realloc=" << sample.allocation_calls << '/'
                 << sample.free_calls << '/' << sample.reallocation_calls
                 << " ticks=" << sample.estimated_ticks
                 << " jit_runs=" << sample.jit_runs
                 << " svc=" << sample.svc_calls
                 << " scene=\"" << sample.scene_hint << "\""
                 << " top-imports={" << sample.top_imports << "}"
                 << " top-gl={" << sample.top_gl << "}\n";
        }
        samples_.push_back(std::move(sample));
    }

    void LogInterval(std::size_t begin, double wall_ms,
                     double process_cpu_before_ms,
                     double process_cpu_after_ms) {
        if (begin >= samples_.size() || wall_ms <= 0.0) return;
        std::vector<double> total;
        std::vector<double> render;
        std::vector<double> swap;
        std::vector<double> gpu;
        total.reserve(samples_.size() - begin);
        render.reserve(samples_.size() - begin);
        swap.reserve(samples_.size() - begin);
        gpu.reserve(samples_.size() - begin);
        u64 imports = 0;
        u64 gl_calls = 0;
        u64 draws = 0;
        u64 slow = 0;
        for (std::size_t index = begin; index < samples_.size(); ++index) {
            const FrameProfileSample& sample = samples_[index];
            total.push_back(sample.total_ms);
            render.push_back(sample.render_ms);
            swap.push_back(sample.swap_ms);
            if (sample.gpu_ms >= 0.0) gpu.push_back(sample.gpu_ms);
            imports += sample.import_calls;
            gl_calls += sample.gl_calls;
            draws += sample.draw_calls;
            if (IsSlow(sample)) ++slow;
        }
        const double fps = static_cast<double>(total.size()) * 1000.0 / wall_ms;
        const double cpu_delta =
            std::max(0.0, process_cpu_after_ms - process_cpu_before_ms);
        const double cpu_percent = wall_ms > 0.0 ? cpu_delta * 100.0 / wall_ms : 0.0;
        const auto [working_set, private_bytes] = CurrentProcessMemoryBytes();
        log_ << std::fixed << std::setprecision(1)
             << "Dynarmic debug-everything interval: fps=" << fps
             << " frames=" << total.size()
             << " frame_p50/p95/p99/max_ms="
             << std::setprecision(2)
             << Percentile(total, 0.50) << '/'
             << Percentile(total, 0.95) << '/'
             << Percentile(total, 0.99) << '/'
             << *std::max_element(total.begin(), total.end())
             << " render_p95_ms=" << Percentile(render, 0.95)
             << " swap_p95_ms=" << Percentile(swap, 0.95);
        if (!gpu.empty())
            log_ << " gpu_p95_ms=" << Percentile(gpu, 0.95);
        log_ << " slow=" << slow
             << " imports/frame=" << (imports / total.size())
             << " gl/frame=" << (gl_calls / total.size())
             << " draws/frame=" << (draws / total.size())
             << " process_cpu=" << std::setprecision(1) << cpu_percent << '%'
             << " working_set_mb=" << std::setprecision(1)
             << static_cast<double>(working_set) / (1024.0 * 1024.0)
             << " private_mb="
             << static_cast<double>(private_bytes) / (1024.0 * 1024.0)
             << '\n';
    }

    void WriteFiles(const GuestExecutor& executor) const {
        WriteCsv();
        WriteSummary(executor);
    }

private:
    bool IsSlow(const FrameProfileSample& sample) const {
        return sample.total_ms >= slow_threshold_ms_ ||
               sample.event_ms >= slow_threshold_ms_ ||
               sample.render_ms >= slow_threshold_ms_ ||
               sample.swap_ms >= slow_threshold_ms_;
    }

    void WriteCsv() const {
        std::ofstream file(csv_path_, std::ios::trunc);
        if (!file) {
            log_ << "WARNING: could not create frame profile CSV: "
                 << csv_path_ << '\n';
            return;
        }
        file << "frame,total_ms,event_ms,render_ms,swap_ms,gpu_ms,"
                "estimated_ticks,jit_runs,svc_calls,import_calls,jni_calls,"
                "gl_calls,draw_calls,draw_vertices,buffer_upload_bytes,"
                "texture_upload_bytes,allocation_calls,free_calls,"
                "reallocation_calls,live_heap_bytes,scene_hint,top_imports,"
                "top_gl\n";
        file << std::fixed << std::setprecision(4);
        for (const FrameProfileSample& sample : samples_) {
            file << sample.frame << ','
                 << sample.total_ms << ','
                 << sample.event_ms << ','
                 << sample.render_ms << ','
                 << sample.swap_ms << ','
                 << sample.gpu_ms << ','
                 << sample.estimated_ticks << ','
                 << sample.jit_runs << ','
                 << sample.svc_calls << ','
                 << sample.import_calls << ','
                 << sample.jni_calls << ','
                 << sample.gl_calls << ','
                 << sample.draw_calls << ','
                 << sample.draw_vertices << ','
                 << sample.buffer_upload_bytes << ','
                 << sample.texture_upload_bytes << ','
                 << sample.allocation_calls << ','
                 << sample.free_calls << ','
                 << sample.reallocation_calls << ','
                 << sample.live_heap_bytes << ','
                 << CsvField(sample.scene_hint) << ','
                 << CsvField(sample.top_imports) << ','
                 << CsvField(sample.top_gl) << '\n';
        }
    }

    void WriteSummary(const GuestExecutor& executor) const {
        std::ofstream file(summary_path_, std::ios::trunc);
        if (!file) {
            log_ << "WARNING: could not create frame profile summary: "
                 << summary_path_ << '\n';
            return;
        }
        std::vector<double> total;
        std::vector<double> event;
        std::vector<double> render;
        std::vector<double> swap;
        std::vector<double> gpu;
        total.reserve(samples_.size());
        event.reserve(samples_.size());
        render.reserve(samples_.size());
        swap.reserve(samples_.size());
        gpu.reserve(samples_.size());
        u64 over_16 = 0;
        u64 over_20 = 0;
        u64 over_25 = 0;
        u64 over_33 = 0;
        u64 over_50 = 0;
        u64 imports = 0;
        u64 gl_calls = 0;
        u64 draw_calls = 0;
        u64 vertices = 0;
        u64 allocations = 0;
        u64 frees = 0;
        for (const FrameProfileSample& sample : samples_) {
            total.push_back(sample.total_ms);
            event.push_back(sample.event_ms);
            render.push_back(sample.render_ms);
            swap.push_back(sample.swap_ms);
            if (sample.gpu_ms >= 0.0) gpu.push_back(sample.gpu_ms);
            if (sample.total_ms > 16.667) ++over_16;
            if (sample.total_ms > 20.0) ++over_20;
            if (sample.total_ms > 25.0) ++over_25;
            if (sample.total_ms > 33.333) ++over_33;
            if (sample.total_ms > 50.0) ++over_50;
            imports += sample.import_calls;
            gl_calls += sample.gl_calls;
            draw_calls += sample.draw_calls;
            vertices += sample.draw_vertices;
            allocations += sample.allocation_calls;
            frees += sample.free_calls;
        }
        file << "Geometry Dash Wrapper 0.9.5-unified3 legacy ARM debug profile\n";
        file << "frames=" << samples_.size() << '\n';
        file << "slow_threshold_ms=" << slow_threshold_ms_ << '\n';
        file << "slow_frames=" << slow_frame_count_ << '\n';
        if (!samples_.empty()) {
            const auto average = [](const std::vector<double>& values) {
                return std::accumulate(values.begin(), values.end(), 0.0) /
                       static_cast<double>(values.size());
            };
            file << std::fixed << std::setprecision(4);
            file << "frame_avg_ms=" << average(total) << '\n';
            file << "frame_p50_ms=" << Percentile(total, 0.50) << '\n';
            file << "frame_p90_ms=" << Percentile(total, 0.90) << '\n';
            file << "frame_p95_ms=" << Percentile(total, 0.95) << '\n';
            file << "frame_p99_ms=" << Percentile(total, 0.99) << '\n';
            file << "frame_p999_ms=" << Percentile(total, 0.999) << '\n';
            file << "frame_max_ms="
                 << *std::max_element(total.begin(), total.end()) << '\n';
            file << "event_p95_ms=" << Percentile(event, 0.95) << '\n';
            file << "render_p95_ms=" << Percentile(render, 0.95) << '\n';
            file << "swap_p95_ms=" << Percentile(swap, 0.95) << '\n';
            if (!gpu.empty()) {
                file << "gpu_p50_ms=" << Percentile(gpu, 0.50) << '\n';
                file << "gpu_p95_ms=" << Percentile(gpu, 0.95) << '\n';
                file << "gpu_p99_ms=" << Percentile(gpu, 0.99) << '\n';
                file << "gpu_max_ms="
                     << *std::max_element(gpu.begin(), gpu.end()) << '\n';
            }
        }
        file << "frames_over_16_667_ms=" << over_16 << '\n';
        file << "frames_over_20_ms=" << over_20 << '\n';
        file << "frames_over_25_ms=" << over_25 << '\n';
        file << "frames_over_33_333_ms=" << over_33 << '\n';
        file << "frames_over_50_ms=" << over_50 << '\n';
        file << "profiled_import_calls=" << imports << '\n';
        file << "profiled_gl_calls=" << gl_calls << '\n';
        file << "profiled_draw_calls=" << draw_calls << '\n';
        file << "profiled_draw_vertices=" << vertices << '\n';
        file << "profiled_allocations=" << allocations << '\n';
        file << "profiled_frees=" << frees << '\n';
        file << "top_imports=" << executor.DescribeTopImports(20u, false)
             << '\n';
        file << "top_gl=" << executor.DescribeTopImports(20u, true) << '\n';
        file << "sampled_host_costs="
             << executor.DescribeTopImportHostSamples(20u) << '\n';

        std::vector<const FrameProfileSample*> worst;
        worst.reserve(samples_.size());
        for (const FrameProfileSample& sample : samples_)
            worst.push_back(&sample);
        std::sort(worst.begin(), worst.end(),
                  [](const FrameProfileSample* lhs,
                     const FrameProfileSample* rhs) {
                      return lhs->total_ms > rhs->total_ms;
                  });
        file << "\nWorst frames:\n";
        const std::size_t shown = std::min<std::size_t>(50u, worst.size());
        for (std::size_t index = 0; index < shown; ++index) {
            const FrameProfileSample& sample = *worst[index];
            file << "frame=" << sample.frame
                 << " total_ms=" << sample.total_ms
                 << " event_ms=" << sample.event_ms
                 << " render_ms=" << sample.render_ms
                 << " swap_ms=" << sample.swap_ms
                 << " imports=" << sample.import_calls
                 << " gl=" << sample.gl_calls
                 << " draws=" << sample.draw_calls
                 << " scene=" << sample.scene_hint
                 << " top_imports=" << sample.top_imports
                 << " top_gl=" << sample.top_gl << '\n';
        }
    }

    std::ostream& log_;
    std::string csv_path_;
    std::string summary_path_;
    double slow_threshold_ms_ = 20.0;
    u64 slow_frame_count_ = 0;
    std::vector<FrameProfileSample> samples_;
};

static std::string Utf8FromCodepoint(u32 codepoint) {
    std::string result;
    if (codepoint <= 0x7Fu) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        result.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) return {};
        result.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0x10FFFFu) {
        result.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    return result;
}

static void RunThumbSmoke() {
    ProbeEnvironment env;
    env.Map(kSmokeBase,kPageSize,true);
    env.MemoryWrite16(kSmokeBase+0,0x0088u);
    env.MemoryWrite16(kSmokeBase+2,0xE7FEu);
    Dynarmic::A32::UserConfig config;
    config.callbacks=&env;
    config.arch_version=Dynarmic::A32::ArchVersion::v5TE;
    config.check_halt_on_memory_access=true;
    Dynarmic::A32::Jit cpu{config};
    env.AttachCpu(&cpu);
    cpu.Regs().fill(0); cpu.Regs()[0]=1; cpu.Regs()[1]=2; cpu.Regs()[15]=kSmokeBase;
    cpu.SetCpsr(0x30u); env.ticks_left=1; cpu.Run();
    if(env.invalid_access||env.interpreter_fallback||env.exception_seen||cpu.Regs()[0]!=8) throw std::runtime_error("Dynarmic Thumb smoke failed");
}

} // namespace

static std::ostream* g_runtime_log_stream = nullptr;

extern "C" void runtime_log(const char* format, ...) {
    if (!format) return;
    std::array<char, 4096> buffer{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    std::cerr << "[host] " << buffer.data() << '\n';
    if (g_runtime_log_stream)
        *g_runtime_log_stream << "[host] " << buffer.data() << '\n';
}

int main(int argc,char** argv) {
    std::string log_path = "gd-dynarmic-interactive.log";
    std::string profile_path = "gd-dynarmic-profile.csv";
    std::string profile_summary_path = "gd-dynarmic-profile-summary.txt";
    double slow_frame_ms = 20.0;
    bool profile_enabled = true;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument.rfind("--log=", 0) == 0 && argument.size() > 6u)
            log_path = std::string(argument.substr(6));
        else if (argument.rfind("--profile=", 0) == 0 &&
                 argument.size() > 10u)
            profile_path = std::string(argument.substr(10));
        else if (argument.rfind("--profile-summary=", 0) == 0 &&
                 argument.size() > 18u)
            profile_summary_path = std::string(argument.substr(18));
        else if (argument.rfind("--slow-frame-ms=", 0) == 0 &&
                 argument.size() > 16u)
            slow_frame_ms = std::max(
                5.0, std::stod(std::string(argument.substr(16))));
        else if (argument == "--no-profile")
            profile_enabled = false;
    }

    std::array<char, 1024u * 1024u> log_buffer{};
    std::ofstream log_file;
    log_file.rdbuf()->pubsetbuf(log_buffer.data(),
                                static_cast<std::streamsize>(log_buffer.size()));
    log_file.open(log_path, std::ios::trunc);
    if (!log_file) {
        std::cerr << "Could not create log file: " << log_path << '\n';
        return 1;
    }
    g_runtime_log_stream = &log_file;
    auto emit=[&](const std::string& line){
        std::cout<<line<<'\n';
        log_file<<line<<'\n';
        log_file.flush();
    };
    try {
        emit(std::string("Geometry Dash Wrapper ") + GD_WRAPPER_VERSION + " backend=" + GD_ARM_LEGACY_BACKEND_NAME);
        emit("Milestone: host minizip acceleration, complete background-music pre-cache, asynchronous sound effects, and first-text-input optimization");
        emit("Log file: " + log_path);
        if (profile_enabled) {
            emit("Frame profile CSV: " + profile_path);
            emit("Frame profile summary: " + profile_summary_path);
            emit("Slow frame threshold: " + std::to_string(slow_frame_ms) +
                 " ms");
        }
        emit("Host pointer bits: "+std::to_string(sizeof(void*)*8));
        emit("RESULT: DYNARMIC_HOST_PROFILE " + HostSystemProfile());
        if(sizeof(void*)!=8)
            throw std::runtime_error(
                "The legacy ARM Dynarmic backend must be compiled as a 64-bit executable");
        RunThumbSmoke();
        emit("RESULT: DYNARMIC_X64_THUMB_SMOKE_OK r0=8 guest=v5TE host=x86_64");
        emit("RESULT: DYNARMIC_GUEST_PAGE_LOOKUP_READY pages=1048576 typed-access=single-copy");
        emit("RESULT: DYNARMIC_DEBUG_EVERYTHING_READY frame-csv=1 slow-frame-dumps=1 import-gl-heap-counters=1 async-effects=1");

        std::string apk_path="game.apk";
        bool probe_only=false;
        int width=1280,height=720,max_frames=0;
        for(int i=1;i<argc;++i){
            const std::string_view argument(argv[i]);
            if(argument=="--probe-only") probe_only=true;
            else if(argument=="--debug-everything") {}
            else if(argument.rfind("--frames=",0)==0)
                max_frames=std::max(
                    1,std::stoi(std::string(argument.substr(9))));
            else if(argument.rfind("--width=",0)==0)
                width=std::max(
                    320,std::stoi(std::string(argument.substr(8))));
            else if(argument.rfind("--height=",0)==0)
                height=std::max(
                    240,std::stoi(std::string(argument.substr(9))));
            else if(!argument.empty()&&argument[0]!='-')
                apk_path=std::string(argument);
        }
        const std::filesystem::path absolute_apk=
            std::filesystem::absolute(apk_path);
        const std::filesystem::path writable=
            std::filesystem::absolute("save");
        emit("Input APK: "+absolute_apk.string());
        const std::vector<u8> apk=ReadFile(absolute_apk.string());
        emit("APK bytes: "+std::to_string(apk.size()));
        const std::vector<u8> libgame=
            ExtractZipMember(apk,"lib/armeabi/libgame.so");
        emit("Extracted lib/armeabi/libgame.so: "+
             std::to_string(libgame.size())+" bytes");
        ProbeEnvironment env;
        ElfRuntime runtime=MapAndRelocateElf(libgame,env);
        const std::size_t zip_hooks=InstallCcFileUtilsZipHooks(runtime,env);
        const std::size_t minizip_hooks=InstallHostMinizipHooks(runtime,env);
        if(zip_hooks!=2u)
            throw std::runtime_error(
                "required cocos2d ZIP hooks were not found");
        const std::size_t browser_hooks=
            InstallCcApplicationOpenUrlHook(runtime,env);
        if(browser_hooks!=1u)
            throw std::runtime_error(
                "required cocos2d openURL hook was not found");
        const std::size_t audio_effect_hooks=
            InstallSimpleAudioEffectHooks(runtime,env);
        if(audio_effect_hooks!=2u)
            throw std::runtime_error(
                "required SimpleAudioEngine effect hooks were not found");
        const std::size_t icon_unlock_hooks =
            InstallConfigurableIconUnlockHooks(runtime, env);
        const std::size_t creator_bypass_hooks =
            InstallConfigurableCreatorBypass(runtime, env);
        {
            std::ostringstream line;
            line<<"Image: 0x"<<std::hex<<runtime.image_min<<"-0x"
                <<runtime.image_max<<" entry=0x"<<runtime.entry<<std::dec;
            emit(line.str());
        }
        emit("Authentic ARM constructors: "+
             std::to_string(runtime.constructors.size()));
        emit("Dynarmic relocation targets: function-imports="+
             std::to_string(runtime.imports.size())+" objects="+
             std::to_string(runtime.objects.size()));
        {
            std::ostringstream line;
            line<<"Exports: JNI_OnLoad=0x"<<std::hex<<runtime.jni_onload
                <<" nativeSetPaths=0x"<<runtime.native_set_paths
                <<" nativeInit=0x"<<runtime.native_init
                <<" nativeRender=0x"<<runtime.native_render
                <<" touches=0x"<<runtime.native_touch_begin<<"/0x"
                <<runtime.native_touch_move<<"/0x"<<runtime.native_touch_end
                <<" key=0x"<<runtime.native_key_down
                <<" pause/resume=0x"<<runtime.native_pause<<"/0x"
                <<runtime.native_resume<<std::dec;
            emit(line.str());
        }
        emit("RESULT: DYNARMIC_RELOCATION_OK");
        emit("RESULT: DYNARMIC_CCFILEUTILS_ZIP_HOOKS_READY count="+
             std::to_string(zip_hooks)+
             " scratch=r0 args=r1-r3-preserved");
        emit("RESULT: DYNARMIC_HOST_MINIZIP_HOOKS_READY count="+
             std::to_string(minizip_hooks)+" expected=33");
        emit("RESULT: DYNARMIC_CCAPPLICATION_OPENURL_HOOK_READY count="+
             std::to_string(browser_hooks));
        emit("RESULT: DYNARMIC_SIMPLEAUDIO_EFFECT_HOOKS_READY count="+
             std::to_string(audio_effect_hooks)+
             " play=direct-host preload=direct-host async-worker=1");
        emit("RESULT: UNIFIED_LAUNCH_SETTINGS server=" +
             std::string(gd_settings_server()) +
             " hack-icons=" + (gd_settings_hack_icons() ? "true" : "false") +
             " icon-hooks=" + std::to_string(icon_unlock_hooks) +
             " full-bypass=" +
             (gd_settings_full_bypass() ? "true" : "false") +
             " bypass-hooks=" + std::to_string(creator_bypass_hooks) +
             " music-pulse-max=" +
             std::to_string(gd_settings_music_pulse_max()));
        GuestExecutor executor(env,runtime,log_file);
        executor.ConfigureHost(absolute_apk.string(),writable.string(),apk);
        emit("RESULT: DYNARMIC_APK_MEMORY_CACHE_READY bytes="+
             std::to_string(apk.size()));
        emit("RESULT: DYNARMIC_OPENGL_IMPORT_CACHE_READY imports="+
             std::to_string(std::count_if(
                 runtime.imports.begin(),runtime.imports.end(),
                 [](const ImportRecord& import){return import.is_gl;})));
        emit("Running "+std::to_string(runtime.constructors.size())+
             " authentic ARM constructors through Dynarmic");
        for(std::size_t index=0;index<runtime.constructors.size();++index){
            const u32 entry=runtime.constructors[index];
            if(entry==0||entry==std::numeric_limits<u32>::max())continue;
            if(index<8||((index+1u)%32u)==0u||
               index+1u==runtime.constructors.size()){
                std::ostringstream line;
                line<<"constructor "<<(index+1u)<<'/'
                    <<runtime.constructors.size()<<": guest 0x"
                    <<std::hex<<entry<<std::dec;
                emit(line.str());
            }
            u32 ignored=0;
            if(!executor.RunFunction(
                    entry,{},&ignored,
                    "constructor "+std::to_string(index+1u))){
                emit("RESULT: DYNARMIC_CONSTRUCTOR_FAILED index="+
                     std::to_string(index+1u));
                throw std::runtime_error(executor.LastError());
            }
        }
        emit("RESULT: DYNARMIC_CONSTRUCTORS_OK count="+
             std::to_string(runtime.constructors.size()));
        u32 result=0;
        if(!executor.RunFunction(
                runtime.jni_onload,{kVmObject,0u},&result,"JNI_OnLoad"))
            throw std::runtime_error(executor.LastError());
        {
            std::ostringstream line;
            line<<"Dynarmic JNI_OnLoad returned 0x"<<std::hex
                <<std::setw(8)<<std::setfill('0')<<result<<std::dec;
            emit(line.str());
        }
        if(result!=kJniVersion14)
            throw std::runtime_error(
                "JNI_OnLoad returned unexpected version");
        emit("RESULT: DYNARMIC_JNI_ONLOAD_OK result=0x00010004");
        if(probe_only){
            emit("RESULT: DYNARMIC_BRINGUP14_PROBE_ONLY_OK");
            return 0;
        }

        const u32 apk_ref=executor.NewStringRef(absolute_apk.string());
        if(!apk_ref||!executor.RunFunction(
                runtime.native_set_paths,{kEnvObject,0u,apk_ref},
                &result,"nativeSetPaths"))
            throw std::runtime_error(executor.LastError());
        emit("RESULT: DYNARMIC_PATHS_SET");
        if(!executor.CreateOpenGlWindow(width,height))
            throw std::runtime_error(
                "could not create Win32 OpenGL host window");
        emit("RESULT: DYNARMIC_OPENGL_HOST_OK");
        const auto init_start=std::chrono::steady_clock::now();
        if(!executor.RunFunction(
                runtime.native_init,
                {kEnvObject,0u,static_cast<u32>(width),
                 static_cast<u32>(height)},
                &result,"nativeInit",0u,
                std::chrono::milliseconds(120000)))
            throw std::runtime_error(executor.LastError());
        const double init_ms=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-init_start).count();
        emit("RESULT: DYNARMIC_NATIVE_INIT_RETURNED time_ms="+
             std::to_string(init_ms));
        executor.ReportHeapStatus("after-nativeInit");
        emit("RESULT: DYNARMIC_INPUT_BRIDGE_READY");
        emit("RESULT: DYNARMIC_RENDER_LOOP_ENTERED");

        FrameProfiler profiler(
            log_file,profile_path,profile_summary_path,slow_frame_ms);
        bool first_frame=false;
        bool native_paused=false;
        std::uint64_t frame_count=0;
        std::uint64_t interval_frames=0;
        auto interval_start=std::chrono::steady_clock::now();
        double interval_render_ms=0.0;
        std::size_t interval_profile_begin=0;
        double interval_cpu_start=CurrentProcessCpuMilliseconds();
        bool running=true;
        while(running){
            const auto loop_start=std::chrono::steady_clock::now();
            ProfilerCounters counters_before{};
            std::vector<u64> imports_before;
            if(profile_enabled){
                counters_before=executor.CaptureProfilerCounters();
                imports_before=executor.CaptureImportCounts();
            }

            const bool window_open=executor.PumpMessages();
            for(const HostEvent& event:executor.TakeHostEvents()){
                bool ok=true;
                switch(event.type){
                case HostEventType::TouchBegin:
                    if(!native_paused)
                        ok=executor.SendTouchPoint(
                            runtime.native_touch_begin,event.x,event.y,
                            "nativeTouchesBegin");
                    break;
                case HostEventType::TouchMove:
                    if(!native_paused)
                        ok=executor.SendTouchMove(
                            runtime.native_touch_move,event.x,event.y);
                    break;
                case HostEventType::TouchEnd:
                    if(!native_paused)
                        ok=executor.SendTouchPoint(
                            runtime.native_touch_end,event.x,event.y,
                            "nativeTouchesEnd");
                    break;
                case HostEventType::KeyDown:
                    if(!native_paused)
                        ok=executor.SendKey(
                            runtime.native_key_down,event.value);
                    break;
                case HostEventType::TextInput:
                    if(!native_paused)
                        ok=executor.SendText(
                            runtime.native_insert_text,
                            Utf8FromCodepoint(event.value));
                    break;
                case HostEventType::DeleteBackward:
                    if(!native_paused)
                        ok=executor.SendDeleteBackward(
                            runtime.native_delete_backward);
                    break;
                case HostEventType::Pause:
                    if(!native_paused){
                        ok=executor.SendLifecycle(
                            runtime.native_pause,"nativeOnPause");
                        if(ok)native_paused=true;
                    }
                    break;
                case HostEventType::Resume:
                    if(native_paused){
                        ok=executor.SendLifecycle(
                            runtime.native_resume,"nativeOnResume");
                        if(ok)native_paused=false;
                    }
                    break;
                }
                if(!ok)throw std::runtime_error(executor.LastError());
                if(executor.TerminationRequested()){
                    running=false;
                    break;
                }
            }
            const auto events_done=std::chrono::steady_clock::now();
            if(executor.TerminationRequested()){
                running=false;
                break;
            }
            if(!window_open){
                running=false;
                break;
            }
            if(native_paused||!executor.WindowActive()){
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            const auto render_start=std::chrono::steady_clock::now();
            if(profile_enabled) executor.BeginGpuFrame(frame_count+1u);
            if(!executor.RunFunction(
                    runtime.native_render,{kEnvObject,0u},&result,
                    "nativeRender",0u,
                    std::chrono::milliseconds(30000)))
                throw std::runtime_error(executor.LastError());
            if(profile_enabled) executor.EndGpuFrame();
            const auto render_done=std::chrono::steady_clock::now();
            if(executor.TerminationRequested()){
                running=false;
                break;
            }
            executor.SwapBuffersHost();
            const auto frame_done=std::chrono::steady_clock::now();

            ++frame_count;
            ++interval_frames;
            const double total_ms=std::chrono::duration<double,std::milli>(
                frame_done-loop_start).count();
            const double event_ms=std::chrono::duration<double,std::milli>(
                events_done-loop_start).count();
            const double render_ms=std::chrono::duration<double,std::milli>(
                render_done-render_start).count();
            const double swap_ms=std::chrono::duration<double,std::milli>(
                frame_done-render_done).count();
            interval_render_ms+=total_ms;

            if(profile_enabled){
                const ProfilerCounters counters_after=
                    executor.CaptureProfilerCounters();
                const std::vector<u64> imports_after=
                    executor.CaptureImportCounts();
                FrameProfileSample sample;
                sample.frame=frame_count;
                sample.total_ms=total_ms;
                sample.event_ms=event_ms;
                sample.render_ms=render_ms;
                sample.swap_ms=swap_ms;
                const GuestCallMetrics& call=executor.LastCallMetrics();
                sample.estimated_ticks=call.estimated_ticks;
                sample.jit_runs=call.jit_runs;
                sample.svc_calls=call.svc_calls;
                sample.import_calls=CounterDelta(
                    counters_before.import_calls,
                    counters_after.import_calls);
                sample.jni_calls=CounterDelta(
                    counters_before.jni_svc_calls,
                    counters_after.jni_svc_calls);
                sample.gl_calls=CounterDelta(
                    counters_before.gl_calls,counters_after.gl_calls);
                sample.draw_calls=CounterDelta(
                    counters_before.draw_calls,counters_after.draw_calls);
                sample.draw_vertices=CounterDelta(
                    counters_before.draw_vertices,
                    counters_after.draw_vertices);
                sample.buffer_upload_bytes=CounterDelta(
                    counters_before.buffer_upload_bytes,
                    counters_after.buffer_upload_bytes);
                sample.texture_upload_bytes=CounterDelta(
                    counters_before.texture_upload_bytes,
                    counters_after.texture_upload_bytes);
                sample.allocation_calls=CounterDelta(
                    counters_before.allocation_calls,
                    counters_after.allocation_calls);
                sample.free_calls=CounterDelta(
                    counters_before.free_calls,counters_after.free_calls);
                sample.reallocation_calls=CounterDelta(
                    counters_before.reallocation_calls,
                    counters_after.reallocation_calls);
                sample.live_heap_bytes=counters_after.live_heap_bytes;
                sample.scene_hint=executor.LastAndroidLog();
                const bool slow=total_ms>=slow_frame_ms||
                    event_ms>=slow_frame_ms||
                    render_ms>=slow_frame_ms||
                    swap_ms>=slow_frame_ms;
                if(slow){
                    sample.top_imports=executor.DescribeTopImportDeltas(
                        imports_before,imports_after,10u,false);
                    sample.top_gl=executor.DescribeTopImportDeltas(
                        imports_before,imports_after,8u,true);
                }
                profiler.Add(std::move(sample));
                for(const auto& [gpu_frame,gpu_ms]:
                    executor.TakeGpuTimings())
                    profiler.SetGpuTiming(gpu_frame,gpu_ms);
            }

            if(!first_frame){
                first_frame=true;
                emit("RESULT: DYNARMIC_FIRST_FRAME_OK frame_ms="+
                     std::to_string(total_ms));
                executor.ReportHeapStatus("first-frame");
            }

            const auto now=std::chrono::steady_clock::now();
            const double interval_ms=
                std::chrono::duration<double,std::milli>(
                    now-interval_start).count();
            if(interval_ms>=5000.0){
                const double fps=
                    static_cast<double>(interval_frames)*1000.0/
                    interval_ms;
                const double avg_ms=interval_frames?
                    interval_render_ms/
                    static_cast<double>(interval_frames):0.0;
                std::ostringstream line;
                line<<std::fixed<<std::setprecision(1)
                    <<"Dynarmic interactive performance: "<<fps
                    <<" FPS avg-frame="<<std::setprecision(2)
                    <<avg_ms<<" ms total-frames="<<frame_count;
                emit(line.str());
                if(profile_enabled){
                    const double cpu_now=
                        CurrentProcessCpuMilliseconds();
                    profiler.LogInterval(
                        interval_profile_begin,interval_ms,
                        interval_cpu_start,cpu_now);
                    interval_profile_begin=profiler.SampleCount();
                    interval_cpu_start=cpu_now;
                }
                executor.ReportHeapStatus("periodic");
                std::ostringstream title;
                title<<"Geometry Dash ARM - Dynarmic x64 Test14-fix1 | "
                     <<std::fixed<<std::setprecision(1)<<fps<<" FPS";
                executor.SetWindowTitle(title.str());
                interval_start=now;
                interval_frames=0;
                interval_render_ms=0.0;
                executor.FlushDiagnostics();
            }
            if(max_frames>0&&
               frame_count>=static_cast<std::uint64_t>(max_frames))
                running=false;
        }

        if(!executor.TerminationRequested()&&!native_paused&&
           runtime.native_pause){
            if(!executor.SendLifecycle(
                    runtime.native_pause,"nativeOnPause shutdown"))
                throw std::runtime_error(executor.LastError());
            native_paused=true;
        }
        if(!first_frame)
            throw std::runtime_error(
                "render loop ended before the first frame");
        if(profile_enabled){
            for(const auto& [gpu_frame,gpu_ms]:
                executor.FinishGpuTimings())
                profiler.SetGpuTiming(gpu_frame,gpu_ms);
            profiler.WriteFiles(executor);
            emit("RESULT: DYNARMIC_FRAME_PROFILE_WRITTEN csv="+
                 profile_path+" summary="+profile_summary_path);
        }
        emit("Dynarmic interactive loop ended after frames="+
             std::to_string(frame_count));
        emit("Dynarmic top imports: "+
             executor.DescribeTopImports(20u,false));
        emit("Dynarmic top OpenGL imports: "+
             executor.DescribeTopImports(20u,true));
        emit("Dynarmic sampled host import costs: "+
             executor.DescribeTopImportHostSamples(20u));
        emit("Permissive runtime import calls: "+
             std::to_string(executor.PermissiveStubCalls())+
             " unique="+
             std::to_string(executor.PermissiveNames().size()));
        if(!executor.PermissiveNames().empty()){
            std::ostringstream names;
            names<<"Permissive imports:";
            for(const auto& name:executor.PermissiveNames())
                names<<' '<<name;
            emit(names.str());
        }
        emit("RESULT: DYNARMIC_BRINGUP14_OK");
        return 0;
    } catch(const std::exception& error){
        emit(std::string("ERROR: ")+error.what());
        emit("RESULT: DYNARMIC_BRINGUP14_FAILED");
        return 1;
    }
}


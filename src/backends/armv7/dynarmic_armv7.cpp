#include <algorithm>
#include <atomic>
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
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <type_traits>
#include <climits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <winhttp.h>
#include <GL/gl.h>
#include <direct.h>
#endif

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/exclusive_monitor.h"
#include "win_dpi.h"

extern "C" {
#include "zlib.h"
#include "storage_win.h"
#include "audio_win.h"
#include "net_compat_win.h"
#include "runtime_settings.h"
#include "extras_menu_win.h"
#include "window_icon_win.h"
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

template <typename ArchVersion>
constexpr ArchVersion DynarmicArmv7ArchVersion() {
    if constexpr (requires { ArchVersion::v7A; }) {
        return ArchVersion::v7A;
    } else if constexpr (requires { ArchVersion::v7; }) {
        return ArchVersion::v7;
    } else {
        static_assert(!sizeof(ArchVersion),
                      "This Dynarmic revision has no recognized ARMv7 architecture enum");
    }
}

constexpr u32 kGameBase = 0x10000000u;
constexpr u32 kV22CompanionBase = 0x18000000u;
constexpr u32 kSmokeBase = 0x0F000000u;
constexpr u32 kV22ThunkBase = 0x10F00000u;
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
constexpr std::size_t kMaximumGuestCString = 64u * 1024u * 1024u;
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

struct ZipNativeLibraryInfo {
    std::string name;
    u32 uncompressed_size = 0;
    u32 crc = 0;
};

static std::vector<ZipNativeLibraryInfo> ListZipNativeLibraries(
    const std::vector<u8>& zip) {
    constexpr u32 kEocdSignature = 0x06054B50u;
    constexpr u32 kCentralSignature = 0x02014B50u;
    const std::size_t search_start =
        zip.size() > (0xFFFFu + 22u) ? zip.size() - (0xFFFFu + 22u) : 0u;
    std::optional<std::size_t> eocd;
    if (zip.size() >= 22u) {
        for (std::size_t pos = zip.size() - 22u;; --pos) {
            if (ReadLe32(zip, pos) == kEocdSignature) {
                eocd = pos;
                break;
            }
            if (pos == search_start) break;
        }
    }
    if (!eocd)
        throw std::runtime_error("APK end-of-central-directory record not found");
    const u16 count = ReadLe16(zip, *eocd + 10u);
    std::size_t pos = ReadLe32(zip, *eocd + 16u);
    std::vector<ZipNativeLibraryInfo> result;
    for (u16 index = 0; index < count; ++index) {
        if (pos + 46u > zip.size() ||
            ReadLe32(zip, pos) != kCentralSignature)
            throw std::runtime_error("invalid APK central-directory entry");
        const u32 crc = ReadLe32(zip, pos + 16u);
        const u32 size = ReadLe32(zip, pos + 24u);
        const u16 name_length = ReadLe16(zip, pos + 28u);
        const u16 extra_length = ReadLe16(zip, pos + 30u);
        const u16 comment_length = ReadLe16(zip, pos + 32u);
        const std::size_t next =
            pos + 46ull + name_length + extra_length + comment_length;
        if (next > zip.size())
            throw std::runtime_error("truncated APK central-directory entry");
        std::string name(reinterpret_cast<const char*>(zip.data() + pos + 46u),
                         name_length);
        std::replace(name.begin(), name.end(), '\\', '/');
        const bool native_path =
            name.starts_with("lib/armeabi-v7a/") ||
            name.starts_with("lib/armeabi/");
        if (native_path && name.ends_with(".so"))
            result.push_back(ZipNativeLibraryInfo{name, size, crc});
        pos = next;
    }
    std::sort(result.begin(), result.end(),
              [](const ZipNativeLibraryInfo& lhs,
                 const ZipNativeLibraryInfo& rhs) { return lhs.name < rhs.name; });
    return result;
}

static bool IsElf32ArmImage(const std::vector<u8>& bytes) {
    if (bytes.size() < sizeof(Elf32Ehdr)) return false;
    const Elf32Ehdr header = ReadPod<Elf32Ehdr>(bytes, 0);
    return std::memcmp(header.ident, "\x7F" "ELF", 4) == 0 &&
           header.ident[4] == 1 && header.ident[5] == 1 &&
           header.type == kEtDyn && header.machine == kEmArm;
}

static std::vector<u8> ExtractV22NativeLibrary(const std::vector<u8>& apk,
                                               std::string& member_name) {
    static constexpr const char* candidates[] = {
        "lib/armeabi-v7a/libcocos2dcpp.so",
        "lib/armeabi/libcocos2dcpp.so",
    };
    std::string errors;
    for (const char* candidate : candidates) {
        try {
            std::vector<u8> result = ExtractZipMember(apk, candidate);
            member_name = candidate;
            return result;
        } catch (const std::exception& error) {
            if (!errors.empty()) errors += "; ";
            errors += std::string(candidate) + ": " + error.what();
        }
    }
    throw std::runtime_error(
        "2.2 beta APK does not contain libcocos2dcpp.so in a supported ARMv7 path (" +
        errors + ")");
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
                     std::size_t maximum = kMaximumGuestCString) const {
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

    bool MemoryWriteExclusive8(u32 vaddr, u8 value, u8 expected) override {
        return CompareExchangeTyped(vaddr, value, expected);
    }

    bool MemoryWriteExclusive16(u32 vaddr, u16 value, u16 expected) override {
        return CompareExchangeTyped(vaddr, value, expected);
    }

    bool MemoryWriteExclusive32(u32 vaddr, u32 value, u32 expected) override {
        return CompareExchangeTyped(vaddr, value, expected);
    }

    bool MemoryWriteExclusive64(u32 vaddr, u64 value, u64 expected) override {
        return CompareExchangeTyped(vaddr, value, expected);
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

    template <typename T>
    bool CompareExchangeTyped(u32 address, T value, T expected) {
        MemoryRegion* region = FindMutable(address, sizeof(T));
        if (!region) {
            WriteFault(address);
            return false;
        }
        T current{};
        u8* const bytes = region->data.data() + (address - region->base);
        std::memcpy(&current, bytes, sizeof(current));
        if (current != expected) return false;
        std::memcpy(bytes, &value, sizeof(value));
        return true;
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
    u64 sampled_host_nanoseconds = 0;
    u64 sampled_host_calls = 0;
};

struct ImportTraceEntry {
    u64 sequence = 0;
    u32 svc = 0;
    u32 import_address = 0;
    u32 pc = 0;
    u32 lr = 0;
    std::array<u32, 4> arguments{};
    u32 result = 0;
    bool worker = false;
    bool completed = false;
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
enum class V22CompanionHookMode {
    Off,
    Safe,
    All,
};

static const char* V22CompanionHookModeName(V22CompanionHookMode mode) {
    switch (mode) {
    case V22CompanionHookMode::Off: return "off";
    case V22CompanionHookMode::Safe: return "safe";
    case V22CompanionHookMode::All: return "all";
    }
    return "off";
}

struct V22CompanionFeatureSpec {
    const char* label;
    const char* symbol;
    bool safe;
};

static constexpr V22CompanionFeatureSpec kV22CompanionFeatures[] = {
    {"MenuLayer", "_ZN12MenuLayerExt10ApplyHooksEv", true},
    {"Options", "_ZN7Options10ApplyHooksEv", true},
    {"EditLevelLayer", "_ZN17EditLevelLayerExt10ApplyHooksEv", true},
    {"LevelEditorLayer", "_ZN19LevelEditorLayerExt10ApplyHooksEv", true},
    {"EditorPauseLayer", "_ZN19EditorPauseLayerExt10ApplyHooksEv", true},
    {"CollisionFix", "_ZN12CollisionFix10ApplyHooksEv", true},
    {"ShaderFix", "_ZN9ShaderFix10ApplyHooksEv", true},
    {"SpeedrunTimer", "_ZN13SpeedrunTimer10ApplyHooksEv", true},
    {"MoreSearch", "_ZN18MoreSearchLayerExt10ApplyHooksEv", true},
    {"SwingIconFix", "_ZN12SwingIconFix10ApplyHooksEv", true},
    {"AbbreviatedLabels", "_ZN17AbbreviatedLabels10ApplyHooksEv", true},
    {"Emojis", "_ZN6Emojis10ApplyHooksEv", true},
    {"AdvancedLevelInfo", "_ZN17AdvancedLevelInfo10ApplyHooksEv", true},
    {"DPAD", "_ZN9DPADHooks10ApplyHooksEv", false},
    {"GDPSManager", "_ZN11GDPSManager10ApplyHooksEv", false},
    {"Servers", "_ZN7Servers10ApplyHooksEv", false},
    {"Hacks", "_ZN5Hacks10ApplyHooksEv", false},
    {"DevDebug", "_ZN13DevDebugHooks10ApplyHooksEv", false},
};

enum class V22EditorRestoreProfile : u32 {
    None = 0,
    Early2019,
    Late2022,
    Late2023,
};

static const char* V22EditorRestoreProfileName(V22EditorRestoreProfile profile) {
    switch (profile) {
    case V22EditorRestoreProfile::Early2019: return "stock-2019-9144004";
    case V22EditorRestoreProfile::Late2022: return "stock-2022-9541500";
    case V22EditorRestoreProfile::Late2023: return "stock-2023-9578364";
    case V22EditorRestoreProfile::None: break;
    }
    return "none";
}

struct ElfRuntime {
    std::size_t primary_file_bytes = 0;
    V22EditorRestoreProfile v22_wrapper_editor_profile = V22EditorRestoreProfile::None;
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
    u32 v22_string_append_bytes = 0;
    u32 v22_empty_string_data = 0;
    u32 v22_prepare_setup_address = 0;
    u32 v22_level_settings_from_string = 0;
    u32 v22_level_settings_create = 0;
    u32 v22_play_layer_level_offset = 0;
    u32 v22_game_level_id_offset = 0;
    u32 v22_game_level_vtable = 0;
    u32 v22_companion_editor_init = 0;
    u32 v22_companion_editor_visibility = 0;
    u32 v22_companion_editor_visibility_original_slot = 0;
    bool v22_companion_editor_init_enabled = false;
    bool v22_companion_editor_visibility_enabled = false;
    u32 v22_game_object_add_main_sprite = 0;
    u32 v22_game_object_has_secondary_color = 0;
    u32 v22_game_object_add_color_sprite = 0;
    u32 v22_game_object_activate = 0;
    u32 v22_game_object_deactivate = 0;
    u32 v22_game_object_set_opacity = 0;
    u32 v22_ccsprite_set_opacity = 0;
    u32 v22_game_manager_get_game_variable = 0;
    u32 v22_gjbase_pre_update_visibility = 0;
    u32 v22_level_editor_update_object_colors = 0;
    u32 v22_ccarray_add_object = 0;
    u32 v22_ccarray_remove_all_objects = 0;
    u32 v22_gjbase_process_area_visual_actions = 0;
    u32 v22_level_editor_sort_batchnode_children = 0;
    u32 v22_level_editor_update_grid_layer = 0;
    u32 v22_draw_grid_vtable = 0;
    u32 v22_draw_grid_update_music_guide_time = 0;
    u32 v22_draw_grid_update_time_markers = 0;
    u32 v22_level_editor_level_settings_updated = 0;
    u32 v22_gjbase_queue_button = 0;
    u32 v22_ui_key_down = 0;
    u32 v22_ui_key_up = 0;
    u32 v22_ui_on_check = 0;
    u32 v22_ui_on_delete_check = 0;
    u32 v22_ui_layer_offset = 0;
    u32 v22_practice_mode_offset = 0;
    u32 v22_editor_playtest_state_offset = 0;
    u32 v22_companion_image_min = 0;
    u32 v22_companion_image_max = 0;
    u32 v22_companion_executable_min = 0;
    u32 v22_companion_executable_max = 0;
    u32 v22_level_editor_create = 0;
    u32 v22_scene_create = 0;
    u32 v22_node_add_child = 0;
    u32 v22_director_shared = 0;
    u32 v22_director_replace_scene = 0;
    u32 v22_transition_fade_create = 0;
    u32 v22_edit_close_text_inputs = 0;
    u32 v22_edit_verify_level_name = 0;
    u32 v22_game_manager_shared = 0;
    u32 v22_game_manager_editor_state_offset = 0;
    u32 v22_edit_level_pointer_offset = 0;
    u32 ccnode_get_tag = 0;
    u32 ccnode_set_tag = 0;
    u32 editor_move_object_call = 0;
    u32 editor_move_edit_command = 0;
    u32 editor_transform_object_call = 0;
    u32 editor_transform_edit_command = 0;
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
        throw std::runtime_error("hook symbol is not marked as Thumb: " + symbol.name);
    if (symbol.size < 8u)
        throw std::runtime_error("hook symbol is too small: " + symbol.name);
    const u32 address = symbol.address & ~1u;
    if ((address & 3u) != 0u || address < runtime.image_min ||
        address > runtime.image_max - 8u)
        throw std::runtime_error("hook target is outside the executable image: " + symbol.name);
    const u16 original = env.MemoryRead16(address);
    if (original != expected_first_halfword) {
        std::ostringstream error;
        error << "hook prologue mismatch for " << symbol.name
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
static void InstallThumbLiteralPcHookPreservingAllArguments(
    ProbeEnvironment& env, const ElfRuntime& runtime, const SymbolRecord& symbol,
    u16 expected_first_halfword, u32 destination) {
    if ((symbol.address & 1u) == 0u)
        throw std::runtime_error("hook symbol is not marked as Thumb: " + symbol.name);
    if (symbol.size < 8u)
        throw std::runtime_error("hook symbol is too small: " + symbol.name);
    const u32 address = symbol.address & ~1u;
    if ((address & 3u) != 0u || address < runtime.image_min ||
        address > runtime.image_max - 8u)
        throw std::runtime_error("hook target is outside the executable image: " + symbol.name);
    const u16 original = env.MemoryRead16(address);
    if (original != expected_first_halfword) {
        std::ostringstream error;
        error << "hook prologue mismatch for " << symbol.name
              << ": expected 0x" << std::hex << expected_first_halfword
              << " got 0x" << original;
        throw std::runtime_error(error.str());
    }
    // Thumb-2 ldr.w pc,[pc,#0], followed by an absolute literal. This leaves
    // R0-R3 and stack arguments untouched, including hidden sret pointers.
    env.MemoryWrite16(address + 0u, 0xF8DFu);
    env.MemoryWrite16(address + 2u, 0xF000u);
    env.MemoryWrite32(address + 4u, destination);
}


static std::optional<u32> DecodeThumbBlTarget(u32 instruction_address,
                                              u16 first, u16 second) {
    if ((first & 0xF800u) != 0xF000u ||
        (second & 0xD000u) != 0xD000u)
        return std::nullopt;
    const u32 sign = (first >> 10u) & 1u;
    const u32 imm10 = first & 0x03FFu;
    const u32 j1 = (second >> 13u) & 1u;
    const u32 j2 = (second >> 11u) & 1u;
    const u32 i1 = (~(j1 ^ sign)) & 1u;
    const u32 i2 = (~(j2 ^ sign)) & 1u;
    const u32 imm11 = second & 0x07FFu;
    const u32 encoded = (sign << 24u) | (i1 << 23u) |
                        (i2 << 22u) | (imm10 << 12u) |
                        (imm11 << 1u);
    const s32 displacement = static_cast<s32>(encoded << 7u) >> 7u;
    return static_cast<u32>(instruction_address + 4u + displacement);
}

static void WriteThumbBl(ProbeEnvironment& env, u32 instruction_address,
                         u32 target_address) {
    const s64 displacement = static_cast<s64>(target_address & ~1u) -
                             static_cast<s64>(instruction_address + 4u);
    if ((displacement & 1ll) != 0ll || displacement < -0x01000000ll ||
        displacement > 0x00FFFFFEll)
        throw std::runtime_error("Thumb BL target is out of range");
    const u32 encoded = static_cast<u32>(static_cast<s32>(displacement)) &
                        0x01FFFFFFu;
    const u32 sign = (encoded >> 24u) & 1u;
    const u32 i1 = (encoded >> 23u) & 1u;
    const u32 i2 = (encoded >> 22u) & 1u;
    const u32 imm10 = (encoded >> 12u) & 0x03FFu;
    const u32 imm11 = (encoded >> 1u) & 0x07FFu;
    const u32 j1 = (~(i1 ^ sign)) & 1u;
    const u32 j2 = (~(i2 ^ sign)) & 1u;
    env.MemoryWrite16(instruction_address,
                      static_cast<u16>(0xF000u | (sign << 10u) | imm10));
    env.MemoryWrite16(instruction_address + 2u,
                      static_cast<u16>(0xD000u | (j1 << 13u) |
                                       (j2 << 11u) | imm11));
}

static void EnsureV22ThunkPage(ProbeEnvironment& env) {
    if (!env.IsMapped(kV22ThunkBase, 1u))
        env.Map(kV22ThunkBase, kPageSize, true);
}

static void WriteV22ThumbImportThunk(ProbeEnvironment& env, u32 address,
                                     u32 destination) {
    if ((address & 3u) != 0u)
        throw std::runtime_error("V22 thunk address is not aligned");
    env.MemoryWrite16(address + 0u, 0xF8DFu); // ldr.w pc,[pc,#0]
    env.MemoryWrite16(address + 2u, 0xF000u);
    env.MemoryWrite32(address + 4u, destination);
}

static void WriteV22ThumbCallThenImportThunk(
    ProbeEnvironment& env, u32 address, u32 original, u32 destination) {
    if ((address & 3u) != 0u)
        throw std::runtime_error("V22 chained thunk address is not aligned");
    // Preserve `this` and the caller return address, run the untouched
    // primary Thumb function in the current guest call, then invoke a host
    // import for the small post-pass. This avoids a nested Dynarmic RunFunction
    // on every rendered frame.
    env.MemoryWrite16(address + 0u, 0xB501u); // push {r0,lr}
    env.MemoryWrite16(address + 2u, 0x4B03u); // ldr r3,[pc,#12]
    env.MemoryWrite16(address + 4u, 0x4798u); // blx r3
    env.MemoryWrite16(address + 6u, 0x9800u); // ldr r0,[sp,#0]
    env.MemoryWrite16(address + 8u, 0x4B02u); // ldr r3,[pc,#8]
    env.MemoryWrite16(address + 10u, 0x4798u); // blx r3
    env.MemoryWrite16(address + 12u, 0xBD01u); // pop {r0,pc}
    env.MemoryWrite16(address + 14u, 0xBF00u); // nop/alignment
    env.MemoryWrite32(address + 16u, original);
    env.MemoryWrite32(address + 20u, destination);
}

static void WriteV22ThumbConditionalImportThenCallThunk(
    ProbeEnvironment& env, u32 address, u32 predicate_import, u32 original) {
    if ((address & 3u) != 0u)
        throw std::runtime_error("V22 conditional thunk address is not aligned");
    /*
     * Preserve the CCPoint pointer and layer, ask the host whether this is the
     * active editor, and call the untouched guest function only when it is not.
     * updateCameraBGArt returns void, so restoring r0/r1 before returning is
     * harmless and keeps the caller ABI exact.
     */
    env.MemoryWrite16(address + 0u, 0xB503u);  // push {r0,r1,lr}
    env.MemoryWrite16(address + 2u, 0x4B05u);  // ldr r3,[pc,#20] -> host
    env.MemoryWrite16(address + 4u, 0x4798u);  // blx r3
    env.MemoryWrite16(address + 6u, 0x2800u);  // cmp r0,#0
    env.MemoryWrite16(address + 8u, 0xD103u);  // bne suppress/return
    env.MemoryWrite16(address + 10u, 0x9800u); // ldr r0,[sp,#0]
    env.MemoryWrite16(address + 12u, 0x9901u); // ldr r1,[sp,#4]
    env.MemoryWrite16(address + 14u, 0x4B03u); // ldr r3,[pc,#12] -> original
    env.MemoryWrite16(address + 16u, 0x4798u); // blx r3
    env.MemoryWrite16(address + 18u, 0xBD03u); // pop {r0,r1,pc}
    env.MemoryWrite16(address + 20u, 0xBF00u); // alignment
    env.MemoryWrite16(address + 22u, 0xBF00u);
    env.MemoryWrite32(address + 24u, predicate_import);
    env.MemoryWrite32(address + 28u, original);
}

static void WriteV22ThumbNullTextureGuardThunk(
    ProbeEnvironment& env, u32 address, u32 original, u32 reject_import) {
    if ((address & 3u) != 0u)
        throw std::runtime_error("V22 null-texture guard address is not aligned");
    /*
     * CCSpriteBatchNode::initWithTexture(this, texture, capacity) returns bool.
     * A null texture is never a usable batch node.  Reject it before any fields
     * are mutated, while valid textures tail-call the untouched guest function.
     * This is intentionally narrower than EnduranceTest11's global atlas
     * substitution, which also changed the editor's legitimate ground-visibility
     * construction path and broke the Show Ground toggle.
     */
    env.MemoryWrite16(address + 0u, 0x2900u); // cmp r1,#0
    env.MemoryWrite16(address + 2u, 0xD001u); // beq reject
    env.MemoryWrite16(address + 4u, 0x4B02u); // ldr r3,[pc,#8] -> original
    env.MemoryWrite16(address + 6u, 0x4718u); // bx r3
    env.MemoryWrite16(address + 8u, 0x4B02u); // ldr r3,[pc,#8] -> host reject
    env.MemoryWrite16(address + 10u, 0x4718u); // bx r3
    env.MemoryWrite16(address + 12u, 0xBF00u);
    env.MemoryWrite16(address + 14u, 0xBF00u);
    env.MemoryWrite32(address + 16u, original);
    env.MemoryWrite32(address + 20u, reject_import);
}

static std::vector<u32> FindThumbBlCallSites(const ElfRuntime& runtime,
                                             ProbeEnvironment& env,
                                             u32 target_address) {
    std::vector<u32> result;
    const u32 target = target_address & ~1u;
    for (u32 address = runtime.image_min;
         address <= runtime.image_max - 4u; address += 2u) {
        const auto decoded = DecodeThumbBlTarget(
            address, env.MemoryRead16(address), env.MemoryRead16(address + 2u));
        if (decoded && (*decoded & ~1u) == target) result.push_back(address);
    }
    return result;
}

static std::vector<u32> FindThumbBlCallSitesInRange(
    ProbeEnvironment& env, u32 begin, u32 size, u32 target_address) {
    std::vector<u32> result;
    begin &= ~1u;
    const u32 target = target_address & ~1u;
    if (size < 4u) return result;
    const u32 end = begin + size;
    for (u32 address = begin; address <= end - 4u; address += 2u) {
        const auto decoded = DecodeThumbBlTarget(
            address, env.MemoryRead16(address), env.MemoryRead16(address + 2u));
        if (decoded && (*decoded & ~1u) == target) result.push_back(address);
    }
    return result;
}


static std::pair<std::size_t, std::size_t> RedirectV22FunctionReferences(
    ElfRuntime& runtime, ProbeEnvironment& env, const SymbolRecord& target,
    u32 replacement, u32 thunk_address) {
    std::size_t pointers = 0u;
    for (u32 address = (runtime.image_min + 3u) & ~3u;
         address <= runtime.image_max - 4u; address += 4u) {
        const u32 value = env.MemoryRead32(address);
        if (value != target.address) continue;
        env.MemoryWrite32(address, replacement);
        ++pointers;
    }
    const std::vector<u32> calls =
        FindThumbBlCallSites(runtime, env, target.address);
    if (!calls.empty()) {
        EnsureV22ThunkPage(env);
        WriteV22ThumbImportThunk(env, thunk_address, replacement);
        for (u32 call : calls) WriteThumbBl(env, call, thunk_address);
    }
    return {pointers, calls.size()};
}

struct V22DesktopTextInputPatchCounts {
    std::size_t keyboard_callbacks = 0u;
    std::size_t offset_delegates = 0u;
};

static bool PatchThumbReturnVoid(ProbeEnvironment& env,
                                 const ElfRuntime& runtime,
                                 const SymbolRecord& symbol) {
    if ((symbol.address & 1u) == 0u || symbol.size < 2u) return false;
    const u32 address = symbol.address & ~1u;
    if (address < runtime.image_min || address > runtime.image_max - 2u)
        return false;
    env.MemoryWrite16(address, 0x4770u); // bx lr
    if (symbol.size >= 4u && address <= runtime.image_max - 4u)
        env.MemoryWrite16(address + 2u, 0xBF00u); // nop
    return true;
}

static bool PatchThumbReturnFalse(ProbeEnvironment& env,
                                  const ElfRuntime& runtime,
                                  const SymbolRecord& symbol) {
    if ((symbol.address & 1u) == 0u || symbol.size < 4u) return false;
    const u32 address = symbol.address & ~1u;
    if (address < runtime.image_min || address > runtime.image_max - 4u)
        return false;
    env.MemoryWrite16(address + 0u, 0x2000u); // movs r0,#0
    env.MemoryWrite16(address + 2u, 0x4770u); // bx lr
    return true;
}

static V22DesktopTextInputPatchCounts InstallV22DesktopTextInputPatches(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    V22DesktopTextInputPatchCounts counts;
    for (const SymbolRecord& symbol : runtime.symbols) {
        // Patch both the concrete methods and their vtable-adjusting thunks.
        // The custom-song dialog can reach the thunk rather than the exported
        // base method, so patching only the five pretty names is insufficient.
        const bool keyboard_callback =
            symbol.name.find("keyboardWillShow") != std::string::npos ||
            symbol.name.find("keyboardWillHide") != std::string::npos ||
            symbol.name.find("forceOffset") != std::string::npos;
        if (keyboard_callback) {
            if (PatchThumbReturnVoid(env, runtime, symbol))
                ++counts.keyboard_callbacks;
            continue;
        }
        // Android delegates ask whether the whole game view should move above
        // the software keyboard. A native Win32 text box has no software
        // keyboard, so every one of these offsets is wrong on desktop.
        if (symbol.name.find("textInputShouldOffset") != std::string::npos &&
            PatchThumbReturnFalse(env, runtime, symbol))
            ++counts.offset_delegates;
    }
    return counts;
}

static bool DiscoverV22GuestStringBuilder(ElfRuntime& runtime,
                                          ProbeEnvironment& env,
                                          const SymbolRecord& function) {
    const u32 begin = function.address & ~1u;
    if (function.size < 32u || begin < runtime.image_min ||
        begin > runtime.image_max - function.size)
        return false;

    u32 builder = 0;
    u32 empty_data = 0;
    for (u32 offset = 0; offset + 6u <= function.size; offset += 2u) {
        const u32 address = begin + offset;
        if (env.MemoryRead16(address) != 0x4620u) continue; // mov r0,r4
        const auto target = DecodeThumbBlTarget(
            address + 2u, env.MemoryRead16(address + 2u),
            env.MemoryRead16(address + 4u));
        if (!target || *target < runtime.image_min ||
            *target >= runtime.image_max)
            continue;
        builder = *target | 1u;
        break;
    }

    for (u32 offset = 0; offset + 12u <= function.size; offset += 2u) {
        const u32 address = begin + offset;
        const u16 literal_load = env.MemoryRead16(address);
        if ((literal_load & 0xFF00u) != 0x4B00u) continue;
        u32 add_address = 0;
        for (u32 delta = 2u; delta <= 8u &&
             offset + delta + 6u <= function.size; delta += 2u) {
            if (env.MemoryRead16(address + delta) == 0x447Bu &&
                env.MemoryRead16(address + delta + 2u) == 0x681Bu &&
                env.MemoryRead16(address + delta + 4u) == 0x330Cu) {
                add_address = address + delta;
                break;
            }
        }
        if (!add_address) continue;
        const u32 literal_address = ((address + 4u) & ~3u) +
            static_cast<u32>(literal_load & 0x00FFu) * 4u;
        if (!env.IsMapped(literal_address, 4u)) continue;
        const s32 relative = static_cast<s32>(env.MemoryRead32(literal_address));
        const u32 got_slot = static_cast<u32>(add_address + 4u + relative);
        if (!env.IsMapped(got_slot, 4u)) continue;
        const u32 representation = env.MemoryRead32(got_slot);
        const u32 candidate = representation + 12u;
        if (!env.IsMapped(candidate - 12u, 13u)) continue;
        if (env.MemoryRead32(candidate - 12u) != 0u ||
            env.MemoryRead32(candidate - 8u) != 0u)
            continue;
        empty_data = candidate;
        break;
    }

    if (!builder || !empty_data) return false;
    runtime.v22_string_append_bytes = builder;
    runtime.v22_empty_string_data = empty_data;
    return true;
}

static bool DiscoverV22LevelObjectOffsets(ElfRuntime& runtime,
                                          ProbeEnvironment& env,
                                          const SymbolRecord& prepare) {
    const SymbolRecord* set_level_id = FindSymbol(
        runtime, "_ZN11GJGameLevel10setLevelIDEi");
    if (!set_level_id || set_level_id->size < 8u) return false;
    if (const SymbolRecord* vtable = FindSymbol(
            runtime, "_ZTV11GJGameLevel"))
        runtime.v22_game_level_vtable = vtable->address;

    const u32 set_id_begin = set_level_id->address & ~1u;
    for (u32 offset = 0u; offset + 4u <= set_level_id->size; offset += 2u) {
        const u32 address = set_id_begin + offset;
        const u16 first = env.MemoryRead16(address);
        const u16 second = env.MemoryRead16(address + 2u);
        // str.w r1, [r4, #imm12]
        if (first == 0xF8C4u && (second & 0xF000u) == 0x1000u) {
            runtime.v22_game_level_id_offset = second & 0x0FFFu;
            break;
        }
    }

    const u32 prepare_begin = prepare.address & ~1u;
    for (u32 offset = 0u; offset + 4u <= prepare.size; offset += 2u) {
        const u32 address = prepare_begin + offset;
        const u16 first = env.MemoryRead16(address);
        const u16 second = env.MemoryRead16(address + 2u);
        // ldr.w r3, [r4, #imm12]
        if (first != 0xF8D4u || (second & 0xF000u) != 0x3000u)
            continue;
        const u32 candidate = second & 0x0FFFu;
        if (candidate < 0x100u) continue;

        // The level pointer is consumed for consecutive RGB metadata reads.
        // The nearby LevelSettingsObject pointer has only one byte read before
        // another r3 <- [r4 + offset] load, so stop at that boundary.
        u32 level_metadata_reads = 0u;
        const u32 lookahead =
            std::min<u32>(prepare.size - offset - 4u, 24u);
        for (u32 delta = 4u; delta + 4u <= lookahead; delta += 2u) {
            const u16 following_first =
                env.MemoryRead16(address + delta);
            const u16 following_second =
                env.MemoryRead16(address + delta + 2u);
            if (following_first == 0xF8D4u &&
                (following_second & 0xF000u) == 0x3000u)
                break;
            if (following_first == 0xF893u)
                ++level_metadata_reads;
        }
        if (level_metadata_reads < 3u) continue;
        runtime.v22_play_layer_level_offset = candidate;
        break;
    }

    return runtime.v22_play_layer_level_offset != 0u &&
           runtime.v22_game_level_id_offset != 0u;
}

static std::size_t InstallV22PrepareLevelBridge(ElfRuntime& runtime,
                                                ProbeEnvironment& env) {
    static constexpr const char* kPrepare =
        "_ZN9PlayLayer29prepareCreateObjectsFromSetupESs";
    static constexpr const char* kDecompress =
        "_ZN7cocos2d8ZipUtils16decompressStringESsbi";
    const SymbolRecord* prepare = FindSymbol(runtime, kPrepare);
    const SymbolRecord* decompress = FindSymbol(runtime, kDecompress);
    if (!prepare || !decompress) return 0u;
    if (!DiscoverV22LevelObjectOffsets(runtime, env, *prepare))
        throw std::runtime_error(
            "could not discover PlayLayer/GJGameLevel level-ID offsets");
    if (!DiscoverV22GuestStringBuilder(runtime, env, *decompress))
        throw std::runtime_error(
            "could not discover the beta guest std::string builder");
    const std::vector<u32> call_sites =
        FindThumbBlCallSites(runtime, env, prepare->address);
    if (call_sites.size() != 1u) {
        std::ostringstream error;
        error << "expected one PlayLayer setup callsite, found "
              << call_sites.size();
        throw std::runtime_error(error.str());
    }
    EnsureV22ThunkPage(env);
    const u32 destination = EnsureImport(
        runtime, env, "__dynarmic_v22_prepare_level_setup");
    const u32 thunk = kV22ThunkBase + 0x00u;
    WriteV22ThumbImportThunk(env, thunk, destination);
    WriteThumbBl(env, call_sites.front(), thunk);
    runtime.v22_prepare_setup_address = prepare->address;
    return call_sites.size();
}

static std::size_t InstallV22LevelSettingsParserBridge(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    static constexpr const char* kPrepare =
        "_ZN9PlayLayer29prepareCreateObjectsFromSetupESs";
    static constexpr const char* kParser =
        "_ZN19LevelSettingsObject16objectFromStringESs";
    const SymbolRecord* prepare = FindSymbol(runtime, kPrepare);
    const SymbolRecord* parser = FindSymbol(runtime, kParser);
    if (!prepare || !parser) return 0u;
    const std::vector<u32> call_sites = FindThumbBlCallSitesInRange(
        env, prepare->address, prepare->size, parser->address);
    if (call_sites.size() != 1u) {
        std::ostringstream error;
        error << "expected one LevelSettingsObject parser call in PlayLayer, found "
              << call_sites.size();
        throw std::runtime_error(error.str());
    }
    EnsureV22ThunkPage(env);
    const u32 destination = EnsureImport(
        runtime, env, "__dynarmic_v22_level_settings_from_string");
    const u32 thunk = kV22ThunkBase + 0x20u;
    WriteV22ThumbImportThunk(env, thunk, destination);
    WriteThumbBl(env, call_sites.front(), thunk);
    runtime.v22_level_settings_from_string = parser->address;
    if (const SymbolRecord* create = FindSymbol(
            runtime, "_ZN19LevelSettingsObject6createEv"))
        runtime.v22_level_settings_create = create->address;
    return call_sites.size();
}

static bool LooksLikeGuestObject(const ElfRuntime& runtime,
                                 ProbeEnvironment& env,
                                 u32 object_address) {
    if (!object_address || !env.IsMapped(object_address, 4u)) return false;
    const u32 vtable = env.MemoryRead32(object_address);
    return vtable >= runtime.image_min && vtable < runtime.image_max &&
           env.IsMapped(vtable, 4u);
}

static std::size_t InstallV22EditButtonBridge(ElfRuntime& runtime,
                                              ProbeEnvironment& env) {
    static constexpr const char* kOnEdit =
        "_ZN14EditLevelLayer6onEditEPN7cocos2d8CCObjectE";
    const SymbolRecord* on_edit = FindSymbol(runtime, kOnEdit);
    const SymbolRecord* editor_create = FindSymbol(
        runtime, "_ZN16LevelEditorLayer6createEP11GJGameLevelb");
    bool editor_has_bool = true;
    if (!editor_create) {
        editor_create = FindSymbol(
            runtime, "_ZN16LevelEditorLayer6createEP11GJGameLevel");
        editor_has_bool = false;
    }
    const SymbolRecord* scene_create = FindSymbol(
        runtime, "_ZN7cocos2d7CCScene6createEv");
    const SymbolRecord* add_child = FindSymbol(
        runtime, "_ZN7cocos2d6CCNode8addChildEPS0_");
    const SymbolRecord* director_shared = FindSymbol(
        runtime, "_ZN7cocos2d10CCDirector14sharedDirectorEv");
    const SymbolRecord* replace_scene = FindSymbol(
        runtime, "_ZN7cocos2d10CCDirector12replaceSceneEPNS_7CCSceneE");
    const SymbolRecord* transition = FindSymbol(
        runtime, "_ZN7cocos2d16CCTransitionFade6createEfPNS_7CCSceneE");
    const SymbolRecord* close_text = FindSymbol(
        runtime, "_ZN14EditLevelLayer15closeTextInputsEv");
    const SymbolRecord* verify_name = FindSymbol(
        runtime, "_ZN14EditLevelLayer15verifyLevelNameEv");
    const SymbolRecord* game_manager = FindSymbol(
        runtime, "_ZN11GameManager11sharedStateEv");
    if (!on_edit || !editor_create || !scene_create || !add_child ||
        !director_shared || !replace_scene || !transition || !close_text ||
        !verify_name || !game_manager)
        return 0u;

    EnsureV22ThunkPage(env);
    const u32 destination = EnsureImport(
        runtime, env, "__dynarmic_v22_edit_level_onEdit");
    const u32 thunk = kV22ThunkBase + 0x10u;
    WriteV22ThumbImportThunk(env, thunk, destination);

    std::size_t patched = 0;
    for (u32 address = (runtime.image_min + 3u) & ~3u;
         address <= runtime.image_max - 4u; address += 4u) {
        if (env.MemoryRead32(address) != on_edit->address) continue;
        env.MemoryWrite32(address, thunk | 1u);
        ++patched;
    }
    if (patched == 0u) return 0u;

    runtime.v22_level_editor_create = editor_create->address;
    runtime.v22_scene_create = scene_create->address;
    runtime.v22_node_add_child = add_child->address;
    runtime.v22_director_shared = director_shared->address;
    runtime.v22_director_replace_scene = replace_scene->address;
    runtime.v22_transition_fade_create = transition->address;
    runtime.v22_edit_close_text_inputs = close_text->address;
    runtime.v22_edit_verify_level_name = verify_name->address;
    runtime.v22_game_manager_shared = game_manager->address;
    runtime.v22_game_manager_editor_state_offset = editor_has_bool ? 0x1BCu : 0u;
    switch (runtime.v22_wrapper_editor_profile) {
    case V22EditorRestoreProfile::Early2019:
        runtime.v22_edit_level_pointer_offset = 0x140u;
        break;
    case V22EditorRestoreProfile::Late2022:
        // Verified directly from the 9,541,500-byte EditLevelLayer::init:
        //   str.w r10, [r4, #0x14c]
        runtime.v22_edit_level_pointer_offset = 0x14Cu;
        break;
    case V22EditorRestoreProfile::Late2023:
        runtime.v22_edit_level_pointer_offset = 0x150u;
        break;
    default:
        runtime.v22_edit_level_pointer_offset = editor_has_bool ? 0x150u : 0x140u;
        break;
    }
    return patched;
}

static std::pair<std::size_t, std::size_t>
InstallV22GameplayEditButtonBridges(ElfRuntime& runtime,
                                    ProbeEnvironment& env) {
    const SymbolRecord* pause_on_edit = FindSymbol(
        runtime, "_ZN10PauseLayer6onEditEPN7cocos2d8CCObjectE");
    const SymbolRecord* end_on_edit = FindSymbol(
        runtime, "_ZN13EndLevelLayer6onEditEPN7cocos2d8CCObjectE");
    if (!pause_on_edit && !end_on_edit) return {};

    EnsureV22ThunkPage(env);
    u32 pause_thunk = 0u;
    u32 end_thunk = 0u;
    if (pause_on_edit) {
        pause_thunk = kV22ThunkBase + 0x30u;
        const u32 destination = EnsureImport(
            runtime, env, "__dynarmic_v22_pause_onEdit");
        WriteV22ThumbImportThunk(env, pause_thunk, destination);
    }
    if (end_on_edit) {
        end_thunk = kV22ThunkBase + 0x38u;
        const u32 destination = EnsureImport(
            runtime, env, "__dynarmic_v22_end_onEdit");
        WriteV22ThumbImportThunk(env, end_thunk, destination);
    }

    std::size_t pause_pointers = 0u;
    std::size_t end_pointers = 0u;
    for (u32 address = (runtime.image_min + 3u) & ~3u;
         address <= runtime.image_max - 4u; address += 4u) {
        const u32 value = env.MemoryRead32(address);
        if (pause_on_edit && value == pause_on_edit->address) {
            env.MemoryWrite32(address, pause_thunk | 1u);
            ++pause_pointers;
        } else if (end_on_edit && value == end_on_edit->address) {
            env.MemoryWrite32(address, end_thunk | 1u);
            ++end_pointers;
        }
    }
    return {pause_pointers, end_pointers};
}

static void ResolveV22InputBridgeSymbols(ElfRuntime& runtime) {
    const auto resolve = [&runtime](const char* name) -> u32 {
        const SymbolRecord* symbol = FindSymbol(runtime, name);
        return symbol ? symbol->address : 0u;
    };
    if (!runtime.v22_game_manager_shared)
        runtime.v22_game_manager_shared =
            resolve("_ZN11GameManager11sharedStateEv");
    runtime.v22_gjbase_queue_button =
        resolve("_ZN15GJBaseGameLayer11queueButtonEibb");
    runtime.v22_ui_key_down =
        resolve("_ZN7UILayer7keyDownEN7cocos2d12enumKeyCodesE");
    runtime.v22_ui_key_up =
        resolve("_ZN7UILayer5keyUpEN7cocos2d12enumKeyCodesE");
    runtime.v22_ui_on_check = resolve(
        "_ZN7UILayer7onCheckEPN7cocos2d8CCObjectE");
    runtime.v22_ui_on_delete_check = resolve(
        "_ZN7UILayer13onDeleteCheckEPN7cocos2d8CCObjectE");

    // These offsets belong to the 9,578,364-byte late-beta ARM image. They are
    // enabled from its independently discovered PlayLayer/GJGameLevel layout,
    // not from an APK name or companion-module presence.
    if (runtime.v22_play_layer_level_offset == 820u &&
        runtime.v22_game_level_id_offset == 272u) {
        runtime.v22_ui_layer_offset = 11424u;
        /* Disassembled from this exact 9,578,364-byte image:
           PlayLayer::togglePracticeMode reads/writes byte +0x29A0. */
        runtime.v22_practice_mode_offset = 0x29A0u;
        runtime.v22_editor_playtest_state_offset = 11404u;
    }
}

static std::size_t InstallV22InflateMemoryHook(ElfRuntime& runtime,
                                               ProbeEnvironment& env) {
    static constexpr const char* kSymbol =
        "_ZN7cocos2d8ZipUtils15ccInflateMemoryEPhjPS1_";
    const SymbolRecord* target = FindSymbol(runtime, kSymbol);
    if (!target) return 0u;
    const u32 destination =
        EnsureImport(runtime, env, "__dynarmic_ziputils_ccInflateMemory");
    // The two known ARMv7 betas expose this as an eight-byte Thumb-2 wrapper:
    //   mov.w r3,#0x40000
    //   b.w   ccInflateMemoryWithHint
    // Replacing only this C-style boundary leaves the original guest
    // decompressString, base64 decoder, std::string construction, copies,
    // destructors and hidden sret ABI completely untouched.
    InstallThumbLiteralPcHookPreservingAllArguments(
        env, runtime, *target, 0xF44Fu, destination);
    return 1u;
}

static bool PatchV22FunctionReturnTrue(
    ProbeEnvironment& env, const ElfRuntime& runtime,
    const SymbolRecord& symbol) {
    const u32 address = symbol.address & ~1u;
    if ((symbol.address & 1u) == 0u || symbol.size < 4u ||
        address < runtime.image_min || address > runtime.image_max - 4u)
        return false;
    env.MemoryWrite16(address + 0u, 0x2001u); // movs r0, #1
    env.MemoryWrite16(address + 2u, 0x4770u); // bx lr
    return true;
}

static bool PatchV22FunctionReturnFalse(
    ProbeEnvironment& env, const ElfRuntime& runtime,
    const SymbolRecord& symbol) {
    const u32 address = symbol.address & ~1u;
    if ((symbol.address & 1u) == 0u || symbol.size < 4u ||
        address < runtime.image_min || address > runtime.image_max - 4u)
        return false;
    env.MemoryWrite16(address + 0u, 0x2000u); // movs r0, #0
    env.MemoryWrite16(address + 2u, 0x4770u); // bx lr
    return true;
}

static bool PatchV22FunctionTailJump(
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

static std::size_t InstallV22ConfigurableIconUnlockHooks(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    static constexpr const char* symbols[] = {
        "_ZN11GameManager14isIconUnlockedEi",
        "_ZN11GameManager14isIconUnlockedEi8IconType",
        "_ZN11GameManager15isColorUnlockedEi",
        "_ZN11GameManager15isColorUnlockedEi10UnlockType",
        "_ZN11GameManager15isColorUnlockedEib",
    };
    if (!gd_settings_hack_icons()) return 0u;
    std::size_t patched = 0u;
    for (const char* name : symbols) {
        const SymbolRecord* symbol = FindSymbol(runtime, name);
        if (symbol && PatchV22FunctionReturnTrue(env, runtime, *symbol))
            ++patched;
    }
    return patched;
}

static std::size_t PatchV22CreatorLayerLockedButtons(
    ProbeEnvironment& env, const ElfRuntime& runtime) {
    const SymbolRecord* symbol = FindSymbol(
        runtime, "_ZN12CreatorLayer4initEv");
    if (!symbol || (symbol->address & 1u) == 0u || symbol->size < 16u)
        return 0u;

    const u32 begin = symbol->address & ~1u;
    const u32 end = std::min<u32>(begin + symbol->size, runtime.image_max);
    std::size_t patched = 0u;

    // Lite/World/SubZero preserve their real callbacks only when this BEQ
    // skips the onOnlyFullVersion replacement and the 140-opacity tint block.
    // Different 2.2 builds keep the per-button "full-only" flag in different
    // registers (2022 uses r10, 2023/SubZero uses r9), so match the Thumb-2
    // `cmp.w rN,#0` encoding rather than pinning the register to r10.
    for (u32 address = begin; address + 12u <= end; address += 2u) {
        const u16 compare_first = env.MemoryRead16(address);
        const u16 compare_second = env.MemoryRead16(address + 2u);
        if ((compare_first & 0xFFF0u) != 0xF1B0u ||
            compare_second != 0x0F00u)
            continue;
        const u32 branch_address = address + 4u;
        const u16 branch = env.MemoryRead16(branch_address);
        if ((branch & 0xFF00u) != 0xD000u) continue; // BEQ
        const s32 displacement =
            static_cast<s32>(static_cast<std::int8_t>(branch & 0xFFu)) * 2;
        const u32 target = static_cast<u32>(
            static_cast<s32>(branch_address + 4u) + displacement);
        if (target <= branch_address || target > end) continue;

        bool tint_block = false;
        for (u32 scan = branch_address + 2u; scan + 2u <= target; scan += 2u) {
            if (env.MemoryRead16(scan) == 0x238Cu) { // movs r3,#140
                tint_block = true;
                break;
            }
        }
        if (!tint_block) continue;

        const s32 delta = static_cast<s32>(target) -
                          static_cast<s32>(branch_address + 4u);
        if ((delta & 1) || delta < -2048 || delta > 2046) continue;
        env.MemoryWrite16(
            branch_address,
            static_cast<u16>(0xE000u | ((delta >> 1) & 0x07FF)));
        ++patched;
    }
    return patched;
}

static std::size_t InstallV22MissingGauntletAssetGuard(
    ElfRuntime& runtime, ProbeEnvironment& env, bool assets_present) {
    if (assets_present) return 0u;
    const SymbolRecord* callback = FindSymbol(
        runtime, "_ZN12CreatorLayer11onGauntletsEPN7cocos2d8CCObjectE");
    if (!callback) return 0u;
    // Some Lite beta APKs expose gauntlet code but omit every GauntletSheet
    // resource. Entering the layer then crashes before any HTTP request.
    return PatchV22FunctionReturnFalse(env, runtime, *callback) ? 1u : 0u;
}

static std::size_t InstallV22CreatorEditorUnlock(ElfRuntime& runtime,
                                                 ProbeEnvironment& env) {
    struct Pair { const char* locked; const char* unlocked; };
    static constexpr Pair pairs[] = {
        {"_ZN9MenuLayer13onFullVersionEPN7cocos2d8CCObjectE",
         "_ZN9MenuLayer9onCreatorEPN7cocos2d8CCObjectE"},
        {"_ZN9MenuLayer13onFullVersionEv",
         "_ZN9MenuLayer9onCreatorEv"},
    };
    static constexpr const char* online_checks[] = {
        "_ZN12CreatorLayer19canPlayOnlineLevelsEv",
    };
    if (!gd_settings_full_bypass()) return 0u;
    std::size_t patched = 0u;
    for (const char* name : online_checks) {
        const SymbolRecord* symbol = FindSymbol(runtime, name);
        if (symbol && PatchV22FunctionReturnTrue(env, runtime, *symbol))
            ++patched;
    }
    for (const Pair& pair : pairs) {
        const SymbolRecord* locked = FindSymbol(runtime, pair.locked);
        const SymbolRecord* unlocked = FindSymbol(runtime, pair.unlocked);
        if (locked && unlocked &&
            PatchV22FunctionTailJump(env, runtime, *locked, *unlocked))
            ++patched;
    }
    patched += PatchV22CreatorLayerLockedButtons(env, runtime);
    return patched;
}

struct V22GraphicsPatchCounts {
    std::size_t hd = 0u;
    std::size_t low_memory = 0u;
};

static V22GraphicsPatchCounts InstallV22HighestGraphicsHooks(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    V22GraphicsPatchCounts counts{};
    if (!gd_settings_force_highest_graphics()) return counts;
    if (const SymbolRecord* symbol =
            FindSymbol(runtime, "_ZN15PlatformToolbox4isHDEv")) {
        if (PatchV22FunctionReturnTrue(env, runtime, *symbol)) ++counts.hd;
    }
    static constexpr const char* low_end_symbols[] = {
        "_ZN15PlatformToolbox17isLowMemoryDeviceEv",
        "_ZN16EveryplayToolbox14isLowEndDeviceEv",
    };
    for (const char* name : low_end_symbols) {
        if (const SymbolRecord* symbol = FindSymbol(runtime, name)) {
            if (PatchV22FunctionReturnFalse(env, runtime, *symbol))
                ++counts.low_memory;
        }
    }
    return counts;
}

static std::size_t InstallV22PlatformerSwingReopenPatch(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    const SymbolRecord* init = FindSymbol(
        runtime,
        "_ZN18LevelSettingsLayer4initEP19LevelSettingsObjectP16LevelEditorLayer");
    if (!init || (init->address & 1u) == 0u) return 0u;
    const u32 base = init->address & ~1u;
    constexpr u32 kSwingStateOffset = 0x6B0u;
    const u32 address = base + kSwingStateOffset;
    if (init->size < kSwingStateOffset + 16u ||
        address < runtime.image_min || address > runtime.image_max - 16u)
        return 0u;

    // Known late-beta initialization sequence for the second restricted mode
    // button (swing): ldrb platformer; load virtual setter; xor 1; call.
    // Keep the setter/call and force only its reconstructed enabled state to 1.
    static constexpr u8 expected[] = {
        0x92, 0xF8, 0x13, 0x11,
        0xD3, 0xF8, 0xA4, 0x30,
        0x81, 0xF0, 0x01, 0x01,
        0x98, 0x47,
    };
    for (std::size_t index = 0; index < sizeof(expected); ++index) {
        if (env.MemoryRead8(address + static_cast<u32>(index)) != expected[index])
            return 0u;
    }
    env.MemoryWrite16(address + 0u, 0x2101u); // movs r1, #1
    env.MemoryWrite16(address + 2u, 0xBF00u); // nop
    // Keep ldr.w r3,[r3,#0xa4] at +4.
    env.MemoryWrite16(address + 8u, 0xBF00u); // nop
    env.MemoryWrite16(address + 10u, 0xBF00u); // nop
    return 1u;
}

static std::size_t InstallCcFileUtilsZipHooks(ElfRuntime& runtime, ProbeEnvironment& env) {
    struct Hook { const char* symbol; const char* import; u16 prologue; };
    static constexpr Hook hooks[] = {
        // The 2.2 beta library uses a Thumb-2 push.w prologue and no longer
        // exports existFileDataFromZip. Keep only the data hook.
        {"_ZN7cocos2d11CCFileUtils18getFileDataFromZipEPKcS2_Pm",
         "__dynarmic_ccfileutils_getFileDataFromZip", 0xE92Du},
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
    // two supplied 2.2 betas use different valid compiler prologues:
    // newer push {r4,r5,lr} (0xB530), earlier push {r0-r4,lr} (0xB51F).
    // Validate against those exact known forms, then reuse the common absolute
    // trampoline, which only needs eight writable bytes at the function entry.
    const u32 address = target->address & ~1u;
    const u16 prologue = env.MemoryRead16(address);
    if (prologue != 0xB530u && prologue != 0xB51Fu) {
        std::ostringstream error;
        error << "hook prologue mismatch for " << target->name
              << ": expected 0xb530 or 0xb51f got 0x" << std::hex
              << prologue;
        throw std::runtime_error(error.str());
    }
    InstallThumbAbsoluteImportHookPreservingArguments(
        env, runtime, *target, prologue, destination);
    return 1u;
}


static std::size_t InstallV22NativeHttpSendHook(ElfRuntime& runtime,
                                                ProbeEnvironment& env) {
    static constexpr const char* kSymbol =
        "_ZN7cocos2d9extension12CCHttpClient4sendEPNS0_13CCHttpRequestE";
    const SymbolRecord* target = FindSymbol(runtime, kSymbol);
    if (!target) return 0u;
    const u32 destination = EnsureImport(
        runtime, env, "__dynarmic_v22_native_http_send");
    InstallThumbLiteralPcHookPreservingAllArguments(
        env, runtime, *target, 0xB570u, destination);
    return 1u;
}

static ElfRuntime MapAndRelocateV22CompanionElf(
    const std::vector<u8>& elf, ProbeEnvironment& env,
    ElfRuntime& primary, u32 load_base = kV22CompanionBase) {
    const Elf32Ehdr header = ReadPod<Elf32Ehdr>(elf, 0);
    if (std::memcmp(header.ident, "\x7F" "ELF", 4) != 0 ||
        header.ident[4] != 1 || header.ident[5] != 1 ||
        header.type != kEtDyn || header.machine != kEmArm)
        throw std::runtime_error("companion libgame.so is not ARM ELF32 ET_DYN");
    if (header.phentsize != sizeof(Elf32Phdr) ||
        header.shentsize != sizeof(Elf32Shdr) ||
        static_cast<u64>(header.phoff) +
                static_cast<u64>(header.phnum) * sizeof(Elf32Phdr) > elf.size() ||
        static_cast<u64>(header.shoff) +
                static_cast<u64>(header.shnum) * sizeof(Elf32Shdr) > elf.size())
        throw std::runtime_error("companion ELF tables are invalid");

    ElfRuntime companion{};
    companion.entry = load_base + header.entry;
    u32 min_vaddr = std::numeric_limits<u32>::max();
    u32 max_vaddr = 0u;
    u32 executable_min = std::numeric_limits<u32>::max();
    u32 executable_max = 0u;
    std::vector<Elf32Phdr> phdrs;
    for (u16 i = 0; i < header.phnum; ++i) {
        const Elf32Phdr ph = ReadPod<Elf32Phdr>(
            elf, header.phoff + static_cast<std::size_t>(i) * sizeof(Elf32Phdr));
        phdrs.push_back(ph);
        if (ph.type != kPtLoad || ph.memsz == 0u) continue;
        if (static_cast<u64>(ph.offset) + ph.filesz > elf.size() ||
            ph.filesz > ph.memsz)
            throw std::runtime_error("invalid companion PT_LOAD segment");
        ++companion.load_segments;
        if (ph.flags & 1u) {
            ++companion.executable_segments;
            executable_min = std::min(executable_min, load_base + ph.vaddr);
            executable_max = std::max(
                executable_max, load_base + ph.vaddr + ph.memsz);
        }
        min_vaddr = std::min(min_vaddr, AlignDown(ph.vaddr, kPageSize));
        max_vaddr = std::max(max_vaddr,
                             AlignUp(ph.vaddr + ph.memsz, kPageSize));
    }
    if (!companion.load_segments || max_vaddr <= min_vaddr ||
        !companion.executable_segments ||
        executable_max <= executable_min)
        throw std::runtime_error("companion ELF has no loadable image");
    companion.image_min = load_base + min_vaddr;
    companion.image_max = load_base + max_vaddr;
    if (companion.image_max >= kObjectBase)
        throw std::runtime_error("companion ELF overlaps wrapper memory regions");
    env.Map(companion.image_min,
            static_cast<std::size_t>(companion.image_max - companion.image_min),
            true);
    for (const Elf32Phdr& ph : phdrs) {
        if (ph.type == kPtLoad && ph.filesz)
            env.CopyIn(load_base + ph.vaddr, elf.data() + ph.offset, ph.filesz);
    }

    std::vector<Elf32Shdr> sections;
    sections.reserve(header.shnum);
    for (u16 i = 0; i < header.shnum; ++i)
        sections.push_back(ReadPod<Elf32Shdr>(
            elf, header.shoff + static_cast<std::size_t>(i) * sizeof(Elf32Shdr)));
    if (header.shstrndx >= sections.size())
        throw std::runtime_error("invalid companion section-name table");
    const Elf32Shdr& shstr = sections[header.shstrndx];

    for (const Elf32Shdr& section : sections) {
        if (section.type != kShtDynsym) continue;
        if (section.entsize != sizeof(Elf32Sym) || section.link >= sections.size())
            throw std::runtime_error("invalid companion .dynsym metadata");
        const Elf32Shdr& strings = sections[section.link];
        companion.dynsym_count = section.size / sizeof(Elf32Sym);
        for (std::size_t i = 0; i < companion.dynsym_count; ++i) {
            const Elf32Sym symbol = ReadPod<Elf32Sym>(
                elf, section.offset + i * sizeof(Elf32Sym));
            if (symbol.shndx == kShnUndef && symbol.name) {
                ++companion.undefined_symbols;
                continue;
            }
            if (symbol.shndx == kShnUndef || !symbol.value) continue;
            const std::string name = StringFromTable(elf, strings, symbol.name);
            if (!name.empty())
                companion.symbols.push_back(
                    SymbolRecord{name, load_base + symbol.value, symbol.size});
        }
    }
    std::sort(companion.symbols.begin(), companion.symbols.end(),
              [](const SymbolRecord& lhs, const SymbolRecord& rhs) {
                  if (lhs.address != rhs.address) return lhs.address < rhs.address;
                  return lhs.size > rhs.size;
              });

    auto resolve_primary = [&](const std::string& name) -> u32 {
        if (const SymbolRecord* symbol = FindSymbol(primary, name))
            return symbol->address;
        return 0u;
    };

    for (const Elf32Shdr& section : sections) {
        if (section.type == kShtRela)
            throw std::runtime_error("companion ELF uses unsupported RELA");
        if (section.type != kShtRel) continue;
        if (section.entsize != sizeof(Elf32Rel) || section.link >= sections.size())
            throw std::runtime_error("invalid companion REL metadata");
        const Elf32Shdr& symbols_section = sections[section.link];
        if (symbols_section.entsize != sizeof(Elf32Sym) ||
            symbols_section.link >= sections.size())
            throw std::runtime_error("invalid companion relocation symbols");
        const Elf32Shdr& strings = sections[symbols_section.link];
        const std::size_t symbol_count =
            symbols_section.size / sizeof(Elf32Sym);
        const std::size_t count = section.size / sizeof(Elf32Rel);
        companion.relocation_count += count;
        for (std::size_t rel_index = 0; rel_index < count; ++rel_index) {
            const Elf32Rel rel = ReadPod<Elf32Rel>(
                elf, section.offset + rel_index * sizeof(Elf32Rel));
            const u32 symbol_index = rel.info >> 8u;
            const u32 type = rel.info & 0xFFu;
            const u32 where = load_base + rel.offset;
            const u32 addend = env.MemoryRead32(where);
            u32 value = 0u;
            std::string name;
            if (symbol_index) {
                if (symbol_index >= symbol_count)
                    throw std::runtime_error(
                        "companion relocation symbol index is outside table");
                const Elf32Sym symbol = ReadPod<Elf32Sym>(
                    elf, symbols_section.offset +
                             static_cast<std::size_t>(symbol_index) *
                                 sizeof(Elf32Sym));
                name = StringFromTable(elf, strings, symbol.name);
                if (symbol.shndx != kShnUndef) {
                    value = load_base + symbol.value;
                } else if ((value = resolve_primary(name)) != 0u) {
                    // Resolve Cocos/game code and imported data against the
                    // already relocated primary libcocos2dcpp.so image.
                } else if (IsImportedObjectName(name, symbol.info & 0x0Fu)) {
                    value = EnsureObject(primary, env, name);
                } else {
                    value = EnsureImport(primary, env, name);
                }
            }
            switch (type) {
            case kRArmNone: break;
            case kRArmAbs32: env.MemoryWrite32(where, value + addend); break;
            case kRArmGlobDat:
            case kRArmJumpSlot: env.MemoryWrite32(where, value); break;
            case kRArmRelative:
                env.MemoryWrite32(where, load_base + addend);
                ++companion.relative_relocations;
                break;
            default: {
                std::ostringstream error;
                error << "unsupported companion ARM relocation " << type
                      << " at 0x" << std::hex << rel.offset << " (" << name
                      << ")";
                throw std::runtime_error(error.str());
            }
            }
            if (type == kRArmAbs32 || type == kRArmGlobDat ||
                type == kRArmJumpSlot)
                ++companion.imported_relocations;
        }
    }

    for (const Elf32Shdr& section : sections) {
        const std::string name = SectionName(elf, shstr, section.name);
        if (section.type != kShtInitArray && name != ".init_array") continue;
        if (section.size % 4u)
            throw std::runtime_error("invalid companion .init_array size");
        const std::size_t count = section.size / 4u;
        companion.constructors.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            companion.constructors.push_back(env.MemoryRead32(
                load_base + section.addr + static_cast<u32>(i * 4u)));
        break;
    }

    const SymbolRecord* editor_init = FindSymbol(
        companion, "_ZN19LevelEditorLayerExt5initHEP11GJGameLevel");
    const SymbolRecord* editor_visibility = FindSymbol(
        companion, "_ZN19LevelEditorLayerExt17updateVisibilityHEf");
    const SymbolRecord* editor_visibility_original = FindSymbol(
        companion, "_ZN19LevelEditorLayerExt17updateVisibilityOE");
    // libgame.so is an optional beta-specific extension, not the executable's
    // primary game image. Several legitimate 2.2 betas ship a different
    // companion ABI (including one with no LevelEditorLayerExt::initH at all).
    // Mapping such a module is still useful for symbol diagnostics and must
    // never make an otherwise runnable APK fail at startup.
    primary.v22_companion_editor_init =
        editor_init ? editor_init->address : 0u;
    primary.v22_companion_editor_visibility =
        editor_visibility ? editor_visibility->address : 0u;
    primary.v22_companion_editor_visibility_original_slot =
        editor_visibility_original ? editor_visibility_original->address : 0u;
    primary.v22_companion_image_min = companion.image_min;
    primary.v22_companion_image_max = companion.image_max;
    primary.v22_companion_executable_min = executable_min;
    primary.v22_companion_executable_max = executable_max;
    primary.symbols.insert(primary.symbols.end(), companion.symbols.begin(),
                           companion.symbols.end());
    std::sort(primary.symbols.begin(), primary.symbols.end(),
              [](const SymbolRecord& lhs, const SymbolRecord& rhs) {
                  if (lhs.address != rhs.address) return lhs.address < rhs.address;
                  return lhs.size > rhs.size;
              });
    return companion;
}

static bool HasV22PrimaryEditorInitializer(const ElfRuntime& runtime,
                                           ProbeEnvironment& env) {
    const SymbolRecord* initializer = FindSymbol(
        runtime, "_ZN16LevelEditorLayer4initEP11GJGameLevelb");
    if (!initializer)
        initializer = FindSymbol(
            runtime, "_ZN16LevelEditorLayer4initEP11GJGameLevel");
    if (!initializer || initializer->address < runtime.image_min ||
        initializer->address >= runtime.image_max)
        return false;

    // SubZero and both supplied beta layouts intentionally replace the editor
    // initializer with a four-byte tail branch to GJBaseGameLayer::init. A
    // future APK with a real in-image implementation can use the same bridge,
    // but never treat that base-only stub as a complete editor.
    if (initializer->size <= 4u) return false;
    return env.IsMapped(initializer->address & ~1u, initializer->size);
}

static V22EditorRestoreProfile DetectV22EditorRestoreProfile(
    const ElfRuntime& runtime, ProbeEnvironment& env) {
    const SymbolRecord* init_late = FindSymbol(
        runtime, "_ZN16LevelEditorLayer4initEP11GJGameLevelb");
    const SymbolRecord* init_early = FindSymbol(
        runtime, "_ZN16LevelEditorLayer4initEP11GJGameLevel");
    const SymbolRecord* create_late = FindSymbol(
        runtime, "_ZN16LevelEditorLayer6createEP11GJGameLevelb");
    const SymbolRecord* create_early = FindSymbol(
        runtime, "_ZN16LevelEditorLayer6createEP11GJGameLevel");
    const SymbolRecord* setup = FindSymbol(
        runtime, "_ZN15GJBaseGameLayer11setupLayersEv");
    const SymbolRecord* editor_ui = FindSymbol(
        runtime, "_ZN8EditorUI6createEP16LevelEditorLayer");
    const SymbolRecord* grid = FindSymbol(
        runtime, "_ZN13DrawGridLayer6createEPN7cocos2d6CCNodeEP16LevelEditorLayer");
    if (!setup || !editor_ui || !grid) return V22EditorRestoreProfile::None;

    auto stub = [&env](const SymbolRecord* symbol) {
        return symbol && symbol->size <= 4u &&
               env.IsMapped(symbol->address & ~1u, std::max<u32>(symbol->size, 2u));
    };
    if (runtime.primary_file_bytes == 9144004u && create_early && stub(init_early))
        return V22EditorRestoreProfile::Early2019;
    if (runtime.primary_file_bytes == 9541500u && create_late && stub(init_late))
        return V22EditorRestoreProfile::Late2022;
    if (runtime.primary_file_bytes == 9578364u && create_late && stub(init_late))
        return V22EditorRestoreProfile::Late2023;
    return V22EditorRestoreProfile::None;
}

static bool HasCompatibleV22CompanionEditorInitializer(
    const ElfRuntime& runtime, ProbeEnvironment& env,
    bool late_beta_layout) {
    if (!late_beta_layout || !runtime.v22_companion_editor_init ||
        runtime.v22_companion_editor_init < runtime.v22_companion_executable_min ||
        runtime.v22_companion_editor_init >= runtime.v22_companion_executable_max)
        return false;
    const SymbolRecord* initializer = FindSymbol(
        runtime, "_ZN19LevelEditorLayerExt5initHEP11GJGameLevel");
    if (!initializer ||
        initializer->address != runtime.v22_companion_editor_init ||
        initializer->size < 256u ||
        !env.IsMapped(initializer->address & ~1u, initializer->size))
        return false;
    const u32 begin = initializer->address & ~1u;
    const u16 prologue = env.MemoryRead16(begin);
    if (prologue != 0xB5F0u && prologue != 0xE92Du) return false;

    // Validate ABI-defining writes instead of pinning support to one APK CRC.
    // The compatible late-beta helper stores GJGameLevel at layer+316 and the
    // newly-created EditorUI at layer+852. A same-named function from another
    // layout is not safe to call merely because its symbol exists.
    bool stores_level = false;
    bool stores_editor_ui = false;
    for (u32 offset = 0u; offset + 4u <= initializer->size; offset += 2u) {
        const u16 first = env.MemoryRead16(begin + offset);
        const u16 second = env.MemoryRead16(begin + offset + 2u);
        if (first != 0xF8C5u) continue; // str.w Rt,[r5,#imm12]
        const u32 field = second & 0x0FFFu;
        stores_level |= field == 316u;
        stores_editor_ui |= field == 852u;
    }
    return stores_level && stores_editor_ui;
}

struct V22VisualHookCounts {
    std::pair<std::size_t, std::size_t> editor_visibility;
    std::pair<std::size_t, std::size_t> play_visibility;
    std::pair<std::size_t, std::size_t> camera_background;
    std::pair<std::size_t, std::size_t> batch_blend;
    std::pair<std::size_t, std::size_t> batch_init_guard;
    u32 ground_asset_max = 0u;
    u32 background_asset_max = 0u;
    bool exact_companion_editor = false;
};

static std::pair<u32, u32> PatchV22ArtAssetLimits(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    const SymbolRecord* load_ground = FindSymbol(
        runtime, "_ZN11GameManager10loadGroundEi");
    const SymbolRecord* get_ground = FindSymbol(
        runtime, "_ZN11GameManager11getGTextureEi");
    const SymbolRecord* load_background = FindSymbol(
        runtime, "_ZN11GameManager14loadBackgroundEi");
    const SymbolRecord* get_background = FindSymbol(
        runtime, "_ZN11GameManager12getBGTextureEi");
    if (!load_ground || !get_ground || !load_background || !get_background)
        throw std::runtime_error(
            "V22 art asset-limit symbols are unavailable");

    /*
     * This community beta exposes the later game's selector ranges (30 ground
     * slots and 45 background slots), while its APK contains only 18 ground
     * textures and 26 background textures.  Selecting an absent entry creates
     * a SpriteBatchNode with a null texture.  Do not let that invalid object
     * enter initWithTexture: clamp the beta's own load/get functions to the
     * assets actually packaged in this APK.  Every patched halfword is checked
     * first so a different binary layout fails closed instead of being edited.
     */
    struct LimitPatch {
        const SymbolRecord* symbol;
        u32 compare_offset;
        u32 move_offset;
        u16 expected_compare;
        u16 expected_move;
        u16 replacement_compare;
        u16 replacement_move;
    };
    const std::array<LimitPatch, 4> patches{{
        {load_ground,       8u, 12u, 0x291Eu, 0x211Eu, 0x2912u, 0x2112u},
        {get_ground,        6u, 10u, 0x291Eu, 0x211Eu, 0x2912u, 0x2112u},
        {load_background,   8u, 12u, 0x292Du, 0x212Du, 0x291Au, 0x211Au},
        {get_background,    6u, 10u, 0x292Du, 0x212Du, 0x291Au, 0x211Au},
    }};
    for (const LimitPatch& patch : patches) {
        const u32 begin = patch.symbol->address & ~1u;
        if (!env.IsMapped(begin + patch.compare_offset, 2u) ||
            !env.IsMapped(begin + patch.move_offset, 2u) ||
            env.MemoryRead16(begin + patch.compare_offset) !=
                patch.expected_compare ||
            env.MemoryRead16(begin + patch.move_offset) !=
                patch.expected_move)
            throw std::runtime_error(
                "V22 art asset-limit instruction validation failed");
    }
    for (const LimitPatch& patch : patches) {
        const u32 begin = patch.symbol->address & ~1u;
        env.MemoryWrite16(begin + patch.compare_offset,
                          patch.replacement_compare);
        env.MemoryWrite16(begin + patch.move_offset,
                          patch.replacement_move);
    }
    return {18u, 26u};
}

static V22VisualHookCounts InstallV22SafeVisualHooks(
    ElfRuntime& runtime, ProbeEnvironment& env) {
    const SymbolRecord* editor_visibility = FindSymbol(
        runtime, "_ZN16LevelEditorLayer16updateVisibilityEf");
    const SymbolRecord* play_visibility = FindSymbol(
        runtime, "_ZN9PlayLayer16updateVisibilityEf");
    const SymbolRecord* add_main_sprite = FindSymbol(
        runtime, "_ZN10GameObject21addMainSpriteToParentEb");
    const SymbolRecord* has_secondary_color = FindSymbol(
        runtime, "_ZN10GameObject17hasSecondaryColorEv");
    const SymbolRecord* add_color_sprite = FindSymbol(
        runtime, "_ZN10GameObject22addColorSpriteToParentEb");
    const SymbolRecord* activate_object = FindSymbol(
        runtime, "_ZN10GameObject14activateObjectEv");
    const SymbolRecord* deactivate_object = FindSymbol(
        runtime, "_ZN10GameObject16deactivateObjectEb");
    const SymbolRecord* game_object_opacity = FindSymbol(
        runtime, "_ZN10GameObject10setOpacityEh");
    const SymbolRecord* sprite_opacity = FindSymbol(
        runtime, "_ZN7cocos2d8CCSprite10setOpacityEh");
    const SymbolRecord* game_manager_shared = FindSymbol(
        runtime, "_ZN11GameManager11sharedStateEv");
    const SymbolRecord* get_game_variable = FindSymbol(
        runtime, "_ZN11GameManager15getGameVariableEPKc");
    const SymbolRecord* pre_update_visibility = FindSymbol(
        runtime, "_ZN15GJBaseGameLayer19preUpdateVisibilityEf");
    const SymbolRecord* update_object_colors = FindSymbol(
        runtime, "_ZN16LevelEditorLayer18updateObjectColorsEPN7cocos2d7CCArrayE");
    const SymbolRecord* add_object = FindSymbol(
        runtime, "_ZN7cocos2d7CCArray9addObjectEPNS_8CCObjectE");
    const SymbolRecord* remove_all_objects = FindSymbol(
        runtime, "_ZN7cocos2d7CCArray16removeAllObjectsEv");
    const SymbolRecord* process_area_visual_actions = FindSymbol(
        runtime, "_ZN15GJBaseGameLayer24processAreaVisualActionsEv");
    const SymbolRecord* sort_batchnode_children = FindSymbol(
        runtime, "_ZN16LevelEditorLayer21sortBatchnodeChildrenEf");
    const SymbolRecord* update_grid_layer = FindSymbol(
        runtime, "_ZN16LevelEditorLayer15updateGridLayerEv");
    const SymbolRecord* draw_grid_vtable = FindSymbol(
        runtime, "_ZTV13DrawGridLayer");
    const SymbolRecord* draw_grid_music = FindSymbol(
        runtime, "_ZN13DrawGridLayer20updateMusicGuideTimeEf");
    const SymbolRecord* draw_grid_markers = FindSymbol(
        runtime, "_ZN13DrawGridLayer17updateTimeMarkersEv");
    const SymbolRecord* level_settings_updated = FindSymbol(
        runtime, "_ZN16LevelEditorLayer20levelSettingsUpdatedEv");
    const SymbolRecord* update_camera_background = FindSymbol(
        runtime, "_ZN15GJBaseGameLayer17updateCameraBGArtEN7cocos2d7CCPointE");
    const SymbolRecord* batch_update_blend = FindSymbol(
        runtime, "_ZN7cocos2d17CCSpriteBatchNode15updateBlendFuncEv");
    const SymbolRecord* batch_init_texture = FindSymbol(
        runtime, "_ZN7cocos2d17CCSpriteBatchNode15initWithTextureEPNS_11CCTexture2DEj");
    if (!editor_visibility || !play_visibility ||
        !update_camera_background || !batch_update_blend ||
        !batch_init_texture)
        throw std::runtime_error(
            "V22 primary visual symbols are unavailable");

    const auto [ground_asset_max, background_asset_max] =
        PatchV22ArtAssetLimits(runtime, env);

    std::pair<std::size_t, std::size_t> editor_visibility_counts{};
    bool exact_companion_editor = false;
    const SymbolRecord* companion_visibility = FindSymbol(
        runtime, "_ZN19LevelEditorLayerExt17updateVisibilityHEf");
    const SymbolRecord* companion_original_slot = FindSymbol(
        runtime, "_ZN19LevelEditorLayerExt17updateVisibilityOE");

    /*
     * The late beta's primary LevelEditorLayer::updateVisibility is only a
     * two-byte stub. Fix6 replaced it with a compact host approximation, which
     * restored object colors but omitted the song marker, BPM guidelines and
     * some clip/camera cleanup. For the endurance branch, prefer the complete
     * ABI-validated companion helper and wire its "original" function slot to
     * the primary stub. This is slower than the approximation because it walks
     * all editor sections, but correctness is the priority in this branch.
     */
    /* The first endurance build proved this full companion redirect can spend several
       seconds in one editor frame and freeze the window. Keep the code for
       forensic comparison, but never activate it in an endurance build. */
    const bool exact_companion_visibility_is_safe = false;
    if (exact_companion_visibility_is_safe &&
        gd_settings_v22_exact_editor_visibility() &&
        companion_visibility && companion_original_slot &&
        runtime.v22_companion_editor_visibility ==
            companion_visibility->address &&
        runtime.v22_companion_editor_visibility_original_slot ==
            companion_original_slot->address &&
        companion_visibility->size >= 128u &&
        companion_original_slot->size >= 4u &&
        env.IsMapped(companion_visibility->address & ~1u,
                     companion_visibility->size) &&
        env.IsMapped(companion_original_slot->address, 4u)) {
        env.MemoryWrite32(companion_original_slot->address,
                          editor_visibility->address);
        editor_visibility_counts = RedirectV22FunctionReferences(
            runtime, env, *editor_visibility,
            companion_visibility->address,
            kV22ThunkBase + 0x40u);
        exact_companion_editor = true;
        runtime.v22_companion_editor_visibility_enabled = true;
    } else {
        if (!add_main_sprite || !has_secondary_color || !add_color_sprite ||
            !activate_object || !deactivate_object ||
            !game_object_opacity || !sprite_opacity ||
            !game_manager_shared || !get_game_variable ||
            !pre_update_visibility || !update_object_colors || !add_object ||
            !remove_all_objects || !process_area_visual_actions ||
            !sort_batchnode_children || !update_grid_layer ||
            !draw_grid_vtable || !draw_grid_music || !draw_grid_markers ||
            !level_settings_updated || !update_camera_background ||
            !batch_update_blend || !batch_init_texture)
            throw std::runtime_error(
                "safe V22 host visual bridge symbols are unavailable");

        runtime.v22_game_object_add_main_sprite = add_main_sprite->address;
        runtime.v22_game_object_has_secondary_color =
            has_secondary_color->address;
        runtime.v22_game_object_add_color_sprite = add_color_sprite->address;
        runtime.v22_game_object_activate = activate_object->address;
        runtime.v22_game_object_deactivate = deactivate_object->address;
        runtime.v22_game_object_set_opacity = game_object_opacity->address;
        runtime.v22_ccsprite_set_opacity = sprite_opacity->address;
        runtime.v22_game_manager_shared = game_manager_shared->address;
        runtime.v22_game_manager_get_game_variable =
            get_game_variable->address;
        runtime.v22_gjbase_pre_update_visibility =
            pre_update_visibility->address;
        runtime.v22_level_editor_update_object_colors =
            update_object_colors->address;
        runtime.v22_ccarray_add_object = add_object->address;
        runtime.v22_ccarray_remove_all_objects =
            remove_all_objects->address;
        runtime.v22_gjbase_process_area_visual_actions =
            process_area_visual_actions->address;
        runtime.v22_level_editor_sort_batchnode_children =
            sort_batchnode_children->address;
        runtime.v22_level_editor_update_grid_layer = update_grid_layer->address;
        runtime.v22_draw_grid_vtable = draw_grid_vtable->address;
        runtime.v22_draw_grid_update_music_guide_time = draw_grid_music->address;
        runtime.v22_draw_grid_update_time_markers = draw_grid_markers->address;
        runtime.v22_level_editor_level_settings_updated =
            level_settings_updated->address;

        const u32 editor_host = EnsureImport(
            runtime, env, "__dynarmic_v22_editor_visibility");
        editor_visibility_counts = RedirectV22FunctionReferences(
            runtime, env, *editor_visibility, editor_host,
            kV22ThunkBase + 0x40u);
    }

    /*
     * updateCameraBGArt has three callers in this exact beta: the direct editor
     * update plus the shared new/old camera paths used while editor playtesting.
     * EnduranceTest8 patched only the direct caller, which is why the wrapped
     * black background kept moving during playtest and disappeared on Stop.
     * Route all three calls through the host.  The host suppresses them only
     * when `this` is the active LevelEditorLayer and invokes the untouched
     * original function for normal PlayLayer gameplay.
     *
     * The blend replacement remains limited to the one call inside
     * SpriteBatchNode::initWithTexture. The numeric limit patch blocks the
     * original out-of-range paths, while the host implementation below also
     * repairs community selector entries that still resolve to a null texture.
     */
    const u32 camera_background_host = EnsureImport(
        runtime, env, "__dynarmic_v22_update_camera_background");
    const std::vector<u32> camera_background_calls =
        FindThumbBlCallSites(
            runtime, env, update_camera_background->address);
    EnsureV22ThunkPage(env);
    WriteV22ThumbConditionalImportThenCallThunk(
        env, kV22ThunkBase + 0x80u, camera_background_host,
        update_camera_background->address);
    for (u32 call : camera_background_calls)
        WriteThumbBl(env, call, kV22ThunkBase + 0x80u);
    const std::pair<std::size_t, std::size_t> camera_background_counts{
        0u, camera_background_calls.size()};

    const u32 reject_null_texture_host = EnsureImport(
        runtime, env, "__dynarmic_v22_reject_null_batch_texture");
    EnsureV22ThunkPage(env);
    const u32 batch_init_guard_wrapper = kV22ThunkBase + 0xC0u;
    WriteV22ThumbNullTextureGuardThunk(
        env, batch_init_guard_wrapper, batch_init_texture->address,
        reject_null_texture_host);
    const auto batch_init_guard_counts = RedirectV22FunctionReferences(
        runtime, env, *batch_init_texture, batch_init_guard_wrapper | 1u,
        kV22ThunkBase + 0xE0u);

    const u32 batch_blend_host = EnsureImport(
        runtime, env, "__dynarmic_v22_batch_update_blend");
    const std::vector<u32> batch_blend_calls = FindThumbBlCallSitesInRange(
        env, batch_init_texture->address, batch_init_texture->size,
        batch_update_blend->address);
    WriteV22ThumbImportThunk(
        env, kV22ThunkBase + 0xA0u, batch_blend_host);
    for (u32 call : batch_blend_calls)
        WriteThumbBl(env, call, kV22ThunkBase + 0xA0u);
    const std::pair<std::size_t, std::size_t> batch_blend_counts{
        0u, batch_blend_calls.size()};
    if (camera_background_calls.size() != 3u ||
        batch_blend_calls.size() != 1u ||
        batch_init_guard_counts.first + batch_init_guard_counts.second == 0u)
        throw std::runtime_error(
            "V22 background/blend exact callsite validation failed");

    const u32 play_host = EnsureImport(
        runtime, env, "__dynarmic_v22_play_visibility");
    EnsureV22ThunkPage(env);
    const u32 play_wrapper = kV22ThunkBase + 0x60u;
    WriteV22ThumbCallThenImportThunk(
        env, play_wrapper, play_visibility->address, play_host);
    const auto play_visibility_counts = RedirectV22FunctionReferences(
        runtime, env, *play_visibility, play_wrapper | 1u,
        kV22ThunkBase + 0x78u);

    if (editor_visibility_counts.first +
                editor_visibility_counts.second ==
            0u ||
        play_visibility_counts.first + play_visibility_counts.second == 0u)
        throw std::runtime_error(
            "safe V22 visual hooks did not find primary references");
    return V22VisualHookCounts{
        editor_visibility_counts, play_visibility_counts,
        camera_background_counts, batch_blend_counts,
        batch_init_guard_counts, ground_asset_max, background_asset_max,
        exact_companion_editor};
}

static std::pair<std::size_t, std::size_t>
InstallV22StockEditorVisibilityBridge(ElfRuntime& runtime,
                                      ProbeEnvironment& env) {
    const SymbolRecord* editor_visibility = FindSymbol(
        runtime, "_ZN16LevelEditorLayer16updateVisibilityEf");
    if (!editor_visibility || editor_visibility->size > 4u)
        return {};
    const SymbolRecord* add_main_sprite = FindSymbol(
        runtime, "_ZN10GameObject21addMainSpriteToParentEb");
    const SymbolRecord* has_secondary_color = FindSymbol(
        runtime, "_ZN10GameObject17hasSecondaryColorEv");
    const SymbolRecord* add_color_sprite = FindSymbol(
        runtime, "_ZN10GameObject22addColorSpriteToParentEb");
    const SymbolRecord* activate_object = FindSymbol(
        runtime, "_ZN10GameObject14activateObjectEv");
    const SymbolRecord* deactivate_object = FindSymbol(
        runtime, "_ZN10GameObject16deactivateObjectEb");
    const SymbolRecord* game_object_opacity = FindSymbol(
        runtime, "_ZN10GameObject10setOpacityEh");
    const SymbolRecord* game_manager_shared = FindSymbol(
        runtime, "_ZN11GameManager11sharedStateEv");
    const SymbolRecord* get_game_variable = FindSymbol(
        runtime, "_ZN11GameManager15getGameVariableEPKc");
    const SymbolRecord* pre_update_visibility = FindSymbol(
        runtime, "_ZN15GJBaseGameLayer19preUpdateVisibilityEf");
    const SymbolRecord* update_object_colors = FindSymbol(
        runtime, "_ZN16LevelEditorLayer18updateObjectColorsEPN7cocos2d7CCArrayE");
    const SymbolRecord* add_object = FindSymbol(
        runtime, "_ZN7cocos2d7CCArray9addObjectEPNS_8CCObjectE");
    const SymbolRecord* remove_all_objects = FindSymbol(
        runtime, "_ZN7cocos2d7CCArray16removeAllObjectsEv");
    const SymbolRecord* process_area_visual_actions = FindSymbol(
        runtime, "_ZN15GJBaseGameLayer24processAreaVisualActionsEv");
    const SymbolRecord* sort_batchnode_children = FindSymbol(
        runtime, "_ZN16LevelEditorLayer21sortBatchnodeChildrenEf");
    if (!add_main_sprite || !has_secondary_color || !add_color_sprite ||
        !activate_object || !deactivate_object || !game_object_opacity ||
        !game_manager_shared || !get_game_variable || !pre_update_visibility ||
        !update_object_colors || !add_object || !remove_all_objects ||
        !process_area_visual_actions || !sort_batchnode_children)
        return {};

    runtime.v22_game_object_add_main_sprite = add_main_sprite->address;
    runtime.v22_game_object_has_secondary_color = has_secondary_color->address;
    runtime.v22_game_object_add_color_sprite = add_color_sprite->address;
    runtime.v22_game_object_activate = activate_object->address;
    runtime.v22_game_object_deactivate = deactivate_object->address;
    runtime.v22_game_object_set_opacity = game_object_opacity->address;
    runtime.v22_game_manager_shared = game_manager_shared->address;
    runtime.v22_game_manager_get_game_variable = get_game_variable->address;
    runtime.v22_gjbase_pre_update_visibility = pre_update_visibility->address;
    runtime.v22_level_editor_update_object_colors = update_object_colors->address;
    runtime.v22_ccarray_add_object = add_object->address;
    runtime.v22_ccarray_remove_all_objects = remove_all_objects->address;
    runtime.v22_gjbase_process_area_visual_actions = process_area_visual_actions->address;
    runtime.v22_level_editor_sort_batchnode_children = sort_batchnode_children->address;

    const u32 editor_host = EnsureImport(
        runtime, env, "__dynarmic_v22_editor_visibility");
    return RedirectV22FunctionReferences(
        runtime, env, *editor_visibility, editor_host,
        kV22ThunkBase + 0x40u);
}

static ElfRuntime MapAndRelocateElf(const std::vector<u8>& elf, ProbeEnvironment& env) {
    const Elf32Ehdr header = ReadPod<Elf32Ehdr>(elf, 0);
    if (std::memcmp(header.ident, "\x7F" "ELF", 4) != 0 || header.ident[4] != 1 || header.ident[5] != 1) {
        throw std::runtime_error("input native library is not a little-endian ELF32 image");
    }
    if (header.type != kEtDyn || header.machine != kEmArm) throw std::runtime_error("input native library is not an ARM ET_DYN shared object");
    if (header.phentsize != sizeof(Elf32Phdr) || header.shentsize != sizeof(Elf32Shdr)) throw std::runtime_error("unexpected ELF table entry sizes");
    if (static_cast<u64>(header.phoff) + static_cast<u64>(header.phnum) * sizeof(Elf32Phdr) > elf.size() ||
        static_cast<u64>(header.shoff) + static_cast<u64>(header.shnum) * sizeof(Elf32Shdr) > elf.size()) {
        throw std::runtime_error("ELF program or section table is truncated");
    }

    ElfRuntime runtime{};
    runtime.primary_file_bytes = elf.size();
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
            else if (name == "_ZN7cocos2d6CCNode6getTagEv" ||
                     name == "_ZNK7cocos2d6CCNode6getTagEv") runtime.ccnode_get_tag = address;
            else if (name == "_ZN7cocos2d6CCNode6setTagEi") runtime.ccnode_set_tag = address;
            else if (name == "_ZN8EditorUI14moveObjectCallEPN7cocos2d6CCNodeE") runtime.editor_move_object_call = address;
            else if (name == "_ZN8EditorUI14moveObjectCallEPN7cocos2d8CCObjectE") runtime.editor_move_object_call = address;
            else if (name == "_ZN8EditorUI14moveObjectCallE11EditCommand") runtime.editor_move_edit_command = address;
            else if (name == "_ZN8EditorUI19transformObjectCallEPN7cocos2d6CCNodeE") runtime.editor_transform_object_call = address;
            else if (name == "_ZN8EditorUI19transformObjectCallEPN7cocos2d8CCObjectE") runtime.editor_transform_object_call = address;
            else if (name == "_ZN8EditorUI19transformObjectCallE11EditCommand") runtime.editor_transform_edit_command = address;
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
        throw std::runtime_error("required JNI/render/input exports were not found in the ARMv7 library");
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

#ifdef _WIN32
enum class AsyncDnsKind { AddrInfo, HostByName };

struct HostAddrInfoRecord {
    int flags = 0;
    int family = AF_UNSPEC;
    int socktype = 0;
    int protocol = 0;
    std::vector<u8> address;
    std::string canonical;
};

static std::atomic<u32> g_async_dns_threads_active{0};

struct AsyncDnsRequest {
    AsyncDnsKind kind = AsyncDnsKind::AddrInfo;
    std::string node;
    std::string service;
    bool has_hints = false;
    int flags = 0;
    int family = AF_UNSPEC;
    int socktype = 0;
    int protocol = 0;
    std::chrono::steady_clock::time_point started{};
    std::atomic<int> state{0}; // 0=pending, 1=finished
    std::atomic<int> code{EAI_AGAIN};
    std::mutex mutex;
    std::vector<HostAddrInfoRecord> records;
};
#endif

struct GuestPollFd {
    s32 fd;
    std::int16_t events;
    std::int16_t revents;
};
static_assert(sizeof(GuestPollFd) == 8);

struct GuestIovec {
    u32 base;
    u32 length;
};
static_assert(sizeof(GuestIovec) == 8);

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
    PracticeCheckpoint,
    EditorCommand,
    ExtrasAction,
    PlatformButton,
    Pause,
    Resume
};

struct HostEvent {
    HostEventType type = HostEventType::TouchMove;
    float x = 0.0f;
    float y = 0.0f;
    u32 value = 0;
    bool pressed = false;
};

#ifdef _WIN32
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
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
        remove_pause_button_option_ = gd_settings_remove_pause_button() != 0;
        hide_cursor_option_ = gd_settings_hide_cursor_when_playing() != 0;
        instance_ = GetModuleHandleA(nullptr);
        const char* class_name = "GeometryDashUnified ARMv7Window";
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
        const char* configured_title = std::getenv("GD_GAME_TITLE");
        const char* window_title = configured_title && *configured_title
            ? configured_title : "Geometry Dash";
        window_ = CreateWindowExA(0, class_name, window_title,
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  window_x, window_y, window_width, window_height,
                                  nullptr, nullptr, instance_, this);
        if (!window_) return Fail("CreateWindowExA failed");
        if (gd_apply_window_icon(window_) && log_) {
            *log_ << "Window icon applied from GD_WINDOW_ICON\n";
        }
        gd_extras_menu_init(&extras_menu_);
        if (extras_menu_.enabled) {
            gd_extras_menu_attach(&extras_menu_, window_);
            if (log_) *log_ << "RESULT: DYNARMIC_EXTRAS_MENU_READY\n";
        }
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
        gd_extras_menu_destroy(&extras_menu_);
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

    std::pair<int, int> ClientSize() const {
        RECT area{};
        int width = native_width_;
        int height = native_height_;
        if (window_ && GetClientRect(window_, &area) &&
            area.right > area.left && area.bottom > area.top) {
            width = area.right - area.left;
            height = area.bottom - area.top;
        }
        return {width, height};
    }

    void ScaleGuestRect(GLint x, GLint y, GLsizei width, GLsizei height,
                        GLint& scaled_x, GLint& scaled_y,
                        GLsizei& scaled_width, GLsizei& scaled_height) const {
        const auto [client_width, client_height] = ClientSize();
        const double sx = native_width_ > 0
            ? static_cast<double>(client_width) / static_cast<double>(native_width_)
            : 1.0;
        const double sy = native_height_ > 0
            ? static_cast<double>(client_height) / static_cast<double>(native_height_)
            : 1.0;
        const double scale = std::max(0.0001, std::min(sx, sy));
        const double content_width = static_cast<double>(native_width_) * scale;
        const double content_height = static_cast<double>(native_height_) * scale;
        const double offset_x = (static_cast<double>(client_width) - content_width) * 0.5;
        const double offset_y = (static_cast<double>(client_height) - content_height) * 0.5;
        scaled_x = static_cast<GLint>(std::lround(offset_x + static_cast<double>(x) * scale));
        scaled_y = static_cast<GLint>(std::lround(offset_y + static_cast<double>(y) * scale));
        scaled_width = static_cast<GLsizei>(std::max<long>(
            0, std::lround(static_cast<double>(width) * scale)));
        scaled_height = static_cast<GLsizei>(std::max<long>(
            0, std::lround(static_cast<double>(height) * scale)));
    }

    void RememberGuestViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
        guest_viewport_ = {x, y, width, height};
        have_guest_viewport_ = true;
    }

    void RememberGuestScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
        guest_scissor_ = {x, y, width, height};
        have_guest_scissor_ = true;
    }

    bool ReadGuestClipRect(GLenum pname, std::array<GLint, 4>& values) const {
        if (pname == 0x0BA2u && have_guest_viewport_) {
            values = guest_viewport_;
            return true;
        }
        if (pname == 0x0C10u && have_guest_scissor_) {
            values = guest_scissor_;
            return true;
        }
        return false;
    }

    // Deliberately leave GL clip state under guest control.  EnduranceTest4's
    // host scissor/viewport sanitizer did not remove the right-side void and
    // risked invalidating editor-owned render state.
    void ResetFrameClipState(bool) {}
    void Swap() {
        if (resize_pending_) {
            ReapplyGuestRects();
            resize_pending_ = false;
        }
        if (device_) SwapBuffers(device_);
    }
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
    void SetGameplayActive(bool active, bool editor_active = false) {
        gameplay_active_ = active;
        editor_active_ = editor_active;
        if (!active || editor_active) {
            cursor_force_visible_ = false;
            cursor_pause_click_seen_ = false;
        }
        UpdateCursorVisibility();
    }
    void SetExtrasVisible(bool visible) {
        gd_extras_menu_set_visible(&extras_menu_, visible ? 1 : 0);
    }

    void SetTextInputActive(bool active) {
        text_input_active_ = active;
        if (active) cursor_force_visible_ = true;
        else if (gameplay_active_ && !editor_active_) cursor_force_visible_ = false;
        UpdateCursorVisibility();
        if (active && keyboard_down_) {
            keyboard_down_ = false;
            Queue(HostEvent{HostEventType::PlatformButton,
                            0.0f, 0.0f, 1u, false});
        }
        if (active && platform_left_down_) {
            platform_left_down_ = false;
            Queue(HostEvent{HostEventType::PlatformButton,
                            0.0f, 0.0f, 2u, false});
        }
        if (active && platform_right_down_) {
            platform_right_down_ = false;
            Queue(HostEvent{HostEventType::PlatformButton,
                            0.0f, 0.0f, 3u, false});
        }
    }
    bool PauseButtonHit(float x, float y) const {
        return remove_pause_button_option_ && gameplay_active_ && !editor_active_ &&
               native_width_ > 0 && native_height_ > 0 &&
               x >= static_cast<float>(native_width_) * 0.86f &&
               y <= static_cast<float>(native_height_) * 0.22f;
    }
    void UpdateCursorVisibility() {
        const bool hidden = hide_cursor_option_ && gameplay_active_ &&
            !editor_active_ && active_ && !text_input_active_ &&
            !cursor_force_visible_;
        if (cursor_hidden_ == hidden) return;
        cursor_hidden_ = hidden;
        if (window_) SetCursor(hidden ? nullptr : LoadCursor(nullptr, IDC_ARROW));
    }
    void ForceCursorVisible() {
        cursor_force_visible_ = true;
        cursor_pause_click_seen_ = false;
        UpdateCursorVisibility();
    }
    void ConfirmKeyboardGameplayInput() {
        if (gameplay_active_ && !editor_active_) {
            cursor_force_visible_ = false;
            cursor_pause_click_seen_ = false;
            UpdateCursorVisibility();
        }
    }
    void ConfirmMouseGameplayInput() {
        if (!gameplay_active_ || editor_active_) return;
        if (cursor_force_visible_) {
            /* First click after Escape is normally Resume; keep the cursor for
               that click. A following gameplay click hides it again. */
            if (!cursor_pause_click_seen_) {
                cursor_pause_click_seen_ = true;
                return;
            }
            cursor_force_visible_ = false;
            cursor_pause_click_seen_ = false;
        }
        UpdateCursorVisibility();
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
        const float scale = std::max(
            0.0001f,
            std::min(client_width / static_cast<float>(native_width_),
                     client_height / static_cast<float>(native_height_)));
        const float content_width = static_cast<float>(native_width_) * scale;
        const float content_height = static_cast<float>(native_height_) * scale;
        const float offset_x = (client_width - content_width) * 0.5f;
        const float offset_y = (client_height - content_height) * 0.5f;
        x = std::clamp((static_cast<float>(raw_x) - offset_x) / scale,
                       0.0f, static_cast<float>(native_width_));
        y = std::clamp((static_cast<float>(raw_y) - offset_y) / scale,
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

        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
            !(lparam & (1L << 30)) &&
            (wparam == VK_F11 ||
             (wparam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000)))) {
            self->ToggleFullscreen();
            return 0;
        }

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
                if (!becoming_active && self->platform_left_down_) {
                    self->platform_left_down_ = false;
                    self->Queue(HostEvent{HostEventType::PlatformButton,
                                          0.0f, 0.0f, 2u, false});
                }
                if (!becoming_active && self->platform_right_down_) {
                    self->platform_right_down_ = false;
                    self->Queue(HostEvent{HostEventType::PlatformButton,
                                          0.0f, 0.0f, 3u, false});
                }
                if (!becoming_active && self->keyboard_down_) {
                    self->keyboard_down_ = false;
                    self->Queue(HostEvent{HostEventType::PlatformButton,
                                          0.0f, 0.0f, 1u, false});
                }
                if (!becoming_active) self->ForceCursorVisible();
                else self->UpdateCursorVisibility();
                self->Queue(HostEvent{becoming_active ? HostEventType::Resume : HostEventType::Pause});
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            self->resize_pending_ = true;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT && self->cursor_hidden_) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
            self->ClientPoint(lparam, x, y);
            self->last_x_ = x; self->last_y_ = y;
            SetFocus(window);
            if (self->PauseButtonHit(x, y)) {
                self->pause_touch_blocked_ = true;
                return 0;
            }
            self->pause_touch_blocked_ = false;
            self->ConfirmMouseGameplayInput();
            self->mouse_down_ = true;
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
            if (self->pause_touch_blocked_) {
                self->pause_touch_blocked_ = false;
                return 0;
            }
            if (self->mouse_down_) {
                self->ClientPoint(lparam, x, y);
                self->last_x_ = x; self->last_y_ = y;
                self->mouse_down_ = false;
                ReleaseCapture();
                self->Queue(HostEvent{HostEventType::TouchEnd, x, y, 0});
            }
            return 0;
        case WM_CAPTURECHANGED:
            self->pause_touch_blocked_ = false;
            if (self->mouse_down_) {
                self->mouse_down_ = false;
                self->Queue(HostEvent{HostEventType::TouchEnd, self->last_x_, self->last_y_, 0});
            }
            return 0;
        case WM_COMMAND: {
            const int action = gd_extras_menu_handle_command(
                &self->extras_menu_, static_cast<unsigned long>(wparam));
            if (action != GD_EXTRAS_ACTION_NONE) {
                self->Queue(HostEvent{HostEventType::ExtrasAction, 0.0f, 0.0f,
                                      static_cast<u32>(action)});
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if (!(lparam & (1L << 30)) && !self->text_input_active_ &&
                gd_settings_editor_controls()) {
                const bool small = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                u32 tag = 0u;
                if (wparam == 'A') tag = small ? 1u : 5u;
                else if (wparam == 'D') tag = small ? 2u : 6u;
                else if (wparam == 'W') tag = small ? 3u : 7u;
                else if (wparam == 'S') tag = small ? 4u : 8u;
                else if (wparam == 'E') tag = 0x13u; /* clockwise */
                else if (wparam == 'Q') tag = 0x14u; /* counter-clockwise */
                if (tag) self->Queue(HostEvent{HostEventType::EditorCommand, 0.0f, 0.0f, tag});
                if (tag && wparam != 'A' && wparam != 'D') return 0;
            }
            if (wparam == VK_ESCAPE) {
                self->ForceCursorVisible();
                self->Queue(HostEvent{HostEventType::KeyDown, 0.0f, 0.0f, 4u});
                return 0;
            }
            if (!(lparam & (1L << 30)) && !self->text_input_active_ &&
                (wparam == 'Z' || wparam == 'X')) {
                self->Queue(HostEvent{HostEventType::PracticeCheckpoint,
                                      0.0f, 0.0f,
                                      wparam == 'Z' ? 1u : 0u});
                return 0;
            }
            if (!self->text_input_active_ &&
                (wparam == 'A' || wparam == VK_LEFT) &&
                !self->platform_left_down_) {
                self->ConfirmKeyboardGameplayInput();
                self->platform_left_down_ = true;
                self->Queue(HostEvent{HostEventType::PlatformButton,
                                      0.0f, 0.0f, 2u, true});
                return 0;
            }
            if (!self->text_input_active_ &&
                (wparam == 'D' || wparam == VK_RIGHT) &&
                !self->platform_right_down_) {
                self->ConfirmKeyboardGameplayInput();
                self->platform_right_down_ = true;
                self->Queue(HostEvent{HostEventType::PlatformButton,
                                      0.0f, 0.0f, 3u, true});
                return 0;
            }
            if ((wparam == VK_SPACE || wparam == VK_UP) &&
                !self->text_input_active_ && !self->keyboard_down_) {
                self->ConfirmKeyboardGameplayInput();
                self->keyboard_down_ = true;
                self->Queue(HostEvent{HostEventType::PlatformButton,
                                      0.0f, 0.0f, 1u, true});
                return 0;
            }
            break;
        case WM_KEYUP:
            if ((wparam == 'A' || wparam == VK_LEFT) &&
                self->platform_left_down_) {
                self->platform_left_down_ = false;
                self->Queue(HostEvent{HostEventType::PlatformButton,
                                      0.0f, 0.0f, 2u, false});
                return 0;
            }
            if ((wparam == 'D' || wparam == VK_RIGHT) &&
                self->platform_right_down_) {
                self->platform_right_down_ = false;
                self->Queue(HostEvent{HostEventType::PlatformButton,
                                      0.0f, 0.0f, 3u, false});
                return 0;
            }
            if ((wparam == VK_SPACE || wparam == VK_UP) && self->keyboard_down_) {
                self->keyboard_down_ = false;
                self->Queue(HostEvent{HostEventType::PlatformButton,
                                      0.0f, 0.0f, 1u, false});
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

    void ToggleFullscreen() {
        if (!window_) return;
        if (!fullscreen_) {
            windowed_style_ = GetWindowLongPtrA(window_, GWL_STYLE);
            windowed_ex_style_ = GetWindowLongPtrA(window_, GWL_EXSTYLE);
            windowed_placement_.length = sizeof(windowed_placement_);
            GetWindowPlacement(window_, &windowed_placement_);
            MONITORINFO monitor_info{};
            monitor_info.cbSize = sizeof(monitor_info);
            const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
            if (!GetMonitorInfoA(monitor, &monitor_info)) return;
            SetWindowLongPtrA(window_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowLongPtrA(window_, GWL_EXSTYLE,
                              windowed_ex_style_ & ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE));
            SetWindowPos(window_, HWND_TOP,
                         monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                         monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                         monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            fullscreen_ = true;
        } else {
            SetWindowLongPtrA(window_, GWL_STYLE, windowed_style_);
            SetWindowLongPtrA(window_, GWL_EXSTYLE, windowed_ex_style_);
            SetWindowPlacement(window_, &windowed_placement_);
            SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            fullscreen_ = false;
        }
        resize_pending_ = true;
        if (log_) {
            *log_ << "Window mode: " << (fullscreen_ ? "fullscreen" : "windowed")
                  << " toggle=F11/Alt+Enter\n";
            log_->flush();
        }
    }

    void ReapplyGuestRects() {
        if (!context_) return;
        if (have_guest_viewport_) {
            auto* function = reinterpret_cast<void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei)>(
                Resolve("glViewport"));
            if (function) {
                GLint x = 0, y = 0;
                GLsizei width = 0, height = 0;
                ScaleGuestRect(guest_viewport_[0], guest_viewport_[1],
                               guest_viewport_[2], guest_viewport_[3],
                               x, y, width, height);
                function(x, y, width, height);
            }
        }
        if (have_guest_scissor_) {
            auto* function = reinterpret_cast<void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei)>(
                Resolve("glScissor"));
            if (function) {
                GLint x = 0, y = 0;
                GLsizei width = 0, height = 0;
                ScaleGuestRect(guest_scissor_[0], guest_scissor_[1],
                               guest_scissor_[2], guest_scissor_[3],
                               x, y, width, height);
                function(x, y, width, height);
            }
        }
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
    bool platform_left_down_ = false;
    bool platform_right_down_ = false;
    bool text_input_active_ = false;
    bool remove_pause_button_option_ = false;
    bool pause_touch_blocked_ = false;
    bool hide_cursor_option_ = false;
    bool gameplay_active_ = false;
    bool editor_active_ = false;
    bool cursor_hidden_ = false;
    bool cursor_force_visible_ = false;
    bool cursor_pause_click_seen_ = false;
    bool fullscreen_ = false;
    bool resize_pending_ = true;
    bool have_guest_viewport_ = false;
    bool have_guest_scissor_ = false;
    LONG_PTR windowed_style_ = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    LONG_PTR windowed_ex_style_ = 0;
    WINDOWPLACEMENT windowed_placement_{sizeof(WINDOWPLACEMENT)};
    std::array<GLint, 4> guest_viewport_{0, 0, 1280, 720};
    std::array<GLint, 4> guest_scissor_{0, 0, 1280, 720};
    int native_width_ = 1280;
    int native_height_ = 720;
    float last_x_ = 0.0f;
    float last_y_ = 0.0f;
    GdExtrasMenu extras_menu_{};
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
    std::pair<int, int> ClientSize() const { return {1280, 720}; }
    void ResetFrameClipState(bool) {}
    void Swap() {}
    void BeginGpuFrame(u64) {}
    void EndGpuFrame() {}
    std::vector<std::pair<u64, double>> TakeGpuTimings() { return {}; }
    std::vector<std::pair<u64, double>> FinishGpuTimings() { return {}; }
    bool Ready() const { return false; }
    bool Active() const { return false; }
    void SetTitle(const std::string&) {}
    void SetGameplayActive(bool, bool = false) {}
    void SetExtrasVisible(bool) {}
    void SetTextInputActive(bool) {}
    void RequestClose() {}
};
#endif


struct NativeHttpJob {
    u64 id = 0;
    u32 client = 0;
    u32 request = 0;
    u32 request_type = 5;
    std::string url;
    std::vector<u8> body;
    std::vector<std::string> headers;
};

struct NativeHttpResult {
    u64 id = 0;
    u32 client = 0;
    u32 request = 0;
    u32 request_type = 5;
    std::string url;
    bool transport_success = false;
    u32 response_code = 0;
    std::vector<u8> response_body;
    std::vector<u8> response_headers;
    std::string error;
    double elapsed_ms = 0.0;
};

class GuestExecutor {
public:
    static bool IsFmodImportName(const std::string& name) {
        return name == "FMOD_System_Create" || name.rfind("_ZN4FMOD", 0) == 0;
    }
    GuestExecutor(ProbeEnvironment& env, ElfRuntime& runtime, std::ostream& log)
        : env_(env), runtime_(runtime), log_(log), global_monitor_(1),
          cpu_(MakeConfig(env, global_monitor_)) {
        env_.AttachCpu(&cpu_);
        log_ << "RESULT: DYNARMIC_GLOBAL_MONITOR_READY processors=1 processor_id=0\n";
        log_ << "RESULT: DYNARMIC_EXCLUSIVE_MEMORY_READY widths=8,16,32,64 compare_exchange=1\n";
        InitializeControlTraps();
        heap_cursor_ = kHeapBase + 0x1000u;
        errno_address_ = kObjectBase + kObjectRegionSize - 0x1000u;
        env_.MemoryWrite32(errno_address_, 0u);
        c_locale_address_ = AllocateString("C");
        tm_address_ = Allocate(sizeof(GuestTmLayout));
        time_zone_address_ = AllocateString("local");
        if (const SymbolRecord* creator = FindSymbol(
                runtime_, "_ZN9MenuLayer9onCreatorEPN7cocos2d8CCObjectE"))
            v22_creator_callback_address_ = creator->address;
        if (const SymbolRecord* float_value = FindSymbol(
                runtime_, "_ZNK7cocos2d8CCString10floatValueEv")) {
            v22_ccstring_float_value_ = float_value->address & ~1u;
            v22_ccstring_float_value_size_ =
                std::max<u32>(float_value->size, 0x28u);
        }
        if (const SymbolRecord* object_ctor = FindSymbol(
                runtime_, "_ZN7cocos2d8CCObjectC2Ev"))
            native_http_ccobject_ctor_ = object_ctor->address;
        if (const SymbolRecord* object_release = FindSymbol(
                runtime_, "_ZN7cocos2d8CCObject7releaseEv"))
            native_http_ccobject_release_ = object_release->address;
        if (const SymbolRecord* response_vtable = FindSymbol(
                runtime_, "_ZTVN7cocos2d9extension14CCHttpResponseE"))
            native_http_response_vtable_ = response_vtable->address + 8u;
    }

    ~GuestExecutor() {
#ifdef _WIN32
        JoinNativeHttpWorkers();
#endif
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
        if (v22_decompress_successes_ || v22_decompress_failures_) {
            log_ << "RESULT: DYNARMIC_V22_INFLATE_TOTALS success="
                 << v22_decompress_successes_ << " failures="
                 << v22_decompress_failures_ << '\n';
            log_.flush();
        }
        if (v22_level_settings_native_successes_ ||
            v22_level_settings_native_failures_ ||
            v22_level_settings_fallback_successes_) {
            log_ << "RESULT: DYNARMIC_V22_LEVEL_SETTINGS_TOTALS native_ok="
                 << v22_level_settings_native_successes_
                 << " native_null=" << v22_level_settings_native_failures_
                 << " fallback_ok=" << v22_level_settings_fallback_successes_
                 << '\n';
            log_.flush();
        }
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
        if (async_dns_ || async_dns_queued_count_ || async_dns_timeout_count_) {
            log_ << "RESULT: DYNARMIC_UNIFIED_ARMV7_DNS_TOTALS queued="
                 << async_dns_queued_count_ << " completed="
                 << async_dns_completed_count_ << " timed_out="
                 << async_dns_timeout_count_ << " native_threads="
                 << g_async_dns_threads_active.load(std::memory_order_relaxed)
                 << '\n';
            log_.flush();
        }
        if (winsock_initialized_) {
            // A timed-out detached resolver may still be inside WinSock. The
            // process owns this one WinSock lifetime, so skip WSACleanup while
            // a resolver thread remains active and let process teardown clean it.
            if (g_async_dns_threads_active.load(std::memory_order_relaxed) == 0u)
                WSACleanup();
            winsock_initialized_ = false;
        }
#endif
        if (audio_initialized_) {
            audio_shutdown();
            audio_initialized_ = false;
        }
        storage_shutdown();
    }

    void ConfigureHost(const std::string& input_path, const std::string& writable_path,
                       const std::vector<u8>& apk_image, bool input_is_apk) {
        apk_path_ = input_path;
        writable_path_ = writable_path;
        apk_image_ = input_is_apk ? &apk_image : nullptr;
        if (input_is_apk) {
            apk_member_cache_.Initialize(apk_image, writable_path, log_);
            StageAndroidExtensionResources();
            LoadV22LevelDataCatalog();
        } else {
            log_ << "RESULT: DYNARMIC_V22_RAW_LIBRARY_MODE apk-cache=disabled assets=unavailable\n";
        }
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
        if (input_is_apk) {
            for (const char* member : kTextInputWarmAssets) {
                if (apk_member_cache_.Load(member)) ++warmed_text_assets;
            }
        }
        log_ << "RESULT: DYNARMIC_TEXT_INPUT_ASSET_PREWARM_READY count="
             << warmed_text_assets << '\n';
        if (input_is_apk) {
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
        } else {
            log_ << "RESULT: DYNARMIC_BACKGROUND_MUSIC_PRECACHE_SKIPPED raw-so=1\n";
        }
        std::error_code save_directory_error;
        std::filesystem::create_directories(
            std::filesystem::path(writable_path_), save_directory_error);
        log_ << "RESULT: DYNARMIC_V22_LOCAL_SAVE_REDIRECT_READY root="
             << writable_path_
             << " guest-android-paths=intercepted-local-only"
                " legacy-drive-scan=disabled migration=disabled\n";
        log_.flush();
#ifdef _WIN32
        CreateDirectoryA(writable_path_.c_str(), nullptr);
        WSADATA winsock_data{};
        if (WSAStartup(MAKEWORD(2, 2), &winsock_data) == 0) {
            winsock_initialized_ = true;
            log_ << "RESULT: DYNARMIC_WINSOCK_BRIDGE_READY version="
                 << static_cast<unsigned>(LOBYTE(winsock_data.wVersion)) << '.'
                 << static_cast<unsigned>(HIBYTE(winsock_data.wVersion))
                 << " api=socket,connect,send,recv,poll,getaddrinfo,"
                    "gethostbyname,getnameinfo,writev,shutdown,pipe,socketpair\n";
        } else {
            log_ << "WARNING: Winsock initialization failed; legacy guest socket bridge unavailable\n";
        }
        log_ << "RESULT: DYNARMIC_V22_NATIVE_WINHTTP_WORKER_READY "
                "mode=CCHttpClient-send-hook per-request-host-threads=1 "
                "proxy=none timeouts-ms=5000,5000,10000,15000 "
                "form-content-type=automatic guest-pthread=bypassed "
                "guest-curl=bypassed guest-openssl=bypassed\n";
#endif
        storage_initialize(writable_path_.c_str());
        const std::filesystem::path executable_directory =
            std::filesystem::path(apk_path_).parent_path();
        const std::string executable_directory_string =
            executable_directory.empty() ? std::string(".") : executable_directory.string();
        audio_initialize(executable_directory_string.c_str());
        audio_set_writable_directory(writable_path_.c_str());
        if (input_is_apk) audio_set_apk_path(apk_path_.c_str());
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
    void ResetFrameClipState() {
        gl_.ResetFrameClipState(IsV22EditorSceneActive());
    }
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
    std::string DescribeCooperativeNetwork() const {
        std::ostringstream out;
        out << "registered=" << cooperative_worker_registered_count_
            << " resumes=" << cooperative_worker_resume_count_
            << " yields=" << cooperative_worker_yield_count_
            << " slice-yields=" << cooperative_worker_slice_yield_count_
            << " watchdog-preemptions=" << cooperative_worker_watchdog_count_
             << " immediate-wakes=" << cooperative_worker_immediate_wake_count_
            << " safe-stub-returns=" << cooperative_worker_stub_return_count_
            << " exits=" << cooperative_worker_done_count_
            << " pending-cond-objects=" << condition_signals_.size();
#ifdef _WIN32
        out << " sockets=" << sockets_.size()
            << " network-log-events=" << network_log_count_
            << " dns-queued=" << async_dns_queued_count_
            << " dns-completed=" << async_dns_completed_count_
            << " dns-timeouts=" << async_dns_timeout_count_;
#endif
        out << " request-markers=" << network_request_marker_count_
            << " native-http-queued=" << native_http_queued_count_
            << " native-http-completed=" << native_http_completed_count_
            << " native-http-callbacks=" << native_http_callback_count_
            << " native-http-active="
            << native_http_active_count_.load(std::memory_order_relaxed);
        return out.str();
    }
    const std::string& LastError() const { return last_error_; }
    std::vector<HostEvent> TakeHostEvents() { return gl_.TakeEvents(); }
    bool WindowActive() const { return gl_.Active(); }
    void SetWindowTitle(const std::string& title) { gl_.SetTitle(title); }

    /*
     * The beta does not call its editor timeline helpers continuously through
     * the Android path used by the wrapper.  The old visibility-hook attempt
     * only refreshed them when a visibility pass happened, so the song guide
     * could appear and then freeze.  Drive only the tiny DrawGridLayer state
     * updates once per rendered editor frame.  This is deliberately separate
     * from the expensive exact companion visibility function that froze the
     * editor in EnduranceTest1.
     */
    bool UpdateV22EditorOverlayFrame() {
        if (!IsV22EditorSceneActive()) {
            v22_editor_overlay_frames_ = 0u;
            v22_editor_overlay_playtest_active_ = false;
            v22_editor_level_settings_refreshed_ = false;
            return true;
        }
        const u32 editor = v22_editor_visual_layer_;
        const u32 draw_grid = FindV22DrawGridLayer(editor);
        ++v22_editor_overlay_frames_;

        if (draw_grid && runtime_.v22_draw_grid_update_music_guide_time) {
            const float song_time = audio_get_background_time();
            if (song_time >= 0.0f && !RunFunction(
                    runtime_.v22_draw_grid_update_music_guide_time,
                    {draw_grid, FloatToWord(song_time)}, nullptr,
                    "DrawGridLayer::updateMusicGuideTime per-frame", 0u,
                    std::chrono::milliseconds(1000)))
                return false;
            if (song_time >= 0.0f &&
                (v22_editor_overlay_frames_ == 1u ||
                 (v22_editor_overlay_frames_ % 300u) == 0u)) {
                log_ << "RESULT: DYNARMIC_V22_EDITOR_SONG_GUIDE_FRAME time="
                     << std::fixed << std::setprecision(3) << song_time
                     << " frame=" << v22_editor_overlay_frames_ << '\n';
                log_.flush();
            }
        }

        /*
         * updateTimeMarkers() merely reparses the string already stored on the
         * DrawGridLayer. In this Android beta that string is still empty when
         * the wrapper first sees the grid, so calling it directly can never
         * create BPM guides. LevelEditorLayer::levelSettingsUpdated() is the
         * game's real setup path: it obtains the current song marker string
         * from LevelSettingsObject/LevelTools and then calls loadTimeMarkers.
         * Run it once, after the first complete editor frame, and never rebuild
         * markers periodically while the user edits.
         */
        if (draw_grid && runtime_.v22_level_editor_level_settings_updated &&
            !v22_editor_level_settings_refreshed_ &&
            v22_editor_overlay_frames_ >= 2u) {
            if (!RunFunction(runtime_.v22_level_editor_level_settings_updated,
                             {editor}, nullptr,
                             "LevelEditorLayer::levelSettingsUpdated session-once",
                             0u, std::chrono::milliseconds(1000)))
                return false;
            v22_editor_level_settings_refreshed_ = true;
            log_ << "RESULT: DYNARMIC_V22_EDITOR_TIME_MARKERS_REFRESH "
                 << "mode=level-settings-updated frame="
                 << v22_editor_overlay_frames_ << '\n';
            log_.flush();
        }

        /* Never call LevelEditorLayer::updateGridLayer from the host. */
        return true;
    }


    bool TerminationRequested() const { return termination_requested_; }
    void ReportHeapStatus(const char* reason) { LogHeapStatus(reason); }
    void FlushDiagnostics() { log_.flush(); }
    const GuestCallMetrics& LastCallMetrics() const { return last_call_metrics_; }
    const std::string& LastAndroidLog() const { return last_android_log_; }
    u64 CompanionHooksInstalled() const { return v22_companion_hooks_installed_; }
    u64 CompanionHooksSkipped() const { return v22_companion_hooks_skipped_; }
    void ClearGuestCodeCache(const char* reason) {
        cpu_.ClearCache();
        log_ << "RESULT: DYNARMIC_GUEST_CODE_CACHE_CLEARED reason="
             << (reason ? reason : "unspecified") << '\n';
        log_.flush();
    }

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

    bool ResolveV22ActiveGameLayer(u32& layer) {
        layer = 0u;
        if (!runtime_.v22_game_manager_shared) return true;
        u32 game_manager = 0u;
        if (!RunFunction(runtime_.v22_game_manager_shared, {}, &game_manager,
                         "V22 GameManager::sharedState for platformer key"))
            return false;
        if (!game_manager || !env_.IsMapped(game_manager + 0x168u, 4u))
            return true;
        const u32 candidate = env_.MemoryRead32(game_manager + 0x168u);
        if (LooksLikeGuestObject(runtime_, env_, candidate))
            layer = candidate;
        return true;
    }

    bool IsV22EditorPlaytestActive(u32& editor) const {
        editor = v22_editor_visual_layer_;
        if (!runtime_.v22_editor_playtest_state_offset ||
            !LooksLikeGuestObject(runtime_, env_, editor) ||
            !env_.IsMapped(
                editor + runtime_.v22_editor_playtest_state_offset, 4u) ||
            env_.MemoryRead32(
                editor + runtime_.v22_editor_playtest_state_offset) != 1u)
            return false;
        return true;
    }

    bool IsV22EditorSceneActive() const {
        const u32 editor = v22_editor_visual_layer_;
        return LooksLikeGuestObject(runtime_, env_, editor) &&
               env_.IsMapped(editor + 234u, 1u) &&
               env_.MemoryRead8(editor + 234u) != 0u;
    }

    static u32 PlatformerButtonKeyCode(u32 button) {
        // cocos2d::enumKeyCodes intentionally mirrors these Windows/ASCII
        // values in this beta: Space=32, A=65, D=68.
        return button == 1u ? 32u : button == 2u ? 65u : 68u;
    }


    u32 FindActiveEditorUi() {
        u32 playtest_editor = 0u;
        if (IsV22EditorPlaytestActive(playtest_editor)) return 0u;
        const u32 editor_layer = v22_editor_visual_layer_;
        if (!editor_layer || !LooksLikeGuestObject(runtime_, env_, editor_layer))
            return 0u;
        for (u32 offset = 0x40u; offset + 4u <= 0x3000u; offset += 4u) {
            if (!env_.IsMapped(editor_layer + offset, 4u)) continue;
            const u32 candidate = env_.MemoryRead32(editor_layer + offset);
            if (GuestObjectTypeContains(candidate, "EditorUI")) return candidate;
        }
        return 0u;
    }

    bool SendEditorCommand(u32 tag) {
        if (!gd_settings_editor_controls()) return true;
        const u32 editor_ui = FindActiveEditorUi();
        if (!editor_ui) return true;

        const bool movement = tag >= 1u && tag <= 8u;
        const u32 direct = movement ? runtime_.editor_move_edit_command
                                    : runtime_.editor_transform_edit_command;
        const u32 sender = movement ? runtime_.editor_move_object_call
                                    : runtime_.editor_transform_object_call;
        const char* direct_name = movement
            ? "EditorUI::moveObjectCall(EditCommand) shortcut"
            : "EditorUI::transformObjectCall(EditCommand) shortcut";
        const char* sender_name = movement
            ? "EditorUI::moveObjectCall sender shortcut"
            : "EditorUI::transformObjectCall sender shortcut";

        /* 2.2 exports the EditCommand overloads directly. Movement and
           transform commands are deliberately kept separate: feeding editor
           keys through EditorUI::keyDown is unsafe on Android builds. */
        if (direct) {
            const bool ok = RunFunction(
                direct, {editor_ui, tag}, nullptr, direct_name, 0u,
                std::chrono::milliseconds(2000));
            if (ok) {
                log_ << "RESULT: DYNARMIC_EDITOR_COMMAND tag=" << tag
                     << " family=" << (movement ? "move" : "transform")
                     << " path=direct-edit-command\n";
                log_.flush();
            }
            return ok;
        }

        if (!runtime_.ccnode_get_tag || !runtime_.ccnode_set_tag || !sender) {
            log_ << "RESULT: DYNARMIC_EDITOR_CONTROLS_UNAVAILABLE reason=missing-"
                 << (movement ? "move" : "transform") << "-symbol\n";
            log_.flush();
            return true;
        }
        u32 old_tag = 0u;
        if (!RunFunction(runtime_.ccnode_get_tag, {editor_ui}, &old_tag,
                         "CCNode::getTag editor shortcut", 0u,
                         std::chrono::milliseconds(1000))) return false;
        if (!RunFunction(runtime_.ccnode_set_tag, {editor_ui, tag}, nullptr,
                         "CCNode::setTag editor shortcut", 0u,
                         std::chrono::milliseconds(1000))) return false;
        const bool invoked = RunFunction(
            sender, {editor_ui, editor_ui}, nullptr, sender_name, 0u,
            std::chrono::milliseconds(2000));
        const bool restored = RunFunction(
            runtime_.ccnode_set_tag, {editor_ui, old_tag}, nullptr,
            "CCNode::setTag restore editor shortcut", 0u,
            std::chrono::milliseconds(1000));
        return invoked && restored;
    }

    void RefreshExtrasMenuVisibility() {
        u32 layer = 0u;
        (void)ResolveV22ActiveGameLayer(layer);
        u32 editor_layer = 0u;
        const bool editor_playtest = IsV22EditorPlaytestActive(editor_layer);
        const bool editor = (v22_editor_visual_layer_ &&
            LooksLikeGuestObject(runtime_, env_, v22_editor_visual_layer_)) ||
            editor_playtest;
        const bool gameplay = layer != 0u && !editor;
        if (gameplay) HideV22PauseButtonVisual(layer);
        else v22_pause_hidden_item_ = 0u;
        gl_.SetGameplayActive(gameplay, editor);
        gl_.SetExtrasVisible(!layer && !editor);
    }

    void HideV22PauseButtonVisual(u32 layer) {
        if (!gd_settings_remove_pause_button() || !layer ||
            !runtime_.v22_ui_layer_offset ||
            !env_.IsMapped(layer + runtime_.v22_ui_layer_offset, 4u)) return;
        const u32 ui_layer = env_.MemoryRead32(layer + runtime_.v22_ui_layer_offset);
        if (!LooksLikeGuestObject(runtime_, env_, ui_layer) ||
            !env_.IsMapped(ui_layer + 0x1C0u, 4u)) return;
        const u32 pause_item = env_.MemoryRead32(ui_layer + 0x1C0u);
        if (!LooksLikeGuestObject(runtime_, env_, pause_item)) return;
        // Do not treat hiding as a one-shot operation. UILayer can make the
        // same CCMenuItem visible again during level setup/restart. tweaks5
        // remembered only the pointer, so a re-shown pause button could linger
        // until the active layer changed. Reassert only when the Cocos visible
        // byte says the same item has become visible again.
        if (v22_pause_hidden_item_ == pause_item &&
            env_.IsMapped(pause_item + 234u, 1u) &&
            env_.MemoryRead8(pause_item + 234u) == 0u)
            return;
        const SymbolRecord* set_visible = FindSymbol(
            runtime_, "_ZN7cocos2d6CCNode10setVisibleEb");
        if (!set_visible) return;
        if (RunFunction(set_visible->address, {pause_item, 0u}, nullptr,
                        "V22 hide top-right pause control", 0u,
                        std::chrono::milliseconds(1000))) {
            v22_pause_hidden_item_ = pause_item;
            log_ << "RESULT: DYNARMIC_V22_PAUSE_BUTTON_HIDDEN ui=0x"
                 << std::hex << ui_layer << " item=0x" << pause_item
                 << std::dec << " escape-pause=preserved\n";
            log_.flush();
        }
    }

    bool HandleExtrasAction(u32 action) {
        log_ << "RESULT: DYNARMIC_EXTRAS_ACTION_UNAVAILABLE backend=armv7 action="
             << action << "\n";
        log_.flush();
        return true;
    }

    bool SendPracticeCheckpoint(bool place) {
        u32 layer = 0u;
        if (!ResolveV22ActiveGameLayer(layer)) return false;
        u32 editor = 0u;
        if (!layer || IsV22EditorPlaytestActive(editor) ||
            !runtime_.v22_ui_layer_offset ||
            !runtime_.v22_practice_mode_offset ||
            !env_.IsMapped(layer + runtime_.v22_practice_mode_offset, 1u) ||
            !env_.IsMapped(layer + runtime_.v22_ui_layer_offset, 4u))
            return true;
        if (env_.MemoryRead8(layer + runtime_.v22_practice_mode_offset) == 0u) {
            log_ << "RESULT: DYNARMIC_PRACTICE_HOTKEY_IGNORED key="
                 << (place ? 'Z' : 'X') << " mode=normal\n";
            log_.flush();
            return true;
        }
        const u32 ui_layer = env_.MemoryRead32(
            layer + runtime_.v22_ui_layer_offset);
        const u32 function = place ? runtime_.v22_ui_on_check
                                   : runtime_.v22_ui_on_delete_check;
        if (!function || !LooksLikeGuestObject(runtime_, env_, ui_layer))
            return true;
        LogHostDispatch(place ? "practiceCheckpointPlace"
                              : "practiceCheckpointRemove",
                        function, std::string("key=") +
                        (place ? "Z" : "X"));
        return RunFunction(function, {ui_layer, 0u}, nullptr,
                           place ? "UILayer::onCheck hotkey"
                                 : "UILayer::onDeleteCheck hotkey",
                           0u, std::chrono::milliseconds(1000));
    }

    bool SendPlatformerButton(u32 button, bool pressed) {
        // Gameplay/playtest input must win over desktop editor hotkeys.
        u32 editor = 0u;
        const bool editor_playtest = IsV22EditorPlaytestActive(editor);

        u32 layer = 0u;
        if (!ResolveV22ActiveGameLayer(layer)) return false;
        if (!editor_playtest && !layer &&
            gd_settings_editor_controls() && FindActiveEditorUi())
            return true;
        if (!layer && !editor_playtest && !v22_editor_visual_layer_) return true;

        const char* name =
            button == 1u ? "jump" : button == 2u ? "left" : "right";

        // GameManager's active-layer field is stale while the editor is
        // playtesting. LevelEditorLayer is itself a GJBaseGameLayer, so queue
        // the same player command directly without touching EditorUI.
        if (editor_playtest) {
            if (!runtime_.v22_gjbase_queue_button) return true;
            LogHostDispatch(
                "editorPlatformerQueueButton",
                runtime_.v22_gjbase_queue_button,
                std::string("button=") + name +
                    " pressed=" + (pressed ? "1" : "0"));
            return RunFunction(
                runtime_.v22_gjbase_queue_button,
                {editor, button, pressed ? 1u : 0u, 0u}, nullptr,
                "LevelEditorLayer::queueButton", 0u,
                std::chrono::milliseconds(10000));
        }

        // Use the game's UILayer keyboard path when available. Besides queuing
        // the same player command, it updates the on-screen platformer-control
        // state, so A/D and Left/Right visibly depress the native buttons.
        if (layer && runtime_.v22_ui_layer_offset &&
            env_.IsMapped(layer + runtime_.v22_ui_layer_offset, 4u)) {
            const u32 ui_layer = env_.MemoryRead32(
                layer + runtime_.v22_ui_layer_offset);
            const u32 function = pressed
                ? runtime_.v22_ui_key_down : runtime_.v22_ui_key_up;
            if (function &&
                LooksLikeGuestObject(runtime_, env_, ui_layer)) {
                const u32 key_code = PlatformerButtonKeyCode(button);
                LogHostDispatch(
                    pressed ? "platformerKeyDown" : "platformerKeyUp",
                    function,
                    std::string("button=") + name +
                        " key=" + std::to_string(key_code));
                return RunFunction(
                    function, {ui_layer, key_code}, nullptr,
                    pressed ? "UILayer::keyDown" : "UILayer::keyUp",
                    0u, std::chrono::milliseconds(10000));
            }
        }

        if (!layer || !runtime_.v22_gjbase_queue_button) return true;
        LogHostDispatch("platformerQueueButton", runtime_.v22_gjbase_queue_button,
                        std::string("button=") + name +
                        " pressed=" + (pressed ? "1" : "0"));
        return RunFunction(runtime_.v22_gjbase_queue_button,
                           {layer, button, pressed ? 1u : 0u, 0u}, nullptr,
                           "GJBaseGameLayer::queueButton", 0u,
                           std::chrono::milliseconds(10000));
    }

    bool PrepareV22MousePlatformerTouch() {
        v22_mouse_platformer_touch_ui_ = 0u;
        v22_mouse_platformer_playtest_fallback_ = false;
        if (!runtime_.v22_ui_layer_offset) return true;

        u32 layer = 0u;
        u32 editor = 0u;
        const bool editor_playtest = IsV22EditorPlaytestActive(editor);
        if (editor_playtest) {
            // GameManager's PlayLayer pointer is stale during editor playtest.
            // LevelEditorLayer shares the GJBaseGameLayer UI slot.
            layer = editor;
        } else if (!ResolveV22ActiveGameLayer(layer)) {
            return false;
        }

        if (!layer ||
            !env_.IsMapped(layer + runtime_.v22_ui_layer_offset, 4u)) {
            v22_mouse_platformer_playtest_fallback_ = editor_playtest;
            return true;
        }
        const u32 ui_layer = env_.MemoryRead32(
            layer + runtime_.v22_ui_layer_offset);
        if (!LooksLikeGuestObject(runtime_, env_, ui_layer) ||
            !env_.IsMapped(ui_layer + 518u, 1u) ||
            !env_.IsMapped(ui_layer + 476u, 4u) ||
            env_.MemoryRead8(ui_layer + 518u) == 0u) {
            v22_mouse_platformer_playtest_fallback_ = editor_playtest;
            return true;
        }

        // UILayer stores the touch identifier claimed by its native
        // platformer left/right control at +476. Windows sends one pointer
        // with identifier zero. Reset a stale completed touch before native
        // dispatch; ccTouchBegan writes zero only when that control owns this
        // new press.
        env_.MemoryWrite32(ui_layer + 476u, 0xFFFFFFFFu);
        v22_mouse_platformer_touch_ui_ = ui_layer;
        return true;
    }

    bool SyncV22MousePlatformerJump(bool pressed) {
        if (!pressed) {
            if (!v22_mouse_platformer_jump_down_) {
                v22_mouse_platformer_touch_ui_ = 0u;
                v22_mouse_platformer_playtest_fallback_ = false;
                return true;
            }
            v22_mouse_platformer_jump_down_ = false;
            v22_mouse_platformer_touch_ui_ = 0u;
            v22_mouse_platformer_playtest_fallback_ = false;
            return SendPlatformerButton(1u, false);
        }
        if (v22_mouse_platformer_jump_down_)
            return true;

        if (v22_mouse_platformer_playtest_fallback_) {
            v22_mouse_platformer_jump_down_ = true;
            log_ << "[host] V22 editor-playtest mouse jump fallback pressed\n";
            log_.flush();
            return SendPlatformerButton(1u, true);
        }

        const u32 ui_layer = v22_mouse_platformer_touch_ui_;
        if (!LooksLikeGuestObject(runtime_, env_, ui_layer) ||
            !env_.IsMapped(ui_layer + 518u, 1u) ||
            !env_.IsMapped(ui_layer + 476u, 4u) ||
            env_.MemoryRead8(ui_layer + 518u) == 0u)
            return true;

        if (env_.MemoryRead32(ui_layer + 476u) == 0u) {
            log_ << "[host] V22 mouse jump suppressed: native platformer "
                    "control owns touch ui=0x"
                 << std::hex << ui_layer << std::dec << '\n';
            log_.flush();
            v22_mouse_platformer_touch_ui_ = 0u;
            return true;
        }

        // +517 is the native control's current direction, not a canvas-touch
        // marker. Bringup15's use of it made mouse jump stop after the user
        // touched the left/right overlay. The ownership field above is stable
        // across direction changes and scene reloads.
        v22_mouse_platformer_jump_down_ = true;
        log_ << "[host] V22 platformer canvas mouse jump pressed ui=0x"
             << std::hex << ui_layer << std::dec << '\n';
        log_.flush();
        return SendPlatformerButton(1u, true);
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
        const bool forensic_root_call = call_depth_ == 1u;
        if (forensic_root_call) {
            forensic_call_started_ = started;
            forensic_next_heartbeat_ = started + std::chrono::milliseconds(250);
            forensic_call_label_ = label;
        }
        ScopeExit forensic_scope([this, forensic_root_call] {
            if (!forensic_root_call) return;
            forensic_call_started_ = {};
            forensic_next_heartbeat_ = {};
            forensic_call_label_.clear();
        });

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
                if (RecoverV22NullCcStringFloatValue(label)) {
                    estimated_ticks += 1024u;
                    if (!unlimited_ticks)
                        budget = budget > 1024u ? budget - 1024u : 0u;
                    continue;
                }
                std::ostringstream error;
                error << label << " invalid guest memory at 0x" << std::hex << env_.fault_address
                      << " PC=0x" << cpu_.Regs()[15] << " (" << DescribeAddress(cpu_.Regs()[15]) << ')'
                      << " LR=0x" << cpu_.Regs()[14] << " (" << DescribeAddress(cpu_.Regs()[14]) << ')'
                      << " R0=0x" << cpu_.Regs()[0] << " R1=0x" << cpu_.Regs()[1]
                      << " R2=0x" << cpu_.Regs()[2] << " R3=0x" << cpu_.Regs()[3]
                      << " SP=0x" << cpu_.Regs()[13];
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
            DumpImportTrace("slow-call:" + label, 128u);
            log_.flush();
        }
        return true;
    }

    bool PumpNetworkWorkerFrame() {
#ifdef _WIN32
        return PumpNativeHttpCallbacks();
#else
        return true;
#endif
    }

private:
    static Dynarmic::A32::UserConfig MakeConfig(
        ProbeEnvironment& env, Dynarmic::ExclusiveMonitor& global_monitor) {
        Dynarmic::A32::UserConfig config;
        config.callbacks = &env;
        config.arch_version = DynarmicArmv7ArchVersion<Dynarmic::A32::ArchVersion>();
        config.global_monitor = &global_monitor;
        config.processor_id = 0;
        config.check_halt_on_memory_access = true;
        return config;
    }

    static std::wstring Utf8ToWide(const std::string& value) {
#ifdef _WIN32
        if (value.empty()) return {};
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        value.data(), static_cast<int>(value.size()),
                                        nullptr, 0);
        if (count <= 0) {
            count = MultiByteToWideChar(CP_ACP, 0, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0);
            if (count <= 0) return {};
            std::wstring output(static_cast<std::size_t>(count), L'\0');
            MultiByteToWideChar(CP_ACP, 0, value.data(),
                                static_cast<int>(value.size()), output.data(), count);
            return output;
        }
        std::wstring output(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), output.data(), count);
        return output;
#else
        return std::wstring(value.begin(), value.end());
#endif
    }

    static std::string WideToUtf8(const wchar_t* value, std::size_t length) {
#ifdef _WIN32
        if (!value || !length) return {};
        const int count = WideCharToMultiByte(
            CP_UTF8, 0, value, static_cast<int>(length), nullptr, 0,
            nullptr, nullptr);
        if (count <= 0) return {};
        std::string output(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length),
                            output.data(), count, nullptr, nullptr);
        return output;
#else
        return value ? std::string(value, value + length) : std::string{};
#endif
    }

#ifdef _WIN32
    static std::string NativeHttpWin32Error(DWORD code) {
        if (!code) return {};
        wchar_t* buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::string text = length && buffer
            ? WideToUtf8(buffer, length)
            : std::string("Win32 error ") + std::to_string(code);
        if (buffer) LocalFree(buffer);
        while (!text.empty() &&
               (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
            text.pop_back();
        return text;
    }

    static const wchar_t* NativeHttpMethod(u32 type) {
        switch (type) {
        case 0u: return L"GET";
        case 1u: return L"POST";
        case 2u: return L"PUT";
        case 3u: return L"DELETE";
        default: return L"GET";
        }
    }

    void QueueNativeHttpTrace(u64 id, std::string message) {
        std::ostringstream stream;
        stream << "[host] Unified ARMv7 native HTTP id=" << id << ' ' << message;
        std::lock_guard<std::mutex> lock(native_http_mutex_);
        native_http_trace_.push_back(stream.str());
    }

    static bool NativeHttpIsPlainHttpUrl(const std::string& url) {
        return url.rfind("http://", 0) == 0;
    }

    static std::string NativeHttpHttpsVariant(const std::string& url) {
        if (!NativeHttpIsPlainHttpUrl(url)) return url;
        return "https://" + url.substr(7);
    }

    static bool NativeHttpLooksLikeApiUrl(const std::string& url) {
        std::string lower = url;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        const std::size_t query = lower.find('?');
        if (query != std::string::npos) lower.resize(query);
        return lower.size() >= 4u &&
               lower.compare(lower.size() - 4u, 4u, ".php") == 0;
    }

    static bool NativeHttpLooksLikeSongInfoUrl(const std::string& url) {
        std::string lower = url;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        const std::size_t query = lower.find('?');
        if (query != std::string::npos) lower.resize(query);
        return lower.size() >= std::strlen("getgjsonginfo.php") &&
               lower.compare(lower.size() - std::strlen("getgjsonginfo.php"),
                             std::strlen("getgjsonginfo.php"),
                             "getgjsonginfo.php") == 0;
    }

    /* 0 = valid song record, 1 = ordinary -1 miss, 2 = malformed/error body. */
    static int NativeHttpSongResponseState(const std::vector<u8>& body) {
        std::size_t begin = 0;
        std::size_t end = body.size();
        while (begin < end &&
               std::isspace(static_cast<unsigned char>(body[begin])))
            ++begin;
        while (end > begin &&
               std::isspace(static_cast<unsigned char>(body[end - 1u])))
            --end;
        if (begin == end) return 2;
        if (end - begin == 2u && body[begin] == '-' &&
            body[begin + 1u] == '1')
            return 1;
        if (body[begin] == '<') return 2;
        const std::size_t sample_size =
            std::min<std::size_t>(end - begin, 8192u);
        std::string sample(
            reinterpret_cast<const char*>(body.data() + begin), sample_size);
        std::transform(sample.begin(), sample.end(), sample.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        if (sample.find("file_get_contents") != std::string::npos ||
            sample.find("warning") != std::string::npos ||
            sample.find("403 forbidden") != std::string::npos ||
            sample.find("<!doctype html") != std::string::npos ||
            sample.find("<html") != std::string::npos)
            return 2;
        return 0;
    }

    static void NativeHttpAppendUniqueAttempt(
            std::vector<std::string>& attempts, const std::string& url) {
        if (url.empty()) return;
        if (std::find(attempts.begin(), attempts.end(), url) == attempts.end())
            attempts.push_back(url);
    }

    static bool NativeHttpLooksLikeHtmlResponse(
            const std::vector<u8>& body) {
        if (body.empty()) return false;
        const std::size_t sample_size =
            std::min<std::size_t>(body.size(), 8192u);
        std::string sample(
            reinterpret_cast<const char*>(body.data()), sample_size);
        std::transform(sample.begin(), sample.end(), sample.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        return sample.find("<!doctype html") != std::string::npos ||
               sample.find("<html") != std::string::npos ||
               sample.find("attention required! | cloudflare") !=
                   std::string::npos ||
               sample.find("sorry, you have been blocked") !=
                   std::string::npos;
    }

    static bool NativeHttpRequestCanRetryAfterResponse(
            const NativeHttpJob& job) {
        if (job.request_type == 0u) return true;
        if (job.request_type != 1u) return false;

        std::string path = job.url;
        const std::size_t query = path.find('?');
        if (query != std::string::npos) path.resize(query);
        const std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) path.erase(0, slash + 1u);
        std::transform(path.begin(), path.end(), path.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        return path.rfind("get", 0) == 0 ||
               path.rfind("download", 0) == 0;
    }

    static bool NativeHttpFailureWasBeforeResponse(
            const NativeHttpResult& result) {
        if (result.response_code != 0u || !result.response_body.empty())
            return false;
        return result.error.find("WinHttpReceiveResponse failed") ==
                   std::string::npos &&
               result.error.find("WinHttpQueryDataAvailable failed") ==
                   std::string::npos &&
               result.error.find("WinHttpReadData failed") ==
                   std::string::npos;
    }

    NativeHttpResult ExecuteNativeHttpAttempt(
            const NativeHttpJob& job,
            const std::string& attempt_url,
            std::size_t attempt_index,
            std::size_t attempt_count) {
        NativeHttpResult output;
        output.id = job.id;
        output.client = job.client;
        output.request = job.request;
        output.request_type = job.request_type;
        output.url = job.url;
        const auto started = std::chrono::steady_clock::now();
        auto trace = [&](const std::string& stage) {
            QueueNativeHttpTrace(job.id, stage);
        };
        auto finish_elapsed = [&] {
            output.elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
        };
        auto fail = [&](const std::string& step,
                        DWORD error = GetLastError()) {
            output.transport_success = false;
            output.error = step;
            const std::string detail = NativeHttpWin32Error(error);
            if (!detail.empty()) output.error += ": " + detail;
            std::ostringstream line;
            line << "attempt=" << (attempt_index + 1u) << '/'
                 << attempt_count << " failed stage=\"" << step
                 << "\" win32=" << error;
            if (!detail.empty())
                line << " detail=\"" << SanitizeLogText(detail) << "\"";
            trace(line.str());
        };
        auto finish_trace = [&] {
            finish_elapsed();
            std::ostringstream line;
            line << "attempt-finish index=" << (attempt_index + 1u)
                 << '/' << attempt_count
                 << " success=" << (output.transport_success ? 1 : 0)
                 << " status=" << output.response_code
                 << " body=" << output.response_body.size()
                 << " elapsed_ms=" << std::fixed << std::setprecision(1)
                 << output.elapsed_ms;
            if (!output.error.empty())
                line << " error=\"" << SanitizeLogText(output.error)
                     << "\"";
            trace(line.str());
        };

        trace("attempt-start index=" + std::to_string(attempt_index + 1u) +
              "/" + std::to_string(attempt_count) + " url=\"" +
              SanitizeLogText(attempt_url) + "\"");

        const std::wstring url = Utf8ToWide(attempt_url);
        if (url.empty()) {
            fail("URL conversion failed", ERROR_INVALID_PARAMETER);
            finish_trace();
            return output;
        }

        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        trace("attempt=" + std::to_string(attempt_index + 1u) +
              " stage=crack-url");
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
            !parts.lpszHostName || !parts.dwHostNameLength) {
            fail("WinHttpCrackUrl failed");
            finish_trace();
            return output;
        }
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path;
        if (parts.lpszUrlPath && parts.dwUrlPathLength)
            path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (path.empty()) path = L"/";
        if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

        trace("attempt=" + std::to_string(attempt_index + 1u) +
              " stage=open-session proxy=none user-agent=empty");
        HINTERNET session = WinHttpOpen(
            L"",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            fail("WinHttpOpen failed");
            finish_trace();
            return output;
        }
        ScopeExit close_session([&] { WinHttpCloseHandle(session); });
        WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

        {
            std::ostringstream line;
            line << "attempt=" << (attempt_index + 1u)
                 << " stage=connect host=\""
                 << SanitizeLogText(WideToUtf8(host.data(), host.size()))
                 << "\" port=" << parts.nPort
                 << " secure=" << (secure ? 1 : 0);
            trace(line.str());
        }
        HINTERNET connection = WinHttpConnect(session, host.c_str(),
                                               parts.nPort, 0);
        if (!connection) {
            fail("WinHttpConnect failed");
            finish_trace();
            return output;
        }
        ScopeExit close_connection([&] { WinHttpCloseHandle(connection); });

        const wchar_t* accept_types[] = {L"*/*", nullptr};
        trace("attempt=" + std::to_string(attempt_index + 1u) +
              " stage=open-request");
        HINTERNET request = WinHttpOpenRequest(
            connection, NativeHttpMethod(job.request_type), path.c_str(),
            nullptr, WINHTTP_NO_REFERER, accept_types,
            secure ? WINHTTP_FLAG_SECURE : 0);
        if (!request) {
            fail("WinHttpOpenRequest failed");
            finish_trace();
            return output;
        }
        ScopeExit close_request([&] { WinHttpCloseHandle(request); });

#if defined(WINHTTP_OPTION_REDIRECT_POLICY) && \
    defined(WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS)
        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirect_policy, sizeof(redirect_policy));
#endif

        bool has_content_type = false;
        for (const std::string& header : job.headers) {
            std::string lower = header;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
            if (lower.rfind("content-type:", 0) == 0)
                has_content_type = true;
            const std::wstring wide_header = Utf8ToWide(header);
            if (!wide_header.empty())
                WinHttpAddRequestHeaders(
                    request, wide_header.c_str(),
                    static_cast<DWORD>(wide_header.size()),
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
        if ((job.request_type == 1u || job.request_type == 2u) &&
            !has_content_type) {
            static constexpr wchar_t kFormHeader[] =
                L"Content-Type: application/x-www-form-urlencoded";
            WinHttpAddRequestHeaders(
                request, kFormHeader, static_cast<DWORD>(-1L),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        void* body_pointer = job.body.empty()
            ? WINHTTP_NO_REQUEST_DATA
            : const_cast<u8*>(job.body.data());
        const DWORD body_size = job.body.size() > MAXDWORD
            ? MAXDWORD : static_cast<DWORD>(job.body.size());
        if (job.body.size() > MAXDWORD) {
            fail("request body exceeds WinHTTP DWORD limit",
                 ERROR_FILE_TOO_LARGE);
            finish_trace();
            return output;
        }

        trace("attempt=" + std::to_string(attempt_index + 1u) +
              " stage=send bytes=" + std::to_string(body_size));
        if (!WinHttpSendRequest(
                request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                body_pointer, body_size, body_size, 0)) {
            fail("WinHttpSendRequest failed");
            finish_trace();
            return output;
        }

        trace("attempt=" + std::to_string(attempt_index + 1u) +
              " stage=receive");
        if (!WinHttpReceiveResponse(request, nullptr)) {
            fail("WinHttpReceiveResponse failed");
            finish_trace();
            return output;
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                WINHTTP_NO_HEADER_INDEX))
            output.response_code = status;
        trace("attempt=" + std::to_string(attempt_index + 1u) +
              " stage=headers status=" +
              std::to_string(output.response_code));

        DWORD header_bytes = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                            &header_bytes, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && header_bytes) {
            std::vector<wchar_t> header_buffer(
                (header_bytes + sizeof(wchar_t) - 1u) /
                sizeof(wchar_t));
            if (WinHttpQueryHeaders(
                    request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                    WINHTTP_HEADER_NAME_BY_INDEX, header_buffer.data(),
                    &header_bytes, WINHTTP_NO_HEADER_INDEX)) {
                std::size_t characters =
                    header_bytes / sizeof(wchar_t);
                while (characters &&
                       header_buffer[characters - 1u] == L'\0')
                    --characters;
                const std::string utf8 =
                    WideToUtf8(header_buffer.data(), characters);
                output.response_headers.assign(utf8.begin(), utf8.end());
            }
        }

        static constexpr std::size_t kMaximumResponse =
            128u * 1024u * 1024u;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                fail("WinHttpQueryDataAvailable failed");
                finish_trace();
                return output;
            }
            if (!available) break;
            if (output.response_body.size() + available >
                kMaximumResponse) {
                fail("response exceeds 128 MiB safety limit",
                     ERROR_FILE_TOO_LARGE);
                finish_trace();
                return output;
            }
            const std::size_t old_size = output.response_body.size();
            output.response_body.resize(old_size + available);
            DWORD read = 0;
            if (!WinHttpReadData(
                    request,
                    output.response_body.data() + old_size,
                    available, &read)) {
                output.response_body.resize(old_size);
                fail("WinHttpReadData failed");
                finish_trace();
                return output;
            }
            output.response_body.resize(old_size + read);
            if (!read) break;
        }

        output.transport_success =
            output.response_code >= 200u &&
            output.response_code < 300u;
        if (!output.transport_success && output.error.empty())
            output.error =
                "HTTP status " + std::to_string(output.response_code);
        finish_trace();
        return output;
    }

    NativeHttpResult ExecuteNativeHttp(const NativeHttpJob& job) {
        const auto thread_started = std::chrono::steady_clock::now();
        auto trace = [&](const std::string& stage) {
            QueueNativeHttpTrace(job.id, stage);
        };

        trace("thread-start method=" + WideToUtf8(
                  NativeHttpMethod(job.request_type),
                  std::wcslen(NativeHttpMethod(job.request_type))) +
              " url=\"" + SanitizeLogText(job.url) + "\" body=" +
              std::to_string(job.body.size()) + " headers=" +
              std::to_string(job.headers.size()));

        std::string configured_url = job.url;
        std::array<char, 2048> rewritten_url{};
        const int server_rewrite = gd_settings_rewrite_url(
            job.url.c_str(), rewritten_url.data(), rewritten_url.size());
        if (server_rewrite > 0) {
            configured_url = rewritten_url.data();
            trace("stage=server-policy original=\"" +
                  SanitizeLogText(job.url) + "\" effective=\"" +
                  SanitizeLogText(configured_url) + "\" base=\"" +
                  SanitizeLogText(gd_settings_server()) + "\"");
        }

        std::vector<std::string> attempts;
        const bool song_info_request =
            NativeHttpLooksLikeSongInfoUrl(job.url) ||
            NativeHttpLooksLikeSongInfoUrl(configured_url);
        if (NativeHttpIsPlainHttpUrl(configured_url)) {
            NativeHttpAppendUniqueAttempt(
                attempts, NativeHttpHttpsVariant(configured_url));
            NativeHttpAppendUniqueAttempt(attempts, configured_url);
        } else {
            NativeHttpAppendUniqueAttempt(attempts, configured_url);
        }
        if (song_info_request) {
            NativeHttpAppendUniqueAttempt(
                attempts, gd_settings_official_song_url());
            std::ostringstream policy;
            policy << "stage=url-policy original=\""
                   << SanitizeLogText(job.url) << "\" attempts=";
            for (std::size_t index = 0; index < attempts.size(); ++index) {
                if (index) policy << " -> ";
                policy << '\"' << SanitizeLogText(attempts[index]) << '\"';
            }
            policy << " reason=custom-song-first+official-https-fallback";
            trace(policy.str());
        } else if (attempts.size() > 1u) {
            trace("stage=url-policy original=\"" +
                  SanitizeLogText(job.url) +
                  "\" primary=\"" +
                  SanitizeLogText(attempts.front()) +
                  "\" fallback=\"" +
                  SanitizeLogText(attempts.back()) +
                  "\" reason=generic-https-first-preserve-configured-host");
        } else {
            trace("stage=url-policy original=\"" +
                  SanitizeLogText(job.url) +
                  "\" primary=\"" + SanitizeLogText(configured_url) +
                  "\" reason=already-secure-or-non-http");
        }

        NativeHttpResult final_result;
        const bool can_retry_after_response =
            NativeHttpRequestCanRetryAfterResponse(job);

        for (std::size_t index = 0; index < attempts.size(); ++index) {
            NativeHttpResult attempt = ExecuteNativeHttpAttempt(
                job, attempts[index], index, attempts.size());
            const bool has_fallback = index + 1u < attempts.size();
            const bool html_api_response =
                attempt.transport_success &&
                NativeHttpLooksLikeApiUrl(attempts[index]) &&
                NativeHttpLooksLikeHtmlResponse(
                    attempt.response_body);
            const int song_response_state =
                song_info_request && attempt.transport_success
                    ? NativeHttpSongResponseState(attempt.response_body)
                    : 0;
            if (html_api_response || song_response_state == 2 ||
                (song_response_state == 1 && has_fallback)) {
                attempt.transport_success = false;
                if (song_response_state == 1)
                    attempt.error = "custom song endpoint returned -1";
                else if (song_info_request)
                    attempt.error =
                        "invalid HTML/PHP response from song endpoint";
                else
                    attempt.error =
                        "HTML/block page returned by API endpoint";
                trace("attempt=" + std::to_string(index + 1u) +
                      " rejected reason=" +
                      (song_info_request
                           ? "invalid-song-response"
                           : "html-api-response"));
            }

            final_result = std::move(attempt);
            if (final_result.transport_success) break;

            if (!has_fallback) break;

            const bool retry_safe =
                can_retry_after_response ||
                NativeHttpFailureWasBeforeResponse(final_result);
            if (!retry_safe) {
                trace("fallback-suppressed reason=request-may-have-been-"
                      "processed");
                break;
            }

            trace("fallback-next from=\"" +
                  SanitizeLogText(attempts[index]) +
                  "\" to=\"" +
                  SanitizeLogText(attempts[index + 1u]) +
                  "\" reason=\"" +
                  SanitizeLogText(final_result.error) + "\"");
        }

        final_result.elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - thread_started).count();
        std::ostringstream line;
        line << "thread-finish success="
             << (final_result.transport_success ? 1 : 0)
             << " status=" << final_result.response_code
             << " body=" << final_result.response_body.size()
             << " elapsed_ms=" << std::fixed << std::setprecision(1)
             << final_result.elapsed_ms;
        if (!final_result.error.empty())
            line << " error=\"" <<
                SanitizeLogText(final_result.error) << "\"";
        trace(line.str());
        return final_result;
    }

    void JoinNativeHttpWorkers() {
        for (std::thread& worker : native_http_threads_) {
            if (worker.joinable()) worker.join();
        }
        native_http_threads_.clear();
    }
#endif

    bool RetainGuestObjectDirect(u32 object) {
        if (!object || !env_.IsMapped(object + 0x10u, sizeof(u32))) return false;
        const u32 count = env_.MemoryRead32(object + 0x10u);
        if (!count || count == std::numeric_limits<u32>::max()) return false;
        env_.MemoryWrite32(object + 0x10u, count + 1u);
        return true;
    }

    bool ReleaseGuestObject(u32 object, const std::string& label) {
        if (!object) return true;
        if (!native_http_ccobject_release_)
            return Fail("native HTTP bridge cannot resolve CCObject::release");
        u32 ignored = 0;
        return RunNestedPreservingState(native_http_ccobject_release_, {object},
                                        ignored, label, 100000000u);
    }

    bool ReadGuestByteVector(u32 object, u32 maximum,
                             std::vector<u8>& output) {
        output.clear();
        if (!object || !env_.IsMapped(object, 12u)) return false;
        const u32 begin = env_.MemoryRead32(object);
        const u32 end = env_.MemoryRead32(object + 4u);
        const u32 capacity = env_.MemoryRead32(object + 8u);
        if (!begin && !end && !capacity) return true;
        if (!begin || end < begin || capacity < end) return false;
        const u32 size = end - begin;
        if (size > maximum || !env_.IsMapped(begin, size)) return false;
        output.resize(size);
        return !size || env_.ReadBytes(begin, output.data(), output.size());
    }

    bool ReadGuestStringVector(u32 object, u32 maximum_count,
                               std::vector<std::string>& output) {
        output.clear();
        if (!object || !env_.IsMapped(object, 12u)) return false;
        const u32 begin = env_.MemoryRead32(object);
        const u32 end = env_.MemoryRead32(object + 4u);
        const u32 capacity = env_.MemoryRead32(object + 8u);
        if (!begin && !end && !capacity) return true;
        if (!begin || end < begin || capacity < end || ((end - begin) & 3u))
            return false;
        const u32 count = (end - begin) / 4u;
        if (count > maximum_count || !env_.IsMapped(begin, count * 4u))
            return false;
        output.reserve(count);
        for (u32 index = 0; index < count; ++index) {
            std::string value;
            if (!ReadGuestCowStringObject(begin + index * 4u, value, 64u * 1024u))
                return false;
            output.push_back(std::move(value));
        }
        return true;
    }

    bool QueueNativeHttpRequest(u32 client, u32 request) {
#ifndef _WIN32
        (void)client;
        (void)request;
        return Fail("native HTTP bridge requires Windows WinHTTP");
#else
        if (!client || !request || !env_.IsMapped(request, 0x74u))
            return Fail("native HTTP send received an invalid request object");
        NativeHttpJob job;
        job.id = ++native_http_next_id_;
        job.client = client;
        job.request = request;
        job.request_type = env_.MemoryRead32(request + 0x30u);
        if (!ReadGuestCowStringObject(request + 0x34u, job.url,
                                      4u * 1024u * 1024u) ||
            job.url.empty())
            return Fail("native HTTP send could not read the request URL");
        if (!ReadGuestByteVector(request + 0x38u,
                                 128u * 1024u * 1024u, job.body))
            return Fail("native HTTP send could not read the request body");
        if (!ReadGuestStringVector(request + 0x58u, 1024u, job.headers))
            return Fail("native HTTP send could not read request headers");
        if (job.request_type > 3u)
            return Fail("native HTTP send received unsupported request type " +
                        std::to_string(job.request_type));
        if (!RetainGuestObjectDirect(request))
            return Fail("native HTTP send could not retain request object");

        ++native_http_queued_count_;
        log_ << "[host] Unified ARMv7 native HTTP queued id=" << job.id
             << " method=" << WideToUtf8(
                    NativeHttpMethod(job.request_type),
                    std::wcslen(NativeHttpMethod(job.request_type)))
             << " url=\"" << SanitizeLogText(job.url) << "\""
             << " body=" << job.body.size()
             << " headers=" << job.headers.size()
             << " scheduling=independent-request-thread\n";
        log_.flush();
        try {
            native_http_threads_.emplace_back(
                [this, job = std::move(job)]() mutable {
                    native_http_active_count_.fetch_add(
                        1u, std::memory_order_relaxed);
                    NativeHttpResult result;
                    try {
                        result = ExecuteNativeHttp(job);
                    } catch (const std::exception& error) {
                        result.id = job.id;
                        result.client = job.client;
                        result.request = job.request;
                        result.request_type = job.request_type;
                        result.url = job.url;
                        result.transport_success = false;
                        result.error = std::string(
                            "native HTTP thread exception: ") + error.what();
                        QueueNativeHttpTrace(
                            job.id, "thread-exception error=" +
                                        SanitizeLogText(result.error));
                    } catch (...) {
                        result.id = job.id;
                        result.client = job.client;
                        result.request = job.request;
                        result.request_type = job.request_type;
                        result.url = job.url;
                        result.transport_success = false;
                        result.error = "native HTTP thread unknown exception";
                        QueueNativeHttpTrace(job.id,
                            "thread-exception error=unknown");
                    }
                    {
                        std::lock_guard<std::mutex> lock(native_http_mutex_);
                        native_http_results_.push_back(std::move(result));
                    }
                    native_http_active_count_.fetch_sub(
                        1u, std::memory_order_relaxed);
                });
        } catch (const std::exception& error) {
            ReleaseGuestObject(request, "native HTTP thread launch cleanup");
            return Fail(std::string(
                            "native HTTP request thread launch failed: ") +
                        error.what());
        }
        return true;
#endif
    }

    bool SetGuestByteVector(u32 object, const std::vector<u8>& data) {
        env_.MemoryWrite32(object, 0u);
        env_.MemoryWrite32(object + 4u, 0u);
        env_.MemoryWrite32(object + 8u, 0u);
        if (data.empty()) return true;
        if (data.size() > std::numeric_limits<u32>::max()) return false;
        const u32 memory = Allocate(static_cast<u32>(data.size()));
        if (!memory || !env_.WriteBytes(memory, data.data(), data.size())) {
            if (memory) Free(memory);
            return false;
        }
        env_.MemoryWrite32(object, memory);
        env_.MemoryWrite32(object + 4u,
                           memory + static_cast<u32>(data.size()));
        env_.MemoryWrite32(object + 8u,
                           memory + static_cast<u32>(data.size()));
        return true;
    }

    bool BuildNativeHttpResponse(const NativeHttpResult& result,
                                 u32& response) {
        response = 0u;
        if (!native_http_ccobject_ctor_ || !native_http_response_vtable_) {
            ReleaseGuestObject(result.request,
                               "native HTTP missing ABI request cleanup");
            return Fail("native HTTP response ABI symbols are unavailable");
        }
        const u32 object = Allocate(0x58u);
        if (!object) {
            ReleaseGuestObject(result.request,
                               "native HTTP allocation request cleanup");
            return Fail("native HTTP response allocation failed");
        }
        std::array<u8, 0x58> zero{};
        env_.WriteBytes(object, zero.data(), zero.size());
        u32 ignored = 0;
        if (!RunNestedPreservingState(native_http_ccobject_ctor_, {object},
                                      ignored, "native HTTP CCObject constructor",
                                      100000000u)) {
            Free(object);
            ReleaseGuestObject(result.request,
                               "native HTTP constructor request cleanup");
            return false;
        }
        env_.MemoryWrite32(object, native_http_response_vtable_);
        env_.MemoryWrite32(object + 0x30u, result.request);
        env_.MemoryWrite8(object + 0x34u,
                          result.transport_success ? 1u : 0u);
        env_.MemoryWrite32(object + 0x38u, 0u);
        env_.MemoryWrite32(object + 0x3cu, 0u);
        env_.MemoryWrite32(object + 0x40u, 0u);
        env_.MemoryWrite32(object + 0x44u, 0u);
        env_.MemoryWrite32(object + 0x48u, 0u);
        env_.MemoryWrite32(object + 0x4cu, 0u);
        env_.MemoryWrite32(object + 0x50u, result.response_code);
        env_.MemoryWrite32(object + 0x54u, runtime_.v22_empty_string_data);
        if (!BuildGuestStringFromBytes(object + 0x54u, result.error) ||
            !SetGuestByteVector(object + 0x38u, result.response_body) ||
            !SetGuestByteVector(object + 0x44u, result.response_headers)) {
            ReleaseGuestObject(object, "native HTTP failed response cleanup");
            return false;
        }
        response = object;
        return true;
    }

    bool DispatchNativeHttpCallback(const NativeHttpResult& result) {
        u32 response = 0u;
        if (!BuildNativeHttpResponse(result, response)) return false;
        const u32 target = env_.MemoryRead32(result.request + 0x48u);
        u32 selector = env_.MemoryRead32(result.request + 0x4cu);
        const u32 adjustment_word = env_.MemoryRead32(result.request + 0x50u);
        const u32 adjustment = static_cast<u32>(
            static_cast<s32>(adjustment_word) >> 1);
        const bool virtual_selector = (adjustment_word & 1u) != 0u;
        const u32 adjusted_target = target + adjustment;
        if (target && (selector || virtual_selector)) {
            if (virtual_selector) {
                if (!env_.IsMapped(adjusted_target, 4u)) {
                    ReleaseGuestObject(response,
                                       "native HTTP invalid callback response cleanup");
                    return Fail("native HTTP callback target is invalid");
                }
                const u32 vtable = env_.MemoryRead32(adjusted_target);
                if (!vtable || !env_.IsMapped(vtable + selector, 4u)) {
                    ReleaseGuestObject(response,
                                       "native HTTP invalid vtable response cleanup");
                    return Fail("native HTTP callback vtable selector is invalid");
                }
                selector = env_.MemoryRead32(vtable + selector);
            }
            u32 ignored = 0u;
            const u64 payload_bytes =
                static_cast<u64>(result.response_body.size()) +
                static_cast<u64>(result.response_headers.size());
            log_ << "[host] Unified ARMv7 native HTTP callback id="
                 << result.id << " payload=" << payload_bytes
                 << " target=0x" << std::hex << adjusted_target
                 << " selector=0x" << selector << std::dec
                 << " tick-budget=1000000000 url=\""
                 << SanitizeLogText(result.url) << "\"\n";
            if (!selector || !RunNestedPreservingState(
                                 selector,
                                 {adjusted_target, result.client, response},
                                 ignored,
                                 "native HTTP response callback",
                                 1000000000u)) {
                ReleaseGuestObject(response,
                                   "native HTTP callback failure response cleanup");
                return false;
            }
        }
        if (!ReleaseGuestObject(response, "native HTTP response release"))
            return false;
        ++native_http_callback_count_;
        return true;
    }

    bool PumpNativeHttpCallbacks() {
#ifndef _WIN32
        return true;
#else
        std::deque<std::string> trace;
        std::deque<NativeHttpResult> ready;
        {
            std::lock_guard<std::mutex> lock(native_http_mutex_);
            trace.swap(native_http_trace_);
            ready.swap(native_http_results_);
        }
        std::sort(ready.begin(), ready.end(),
                  [](const NativeHttpResult& left,
                     const NativeHttpResult& right) {
                      return left.id < right.id;
                  });
        for (const std::string& line : trace) log_ << line << '\n';
        if (!trace.empty()) log_.flush();
        for (const NativeHttpResult& result : ready) {
            ++native_http_completed_count_;
            log_ << "[host] Unified ARMv7 native HTTP completed id=" << result.id
                 << " success=" << (result.transport_success ? 1 : 0)
                 << " code=" << result.response_code
                 << " body=" << result.response_body.size()
                 << " headers=" << result.response_headers.size()
                 << " elapsed_ms=" << std::fixed << std::setprecision(1)
                 << result.elapsed_ms;
            if (!result.error.empty())
                log_ << " error=\"" << SanitizeLogText(result.error) << "\"";
            log_ << '\n';
            log_.flush();
            if (!DispatchNativeHttpCallback(result)) return false;
        }
        return true;
#endif
    }

    bool RecoverV22NullCcStringFloatValue(const std::string& label) {
        if (!v22_ccstring_float_value_ || cpu_.Regs()[0] != 0u) return false;
        const u32 pc = cpu_.Regs()[15] & ~1u;
        const u32 lr = cpu_.Regs()[14];
        const u32 lr_address = lr & ~1u;
        const u32 begin = v22_ccstring_float_value_;
        const u32 end = begin + v22_ccstring_float_value_size_;
        const bool inside_float_value =
            (pc >= begin && pc < end) ||
            (lr_address >= begin && lr_address < end);
        const bool null_underflow =
            env_.fault_address == 0x30u || env_.fault_address >= 0xFFFFF000u;
        if (!inside_float_value || !null_underflow || !lr) return false;

        // CCString::floatValue calls CCString::length first. A malformed beta
        // level can pass a null CCString pointer on the end screen. Resume as
        // though length() returned zero; floatValue then follows its own
        // normal empty-string path and returns 0.0f.
        cpu_.Regs()[0] = 0u;
        cpu_.Regs()[15] = lr_address;
        u32 cpsr = cpu_.Cpsr();
        if (lr & 1u) cpsr |= 0x20u;
        else cpsr &= ~0x20u;
        cpu_.SetCpsr(cpsr);
        const u32 fault = env_.fault_address;
        env_.ResetStopState();
        ++v22_null_ccstring_float_recoveries_;
        log_ << "RESULT: DYNARMIC_V22_NULL_CCSTRING_FLOAT_RECOVERED call="
             << v22_null_ccstring_float_recoveries_
             << " label=" << label << " fault=0x" << std::hex << fault
             << " resume=0x" << lr_address << std::dec << '\n';
        log_.flush();
        return true;
    }

    bool Fail(const std::string& message) {
        last_error_ = message;
        log_ << "ERROR: " << message << '\n';
        DumpImportTrace("failure:" + message, 128u);
        log_.flush();
        std::cerr << "DYNARMIC EXECUTION ERROR: " << message << '\n';
        return false;
    }
    void RememberEvent(const std::string& event) {
        if (!recent_events_.empty() && recent_events_.back() == event) return;
        recent_events_.push_back(event);
        while (recent_events_.size() > 256u) recent_events_.pop_front();
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
        if (runtime_.v22_companion_image_min &&
            address >= runtime_.v22_companion_image_min &&
            address < runtime_.v22_companion_image_max) {
            const auto found = std::upper_bound(
                runtime_.symbols.begin(), runtime_.symbols.end(), address,
                [](u32 value, const SymbolRecord& symbol) {
                    return value < symbol.address;
                });
            if (found != runtime_.symbols.begin()) {
                const SymbolRecord& symbol = *std::prev(found);
                if (symbol.address >= runtime_.v22_companion_image_min) {
                    const u32 offset = address - symbol.address;
                    output << symbol.name;
                    if (offset) output << "+0x" << std::hex << offset;
                    output << " [libgame+0x" << std::hex
                           << (address - kV22CompanionBase) << ']';
                    return output.str();
                }
            }
            output << "libgame+0x" << std::hex
                   << (address - kV22CompanionBase);
            return output.str();
        }
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
    bool IsForensicImport(const std::string& name) const {
        if (name == "__android_log_print" || name == "pthread_create" ||
            name == "pthread_exit" || name == "pthread_join" ||
            name == "pthread_detach" || name == "pthread_cond_init" ||
            name == "pthread_cond_destroy" || name == "pthread_cond_signal" ||
            name == "pthread_cond_broadcast" || name == "pthread_cond_wait" ||
            name == "sem_init" || name == "sem_destroy" ||
            name == "sem_wait" || name == "sem_post" ||
            name == "socket" || name == "socketpair" || name == "pipe" ||
            name == "connect" || name == "accept" || name == "bind" ||
            name == "listen" || name == "shutdown" || name == "poll" ||
            name == "select" || name == "send" || name == "sendto" ||
            name == "recv" || name == "recvfrom" || name == "writev" ||
            name == "getaddrinfo" || name == "freeaddrinfo" ||
            name == "gethostbyname" || name == "getnameinfo" ||
            name == "getsockopt" || name == "setsockopt" ||
            name == "getsockname" || name == "getpeername" ||
            name == "inet_pton" || name == "inet_ntop" ||
            name == "fcntl" || name == "ioctl") return true;
        return false;
    }

    std::size_t RecordImportTrace(u32 svc, const ImportRecord& import) {
        const std::size_t index = import_trace_cursor_;
        ImportTraceEntry& entry = import_trace_[index];
        entry = {};
        entry.sequence = ++import_trace_sequence_;
        entry.svc = svc;
        entry.import_address = import.address;
        entry.pc = cpu_.Regs()[15];
        entry.lr = cpu_.Regs()[14];
        entry.arguments = {cpu_.Regs()[0], cpu_.Regs()[1], cpu_.Regs()[2], cpu_.Regs()[3]};
        entry.worker = running_cooperative_worker_;
        import_trace_cursor_ = (import_trace_cursor_ + 1u) % import_trace_.size();
        import_trace_count_ = std::min(import_trace_count_ + 1u, import_trace_.size());
        return index;
    }

    void CompleteImportTrace(std::size_t index, bool completed) {
        ImportTraceEntry& entry = import_trace_[index];
        entry.result = cpu_.Regs()[0];
        entry.completed = completed;
    }

    void DumpImportTrace(const std::string& reason, std::size_t requested = 96u) {
        if (!import_trace_count_) return;
        const std::size_t count = std::min(requested, import_trace_count_);
        log_ << "[trace] Unified ARMv7 import-ring reason=" << reason
             << " entries=" << count << '/' << import_trace_count_ << '\n';
        const std::size_t oldest =
            (import_trace_cursor_ + import_trace_.size() - import_trace_count_) % import_trace_.size();
        const std::size_t skip = import_trace_count_ - count;
        for (std::size_t offset = skip; offset < import_trace_count_; ++offset) {
            const ImportTraceEntry& entry = import_trace_[(oldest + offset) % import_trace_.size()];
            const std::size_t import_index = entry.svc ? static_cast<std::size_t>(entry.svc - 1u) : runtime_.imports.size();
            const std::string name = import_index < runtime_.imports.size()
                ? runtime_.imports[import_index].name : std::string("<unknown>");
            log_ << "[trace] #" << entry.sequence
                 << " ctx=" << (entry.worker ? "worker" : "main")
                 << " svc=" << entry.svc
                 << " stub=0x" << std::hex << entry.import_address
                 << " pc=0x" << entry.pc
                 << " lr=0x" << entry.lr << std::dec
                 << " caller=" << DescribeAddress(entry.lr)
                 << " name=" << name
                 << " r0=0x" << std::hex << entry.arguments[0]
                 << " r1=0x" << entry.arguments[1]
                 << " r2=0x" << entry.arguments[2]
                 << " r3=0x" << entry.arguments[3]
                 << " result=0x" << entry.result << std::dec
                 << " completed=" << (entry.completed ? 1 : 0) << '\n';
        }
        log_.flush();
    }

    void MaybeLogForensicHeartbeat(const std::string& current_import) {
        if ((total_import_calls_ & 0x0fffu) != 0u ||
            forensic_next_heartbeat_ == std::chrono::steady_clock::time_point{}) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < forensic_next_heartbeat_) return;
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
            now - forensic_call_started_).count();
        log_ << "[trace] Unified ARMv7 guest heartbeat elapsed_ms="
             << std::fixed << std::setprecision(1) << elapsed_ms
             << " active=\"" << forensic_call_label_ << "\""
             << " current-import=" << current_import
             << " ctx=" << (running_cooperative_worker_ ? "worker" : "main")
             << " pc=0x" << std::hex << cpu_.Regs()[15]
             << " lr=0x" << cpu_.Regs()[14] << std::dec
             << " caller=" << DescribeAddress(cpu_.Regs()[14])
             << " total-imports=" << total_import_calls_ << '\n';
        DumpImportTrace("heartbeat:" + forensic_call_label_, 32u);
        forensic_next_heartbeat_ = now + std::chrono::milliseconds(250);
    }

    void LogForensicImport(const char* phase, const ImportRecord& import, bool ok = true) {
        log_ << "[trace] Unified ARMv7 import " << phase
             << " ctx=" << (running_cooperative_worker_ ? "worker" : "main")
             << " svc=" << import.svc
             << " stub=0x" << std::hex << import.address
             << " lr=0x" << cpu_.Regs()[14] << std::dec
             << " caller=" << DescribeAddress(cpu_.Regs()[14])
             << " name=" << import.name
             << " r0=0x" << std::hex << cpu_.Regs()[0]
             << " r1=0x" << cpu_.Regs()[1]
             << " r2=0x" << cpu_.Regs()[2]
             << " r3=0x" << cpu_.Regs()[3] << std::dec;
        if (std::string_view(phase) == "return")
            log_ << " ok=" << (ok ? 1 : 0) << " result=0x" << std::hex << cpu_.Regs()[0] << std::dec;
        log_ << '\n';
        log_.flush();
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
        const std::size_t trace_index = RecordImportTrace(svc, import);
        const bool forensic_import = IsForensicImport(import.name);
        if (forensic_import) LogForensicImport("call", import);
        MaybeLogForensicHeartbeat(import.name);

        // Recording a heap-allocated string for every libc trap was itself a
        // major cost during ZIP scans. Sample normal imports, but always retain
        // fatal/exception-related calls and every network/thread call.
        const bool diagnostic_import = forensic_import || import.name == "abort" ||
            import.name == "exit" || import.name == "__stack_chk_fail" ||
            import.name == "longjmp" || import.name == "siglongjmp";
        if (diagnostic_import || (total_import_calls_ & 0x0fffu) == 0u)
            RememberEvent("import:" + import.name);

        bool ok = false;
        // Sample one host dispatch out of every 1024 calls per import. This
        // identifies expensive bridges without putting a clock read around
        // every libc/OpenGL trap on low-end systems.
        if ((import.calls & 0x03ffu) == 1u) {
            const auto host_started = std::chrono::steady_clock::now();
            ok = DispatchImport(import);
            const auto host_elapsed = std::chrono::steady_clock::now() - host_started;
            import.sampled_host_nanoseconds += static_cast<u64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(host_elapsed).count());
            ++import.sampled_host_calls;
        } else {
            ok = DispatchImport(import);
        }
        CompleteImportTrace(trace_index, ok);
        if (forensic_import) LogForensicImport("return", import, ok);
        return ok;
    }
    void ResumeAfterStub(u32 stub_address) {
        cpu_.Regs()[15] = stub_address + 4u;
        cpu_.SetCpsr(cpu_.Cpsr() & ~0x20u);
    }

    bool IsImportStubReturnPc(u32 pc) const {
        pc &= ~1u;
        if (pc < kImportBase + 4u) return false;
        const u32 relative = pc - (kImportBase + 4u);
        return (relative & 7u) == 0u && relative / 8u < runtime_.imports.size();
    }

    bool IsVmOrJniStubReturnPc(u32 pc) const {
        pc &= ~1u;
        const auto in_stub_table = [pc](u32 base, u32 count) {
            if (pc < base + 4u) return false;
            const u32 relative = pc - (base + 4u);
            return (relative & 7u) == 0u && relative / 8u < count;
        };
        return in_stub_table(kVmStubs, 8u) ||
               in_stub_table(kEnvStubs, static_cast<u32>(kJniTableSize));
    }

    bool CompletePendingStubReturn(const char* reason) {
        const u32 pc = cpu_.Regs()[15] & ~1u;
        if (!IsImportStubReturnPc(pc) && !IsVmOrJniStubReturnPc(pc)) return false;
        const u32 lr = cpu_.Regs()[14];
        u32 cpsr = cpu_.Cpsr();
        if (lr & 1u) cpsr |= 0x20u;
        else cpsr &= ~0x20u;
        cpu_.SetCpsr(cpsr);
        cpu_.Regs()[15] = lr & ~1u;
        if (cooperative_worker_stub_return_count_++ < 256u) {
            log_ << "[host] Unified ARMv7 completed pending stub return reason="
                 << (reason ? reason : "unspecified")
                 << " stub-pc=0x" << std::hex << pc
                 << " target=0x" << (lr & ~1u) << std::dec
                 << " thumb=" << ((lr & 1u) ? 1 : 0)
                 << " target-desc=" << DescribeAddress(lr) << '\n';
            log_.flush();
        }
        return true;
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
        const std::size_t maximum =
            std::min<std::size_t>(available, kMaximumGuestCString);
        const void* end = std::memchr(bytes, 0, maximum);
        return end ? static_cast<u32>(static_cast<const u8*>(end) - bytes) : 0;
    }
    std::string ReadCString(
        u32 address, std::size_t maximum = kMaximumGuestCString) const {
        std::string text;
        if (!env_.ReadCString(address, text, maximum)) return {};
        return text;
    }
    bool GuestObjectTypeContains(u32 object, const char* needle) const {
        if (!object || !needle || !env_.IsMapped(object, 4u)) return false;
        const u32 vtable = env_.MemoryRead32(object);
        if (vtable < 4u || !env_.IsMapped(vtable - 4u, 4u)) return false;
        const u32 type_info = env_.MemoryRead32(vtable - 4u);
        if (!type_info || !env_.IsMapped(type_info + 4u, 4u)) return false;
        const u32 name_address = env_.MemoryRead32(type_info + 4u);
        const std::string name = ReadCString(name_address, 96u);
        return !name.empty() && name.find(needle) != std::string::npos;
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
        } else if (method->name == "getDeviceRefreshRate") {
            // Returning the old default 0.0f leaves newer Geometry Dash builds
            // with an invalid simulation cadence: editor playtest renders one
            // frame but never advances the cube. Android always reports a
            // positive display refresh rate here; use the wrapper's 60 Hz
            // presentation target until variable-refresh support is added.
            result = 60.0f;
            if (!refresh_rate_bridge_logged_) {
                refresh_rate_bridge_logged_ = true;
                log_ << "RESULT: DYNARMIC_V22_REFRESH_RATE_BRIDGE hz=60\n";
            }
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


    bool ReadGuestCowStringObject(u32 object_address, std::string& value,
                                  std::size_t maximum = 64u * 1024u * 1024u) const {
        value.clear();
        if (!object_address || !env_.IsMapped(object_address, 4u)) return false;
        const u32 data_address = env_.MemoryRead32(object_address);
        if (!data_address || data_address < 12u ||
            !env_.IsMapped(data_address - 12u, 12u))
            return false;
        const u32 length = env_.MemoryRead32(data_address - 12u);
        if (length > maximum) return false;
        const void* bytes = env_.HostPointer(data_address, length);
        if (!bytes && length) return false;
        value.assign(static_cast<const char*>(bytes), length);
        return true;
    }

    bool BuildGuestStringFromBytes(u32 object_address,
                                   const std::string& value) {
        if (!object_address || !runtime_.v22_string_append_bytes ||
            !runtime_.v22_empty_string_data ||
            value.size() > 64u * 1024u * 1024u ||
            value.size() > std::numeric_limits<u32>::max() - 1u)
            return false;
        const u32 length = static_cast<u32>(value.size());
        const u32 source = Allocate(length + 1u);
        if (!source) return false;
        const bool copied = value.empty() ||
            env_.WriteBytes(source, value.data(), value.size());
        env_.MemoryWrite8(source + length, 0u);
        if (!copied) {
            Free(source);
            return false;
        }
        // Match the Bringup9 repair path: detach the caller's temporary before
        // rebuilding it, without trying to free a representation whose COW
        // ownership may already have been damaged by the beta.
        env_.MemoryWrite32(object_address, runtime_.v22_empty_string_data);
        const auto saved_regs = cpu_.Regs();
        const auto saved_ext = cpu_.ExtRegs();
        const u32 saved_cpsr = cpu_.Cpsr();
        const u32 saved_fpscr = cpu_.Fpscr();
        u32 ignored = 0;
        const bool ok = RunFunction(
            runtime_.v22_string_append_bytes,
            {object_address, source, length}, &ignored,
            "V22 guest std::string byte builder", 100000000u,
            std::chrono::milliseconds(15000));
        cpu_.Regs() = saved_regs;
        cpu_.ExtRegs() = saved_ext;
        cpu_.SetCpsr(saved_cpsr);
        cpu_.SetFpscr(saved_fpscr);
        Free(source);
        if (!ok) return false;
        std::string validation;
        if (!ReadGuestCowStringObject(object_address, validation) ||
            validation != value) {
            log_ << "ERROR: V22 repaired std::string validation failed expected="
                 << value.size() << " actual=" << validation.size()
                 << " object=0x" << std::hex << object_address
                 << " builder=0x" << runtime_.v22_string_append_bytes
                 << std::dec << '\n';
            log_.flush();
            return false;
        }
        return true;
    }

    bool RunNestedPreservingState(u32 address,
                                  const std::vector<u32>& arguments,
                                  u32& result, const std::string& label,
                                  u64 tick_budget = 500000000u) {
        const auto saved_regs = cpu_.Regs();
        const auto saved_ext = cpu_.ExtRegs();
        const u32 saved_cpsr = cpu_.Cpsr();
        const u32 saved_fpscr = cpu_.Fpscr();
        const bool ok = RunFunction(address, arguments, &result, label,
                                    tick_budget,
                                    std::chrono::milliseconds(30000));
        cpu_.Regs() = saved_regs;
        cpu_.ExtRegs() = saved_ext;
        cpu_.SetCpsr(saved_cpsr);
        cpu_.SetFpscr(saved_fpscr);
        return ok;
    }

    struct GuestCcArrayView {
        u32 count = 0u;
        u32 elements = 0u;
    };

    bool ReadGuestCcArray(u32 array, u32 maximum_count,
                          GuestCcArrayView& view) {
        view = {};
        if (!array || !env_.IsMapped(array + 48u, 4u)) return false;
        const u32 data = env_.MemoryRead32(array + 48u);
        if (!data || !env_.IsMapped(data, 16u)) return false;
        const u32 count = env_.MemoryRead32(data);
        const u32 elements = env_.MemoryRead32(data + 12u);
        if (count > maximum_count) return false;
        const std::size_t bytes =
            static_cast<std::size_t>(count) * sizeof(u32);
        if (count && (!elements || !env_.IsMapped(elements, bytes)))
            return false;
        view.count = count;
        view.elements = elements;
        return true;
    }

    u32 FindDirectChildByTag(u32 node, s32 wanted_tag) {
        if (!node || !env_.IsMapped(node + 188u, 4u)) return 0u;
        GuestCcArrayView children;
        if (!ReadGuestCcArray(env_.MemoryRead32(node + 188u), 4096u,
                              children))
            return 0u;
        for (u32 index = 0; index < children.count; ++index) {
            const u32 child =
                env_.MemoryRead32(children.elements + index * 4u);
            if (!child || !env_.IsMapped(child + 12u, 4u)) continue;
            if (static_cast<s32>(env_.MemoryRead32(child + 12u)) ==
                wanted_tag)
                return child;
        }
        return 0u;
    }

    bool ForceSpriteVisibleAndOpaque(u32 sprite, const char* label) {
        if (!LooksLikeGuestObject(runtime_, env_, sprite) ||
            !env_.IsMapped(sprite + 265u, 1u))
            return true;
        env_.MemoryWrite8(sprite + 234u, 1u);
        if (env_.MemoryRead8(sprite + 265u) == 255u) return true;
        u32 ignored = 0u;
        return RunNestedPreservingState(
            runtime_.v22_ccsprite_set_opacity, {sprite, 255u}, ignored,
            label, 100000000u);
    }

    bool HostV22PlayVisibility(u32 play_layer) {
        if (!LooksLikeGuestObject(runtime_, env_, play_layer))
            return Fail("V22 PlayLayer visibility received an invalid layer");

        constexpr u32 kUiLayerOffset = 11424u;
        if (!env_.IsMapped(play_layer + kUiLayerOffset, 4u)) return true;
        const u32 ui_layer =
            env_.MemoryRead32(play_layer + kUiLayerOffset);
        if (!LooksLikeGuestObject(runtime_, env_, ui_layer) ||
            !env_.IsMapped(ui_layer + 519u, 1u) ||
            env_.MemoryRead8(ui_layer + 518u) == 0u)
            return true;

        // UILayer::init creates the platformer control sprite at +472 and the
        // optional second control as direct child tag 2990. The companion
        // helper only changed their opacity, but obtained that opacity through
        // GDPSManager::sharedState(), whose constructor caused Log18's
        // detectEmulators/__stack_chk_fail crash. Use the normal fully-visible
        // value directly and never instantiate that unrelated singleton.
        const u32 primary_control = env_.IsMapped(ui_layer + 472u, 4u)
            ? env_.MemoryRead32(ui_layer + 472u) : 0u;
        if (!ForceSpriteVisibleAndOpaque(
                primary_control, "V22 platformer primary control opacity"))
            return false;
        const u32 secondary_control =
            FindDirectChildByTag(ui_layer, 2990);
        if (!ForceSpriteVisibleAndOpaque(
                secondary_control, "V22 platformer secondary control opacity"))
            return false;

        if (v22_platformer_ui_logged_ != ui_layer) {
            v22_platformer_ui_logged_ = ui_layer;
            log_ << "RESULT: DYNARMIC_V22_PLATFORMER_CONTROLS_VISIBLE "
                 << "ui=0x" << std::hex << ui_layer
                 << " primary=0x" << primary_control
                 << " secondary=0x" << secondary_control
                 << std::dec
                 << " source=host-opacity companion-gdps=disabled\n";
            log_.flush();
        }
        return true;
    }

    u32 FindV22DrawGridLayer(u32 editor_layer) {
        if (v22_draw_grid_layer_ && LooksLikeGuestObject(runtime_, env_, v22_draw_grid_layer_))
            return v22_draw_grid_layer_;
        const u32 expected = runtime_.v22_draw_grid_vtable + 8u;
        for (u32 offset = 0x100u; offset < 0x4000u; offset += 4u) {
            if (!env_.IsMapped(editor_layer + offset, 4u)) continue;
            const u32 candidate = env_.MemoryRead32(editor_layer + offset);
            if (!candidate || !env_.IsMapped(candidate, 4u)) continue;
            if (env_.MemoryRead32(candidate) == expected) {
                v22_draw_grid_layer_ = candidate;
                log_ << "RESULT: DYNARMIC_V22_DRAW_GRID_FOUND editor=0x"
                     << std::hex << editor_layer << " grid=0x" << candidate
                     << " field=0x" << offset << std::dec << '\n';
                log_.flush();
                return candidate;
            }
        }
        return 0u;
    }

    bool HostV22UpdateCameraBackground(
        u32 layer, u32 point_address, u32& suppress) {
        (void)point_address;
        suppress = 0u;
        if (layer && layer == v22_editor_visual_layer_ &&
            IsV22EditorSceneActive()) {
            suppress = 1u;
            ++v22_editor_background_updates_suppressed_;
            if (!v22_editor_background_suppression_logged_) {
                v22_editor_background_suppression_logged_ = true;
                log_ << "RESULT: DYNARMIC_V22_EDITOR_BACKGROUND_UPDATE_SUPPRESSED"
                     << " editor=0x" << std::hex << layer << std::dec
                     << " callsites=3 policy=editor-only-gameplay-original\n";
                log_.flush();
            }
        }
        return true;
    }

    bool HostV22RejectNullBatchTexture(u32 batch_node) {
        ++v22_null_batch_texture_rejections_;
        log_ << "RESULT: DYNARMIC_V22_NULL_BATCH_TEXTURE_REJECTED count="
             << v22_null_batch_texture_rejections_
             << " batch=0x" << std::hex << batch_node << std::dec
             << " action=init-return-false policy=no-half-built-node\n";
        log_.flush();
        return true;
    }

    bool HostV22BatchUpdateBlend(u32 batch_node) {
        /* Exact beta CCSpriteBatchNode::updateBlendFunc field behavior. */
        static constexpr u32 kTextureAtlasOffset = 0x108u;
        static constexpr u32 kBlendSourceOffset = 0x10Cu;
        static constexpr u32 kBlendDestinationOffset = 0x110u;
        static constexpr u32 kAtlasTextureOffset = 0x48u;
        static constexpr u32 kTexturePmaOffset = 0x54u;
        static constexpr u32 kGlOne = 1u;
        static constexpr u32 kGlSrcAlpha = 0x302u;
        static constexpr u32 kGlOneMinusSrcAlpha = 0x303u;

        ++v22_batch_blend_repairs_;
        if (!batch_node ||
            !env_.IsMapped(batch_node + kTextureAtlasOffset, 4u) ||
            !env_.IsMapped(batch_node + kBlendSourceOffset, 8u)) {
            ++v22_missing_batch_texture_fallbacks_;
            log_ << "RESULT: DYNARMIC_V22_BATCH_BLEND_INVALID_NODE count="
                 << v22_missing_batch_texture_fallbacks_
                 << " batch=0x" << std::hex << batch_node << std::dec
                 << " action=skip\n";
            log_.flush();
            return true;
        }

        const u32 atlas = env_.MemoryRead32(batch_node + kTextureAtlasOffset);
        const u32 texture =
            atlas && env_.IsMapped(atlas + kAtlasTextureOffset, 4u)
                ? env_.MemoryRead32(atlas + kAtlasTextureOffset) : 0u;
        const bool texture_valid =
            texture && env_.IsMapped(texture + kTexturePmaOffset, 1u);
        const bool premultiplied_alpha =
            texture_valid && env_.MemoryRead8(texture + kTexturePmaOffset) != 0u;

        env_.MemoryWrite32(batch_node + kBlendDestinationOffset,
                           kGlOneMinusSrcAlpha);
        env_.MemoryWrite32(batch_node + kBlendSourceOffset,
                           premultiplied_alpha ? kGlOne : kGlSrcAlpha);

        if (!texture_valid) {
            ++v22_missing_batch_texture_fallbacks_;
            log_ << "RESULT: DYNARMIC_V22_BATCH_BLEND_MISSING_TEXTURE count="
                 << v22_missing_batch_texture_fallbacks_
                 << " batch=0x" << std::hex << batch_node
                 << " atlas=0x" << atlas << " texture=0x" << texture
                 << std::dec << " action=defensive-blend-only\n";
            log_.flush();
        } else if (v22_batch_blend_repairs_ == 1u) {
            log_ << "RESULT: DYNARMIC_V22_BATCH_BLEND_HOST_EXACT"
                 << " batch=0x" << std::hex << batch_node
                 << " atlas=0x" << atlas
                 << " texture=0x" << texture << std::dec
                 << " pma=" << (premultiplied_alpha ? 1 : 0)
                 << " source=exact-field-reproduction\n";
            log_.flush();
        }
        return true;
    }

    bool HostV22EditorVisibility(u32 editor_layer, u32 delta_bits) {
        if (!LooksLikeGuestObject(runtime_, env_, editor_layer))
            return Fail("V22 editor visibility received an invalid layer");
        if (v22_editor_visual_layer_ != editor_layer) {
            v22_editor_visual_layer_ = editor_layer;
            v22_editor_visualized_objects_.clear();
            v22_editor_visibility_passes_ = 0u;
            v22_draw_grid_layer_ = 0u;
            v22_editor_overlay_frames_ = 0u;
            v22_editor_overlay_playtest_active_ = false;
            v22_editor_level_settings_refreshed_ = false;
            v22_editor_background_suppression_logged_ = false;
            log_ << "RESULT: DYNARMIC_V22_EDITOR_OVERLAY_SESSION_RESET editor=0x"
                 << std::hex << editor_layer << std::dec << '\n';
            log_.flush();
        }
        ++v22_editor_visibility_passes_;

        u32 ignored = 0u;
        if (!RunNestedPreservingState(
                runtime_.v22_gjbase_pre_update_visibility,
                {editor_layer, delta_bits}, ignored,
                "V22 editor pre-update visibility", 250000000u))
            return false;

        const bool late2022 = runtime_.v22_wrapper_editor_profile ==
            V22EditorRestoreProfile::Late2022;
        const u32 kSectionArrayOffset = late2022 ? 0x344u : 0x348u;
        const u32 kColorArrayOffset = late2022 ? 0x2BD0u : 0x2C04u;
        const u32 kCameraNodeOffset = late2022 ? 0x470u : 0x48Cu;
        constexpr u32 kNodeScaleOffset = 0x38u;
        constexpr u32 kNodePositionOffset = 0x44u;
        constexpr u32 kNodeVisibleOffset = 0xEAu;
        const u32 color_array =
            env_.IsMapped(editor_layer + kColorArrayOffset, 4u)
                ? env_.MemoryRead32(editor_layer + kColorArrayOffset)
                : 0u;
        if (!env_.IsMapped(editor_layer + kSectionArrayOffset, 4u))
            return true;
        GuestCcArrayView sections;
        if (!ReadGuestCcArray(
                env_.MemoryRead32(editor_layer + kSectionArrayOffset),
                20000u, sections))
            return true;

        /*
         * Reproduce the beta companion's camera culling without emulating its
         * very expensive dynamic_cast/10,000-section loop. The old host bridge
         * marked every object visible forever and never called deactivateObject;
         * after playtest/panning this left stale batches and could produce the
         * moving right-side void or apparently missing objects.
         */
        bool camera_valid = false;
        float visible_left = 0.0f;
        float visible_bottom = 0.0f;
        float visible_right = 0.0f;
        float visible_top = 0.0f;
        u32 section_begin = 0u;
        u32 section_end = sections.count ? sections.count - 1u : 0u;
        if (env_.IsMapped(editor_layer + kCameraNodeOffset, 4u)) {
            const u32 camera = env_.MemoryRead32(editor_layer + kCameraNodeOffset);
            if (LooksLikeGuestObject(runtime_, env_, camera) &&
                env_.IsMapped(camera + kNodeScaleOffset, 4u) &&
                env_.IsMapped(camera + kNodePositionOffset, 8u)) {
                const float scale = WordToFloat(
                    env_.MemoryRead32(camera + kNodeScaleOffset));
                const float camera_x = WordToFloat(
                    env_.MemoryRead32(camera + kNodePositionOffset));
                const float camera_y = WordToFloat(
                    env_.MemoryRead32(camera + kNodePositionOffset + 4u));
                if (std::isfinite(scale) && std::fabs(scale) > 0.0001f &&
                    std::isfinite(camera_x) && std::isfinite(camera_y)) {
                    visible_left = -camera_x / scale;
                    visible_bottom = -camera_y / scale;
                    const auto client_size = gl_.ClientSize();
                    visible_right = visible_left +
                        static_cast<float>(client_size.first) / scale + 30.0f;
                    visible_top = visible_bottom +
                        static_cast<float>(client_size.second) / scale + 30.0f;
                    camera_valid = std::isfinite(visible_right) &&
                                   std::isfinite(visible_top) &&
                                   visible_right >= visible_left &&
                                   visible_top >= visible_bottom;
                    if (camera_valid && sections.count) {
                        const int first = static_cast<int>(
                            std::floor(visible_left / 100.0f)) - 1;
                        const int last = static_cast<int>(
                            std::ceil(visible_right / 100.0f)) + 1;
                        section_begin = static_cast<u32>(
                            std::clamp(first, 0,
                                static_cast<int>(sections.count) - 1));
                        section_end = static_cast<u32>(
                            std::clamp(last, 0,
                                static_cast<int>(sections.count) - 1));
                        if (section_end < section_begin)
                            std::swap(section_begin, section_end);
                    }
                }
            }
        }

        constexpr u32 kMaximumObjectsPerSection = 100000u;
        constexpr u32 kMaximumObjectsScanned = 250000u;
        constexpr u32 kMaximumColorObjectsPerFrame = 8192u;
        u32 objects_scanned = 0u;
        u32 visible_objects = 0u;
        u32 newly_activated = 0u;
        u32 deactivated = 0u;
        u32 color_queued = 0u;
        u32 color_queue_truncated = 0u;
        u32 opacity_hidden = 0u;
        u32 opacity_dimmed = 0u;
        u32 opacity_full = 0u;

        /*
         * LevelEditorLayerExt::updateVisibilityH uses game variable "0121"
         * when deciding whether special editor-only objects must be fully
         * hidden.  Query it once per visibility pass instead of once per
         * object.  The exact companion code then combines that option with
         * object flags at +0x4AF, +0x236 and +0x405.
         */
        bool hide_special_editor_objects = false;
        if (runtime_.v22_game_manager_shared &&
            runtime_.v22_game_manager_get_game_variable) {
            if (!v22_editor_hide_variable_address_)
                v22_editor_hide_variable_address_ = AllocateString("0121");
            u32 game_manager = 0u;
            u32 hide_value = 0u;
            if (v22_editor_hide_variable_address_) {
                if (!RunNestedPreservingState(
                        runtime_.v22_game_manager_shared, {}, game_manager,
                        "V22 GameManager::sharedState for editor opacity",
                        100000000u))
                    return false;
                if (game_manager &&
                    !RunNestedPreservingState(
                        runtime_.v22_game_manager_get_game_variable,
                        {game_manager, v22_editor_hide_variable_address_},
                        hide_value,
                        "V22 GameManager::getGameVariable(0121)",
                        100000000u))
                    return false;
                hide_special_editor_objects = hide_value != 0u;
            }
        }

        const u32 selected_color_group_offset = late2022 ? 0x2C1Cu : 0x2C50u;
        const u32 selected_color_group =
            env_.IsMapped(editor_layer + selected_color_group_offset, 4u)
                ? env_.MemoryRead32(editor_layer + selected_color_group_offset)
                : 0u;
        std::unordered_set<u32> visible_now;
        visible_now.reserve(v22_editor_visualized_objects_.size() + 64u);

        if (sections.count) {
            for (u32 section_index = section_begin;
                 section_index <= section_end &&
                 section_index < sections.count &&
                 objects_scanned < kMaximumObjectsScanned;
                 ++section_index) {
                const u32 section = env_.MemoryRead32(
                    sections.elements + section_index * 4u);
                if (!section) continue;
                GuestCcArrayView objects;
                if (!ReadGuestCcArray(section, kMaximumObjectsPerSection, objects))
                    continue;

                for (u32 object_index = 0;
                     object_index < objects.count &&
                     objects_scanned < kMaximumObjectsScanned;
                     ++object_index) {
                    const u32 object = env_.MemoryRead32(
                        objects.elements + object_index * 4u);
                    ++objects_scanned;
                    if (!LooksLikeGuestObject(runtime_, env_, object) ||
                        !env_.IsMapped(object + 0x4B0u, 1u))
                        continue;

                    bool inside = true;
                    if (camera_valid &&
                        env_.IsMapped(object + kNodePositionOffset, 8u)) {
                        const float x = WordToFloat(
                            env_.MemoryRead32(object + kNodePositionOffset));
                        const float y = WordToFloat(
                            env_.MemoryRead32(object + kNodePositionOffset + 4u));
                        inside = std::isfinite(x) && std::isfinite(y) &&
                                 x >= visible_left && x <= visible_right &&
                                 y >= visible_bottom && y <= visible_top;
                    }
                    if (!inside) continue;

                    visible_now.insert(object);
                    ++visible_objects;
                    const bool was_visible =
                        v22_editor_visualized_objects_.contains(object);
                    const bool node_visible =
                        env_.IsMapped(object + kNodeVisibleOffset, 1u) &&
                        env_.MemoryRead8(object + kNodeVisibleOffset) != 0u;
                    if (!was_visible || !node_visible) {
                        if (!RunNestedPreservingState(
                                runtime_.v22_game_object_add_main_sprite,
                                {object, 0u}, ignored,
                                "V22 editor add main sprite", 100000000u))
                            return false;
                        u32 has_secondary = 0u;
                        if (!RunNestedPreservingState(
                                runtime_.v22_game_object_has_secondary_color,
                                {object}, has_secondary,
                                "V22 editor secondary-color query", 100000000u))
                            return false;
                        if (has_secondary &&
                            !RunNestedPreservingState(
                                runtime_.v22_game_object_add_color_sprite,
                                {object, 1u}, ignored,
                                "V22 editor add color sprite", 100000000u))
                            return false;
                        if (!RunNestedPreservingState(
                                runtime_.v22_game_object_activate, {object}, ignored,
                                "V22 editor activate object", 100000000u))
                            return false;
                        ++newly_activated;
                    }

                    /*
                     * Faithfully reproduce the companion's editor opacity
                     * policy.  The old host bridge forced every activated
                     * object to 255, exposing objects the beta deliberately
                     * keeps at opacity 0 or 70.  Large hidden helper sprites are
                     * a direct candidate for the moving black "void".
                     */
                    const bool object_flag_4af =
                        env_.IsMapped(object + 0x4AFu, 1u) &&
                        env_.MemoryRead8(object + 0x4AFu) != 0u;
                    const bool object_flag_236 =
                        env_.IsMapped(object + 0x236u, 1u) &&
                        env_.MemoryRead8(object + 0x236u) != 0u;
                    const bool color_pending =
                        env_.MemoryRead8(object + 0x405u) == 0u;
                    u32 opacity = 70u;
                    if ((object_flag_4af && hide_special_editor_objects) ||
                        (object_flag_236 && color_pending)) {
                        opacity = 0u;
                        ++opacity_hidden;
                    } else {
                        const u32 primary_group =
                            env_.IsMapped(object + 0x450u, 4u)
                                ? env_.MemoryRead32(object + 0x450u)
                                : 0u;
                        const u32 secondary_group =
                            env_.IsMapped(object + 0x454u, 4u)
                                ? env_.MemoryRead32(object + 0x454u)
                                : 0u;
                        const bool selected =
                            selected_color_group == primary_group ||
                            (selected_color_group != 0xFFFFFFFFu &&
                             secondary_group != 0u &&
                             selected_color_group == secondary_group);
                        opacity = selected ? 255u : 70u;
                        if (selected) ++opacity_full;
                        else ++opacity_dimmed;
                    }
                    if (!RunNestedPreservingState(
                            runtime_.v22_game_object_set_opacity,
                            {object, opacity}, ignored,
                            "V22 editor exact object opacity", 100000000u))
                        return false;

                    if (color_array && color_pending) {
                        if (color_queued < kMaximumColorObjectsPerFrame) {
                            if (!RunNestedPreservingState(
                                    runtime_.v22_ccarray_add_object,
                                    {color_array, object}, ignored,
                                    "V22 editor queue object colour", 100000000u))
                                return false;
                            ++color_queued;
                        } else {
                            ++color_queue_truncated;
                        }
                    }
                }
            }
        }

        if (camera_valid) {
            for (const u32 object : v22_editor_visualized_objects_) {
                if (visible_now.contains(object) ||
                    !LooksLikeGuestObject(runtime_, env_, object))
                    continue;
                if (!RunNestedPreservingState(
                        runtime_.v22_game_object_deactivate,
                        {object, 0u}, ignored,
                        "V22 editor deactivate offscreen object", 100000000u))
                    return false;
                ++deactivated;
            }
            v22_editor_visualized_objects_ = std::move(visible_now);
        } else {
            /* If camera state is temporarily invalid, never hide known objects. */
            v22_editor_visualized_objects_.insert(
                visible_now.begin(), visible_now.end());
        }

        if (color_array) {
            if (!RunNestedPreservingState(
                    runtime_.v22_level_editor_update_object_colors,
                    {editor_layer, color_array}, ignored,
                    "V22 editor update object colors", 250000000u))
                return false;
            if (!RunNestedPreservingState(
                    runtime_.v22_ccarray_remove_all_objects,
                    {color_array}, ignored,
                    "V22 editor clear color queue", 100000000u))
                return false;
        }
        if (!RunNestedPreservingState(
                runtime_.v22_gjbase_process_area_visual_actions,
                {editor_layer}, ignored,
                "V22 editor process area visuals", 250000000u))
            return false;
        if (!RunNestedPreservingState(
                runtime_.v22_level_editor_sort_batchnode_children,
                {editor_layer, 0u}, ignored,
                "V22 editor sort batch nodes", 250000000u))
            return false;

        if (newly_activated || deactivated || color_queue_truncated ||
            v22_editor_visibility_passes_ == 1u ||
            (v22_editor_visibility_passes_ % 120u) == 0u) {
            log_ << "RESULT: DYNARMIC_V22_EDITOR_CAMERA_CULL"
                 << " editor=0x" << std::hex << editor_layer << std::dec
                 << " camera=" << (camera_valid ? 1 : 0)
                 << " rect=" << std::fixed << std::setprecision(1)
                 << visible_left << ',' << visible_bottom << ','
                 << visible_right << ',' << visible_top
                 << " sections=" << section_begin << '-' << section_end
                 << '/' << sections.count
                 << " scanned=" << objects_scanned
                 << " visible=" << visible_objects
                 << " activated=" << newly_activated
                 << " deactivated=" << deactivated
                 << " tracked=" << v22_editor_visualized_objects_.size()
                 << " color-queued=" << color_queued
                 << " color-truncated=" << color_queue_truncated
                 << " opacity=0:" << opacity_hidden
                 << ",70:" << opacity_dimmed
                 << ",255:" << opacity_full
                 << " hide-0121="
                 << (hide_special_editor_objects ? 1 : 0)
                 << " pass=" << v22_editor_visibility_passes_ << '\n';
            log_.flush();
        }
        return true;
    }

    static std::string SanitizeV22LevelSettingsHeader(
        const std::string& original) {
        const std::size_t begin = original.find("kS38,");
        if (begin == std::string::npos) return {};
        const std::size_t tail = original.find(",kA13,", begin + 5u);
        if (tail == std::string::npos) return {};
        std::string sanitized = original.substr(0u, begin);
        if (!sanitized.empty() && sanitized.back() == ',') sanitized.pop_back();
        if (!sanitized.empty()) sanitized.push_back(',');
        sanitized.append(original.substr(tail + 1u));
        return sanitized;
    }

    bool HostV22HookManagerDoHook(u32 target_name_address,
                                 u32 replacement,
                                 u32 original_storage) {
        const std::string target_name =
            ReadCString(target_name_address, 1024u);
        if (target_name.empty())
            return Fail("V22 companion HookManager received an empty target");
        const SymbolRecord* target = nullptr;
        for (const SymbolRecord& symbol : runtime_.symbols) {
            if (symbol.name == target_name &&
                symbol.address >= runtime_.image_min &&
                symbol.address < runtime_.image_max) {
                target = &symbol;
                break;
            }
        }
        if (!target) {
            if (original_storage && env_.IsMapped(original_storage, 4u))
                env_.MemoryWrite32(original_storage, 0u);
            ++v22_companion_hooks_skipped_;
            log_ << "RESULT: DYNARMIC_V22_COMPANION_HOOK_SKIPPED target="
                 << target_name << " reason=primary-symbol-unavailable skip="
                 << v22_companion_hooks_skipped_ << '\n';
            log_.flush();
            return true;
        }
        if (!replacement ||
            (replacement & ~1u) < runtime_.v22_companion_executable_min ||
            (replacement & ~1u) >= runtime_.v22_companion_executable_max)
            return Fail(
                "V22 companion hook replacement is not executable libgame.so code");
        if (original_storage) {
            if (!env_.IsMapped(original_storage, 4u))
                return Fail("V22 companion hook original storage is invalid");
            env_.MemoryWrite32(original_storage, target->address);
        }
        if (v22_hook_thunk_cursor_ + 8u > kV22ThunkBase + kPageSize)
            return Fail("V22 companion hook thunk page is full");
        const u32 thunk = v22_hook_thunk_cursor_;
        v22_hook_thunk_cursor_ += 8u;
        const auto counts = RedirectV22FunctionReferences(
            runtime_, env_, *target, replacement, thunk);
        ++v22_companion_hooks_installed_;
        log_ << "RESULT: DYNARMIC_V22_COMPANION_HOOK target="
             << target_name << " replacement=0x" << std::hex << replacement
             << " original=0x" << target->address
             << " storage=0x" << original_storage << std::dec
             << " pointers=" << counts.first << " calls=" << counts.second
             << " hook=" << v22_companion_hooks_installed_ << '\n';
        log_.flush();
        return true;
    }

    bool HostV22LevelSettingsFromString(u32& result) {
        result = 0u;
        if (!runtime_.v22_level_settings_from_string)
            return Fail("V22 LevelSettingsObject parser target is unavailable");
        const u32 original_object = cpu_.Regs()[0];
        std::string original;
        if (!ReadGuestCowStringObject(original_object, original, 4u * 1024u * 1024u))
            return Fail("V22 level settings argument is not a valid guest std::string");
        if (!RunNestedPreservingState(runtime_.v22_level_settings_from_string,
                                      {original_object}, result,
                                      "V22 LevelSettingsObject::objectFromString"))
            return false;
        if (result) {
            ++v22_level_settings_native_successes_;
            return true;
        }

        ++v22_level_settings_native_failures_;
        std::string sanitized = SanitizeV22LevelSettingsHeader(original);
        const char* mode = "strip-kS38";
        if (sanitized.empty()) {
            sanitized =
                "kA13,0,kA15,0,kA16,0,kA14,,kA6,0,kA7,0,kA25,0,"
                "kA17,1,kA18,0,kS39,0,kA2,0,kA3,0,kA4,0,kA8,0,kA10,0";
            mode = "minimal-default";
        }
        const u32 temporary = Allocate(4u);
        if (!temporary) return Fail("V22 could not allocate fallback settings string");
        env_.MemoryWrite32(temporary, runtime_.v22_empty_string_data);
        if (!BuildGuestStringFromBytes(temporary, sanitized))
            return Fail("V22 could not build sanitized level settings string");
        u32 retry = 0u;
        if (!RunNestedPreservingState(runtime_.v22_level_settings_from_string,
                                      {temporary}, retry,
                                      "V22 sanitized LevelSettingsObject::objectFromString"))
            return false;
        if (!retry && std::string(mode) != "minimal-default") {
            const std::string minimal =
                "kA13,0,kA15,0,kA16,0,kA14,,kA6,0,kA7,0,kA25,0,"
                "kA17,1,kA18,0,kS39,0,kA2,0,kA3,0,kA4,0,kA8,0,kA10,0";
            const u32 second = Allocate(4u);
            if (!second) return Fail("V22 could not allocate minimal settings string");
            env_.MemoryWrite32(second, runtime_.v22_empty_string_data);
            if (!BuildGuestStringFromBytes(second, minimal))
                return Fail("V22 could not build minimal settings string");
            if (!RunNestedPreservingState(
                    runtime_.v22_level_settings_from_string, {second}, retry,
                    "V22 minimal LevelSettingsObject::objectFromString"))
                return false;
            mode = "minimal-default";
            sanitized = minimal;
        }
        if (!retry && runtime_.v22_level_settings_create) {
            if (!RunNestedPreservingState(
                    runtime_.v22_level_settings_create, {}, retry,
                    "V22 default LevelSettingsObject::create"))
                return false;
            mode = "default-object";
            sanitized.clear();
        }
        if (!retry) {
            log_ << "ERROR: V22 level settings fallback rejected original="
                 << original.size() << " sanitized=" << sanitized.size() << '\n';
            log_.flush();
            result = 0u;
            return true;
        }
        result = retry;
        ++v22_level_settings_fallback_successes_;
        log_ << "WARNING: V22 level settings parser fallback mode=" << mode
             << " original=" << original.size()
             << " sanitized=" << sanitized.size()
             << " object=0x" << std::hex << result << std::dec
             << " fallback=" << v22_level_settings_fallback_successes_ << '\n';
        log_.flush();
        return true;
    }

    bool IsV22GameLevelObject(u32 object) const {
        if (!LooksLikeGuestObject(runtime_, env_, object))
            return false;
        if (!runtime_.v22_game_level_vtable) return true;
        const u32 vtable = env_.MemoryRead32(object);
        // Itanium C++ ABI objects point eight bytes into the vtable (past the
        // offset-to-top and typeinfo words). Keep a tiny allowance for beta
        // toolchain variants while rejecting unrelated mapped objects.
        return vtable >= runtime_.v22_game_level_vtable + 8u &&
               vtable <= runtime_.v22_game_level_vtable + 16u;
    }

    u32 FindV22PlayLayerGameLevel(u32 play_layer, s32& level_id,
                                 u32& field_offset) const {
        level_id = 0;
        field_offset = 0u;
        if (!play_layer || !runtime_.v22_game_level_id_offset)
            return 0u;

        u32 first_level = 0u;
        s32 first_id = 0;
        u32 first_offset = 0u;
        const auto inspect = [&](u32 offset) -> u32 {
            if (!env_.IsMapped(play_layer + offset, 4u)) return 0u;
            const u32 candidate = env_.MemoryRead32(play_layer + offset);
            if (!IsV22GameLevelObject(candidate) ||
                !env_.IsMapped(
                    candidate + runtime_.v22_game_level_id_offset, 4u))
                return 0u;
            const s32 candidate_id = static_cast<s32>(env_.MemoryRead32(
                candidate + runtime_.v22_game_level_id_offset));
            if (!first_level) {
                first_level = candidate;
                first_id = candidate_id;
                first_offset = offset;
            }
            if (v22_level_data_encoded_.contains(candidate_id) ||
                v22_level_data_decoded_.contains(candidate_id)) {
                level_id = candidate_id;
                field_offset = offset;
                return candidate;
            }
            return 0u;
        };

        if (runtime_.v22_play_layer_level_offset) {
            if (const u32 exact =
                    inspect(runtime_.v22_play_layer_level_offset))
                return exact;
        }
        // The two known beta layouts keep their GJGameLevel pointer at 0x284
        // and 0x334 respectively. A vtable-checked field scan makes this
        // resilient to nearby 2019 beta layouts without guessing by APK name.
        for (u32 offset = 0x100u; offset <= 0x500u; offset += 4u) {
            if (offset == runtime_.v22_play_layer_level_offset) continue;
            if (const u32 scanned = inspect(offset)) return scanned;
        }
        level_id = first_id;
        field_offset = first_offset;
        return first_level;
    }

    static s32 OfficialLevelIdForMusicPath(std::string path) {
        std::transform(path.begin(), path.end(), path.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        static constexpr std::array<std::pair<std::string_view, s32>, 21>
            kOfficialSongs = {{
                {"stereomadness.mp3", 1},
                {"backontrack.mp3", 2},
                {"polargeist.mp3", 3},
                {"dryout.mp3", 4},
                {"baseafterbase.mp3", 5},
                {"cantletgo.mp3", 6},
                {"jumper.mp3", 7},
                {"timemachine.mp3", 8},
                {"cycles.mp3", 9},
                {"xstep.mp3", 10},
                {"clutterfunk.mp3", 11},
                {"theoryofeverything.mp3", 12},
                {"electroman.mp3", 13},
                {"clubstep.mp3", 14},
                {"electrodynamix.mp3", 15},
                {"hexagonforce.mp3", 16},
                {"blastprocessing.mp3", 17},
                {"theoryofeverything2.mp3", 18},
                {"geometricaldominator.mp3", 19},
                {"deadlocked.mp3", 20},
                {"fingerdash.mp3", 21},
            }};
        for (const auto& [suffix, level_id] : kOfficialSongs) {
            if (path.ends_with(suffix)) return level_id;
        }
        return 0;
    }

    bool HostV22PrepareLevelSetup() {
        if (!runtime_.v22_prepare_setup_address)
            return Fail("V22 PlayLayer setup target is unavailable");
        // The old ARM libstdc++ ABI passes this by-value std::string as a
        // pointer to the caller-owned four-byte COW object (r5 in PlayLayer::init),
        // not as the character-data pointer itself. Repair that exact temporary
        // in place so the caller's existing destructor owns the new value.
        const u32 string_object = cpu_.Regs()[1];
        if (!string_object || !env_.IsMapped(string_object, 4u))
            return Fail("V22 PlayLayer setup std::string object is invalid");

        s32 level_id = 0;
        u32 level_field_offset = 0u;
        const u32 play_layer = cpu_.Regs()[0];
        const u32 level = FindV22PlayLayerGameLevel(
            play_layer, level_id, level_field_offset);
        if (level && level_field_offset &&
            level_field_offset != runtime_.v22_play_layer_level_offset) {
            log_ << "WARNING: V22 PlayLayer GJGameLevel field corrected from "
                 << runtime_.v22_play_layer_level_offset << " to "
                 << level_field_offset << " using vtable validation\n";
            log_.flush();
        }

        std::string current;
        const bool current_valid =
            ReadGuestCowStringObject(string_object, current);
        // A valid non-empty setup belongs to the game. Bringup11 replaced it
        // whenever it differed from LevelData.plist, which could overwrite a
        // correctly transformed runtime string and regress levels that already
        // worked in Bringup9. The catalog and inflate cache are recovery
        // sources only; they must never be authoritative over usable guest
        // state.
        if (!current_valid || current.empty()) {
            const std::string* recovery =
                GetV22OfficialLevelSetup(level_id);
            const char* source = "apk-catalog";
            const s32 music_level_id =
                OfficialLevelIdForMusicPath(v22_recent_music_path_);
            if (!recovery && music_level_id &&
                music_level_id != level_id) {
                recovery = GetV22OfficialLevelSetup(music_level_id);
                if (recovery) {
                    level_id = music_level_id;
                    source = "apk-catalog-music";
                }
            }
            if (!recovery && v22_pending_level_setup_) {
                recovery = &*v22_pending_level_setup_;
                source = "latest-inflate";
            }
            if (!recovery) {
                // An optional recovery bridge must not terminate a beta which
                // can handle its own empty/local level state. Forward valid
                // guest strings untouched; only repair an invalid COW object
                // into a parseable empty setup before resuming native code.
                if (!current_valid) {
                    static const std::string kEmptySetup =
                        "kS38,kA13,0,kA15,0,kA16,0,kA14,,kA6,0,kA7,0,"
                        "kA25,0,kA17,1,kA18,0,kS39,0,kA2,0,kA3,0,"
                        "kA4,0,kA8,0,kA10,0;";
                    if (!BuildGuestStringFromBytes(
                            string_object, kEmptySetup))
                        return Fail(
                            "V22 could not repair an invalid empty level setup");
                }
                ++v22_level_setup_passthroughs_;
                log_ << "WARNING: V22 level setup recovery unavailable "
                     << "level=" << level_id
                     << " field=" << level_field_offset
                     << " music=\"" << SanitizeLogText(v22_recent_music_path_)
                     << "\" action=native-passthrough count="
                     << v22_level_setup_passthroughs_ << '\n';
                log_.flush();
                v22_pending_level_setup_.reset();
                cpu_.Regs()[15] =
                    runtime_.v22_prepare_setup_address & ~1u;
                cpu_.SetCpsr(
                    (cpu_.Cpsr() & ~0x20u) |
                    ((runtime_.v22_prepare_setup_address & 1u)
                         ? 0x20u : 0u));
                return true;
            }
            const std::size_t previous_size =
                current_valid ? current.size() : 0u;
            if (!BuildGuestStringFromBytes(string_object, *recovery))
                return Fail("V22 failed to rebuild the level setup std::string argument");
            ++v22_level_setup_repairs_;
            log_ << "[host] V22 PlayLayer setup recovered bytes="
                 << recovery->size() << " previous=" << previous_size
                 << " level=" << level_id << " source=" << source
                 << " object=0x" << std::hex << string_object << std::dec
                 << " recovery=" << v22_level_setup_repairs_ << '\n';
            log_.flush();
        }
        v22_pending_level_setup_.reset();
        cpu_.Regs()[15] = runtime_.v22_prepare_setup_address & ~1u;
        cpu_.SetCpsr((cpu_.Cpsr() & ~0x20u) |
                     ((runtime_.v22_prepare_setup_address & 1u) ? 0x20u : 0u));
        return true;
    }

    struct V22EditorLayout {
        const char* name = "none";
        u32 level_field = 0u;
        u32 level_settings_field = 0u;
        u32 object_layer_field = 0u;
        u32 draw_grid_field = 0u;
        u32 editor_ui_field = 0u;
        u32 obb_field = 0u;
        u32 point_buffer_field = 0u;
        u32 arrow_field = 0u;
        u32 setup_cache_field = 0u;
        u32 manager_layer_field = 0u;
        u32 manager_flag_field = 0u;
        u32 level_setup_hint = 0u;
        u32 vector_capacity = 0u;
        bool vectors_are_resized = false;
        bool late_background_api = false;
        std::vector<u32> array_fields;
        std::vector<u32> dictionary_fields;
    };

    static V22EditorLayout V22EditorLayoutFor(
        V22EditorRestoreProfile profile) {
        V22EditorLayout layout{};
        switch (profile) {
        case V22EditorRestoreProfile::Early2019:
            layout.name = "early2019-9144004";
            layout.level_field = 0x4ECu;
            layout.level_settings_field = 0x28Cu;
            layout.object_layer_field = 0x14Cu;
            layout.draw_grid_field = 0x4E8u;
            layout.editor_ui_field = 0x4C4u;
            layout.obb_field = 0x4A0u;
            layout.point_buffer_field = 0x50Cu;
            layout.arrow_field = 0x4A4u;
            layout.setup_cache_field = 0x508u;
            layout.manager_layer_field = 0x15Cu;
            layout.manager_flag_field = 0x1AAu;
            // Verified in the stock 9,144,004-byte PlayLayer::init: it loads
            // PlayLayer+0x668 (GJGameLevel*), adds 0x110, copies that std::string
            // and passes it to ZipUtils::decompressString(..., false, 11).
            layout.level_setup_hint = 0x110u;
            layout.vector_capacity = 0x44Du;
            layout.vectors_are_resized = true;
            layout.late_background_api = false;
            // These fields are mapped from the last full pre-stub editor
            // initializer by destructor order and matching class methods.
            layout.array_fields = {
                0x418u, 0x41Cu, 0x428u, 0x5D0u, 0x42Cu,
                0x430u, 0x434u, 0x440u, 0x448u, 0x47Cu,
                0x44Cu, 0x458u, 0x43Cu, 0x4CCu, 0x4D0u};
            layout.dictionary_fields = {
                0x420u, 0x474u, 0x484u, 0x488u,
                0x438u, 0x454u, 0x4FCu};
            break;
        case V22EditorRestoreProfile::Late2022:
            layout.name = "late2022-9541500";
            layout.level_field = 0x138u;
            layout.level_settings_field = 0x338u;
            layout.object_layer_field = 0x470u;
            layout.draw_grid_field = 0x2C54u;
            layout.editor_ui_field = 0x2C3Cu;
            layout.obb_field = 0x2C24u;
            layout.point_buffer_field = 0x2C64u;
            layout.arrow_field = 0x2C2Cu;
            layout.setup_cache_field = 0x2C60u;
            layout.manager_layer_field = 0x16Cu;
            layout.manager_flag_field = 0x1BAu;
            layout.level_setup_hint = 0x118u;
            layout.vector_capacity = 0x270Fu;
            layout.vectors_are_resized = true;
            layout.late_background_api = true;
            layout.array_fields = {
                0x2BCCu, 0x2BC4u, 0x2BBCu, 0x2BB8u,
                0x2BB0u, 0x2BB4u, 0x2BD4u, 0x2BACu,
                0x350u, 0x34Cu, 0x348u, 0x2C08u, 0x2C1Cu,
                0x2BC8u, 0x2BC0u, 0x2C00u, 0x344u, 0x340u,
                0x2C40u, 0x2C44u, 0x2D18u, 0x2BD0u};
            layout.dictionary_fields = {0x450u, 0x43Cu, 0x2C80u};
            break;
        case V22EditorRestoreProfile::Late2023:
            layout.name = "late2023-9578364";
            layout.level_field = 0x13Cu;
            layout.level_settings_field = 0x33Cu;
            layout.object_layer_field = 0x48Cu;
            layout.draw_grid_field = 0x2C88u;
            layout.editor_ui_field = 0x2C70u;
            layout.obb_field = 0x2C58u;
            layout.point_buffer_field = 0x2C98u;
            layout.arrow_field = 0x2C60u;
            layout.setup_cache_field = 0x2C94u;
            layout.manager_layer_field = 0x16Cu;
            layout.manager_flag_field = 0x1BAu;
            layout.level_setup_hint = 0x11Cu;
            layout.vector_capacity = 0x270Fu;
            layout.vectors_are_resized = true;
            layout.late_background_api = true;
            layout.array_fields = {
                0x2C00u, 0x2BF8u, 0x2BF0u, 0x2BECu,
                0x2BE4u, 0x2BE8u, 0x2C08u, 0x2BE0u,
                0x354u, 0x350u, 0x34Cu, 0x2C3Cu, 0x2C50u,
                0x2BFCu, 0x2BF4u, 0x2C34u, 0x348u, 0x344u,
                0x2C74u, 0x2C78u, 0x2D50u, 0x2C04u};
            layout.dictionary_fields = {0x46Cu, 0x43Cu, 0x2CB4u};
            break;
        default:
            break;
        }
        return layout;
    }

    u32 V22PrimarySymbolAddress(const char* name) const {
        if (!name || !*name) return 0u;
        for (const SymbolRecord& symbol : runtime_.symbols) {
            if (symbol.name == name &&
                symbol.address >= runtime_.image_min &&
                symbol.address < runtime_.image_max)
                return symbol.address;
        }
        return 0u;
    }

    bool CallV22Primary(const char* name, const std::vector<u32>& args,
                        u32& result, std::string_view label,
                        bool required = true,
                        u64 ticks = 500000000u) {
        const u32 address = V22PrimarySymbolAddress(name);
        if (!address) {
            if (!required) return true;
            return Fail(std::string("V22 editor restore missing symbol ") + name);
        }
        return RunNestedPreservingState(address, args, result,
                                        std::string(label), ticks);
    }

    bool RetainV22Object(u32 object, std::string_view label) {
        if (!object) return false;
        u32 ignored = 0u;
        return CallV22Primary("_ZN7cocos2d8CCObject6retainEv", {object},
                              ignored, label);
    }

    bool CreateRetainedV22Field(u32 editor, u32 field,
                                const char* create_symbol,
                                std::string_view label,
                                u32 create_argument = 0u,
                                bool has_argument = false) {
        if (!field || !env_.IsMapped(editor + field, 4u))
            return Fail("V22 editor restore field is outside LevelEditorLayer");
        const u32 existing = env_.MemoryRead32(editor + field);
        if (LooksLikeGuestObject(runtime_, env_, existing)) return true;
        u32 object = 0u;
        const std::vector<u32> args = has_argument
            ? std::vector<u32>{create_argument} : std::vector<u32>{};
        if (!CallV22Primary(create_symbol, args, object, label) || !object)
            return Fail(std::string(label) + " returned null");
        env_.MemoryWrite32(editor + field, object);
        return RetainV22Object(object, std::string(label) + " retain");
    }

    bool InitV22StdVector(u32 editor, u32 field, u32 count,
                          u32 element_size, bool resized) {
        if (!field || !count || !element_size ||
            !env_.IsMapped(editor + field, 12u))
            return false;
        if (env_.MemoryRead32(editor + field)) return true;
        const u64 byte_count64 = static_cast<u64>(count) * element_size;
        if (byte_count64 > std::numeric_limits<u32>::max()) return false;
        const u32 byte_count = static_cast<u32>(byte_count64);
        const u32 memory = AllocateAligned(byte_count, 16u);
        if (!memory) return false;
        if (void* host = env_.HostPointer(memory, byte_count))
            std::memset(host, 0, byte_count);
        else
            return false;
        env_.MemoryWrite32(editor + field, memory);
        env_.MemoryWrite32(editor + field + 4u,
                           resized ? memory + byte_count : memory);
        env_.MemoryWrite32(editor + field + 8u, memory + byte_count);
        return true;
    }

    bool InitV22BoolVector(u32 editor, u32 field, u32 count, bool resized) {
        if (!field || !count || !env_.IsMapped(editor + field, 20u))
            return false;
        if (env_.MemoryRead32(editor + field)) return true;
        const u32 words = (count + 31u) / 32u;
        const u32 bytes = words * 4u;
        const u32 memory = AllocateAligned(bytes, 16u);
        if (!memory) return false;
        if (void* host = env_.HostPointer(memory, bytes))
            std::memset(host, 0, bytes);
        else
            return false;
        env_.MemoryWrite32(editor + field, memory);
        env_.MemoryWrite32(editor + field + 4u, 0u);
        if (resized) {
            env_.MemoryWrite32(editor + field + 8u,
                               memory + (count / 32u) * 4u);
            env_.MemoryWrite32(editor + field + 12u, count % 32u);
        } else {
            env_.MemoryWrite32(editor + field + 8u, memory);
            env_.MemoryWrite32(editor + field + 12u, 0u);
        }
        env_.MemoryWrite32(editor + field + 16u, memory + bytes);
        return true;
    }

    bool InitV22EditorVectors(u32 editor, const V22EditorLayout& layout) {
        const u32 count = layout.vector_capacity;
        if (!count) return false;
        const bool resized = layout.vectors_are_resized;
        if (runtime_.v22_wrapper_editor_profile ==
            V22EditorRestoreProfile::Early2019) {
            // The early beta retained the older 1101-entry editor tables.
            return
                InitV22StdVector(editor, 0x510u, count, 4u, resized) &&
                InitV22StdVector(editor, 0x51Cu, count, 4u, resized) &&
                InitV22StdVector(editor, 0x528u, count, 4u, resized) &&
                InitV22StdVector(editor, 0x538u, count, 4u, resized) &&
                InitV22BoolVector(editor, 0x548u, count, resized) &&
                InitV22BoolVector(editor, 0x55Cu, count, resized) &&
                InitV22BoolVector(editor, 0x570u, count, resized) &&
                InitV22BoolVector(editor, 0x584u, count, resized) &&
                InitV22BoolVector(editor, 0x598u, count, resized) &&
                InitV22StdVector(editor, 0x5ACu, count, 1u, resized) &&
                InitV22StdVector(editor, 0x5B8u, count, 4u, resized);
        }
        const u32 delta = runtime_.v22_wrapper_editor_profile ==
                                  V22EditorRestoreProfile::Late2022
                              ? 0x34u : 0u;
        const auto field = [delta](u32 late23) { return late23 - delta; };
        return
            InitV22StdVector(editor, field(0x2CDCu), count, 1u, resized) &&
            InitV22StdVector(editor, field(0x2D2Cu), count, 1u, resized) &&
            InitV22BoolVector(editor, field(0x2D18u), count, resized) &&
            InitV22StdVector(editor, field(0x2D38u), count, 4u, resized) &&
            InitV22BoolVector(editor, field(0x2CC8u), count, resized) &&
            InitV22StdVector(editor, field(0x2C9Cu), count, 4u, resized) &&
            InitV22BoolVector(editor, field(0x2CF0u), count, resized) &&
            InitV22BoolVector(editor, field(0x2D04u), count, resized) &&
            InitV22StdVector(editor, field(0x2CB8u), count, 4u, resized) &&
            InitV22StdVector(editor, field(0x2B9Cu), count, 4u, resized);
    }

    bool FindV22EditorLevelSetup(u32 level, const V22EditorLayout& layout,
                                 u32& string_object,
                                 std::string& encoded) {
        string_object = 0u;
        encoded.clear();
        if (layout.level_setup_hint &&
            ReadGuestCowStringObject(level + layout.level_setup_hint, encoded,
                                     32u * 1024u * 1024u) &&
            !encoded.empty()) {
            string_object = level + layout.level_setup_hint;
            return true;
        }

        // The early beta moved GJGameLevel fields substantially. Find the
        // compressed setup by COW-string validity and content/length rather
        // than assigning a guessed early offset. The level setup is by far the
        // largest string in normal GJGameLevel objects.
        std::size_t best_score = 0u;
        for (u32 offset = 0x80u; offset <= 0x500u; offset += 4u) {
            std::string candidate;
            if (!ReadGuestCowStringObject(level + offset, candidate,
                                          32u * 1024u * 1024u))
                continue;
            if (candidate.empty()) continue;
            std::size_t score = candidate.size();
            if (candidate.find("kS38") != std::string::npos) score += 1u << 22;
            if (candidate.find(';') != std::string::npos) score += 1u << 20;
            if (candidate.size() > 128u) score += 1u << 18;
            if (score <= best_score) continue;
            best_score = score;
            encoded = std::move(candidate);
            string_object = level + offset;
        }
        if (string_object) return true;

        if (runtime_.v22_game_level_id_offset &&
            env_.IsMapped(level + runtime_.v22_game_level_id_offset, 4u)) {
            const s32 level_id = static_cast<s32>(env_.MemoryRead32(
                level + runtime_.v22_game_level_id_offset));
            if (const auto found = v22_level_data_encoded_.find(level_id);
                found != v22_level_data_encoded_.end()) {
                encoded = found->second;
                const u32 temporary = Allocate(4u);
                if (!temporary) return false;
                env_.MemoryWrite32(temporary, runtime_.v22_empty_string_data);
                if (!BuildGuestStringFromBytes(temporary, encoded)) return false;
                string_object = temporary;
                return true;
            }
            if (const auto found = v22_level_data_decoded_.find(level_id);
                found != v22_level_data_decoded_.end()) {
                encoded = found->second;
                string_object = 0u; // already decoded; handled by caller
                return true;
            }
        }
        if (v22_pending_level_setup_) {
            encoded = *v22_pending_level_setup_;
            string_object = 0u;
            return true;
        }
        return false;
    }

    bool DecodeV22EditorLevelSetup(u32 level, const V22EditorLayout& layout,
                                   std::string& decoded) {
        decoded.clear();
        u32 source_object = 0u;
        std::string source;
        if (!FindV22EditorLevelSetup(level, layout, source_object, source)) {
            decoded =
                "kS38,kA13,0,kA15,0,kA16,0,kA14,,kA6,0,kA7,0,"
                "kA25,0,kA17,1,kA18,0,kS39,0,kA2,0,kA3,0,"
                "kA4,0,kA8,0,kA10,0;";
            return true;
        }
        if (!source_object || source.find("kS38") != std::string::npos ||
            source.find("kA13,") != std::string::npos) {
            decoded = source;
            return true;
        }
        const u32 unzip = V22PrimarySymbolAddress(
            "_ZN7cocos2d8ZipUtils16decompressStringESsbi");
        if (!unzip) return Fail("V22 editor restore ZipUtils symbol missing");
        const u32 destination = Allocate(4u);
        if (!destination) return Fail("V22 editor restore string allocation failed");
        env_.MemoryWrite32(destination, runtime_.v22_empty_string_data);
        u32 ignored = 0u;
        if (!RunNestedPreservingState(unzip, {destination, source_object, 0u, 11u},
                                      ignored,
                                      "V22 editor restore decompress level",
                                      1000000000u))
            return false;
        if (!ReadGuestCowStringObject(destination, decoded,
                                      64u * 1024u * 1024u) ||
            decoded.empty()) {
            // Never feed a failed compressed/base64 payload to
            // createObjectsFromSetup(). tweaks5 did that on the 2019 stock
            // beta after selecting the wrong heuristic string and it eventually
            // crashed in std::string cleanup. A genuinely pre-decoded community
            // setup still has the normal comma/semicolon object syntax.
            const bool looks_decoded =
                source.find(',') != std::string::npos &&
                source.find(';') != std::string::npos;
            if (looks_decoded) {
                decoded = source;
            } else {
                log_ << "RESULT: DYNARMIC_V22_EDITOR_SETUP_DECODE_FAILED profile="
                     << V22EditorRestoreProfileName(
                            runtime_.v22_wrapper_editor_profile)
                     << " source-bytes=" << source.size()
                     << " action=safe-empty-setup\n";
                log_.flush();
                decoded =
                    "kS38,kA13,0,kA15,0,kA16,0,kA14,,kA6,0,kA7,0,"
                    "kA25,0,kA17,1,kA18,0,kS39,0,kA2,0,kA3,0,"
                    "kA4,0,kA8,0,kA10,0;";
            }
        }
        return true;
    }

    bool InitializeV22EditorCollections(u32 editor,
                                        const V22EditorLayout& layout) {
        for (u32 field : layout.array_fields) {
            const bool capacity_array =
                runtime_.v22_wrapper_editor_profile ==
                    V22EditorRestoreProfile::Early2019 && field == 0x44Cu;
            if (!CreateRetainedV22Field(
                    editor, field,
                    capacity_array
                        ? "_ZN7cocos2d7CCArray18createWithCapacityEj"
                        : "_ZN7cocos2d7CCArray6createEv",
                    "V22 wrapper editor CCArray",
                    100u, capacity_array))
                return false;
        }
        for (u32 field : layout.dictionary_fields) {
            if (!CreateRetainedV22Field(
                    editor, field, "_ZN7cocos2d12CCDictionary6createEv",
                    "V22 wrapper editor CCDictionary"))
                return false;
        }
        return InitV22EditorVectors(editor, layout);
    }

    bool InitializeV22EditorFromWrapper(u32 editor, u32 level,
                                        std::string_view source) {
        const V22EditorLayout layout =
            V22EditorLayoutFor(runtime_.v22_wrapper_editor_profile);
        if (!layout.level_field || !layout.level_settings_field ||
            !layout.object_layer_field || !layout.editor_ui_field)
            return Fail("V22 wrapper editor restore has no active layout");
        if (!env_.IsMapped(editor + layout.point_buffer_field, 4u) ||
            !env_.IsMapped(editor + layout.editor_ui_field, 4u))
            return Fail("V22 wrapper editor layout exceeds allocated object");

        u32 ignored = 0u;
        u32 game_manager = 0u;
        if (!CallV22Primary("_ZN11GameManager11sharedStateEv", {}, game_manager,
                            "V22 wrapper editor GameManager") ||
            !game_manager)
            return false;
        if (env_.IsMapped(game_manager + layout.manager_flag_field, 1u))
            env_.MemoryWrite8(game_manager + layout.manager_flag_field, 1u);
        if (env_.IsMapped(game_manager + layout.manager_layer_field, 4u))
            env_.MemoryWrite32(game_manager + layout.manager_layer_field, editor);

        // The late stock constructors clear this byte before calling the
        // intentionally stubbed init. The 2023 restoration donor sets it back
        // to true at the start of the real editor init, and the 2022 layout
        // uses the same field in its stock editor methods. Keep this narrowly
        // scoped to the two late layouts; the 2019 ABI is unrelated.
        if ((runtime_.v22_wrapper_editor_profile ==
                 V22EditorRestoreProfile::Late2022 ||
             runtime_.v22_wrapper_editor_profile ==
                 V22EditorRestoreProfile::Late2023) &&
            env_.IsMapped(editor + 0x2780u, 1u))
            env_.MemoryWrite8(editor + 0x2780u, 1u);

        if (!CallV22Primary("_ZN16LevelEditorLayer14setObjectCountEi",
                            {editor, 0u}, ignored,
                            "V22 wrapper editor setObjectCount"))
            return false;
        // updateOptions owns the version-specific GameManager preference fields
        // and is much safer than transplanting the mod's raw option offsets.
        if (!CallV22Primary("_ZN16LevelEditorLayer13updateOptionsEv",
                            {editor}, ignored,
                            "V22 wrapper editor updateOptions"))
            return false;

        env_.MemoryWrite32(editor + layout.level_field, level);
        if (!RetainV22Object(level, "V22 wrapper editor retain level"))
            return false;
        if (!RetainV22Object(editor, "V22 wrapper editor retain layer"))
            return false;

        u32 sound_manager = 0u;
        if (CallV22Primary("_ZN16GameSoundManager13sharedManagerEv", {},
                           sound_manager,
                           "V22 wrapper editor sound manager", false) &&
            sound_manager) {
            CallV22Primary("_ZN16GameSoundManager19stopBackgroundMusicEv",
                           {sound_manager}, ignored,
                           "V22 wrapper editor stop background music", false);
        }

        if (!InitializeV22EditorCollections(editor, layout))
            return Fail("V22 wrapper editor collection initialization failed");

        // OBB2D::create(CCPoint(1,1),1,1,0). The ARM ABI places the two
        // CCPoint floats in r0/r1, the next two floats in r2/r3 and the last
        // float on the stack. Selection still works if a particular beta
        // rejects this optional helper, so leave the field null in that case.
        u32 obb = 0u;
        if (CallV22Primary("_ZN5OBB2D6createEN7cocos2d7CCPointEfff",
                           {0x3F800000u, 0x3F800000u, 0x3F800000u,
                            0x3F800000u, 0u},
                           obb, "V22 wrapper editor OBB", false) && obb) {
            env_.MemoryWrite32(editor + layout.obb_field, obb);
            RetainV22Object(obb, "V22 wrapper editor retain OBB");
        }

        const u32 point_bytes =
            runtime_.v22_wrapper_editor_profile ==
                    V22EditorRestoreProfile::Early2019
                ? 0xC80u : 0x3200u;
        if (!env_.MemoryRead32(editor + layout.point_buffer_field)) {
            const u32 points = AllocateAligned(point_bytes, 16u);
            if (!points) return Fail("V22 wrapper editor point buffer failed");
            if (void* host = env_.HostPointer(points, point_bytes))
                std::memset(host, 0, point_bytes);
            env_.MemoryWrite32(editor + layout.point_buffer_field, points);
        }

        if (!CallV22Primary("_ZN15GJBaseGameLayer11setupLayersEv",
                            {editor}, ignored,
                            "V22 wrapper editor setupLayers", true,
                            2000000000u))
            return false;
        const u32 object_layer =
            env_.MemoryRead32(editor + layout.object_layer_field);
        if (!LooksLikeGuestObject(runtime_, env_, object_layer))
            return Fail("V22 wrapper editor object layer was not created");

        u32 draw_grid = 0u;
        if (!CallV22Primary(
                "_ZN13DrawGridLayer6createEPN7cocos2d6CCNodeEP16LevelEditorLayer",
                {object_layer, editor}, draw_grid,
                "V22 wrapper editor DrawGridLayer::create") ||
            !draw_grid)
            return false;
        env_.MemoryWrite32(editor + layout.draw_grid_field, draw_grid);
        const u32 add_child_z = V22PrimarySymbolAddress(
            "_ZN7cocos2d6CCNode8addChildEPS0_i");
        if (add_child_z &&
            !RunNestedPreservingState(add_child_z,
                                      {object_layer, draw_grid,
                                       static_cast<u32>(-100)},
                                      ignored,
                                      "V22 wrapper editor add grid"))
            return false;

        if (runtime_.v22_wrapper_editor_profile ==
            V22EditorRestoreProfile::Early2019) {
            const u32 player_create = V22PrimarySymbolAddress(
                "_ZN12PlayerObject6createEiiPN7cocos2d7CCLayerE");
            if (!player_create)
                return Fail("V22 early editor PlayerObject::create is missing");
            for (u32 player_field : {0x284u, 0x288u}) {
                u32 player = 0u;
                if (!RunNestedPreservingState(
                        player_create, {1u, 1u, object_layer}, player,
                        "V22 early editor PlayerObject::create",
                        1000000000u) || !player)
                    return false;
                env_.MemoryWrite32(editor + player_field, player);
                if (add_child_z &&
                    !RunNestedPreservingState(
                        add_child_z, {object_layer, player, 10u}, ignored,
                        "V22 early editor add player"))
                    return false;
            }
        } else if (!CallV22Primary(
                       "_ZN15GJBaseGameLayer12createPlayerEv",
                       {editor}, ignored,
                       "V22 wrapper editor createPlayer", true,
                       1500000000u)) {
            return false;
        }
        if (!CallV22Primary("_ZN15GJBaseGameLayer26createPlayerCollisionBlockEv",
                            {editor}, ignored,
                            "V22 wrapper editor collision block", false))
            return false;
        if (!CallV22Primary("_ZN16LevelEditorLayer23addPlayerCollisionBlockEv",
                            {editor}, ignored,
                            "V22 wrapper editor add collision block", false))
            return false;

        std::string decoded_setup;
        if (!DecodeV22EditorLevelSetup(level, layout, decoded_setup))
            return false;
        const u32 setup_object = Allocate(4u);
        if (!setup_object) return Fail("V22 wrapper editor setup object allocation failed");
        env_.MemoryWrite32(setup_object, runtime_.v22_empty_string_data);
        if (!BuildGuestStringFromBytes(setup_object, decoded_setup))
            return false;
        if (layout.setup_cache_field &&
            env_.IsMapped(editor + layout.setup_cache_field, 4u)) {
            // The early ABI embeds std::string inline. The late bool ABI stores
            // a pointer to a separately allocated std::string object. Never
            // store the COW character-data pointer itself: teardown/save code
            // dereferences this field as a C++ string object.
            if (runtime_.v22_wrapper_editor_profile ==
                V22EditorRestoreProfile::Early2019) {
                env_.MemoryWrite32(editor + layout.setup_cache_field,
                                   runtime_.v22_empty_string_data);
                if (!BuildGuestStringFromBytes(
                        editor + layout.setup_cache_field, decoded_setup))
                    return false;
            } else {
                const u32 cached_string = Allocate(4u);
                if (!cached_string)
                    return Fail("V22 wrapper editor cached string allocation failed");
                env_.MemoryWrite32(cached_string, runtime_.v22_empty_string_data);
                if (!BuildGuestStringFromBytes(cached_string, decoded_setup))
                    return false;
                env_.MemoryWrite32(editor + layout.setup_cache_field,
                                   cached_string);
            }
        }
        if (!CallV22Primary("_ZN16LevelEditorLayer22createObjectsFromSetupESs",
                            {editor, setup_object}, ignored,
                            "V22 wrapper editor createObjectsFromSetup", true,
                            3000000000u))
            return false;
        if (!CallV22Primary("_ZN15GJBaseGameLayer16createTextLayersEv",
                            {editor}, ignored,
                            "V22 wrapper editor createTextLayers", false,
                            1500000000u))
            return false;

        u32 level_settings =
            env_.MemoryRead32(editor + layout.level_settings_field);
        if (!LooksLikeGuestObject(runtime_, env_, level_settings)) {
            if (!CallV22Primary("_ZN19LevelSettingsObject6createEv", {},
                                level_settings,
                                "V22 wrapper editor LevelSettingsObject") ||
                !level_settings)
                return false;
            env_.MemoryWrite32(editor + layout.level_settings_field,
                               level_settings);
            // GJGameLevel pointer inside LevelSettingsObject moved from +0x114
            // in the old ABI to +0x138 in the late bool ABI.
            const u32 settings_level_field =
                runtime_.v22_wrapper_editor_profile ==
                        V22EditorRestoreProfile::Early2019
                    ? 0x114u : 0x138u;
            if (env_.IsMapped(level_settings + settings_level_field, 4u))
                env_.MemoryWrite32(level_settings + settings_level_field, level);
            if (!RetainV22Object(level_settings,
                                 "V22 wrapper editor retain LevelSettingsObject"))
                return false;
        }

        u32 editor_ui = 0u;
        if (!CallV22Primary("_ZN8EditorUI6createEP16LevelEditorLayer",
                            {editor}, editor_ui,
                            "V22 wrapper editor EditorUI::create", true,
                            2000000000u) ||
            !editor_ui)
            return false;
        env_.MemoryWrite32(editor + layout.editor_ui_field, editor_ui);
        if (add_child_z &&
            !RunNestedPreservingState(add_child_z, {editor, editor_ui, 100u},
                                      ignored,
                                      "V22 wrapper editor add EditorUI"))
            return false;

        const u32 markers = V22PrimarySymbolAddress(
            "_ZN13DrawGridLayer17updateTimeMarkersEv");
        if (markers)
            RunNestedPreservingState(markers, {draw_grid}, ignored,
                                     "V22 wrapper editor time markers");

        if (layout.late_background_api) {
            const u32 create_background = V22PrimarySymbolAddress(
                "_ZN15GJBaseGameLayer16createBackgroundEi");
            const u32 create_middle = V22PrimarySymbolAddress(
                "_ZN15GJBaseGameLayer18createMiddlegroundEi");
            const u32 create_ground = V22PrimarySymbolAddress(
                "_ZN15GJBaseGameLayer17createGroundLayerEii");
            if (LooksLikeGuestObject(runtime_, env_, level_settings)) {
                if (create_background && env_.IsMapped(level_settings + 0x11Cu, 4u))
                    RunNestedPreservingState(
                        create_background,
                        {editor, env_.MemoryRead32(level_settings + 0x11Cu)},
                        ignored, "V22 wrapper editor background");
                if (create_middle && env_.IsMapped(level_settings + 0x128u, 4u))
                    RunNestedPreservingState(
                        create_middle,
                        {editor, env_.MemoryRead32(level_settings + 0x128u)},
                        ignored, "V22 wrapper editor middleground");
                if (create_ground && env_.IsMapped(level_settings + 0x148u, 4u))
                    RunNestedPreservingState(
                        create_ground,
                        {editor, env_.MemoryRead32(level_settings + 0x120u),
                         env_.MemoryRead32(level_settings + 0x148u)},
                        ignored, "V22 wrapper editor ground");
            }
        } else {
            CallV22Primary("_ZN16LevelEditorLayer17createGroundLayerEv",
                           {editor}, ignored,
                           "V22 wrapper editor early ground", false);
            CallV22Primary("_ZN16LevelEditorLayer16createBackgroundEv",
                           {editor}, ignored,
                           "V22 wrapper editor early background", false);
        }

        // Exact late-2023 initH behavior: this post-background byte is set to
        // true before EditorUI::updateSlider. Its semantic field name is not
        // exported, so do not guess an equivalent offset for 2019/2022.
        if (runtime_.v22_wrapper_editor_profile ==
                V22EditorRestoreProfile::Late2023 &&
            env_.IsMapped(editor + 0x2A19u, 1u))
            env_.MemoryWrite8(editor + 0x2A19u, 1u);

        CallV22Primary("_ZN8EditorUI12updateSliderEv", {editor_ui}, ignored,
                       "V22 wrapper editor update slider", false);
        if (!CallV22Primary("_ZN15GJBaseGameLayer18resetGroupCountersEb",
                            {editor, 0u}, ignored,
                            "V22 wrapper editor reset groups", false))
            return false;
        if (!CallV22Primary("_ZN15GJBaseGameLayer16sortStickyGroupsEv",
                            {editor}, ignored,
                            "V22 wrapper editor sort sticky", false))
            CallV22Primary("_ZN16LevelEditorLayer16sortStickyGroupsEv",
                           {editor}, ignored,
                           "V22 wrapper editor sort sticky early", false);
        CallV22Primary("_ZN16LevelEditorLayer16updateEditorModeEv",
                       {editor}, ignored,
                       "V22 wrapper editor update mode", false);
        CallV22Primary("_ZN15GJBaseGameLayer24generateAreaTargetGroupsEv",
                       {editor}, ignored,
                       "V22 wrapper editor area groups", false);
        CallV22Primary("_ZN15GJBaseGameLayer27generateSpecialTargetGroupsEv",
                       {editor}, ignored,
                       "V22 wrapper editor special groups", false);
        CallV22Primary("_ZN7cocos2d6CCNode14scheduleUpdateEv",
                       {editor}, ignored,
                       "V22 wrapper editor schedule update", false);
        CallV22Primary("_ZN16LevelEditorLayer17updatePreviewAnimEv",
                       {editor}, ignored,
                       "V22 wrapper editor preview animation", false);
        CallV22Primary("_ZN16LevelEditorLayer22updatePreviewParticlesEv",
                       {editor}, ignored,
                       "V22 wrapper editor preview particles", false);

        log_ << "RESULT: DYNARMIC_V22_WRAPPER_EDITOR_INIT_OK profile="
             << layout.name << " editor=0x" << std::hex << editor
             << " level=0x" << level << std::dec
             << " setup-bytes=" << decoded_setup.size()
             << " source=" << source << '\n';
        log_.flush();
        return true;
    }

    bool EnterV22LevelEditor(u32 level, u32 create_mode,
                               bool set_editor_state,
                               std::string_view source) {
        if (!IsV22GameLevelObject(level)) {
            std::ostringstream error;
            error << "V22 editor entry has invalid GJGameLevel pointer 0x"
                  << std::hex << level << " source=" << source;
            return Fail(error.str());
        }

        u32 director = 0u;
        u32 editor = 0u;
        u32 scene = 0u;
        u32 transition = 0u;
        u32 ignored = 0u;
        if (set_editor_state && runtime_.v22_game_manager_editor_state_offset) {
            u32 game_manager = 0u;
            if (!RunNestedPreservingState(runtime_.v22_game_manager_shared, {},
                                          game_manager,
                                          "V22 GameManager::sharedState") ||
                !game_manager ||
                !env_.IsMapped(
                    game_manager + runtime_.v22_game_manager_editor_state_offset,
                    4u))
                return Fail("V22 GameManager editor-state field is unavailable");
            env_.MemoryWrite32(
                game_manager + runtime_.v22_game_manager_editor_state_offset,
                3u);
        }
        if (!RunNestedPreservingState(runtime_.v22_director_shared, {},
                                      director,
                                      "V22 CCDirector::sharedDirector") ||
            !director)
            return false;
        if (!RunNestedPreservingState(runtime_.v22_level_editor_create,
                                      {level, create_mode}, editor,
                                      "V22 LevelEditorLayer::create") ||
            !editor)
            return false;
        v22_editor_visual_layer_ = editor;
        v22_editor_visualized_objects_.clear();
        v22_editor_visibility_passes_ = 0u;
        v22_draw_grid_layer_ = 0u;
        v22_editor_overlay_frames_ = 0u;
        v22_editor_overlay_playtest_active_ = false;
        log_ << "RESULT: DYNARMIC_V22_EDITOR_OVERLAY_SESSION_RESET editor=0x"
             << std::hex << editor << std::dec << " source=create\n";
        log_.flush();
        // Stock 2.2 betas intentionally ship a four-byte editor initializer.
        // gdpstweaks6 restores only that missing initialization in the host;
        // no modded APK or libgame.so is required for known stock profiles.
        if (runtime_.v22_wrapper_editor_profile != V22EditorRestoreProfile::None) {
            if (!InitializeV22EditorFromWrapper(editor, level, source)) return false;
            log_ << "RESULT: DYNARMIC_V22_EDITOR_INIT_SOURCE host-stock-restore profile="
                 << V22EditorRestoreProfileName(runtime_.v22_wrapper_editor_profile)
                 << " source=" << source << '\n';
        } else if (runtime_.v22_companion_editor_init_enabled) {
            u32 companion_init_result = 0u;
            if (!RunNestedPreservingState(
                    runtime_.v22_companion_editor_init,
                    {editor, level}, companion_init_result,
                    "V22 companion LevelEditorLayerExt::initH",
                    3000000000u))
                return false;
            if (!companion_init_result)
                return Fail("V22 companion editor initialization returned false");
            log_ << "RESULT: DYNARMIC_V22_COMPANION_EDITOR_INIT_OK editor=0x"
                 << std::hex << editor << " level=0x" << level
                 << " init=0x" << runtime_.v22_companion_editor_init
                 << std::dec
                 << " mode=capability-gated-targeted-initH source="
                 << source << '\n';
        } else {
            log_ << "RESULT: DYNARMIC_V22_NATIVE_EDITOR_INIT_OK editor=0x"
                 << std::hex << editor << " level=0x" << level
                 << std::dec << " mode=primary-library-only source="
                 << source << '\n';
        }
        log_.flush();
        if (!RunNestedPreservingState(runtime_.v22_scene_create, {}, scene,
                                      "V22 CCScene::create") ||
            !scene)
            return false;
        if (!RunNestedPreservingState(runtime_.v22_node_add_child,
                                      {scene, editor}, ignored,
                                      "V22 CCNode::addChild"))
            return false;
        if (!RunNestedPreservingState(runtime_.v22_transition_fade_create,
                                      {0x3F000000u, scene}, transition,
                                      "V22 CCTransitionFade::create") ||
            !transition)
            return false;
        if (!RunNestedPreservingState(runtime_.v22_director_replace_scene,
                                      {director, transition}, ignored,
                                      "V22 CCDirector::replaceScene"))
            return false;

        ++v22_editor_entries_;
        log_ << "RESULT: DYNARMIC_V22_LEVEL_EDITOR_ENTERED source=" << source
             << " count=" << v22_editor_entries_ << '\n';
        log_.flush();
        cpu_.Regs()[0] = 0u;
        return true;
    }

    bool HostV22EditLevelButton() {
        const u32 edit_layer = cpu_.Regs()[0];
        if (!edit_layer ||
            !env_.IsMapped(edit_layer,
                           runtime_.v22_edit_level_pointer_offset + 4u))
            return Fail("V22 EditLevelLayer pointer is invalid");
        const u32 level = env_.MemoryRead32(
            edit_layer + runtime_.v22_edit_level_pointer_offset);
        if (!IsV22GameLevelObject(level)) {
            std::ostringstream error;
            error << "V22 editor button has invalid GJGameLevel pointer 0x"
                  << std::hex << level << " at EditLevelLayer+0x"
                  << runtime_.v22_edit_level_pointer_offset;
            return Fail(error.str());
        }
        log_ << "[host] V22 wrench-and-hammer editor button editLayer=0x"
             << std::hex << edit_layer << " level=0x" << level
             << std::dec << '\n';
        log_.flush();

        u32 ignored = 0u;
        if (!RunNestedPreservingState(runtime_.v22_edit_close_text_inputs,
                                      {edit_layer}, ignored,
                                      "V22 EditLevelLayer::closeTextInputs"))
            return false;
        if (!RunNestedPreservingState(runtime_.v22_edit_verify_level_name,
                                      {edit_layer}, ignored,
                                      "V22 EditLevelLayer::verifyLevelName"))
            return false;
        return EnterV22LevelEditor(level, 1u, true, "wrench-hammer");
    }

    bool HostV22GameplayEditorButton(std::string_view source) {
        u32 game_manager = 0u;
        if (!runtime_.v22_game_manager_shared ||
            !RunNestedPreservingState(runtime_.v22_game_manager_shared, {},
                                      game_manager,
                                      "V22 GameManager::sharedState") ||
            !game_manager || !env_.IsMapped(game_manager + 0x168u, 4u))
            return Fail("V22 gameplay editor bridge cannot find GameManager");
        const u32 play_layer = env_.MemoryRead32(game_manager + 0x168u);
        if (!play_layer || !env_.IsMapped(play_layer + 0x13Cu, 4u))
            return Fail("V22 gameplay editor bridge cannot find PlayLayer");
        const u32 level = env_.MemoryRead32(play_layer + 0x13Cu);
        if (!IsV22GameLevelObject(level)) {
            std::ostringstream error;
            error << "V22 gameplay editor bridge has invalid level 0x"
                  << std::hex << level << " PlayLayer=0x" << play_layer;
            return Fail(error.str());
        }
        log_ << "[host] V22 gameplay editor button source=" << source
             << " playLayer=0x" << std::hex << play_layer
             << " level=0x" << level << std::dec << '\n';
        log_.flush();
        return EnterV22LevelEditor(level, 0u, false, source);
    }

    static bool InflateV22PayloadWithWindow(const std::vector<u8>& compressed,
                                            int window_bits,
                                            std::string& output) {
        output.clear();
        if (compressed.empty() ||
            compressed.size() > std::numeric_limits<uInt>::max())
            return false;
        z_stream stream{};
        stream.next_in = const_cast<Bytef*>(
            reinterpret_cast<const Bytef*>(compressed.data()));
        stream.avail_in = static_cast<uInt>(compressed.size());
        if (inflateInit2(&stream, window_bits) != Z_OK) return false;
        ScopeExit cleanup([&stream] { inflateEnd(&stream); });
        std::vector<u8> bytes(256u * 1024u);
        constexpr std::size_t kMaximum = 64u * 1024u * 1024u;
        for (;;) {
            if (stream.total_out == bytes.size()) {
                if (bytes.size() >= kMaximum) return false;
                bytes.resize(std::min<std::size_t>(bytes.size() * 2u, kMaximum));
            }
            stream.next_out = reinterpret_cast<Bytef*>(bytes.data() + stream.total_out);
            stream.avail_out = static_cast<uInt>(bytes.size() - stream.total_out);
            const int status = inflate(&stream, Z_NO_FLUSH);
            if (status == Z_STREAM_END) {
                output.assign(reinterpret_cast<const char*>(bytes.data()),
                              stream.total_out);
                return true;
            }
            if (status != Z_OK) return false;
            if (stream.avail_out == 0u) continue;
            if (stream.avail_in == 0u) return false;
        }
    }

    static bool InflateV22Payload(const std::vector<u8>& compressed,
                                  std::string& output) {
        return InflateV22PayloadWithWindow(compressed, 15 + 32, output) ||
               InflateV22PayloadWithWindow(compressed, -15, output);
    }

    static std::vector<u8> DecodeV22Base64(std::string_view encoded) {
        std::vector<u8> output;
        output.reserve(encoded.size() * 3u / 4u);
        u32 accumulator = 0u;
        unsigned bits = 0u;
        for (unsigned char character : encoded) {
            if (std::isspace(character)) continue;
            if (character == '=') break;
            int value = -1;
            if (character >= 'A' && character <= 'Z')
                value = character - 'A';
            else if (character >= 'a' && character <= 'z')
                value = character - 'a' + 26;
            else if (character >= '0' && character <= '9')
                value = character - '0' + 52;
            else if (character == '+' || character == '-')
                value = 62;
            else if (character == '/' || character == '_')
                value = 63;
            if (value < 0) return {};
            accumulator = (accumulator << 6u) | static_cast<u32>(value);
            bits += 6u;
            if (bits >= 8u) {
                bits -= 8u;
                output.push_back(static_cast<u8>(
                    (accumulator >> bits) & 0xffu));
            }
        }
        return output;
    }

    void LoadV22LevelDataCatalog() {
        static constexpr std::array<const char*, 2> kCatalogMembers = {
            "assets/LevelData.plist",
            "assets/LevelDataSubZero.plist",
        };
        std::size_t source_files = 0u;
        for (const char* member_name : kCatalogMembers) {
            const auto bytes = apk_member_cache_.Load(member_name);
            if (!bytes || bytes->empty()) continue;
            ++source_files;
            const std::string_view xml(
                reinterpret_cast<const char*>(bytes->data()), bytes->size());
            std::size_t cursor = 0u;
            for (;;) {
                const std::size_t key_begin = xml.find("<key>", cursor);
                if (key_begin == std::string_view::npos) break;
                const std::size_t key_end = xml.find("</key>", key_begin + 5u);
                if (key_end == std::string_view::npos) break;
                const std::size_t value_begin =
                    xml.find("<string>", key_end + 6u);
                if (value_begin == std::string_view::npos) break;
                const std::size_t value_end =
                    xml.find("</string>", value_begin + 8u);
                if (value_end == std::string_view::npos) break;
                const std::string key(xml.substr(
                    key_begin + 5u, key_end - key_begin - 5u));
                char* end = nullptr;
                errno = 0;
                const long parsed = std::strtol(key.c_str(), &end, 10);
                if (errno == 0 && end && *end == '\0' &&
                    parsed >= std::numeric_limits<s32>::min() &&
                    parsed <= std::numeric_limits<s32>::max()) {
                    std::string encoded(xml.substr(
                        value_begin + 8u,
                        value_end - value_begin - 8u));
                    encoded.erase(std::remove_if(
                        encoded.begin(), encoded.end(),
                        [](unsigned char character) {
                            return std::isspace(character) != 0;
                        }), encoded.end());
                    if (!encoded.empty())
                        v22_level_data_encoded_[
                            static_cast<s32>(parsed)] = std::move(encoded);
                }
                cursor = value_end + 9u;
            }
        }
        log_ << "RESULT: DYNARMIC_V22_LEVEL_CATALOG_READY sources="
             << source_files << " levels=" << v22_level_data_encoded_.size()
             << " mode=lazy-empty-setup-recovery-only\n";
        log_.flush();
    }

    const std::string* GetV22OfficialLevelSetup(s32 level_id) {
        const auto decoded = v22_level_data_decoded_.find(level_id);
        if (decoded != v22_level_data_decoded_.end())
            return &decoded->second;
        const auto encoded = v22_level_data_encoded_.find(level_id);
        if (encoded == v22_level_data_encoded_.end()) return nullptr;

        std::vector<u8> compressed = DecodeV22Base64(encoded->second);
        std::string setup;
        const auto is_valid_setup = [&setup] {
            return setup.size() >= 4u && setup.starts_with("kS") &&
                   setup.find(';') != std::string::npos;
        };
        bool restored_gzip_prefix = false;
        bool inflated = !compressed.empty() &&
                        InflateV22Payload(compressed, setup) &&
                        is_valid_setup();

        // The earlier beta saves 13 bytes per catalog entry by omitting the
        // common URL-safe base64 prefix for a gzip stream. The later beta
        // stores the complete stream. Supporting both forms keeps level
        // selection deterministic without depending on a previous inflate.
        if (!inflated) {
            static constexpr std::string_view kGzipBase64Prefix =
                "H4sIAAAAAAAAA";
            std::string complete;
            complete.reserve(kGzipBase64Prefix.size() +
                             encoded->second.size());
            complete.append(kGzipBase64Prefix);
            complete.append(encoded->second);
            compressed = DecodeV22Base64(complete);
            inflated = !compressed.empty() &&
                       InflateV22Payload(compressed, setup) &&
                       is_valid_setup();
            restored_gzip_prefix = inflated;
        }

        if (!inflated) {
            log_ << "WARNING: V22 APK level catalog rejected level="
                 << level_id << " encoded=" << encoded->second.size()
                 << " compressed=" << compressed.size() << '\n';
            log_.flush();
            v22_level_data_encoded_.erase(encoded);
            return nullptr;
        }
        const auto [stored, inserted] =
            v22_level_data_decoded_.emplace(level_id, std::move(setup));
        (void)inserted;
        ++v22_level_catalog_decodes_;
        log_ << "[host] V22 APK level catalog decoded level=" << level_id
             << " bytes=" << stored->second.size()
             << " gzip-prefix="
             << (restored_gzip_prefix ? "restored" : "embedded")
             << " decode=" << v22_level_catalog_decodes_ << '\n';
        log_.flush();
        return &stored->second;
    }

    bool HostV22InflateMemory(u32 input_address, u32 input_size,
                              u32 output_pointer, u32& result) {
        result = 0u;
        if (!output_pointer || !env_.IsMapped(output_pointer, 4u)) {
            ++v22_decompress_failures_;
            return true;
        }
        env_.MemoryWrite32(output_pointer, 0u);
        constexpr u32 kMaximumInput = 64u * 1024u * 1024u;
        if (!input_address || !input_size || input_size > kMaximumInput) {
            ++v22_decompress_failures_;
            return true;
        }
        const u8* input = static_cast<const u8*>(
            env_.HostPointer(input_address, input_size));
        if (!input) {
            ++v22_decompress_failures_;
            return true;
        }
        std::vector<u8> compressed(input, input + input_size);
        std::string decompressed;
        if (!InflateV22Payload(compressed, decompressed) ||
            decompressed.empty() ||
            decompressed.size() > std::numeric_limits<u32>::max() - 1u) {
            ++v22_decompress_failures_;
            if (v22_decompress_log_count_ < 16u) {
                ++v22_decompress_log_count_;
                log_ << "[host] V22 ccInflateMemory input=" << input_size
                     << " output=0 status=failed\n";
                log_.flush();
            }
            return true;
        }
        const u32 output_size = static_cast<u32>(decompressed.size());
        const u32 guest_output = Allocate(output_size + 1u);
        if (!guest_output ||
            !env_.WriteBytes(guest_output, decompressed.data(), output_size)) {
            if (guest_output) Free(guest_output);
            ++v22_decompress_failures_;
            return true;
        }
        env_.MemoryWrite8(guest_output + output_size, 0u);
        env_.MemoryWrite32(output_pointer, guest_output);
        result = output_size;
        if (decompressed.size() >= 1024u &&
            decompressed.size() >= 2u && decompressed[0] == 'k' &&
            decompressed[1] == 'S' &&
            decompressed.find(';') != std::string::npos) {
            v22_pending_level_setup_ = decompressed;
            ++v22_level_payload_caches_;
        }
        ++v22_decompress_successes_;
        if (v22_decompress_log_count_ < 16u) {
            ++v22_decompress_log_count_;
            log_ << "[host] V22 ccInflateMemory input=" << input_size
                 << " output=" << output_size
                 << " guest=0x" << std::hex << guest_output << std::dec
                 << " status=ok original-cpp-string-path=1\n";
            log_.flush();
        }
        return true;
    }

    int CompareStrings(u32 left, u32 right, u32 maximum, bool limited, bool insensitive) const {
        std::size_t left_available = 0, right_available = 0;
        const u8* a = env_.HostPointerToRegionEnd(left, left_available);
        const u8* b = env_.HostPointerToRegionEnd(right, right_available);
        if (!a || !b) return 0;
        std::size_t limit = std::min(left_available, right_available);
        limit = std::min<std::size_t>(limit, kMaximumGuestCString);
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
    std::vector<std::string> ExtensionResourceCandidates(
        const std::string& guest_path) const {
        std::string normalized = guest_path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        const std::string marker = "/extensions/";
        std::size_t relative_start = std::string::npos;
        if (normalized.starts_with("extensions/"))
            relative_start = std::string("extensions/").size();
        else {
            const std::size_t marker_pos = normalized.find(marker);
            if (marker_pos != std::string::npos)
                relative_start = marker_pos + marker.size();
        }
        if (relative_start == std::string::npos) return {};

        std::string relative = normalized.substr(relative_start);
        while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
        if (relative.empty() || relative.find("..") != std::string::npos) return {};

        std::vector<std::string> candidates;
        candidates.push_back(relative);
        const std::size_t last_slash = relative.find_last_of('/');
        if (last_slash != std::string::npos && last_slash + 1u < relative.size()) {
            const std::string basename = relative.substr(last_slash + 1u);
            if (basename != relative) candidates.push_back(basename);
        }

        const std::size_t original_count = candidates.size();
        for (std::size_t i = 0; i < original_count; ++i) {
            const std::string& candidate = candidates[i];
            const std::size_t slash = candidate.find_last_of('/');
            const std::size_t dot = candidate.find_last_of('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot <= slash) ||
                candidate.substr(0, dot).ends_with("-hd"))
                continue;
            std::string hd = candidate;
            hd.insert(dot, "-hd");
            if (std::find(candidates.begin(), candidates.end(), hd) == candidates.end())
                candidates.push_back(std::move(hd));
        }
        return candidates;
    }

    std::shared_ptr<const std::vector<u8>> LoadExtensionResourceFallback(
        const std::string& guest_path, std::string* resolved_member = nullptr) {
        for (const std::string& candidate : ExtensionResourceCandidates(guest_path)) {
            const auto bytes = apk_member_cache_.Load(candidate);
            if (!bytes) continue;
            if (resolved_member) *resolved_member = "assets/" + candidate;
            return bytes;
        }
        return {};
    }

    bool ExtensionResourceExistsFallback(const std::string& guest_path) const {
        for (const std::string& candidate : ExtensionResourceCandidates(guest_path)) {
            if (apk_member_cache_.Exists(candidate)) return true;
        }
        return false;
    }

    std::size_t StageAndroidExtensionResources() {
        if (!apk_image_) return 0u;
        static constexpr const char* kMembers[] = {
            "assets/CCControlColourPickerSpriteSheet.plist",
            "assets/CCControlColourPickerSpriteSheet.png",
            "assets/CCControlColourPickerSpriteSheet-hd.plist",
            "assets/CCControlColourPickerSpriteSheet-hd.png",
        };

        const std::filesystem::path extension_root =
            std::filesystem::path(writable_path_) / "extensions";
        std::error_code error;
        std::filesystem::create_directories(extension_root, error);
        if (error) {
            log_ << "WARNING: could not create Android extension resource mirror: "
                 << extension_root.string() << " error=" << error.message() << '\n';
            return 0u;
        }

        std::size_t ready = 0u;
        std::size_t available = 0u;
        for (const char* member : kMembers) {
            const auto bytes = apk_member_cache_.Load(member);
            if (!bytes) continue;
            ++available;
            const std::filesystem::path destination =
                extension_root / std::filesystem::path(member).filename();

            bool already_ready = false;
            error.clear();
            if (std::filesystem::is_regular_file(destination, error) && !error) {
                error.clear();
                already_ready =
                    std::filesystem::file_size(destination, error) == bytes->size() && !error;
            }
            if (!already_ready) {
                const std::filesystem::path temporary =
                    destination.string() + ".wrapper.tmp";
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (output) {
                    if (!bytes->empty())
                        output.write(reinterpret_cast<const char*>(bytes->data()),
                                     static_cast<std::streamsize>(bytes->size()));
                    output.close();
                }
                if (!output) {
                    std::filesystem::remove(temporary, error);
                    log_ << "WARNING: could not stage Android extension resource: "
                         << member << '\n';
                    continue;
                }
                error.clear();
                std::filesystem::remove(destination, error);
                error.clear();
                std::filesystem::rename(temporary, destination, error);
                if (error) {
                    std::filesystem::remove(temporary, error);
                    log_ << "WARNING: could not install Android extension resource: "
                         << member << " error=" << error.message() << '\n';
                    continue;
                }
            }
            ++ready;
            log_ << "[host] Android extension resource ready: " << member
                 << " -> " << destination.string() << " bytes=" << bytes->size()
                 << (already_ready ? " cached=yes" : " cached=no") << '\n';
        }
        log_ << "RESULT: DYNARMIC_ANDROID_EXTENSION_RESOURCE_MIRROR_READY ready="
             << ready << " available=" << available
             << " root=" << extension_root.string() << '\n';
        return ready;
    }

    std::string TranslatePath(const std::string& input) const {
        if (input.empty()) return input;
        std::string normalized = input;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::string apk_normalized = apk_path_;
        std::replace(apk_normalized.begin(), apk_normalized.end(), '\\', '/');
        if (normalized == apk_normalized || normalized == "game.apk" ||
            normalized.ends_with("/game.apk"))
            return apk_path_;
        // The guest still supplies Android-style names, but they are only
        // virtual inputs to this interceptor. Never probe or create a host
        // drive-root data\data tree; all persistent files live directly in
        // the wrapper's shared local save directory.
        static constexpr std::string_view kAndroidDataRoot = "/data/data/";
        if (normalized.starts_with(kAndroidDataRoot)) {
            const std::size_t package_end =
                normalized.find('/', kAndroidDataRoot.size());
            if (package_end == std::string::npos) return writable_path_;
            const std::string relative = normalized.substr(package_end + 1u);
            if (relative.empty()) return writable_path_;
            return (std::filesystem::path(writable_path_) /
                    std::filesystem::path(relative)).string();
        }
        if (normalized == "/save" || normalized == "/save/")
            return writable_path_;
        if (normalized.starts_with("/save/"))
            return (std::filesystem::path(writable_path_) /
                    std::filesystem::path(normalized.substr(6u))).string();
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
        if (!stream && read_only) {
            std::string resolved_member;
            const auto bytes = LoadExtensionResourceFallback(guest_path, &resolved_member);
            if (bytes) {
                const u32 handle = NewGuestFile(nullptr, false, guest_path, mode);
                GuestFile* file = FindGuestFile(handle);
                if (!file) return 0;
                file->memory = bytes.get();
                env_.MemoryWrite32(errno_address_, 0u);
                log_ << "Dynarmic extension resource fallback: " << guest_path
                     << " -> " << resolved_member << " bytes=" << bytes->size() << '\n';
                return handle;
            }
        }
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
            auto append_formatted = [&](auto value) {
                const int needed = std::snprintf(temporary, sizeof(temporary), token.c_str(), value);
                if (needed < 0) return;
                const std::size_t room = output.size() < maximum ? maximum - output.size() : 0u;
                const std::size_t wanted = std::min<std::size_t>(static_cast<std::size_t>(needed), room);
                if (!wanted) return;
                if (static_cast<std::size_t>(needed) < sizeof(temporary)) {
                    output.append(temporary, wanted);
                    return;
                }
                // snprintf reports the complete required size even when its output buffer is
                // too small. Re-render into a buffer large enough for the remaining guest
                // output instead of silently chopping long HTTP/level strings at 4095 bytes.
                std::vector<char> dynamic(wanted + 1u);
                const int rendered = std::snprintf(dynamic.data(), dynamic.size(), token.c_str(), value);
                if (rendered < 0) return;
                output.append(dynamic.data(), std::min<std::size_t>(wanted, static_cast<std::size_t>(rendered)));
            };
            switch (specifier) {
            case 's': {
                const std::string value = ReadCString(cursor.Word());
                append_formatted(value.c_str());
                break;
            }
            case 'c': append_formatted(static_cast<int>(cursor.Word())); break;
            case 'd': case 'i':
                if (length == "ll" || length == "j") append_formatted(static_cast<long long>(static_cast<s64>(cursor.U64())));
                else if (length == "l") append_formatted(static_cast<long>(static_cast<s32>(cursor.Word())));
                else append_formatted(static_cast<int>(static_cast<s32>(cursor.Word())));
                break;
            case 'u': case 'o': case 'x': case 'X':
                if (length == "ll" || length == "j") append_formatted(static_cast<unsigned long long>(cursor.U64()));
                else if (length == "l") append_formatted(static_cast<unsigned long>(cursor.Word()));
                else append_formatted(static_cast<unsigned>(cursor.Word()));
                break;
            case 'p': append_formatted(reinterpret_cast<void*>(static_cast<std::uintptr_t>(cursor.Word()))); break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
                const u64 bits = cursor.U64();
                append_formatted(WordsToDouble(static_cast<u32>(bits), static_cast<u32>(bits >> 32)));
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
            if (output.size() >= maximum) { output.resize(maximum); break; }
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
            {"glBindFramebuffer",2},{"glBindRenderbuffer",2},{"glBindTexture",2},{"glBlendEquation",1},{"glBlendFunc",2},
            {"glBufferData",4},{"glBufferSubData",4},{"glCheckFramebufferStatus",1},{"glClear",1},
            {"glClearColor",4},{"glClearDepthf",1},{"glClearStencil",1},{"glCompileShader",1},
            {"glCompressedTexImage2D",8},{"glCreateProgram",0},{"glCreateShader",1},{"glDeleteBuffers",2},
            {"glDeleteFramebuffers",2},{"glDeleteProgram",1},{"glDeleteRenderbuffers",2},{"glDeleteShader",1},
            {"glDeleteTextures",2},{"glDepthFunc",1},{"glDepthMask",1},{"glDisable",1},{"glDisableVertexAttribArray",1},
            {"glDrawArrays",3},{"glDrawElements",4},{"glEnable",1},{"glEnableVertexAttribArray",1},
            {"glFramebufferRenderbuffer",4},{"glFramebufferTexture2D",5},{"glGenBuffers",2},
            {"glGenFramebuffers",2},{"glGenRenderbuffers",2},{"glGenTextures",2},{"glGenerateMipmap",1},
            {"glGetBooleanv",2},{"glGetError",0},{"glGetFloatv",2},{"glGetIntegerv",2},{"glGetProgramInfoLog",4},
            {"glGetProgramiv",3},{"glGetShaderInfoLog",4},{"glGetShaderSource",4},{"glGetShaderiv",3},{"glGetString",1},
            {"glGetUniformLocation",2},{"glIsEnabled",1},{"glLineWidth",1},{"glLinkProgram",1},{"glPixelStorei",2},
            {"glReadPixels",7},{"glRenderbufferStorage",4},{"glScissor",4},{"glShaderSource",4},{"glStencilFunc",3},{"glStencilMask",1},{"glStencilOp",3},
            {"glTexImage2D",9},{"glTexParameteri",3},{"glUniform1f",2},{"glUniform1i",2},{"glUniform2i",3},{"glUniform2iv",3},
            {"glUniform2f",3},{"glUniform2fv",3},{"glUniform3f",4},{"glUniform3fv",3},{"glUniform3i",4},{"glUniform3iv",3},
            {"glUniform4f",5},{"glUniform4fv",3},{"glUniform4i",5},{"glUniform4iv",3},{"glUniformMatrix3fv",4},{"glUniformMatrix4fv",4},{"glUseProgram",1},
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
        } else if (name == "glBindFramebuffer") {
            gl_framebuffer_binding_ = static_cast<u32>(arguments[1]);
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
            if (source_count < 0 || source_count > 4096)
                return Fail("glShaderSource count outside limit");
            std::vector<u32> guest_strings(static_cast<std::size_t>(source_count));
            std::vector<GLint> lengths(static_cast<std::size_t>(source_count), -1);
            std::vector<std::string> source_storage(static_cast<std::size_t>(source_count));
            std::vector<const char*> pointers(static_cast<std::size_t>(source_count));
            if (source_count &&
                !env_.ReadBytes(static_cast<u32>(arguments[2]), guest_strings.data(),
                                guest_strings.size() * sizeof(u32)))
                return Fail("glShaderSource string array invalid");
            if (arguments[3] && source_count &&
                !env_.ReadBytes(static_cast<u32>(arguments[3]), lengths.data(),
                                lengths.size() * sizeof(GLint)))
                return Fail("glShaderSource length array invalid");
            for (GLsizei i = 0; i < source_count; ++i) {
                const std::size_t index = static_cast<std::size_t>(i);
                if (lengths[index] >= 0) {
                    const std::size_t size = static_cast<std::size_t>(lengths[index]);
                    if (size > 16u * 1024u * 1024u)
                        return Fail("glShaderSource source outside limit");
                    source_storage[index].resize(size);
                    if (size && !env_.ReadBytes(guest_strings[index],
                                                source_storage[index].data(), size))
                        return Fail("glShaderSource source invalid");
                } else {
                    source_storage[index] = ReadCString(guest_strings[index]);
                }
                pointers[index] = source_storage[index].data();
            }
            /* Preserve the guest's GLES shader text byte-for-byte. Removing
               precision declarations made the 2.2 beta abort during nativeInit. */
            reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]),
                                           source_count, pointers.data(),
                                           arguments[3] ? lengths.data() : nullptr);
        } else if (name == "glCompileShader") {
            using CompileFn = void (APIENTRY*)(GLuint);
            reinterpret_cast<CompileFn>(function)(static_cast<GLuint>(arguments[0]));
            using GetFn = void (APIENTRY*)(GLuint, GLenum, GLint*);
            using LogFn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
            void* get_address = gl_.Resolve("glGetShaderiv");
            void* log_address = gl_.Resolve("glGetShaderInfoLog");
            GLint compiled = 1;
            if (get_address) {
                reinterpret_cast<GetFn>(get_address)(static_cast<GLuint>(arguments[0]),
                                                     0x8B81u, &compiled);
            }
            if (!compiled && log_address) {
                std::array<char, 4096> message{};
                GLsizei written = 0;
                reinterpret_cast<LogFn>(log_address)(static_cast<GLuint>(arguments[0]),
                                                     static_cast<GLsizei>(message.size() - 1u),
                                                     &written, message.data());
                log_ << "ERROR: ARMv7 desktop shader compile failed id="
                     << arguments[0] << " log=" << message.data() << '\n';
            }
        } else if (name == "glLinkProgram") {
            using LinkFn = void (APIENTRY*)(GLuint);
            reinterpret_cast<LinkFn>(function)(static_cast<GLuint>(arguments[0]));
            using GetFn = void (APIENTRY*)(GLuint, GLenum, GLint*);
            using LogFn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
            void* get_address = gl_.Resolve("glGetProgramiv");
            void* log_address = gl_.Resolve("glGetProgramInfoLog");
            GLint linked = 1;
            if (get_address) {
                reinterpret_cast<GetFn>(get_address)(static_cast<GLuint>(arguments[0]),
                                                     0x8B82u, &linked);
            }
            if (!linked && log_address) {
                std::array<char, 4096> message{};
                GLsizei written = 0;
                reinterpret_cast<LogFn>(log_address)(static_cast<GLuint>(arguments[0]),
                                                     static_cast<GLsizei>(message.size() - 1u),
                                                     &written, message.data());
                log_ << "ERROR: ARMv7 desktop program link failed id="
                     << arguments[0] << " log=" << message.data() << '\n';
            }
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
            if (name == "glDeleteBuffers") {
                for (const GLuint value : values) {
                    if (value == gl_array_buffer_binding_)
                        gl_array_buffer_binding_ = 0u;
                    if (value == gl_element_buffer_binding_)
                        gl_element_buffer_binding_ = 0u;
                }
            }
            reinterpret_cast<Fn>(function)(static_cast<GLsizei>(arguments[0]), values.data());
        } else if (name == "glGetBooleanv") {
            using Fn = void (APIENTRY*)(GLenum, GLboolean*);
            std::array<GLboolean, 16> values{};
            reinterpret_cast<Fn>(function)(static_cast<GLenum>(arguments[0]), values.data());
            const std::size_t count = arguments[0] == 0x0C23u ? 4u : 1u;
            env_.WriteBytes(static_cast<u32>(arguments[1]), values.data(), count * sizeof(GLboolean));
        } else if (name == "glGetIntegerv") {
            using Fn = void (APIENTRY*)(GLenum, GLint*);
            std::array<GLint, 16> values{};
            std::array<GLint, 4> guest_clip{};
            const GLenum pname = static_cast<GLenum>(arguments[0]);
            const bool guest_clip_query = gl_.ReadGuestClipRect(pname, guest_clip);
            if (guest_clip_query)
                std::copy(guest_clip.begin(), guest_clip.end(), values.begin());
            else
                reinterpret_cast<Fn>(function)(pname, values.data());
            const std::size_t count = pname == 0x0BA2u || pname == 0x0C10u ? 4u : 1u;
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
        } else if (name == "glGetProgramInfoLog" || name == "glGetShaderInfoLog" || name == "glGetShaderSource") {
            using Fn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
            const GLsizei capacity = static_cast<GLsizei>(arguments[1]);
            std::vector<char> text(capacity > 0 ? static_cast<std::size_t>(capacity) : 1u);
            GLsizei length = 0;
            reinterpret_cast<Fn>(function)(static_cast<GLuint>(arguments[0]), capacity, &length, text.data());
            if (arguments[2]) env_.MemoryWrite32(static_cast<u32>(arguments[2]), static_cast<u32>(length));
            if (arguments[3] && capacity > 0) env_.WriteBytes(static_cast<u32>(arguments[3]), text.data(), text.size());
        } else if (name == "glTexParameteri") {
            using Fn = void (APIENTRY*)(GLenum, GLenum, GLint);
            const GLenum target = static_cast<GLenum>(arguments[0]);
            const GLenum pname = static_cast<GLenum>(arguments[1]);
            const GLint param = static_cast<GLint>(arguments[2]);
            reinterpret_cast<Fn>(function)(target, pname, param);
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
        } else if (name == "glUniform2iv" || name == "glUniform3iv" || name == "glUniform4iv") {
            using Fn = void (APIENTRY*)(GLint, GLsizei, const GLint*);
            const unsigned components = name == "glUniform2iv" ? 2u : name == "glUniform3iv" ? 3u : 4u;
            const std::size_t bytes = static_cast<std::size_t>(arguments[1]) * components * sizeof(GLint);
            const GLint* values = static_cast<const GLint*>(env_.HostPointer(static_cast<u32>(arguments[2]), bytes));
            reinterpret_cast<Fn>(function)(static_cast<GLint>(arguments[0]), static_cast<GLsizei>(arguments[1]), values);
        } else if (name == "glUniformMatrix3fv") {
            using Fn = void (APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
            const std::size_t bytes = static_cast<std::size_t>(arguments[1]) * 9u * sizeof(GLfloat);
            const GLfloat* values = static_cast<const GLfloat*>(env_.HostPointer(static_cast<u32>(arguments[3]), bytes));
            reinterpret_cast<Fn>(function)(static_cast<GLint>(arguments[0]), static_cast<GLsizei>(arguments[1]), static_cast<GLboolean>(arguments[2]), values);
        } else if (name == "glUniformMatrix4fv") {
            using Fn = void (APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
            const std::size_t bytes = static_cast<std::size_t>(arguments[1]) * 16u * sizeof(GLfloat);
            const GLfloat* values = static_cast<const GLfloat*>(env_.HostPointer(static_cast<u32>(arguments[3]), bytes));
            reinterpret_cast<Fn>(function)(static_cast<GLint>(arguments[0]), static_cast<GLsizei>(arguments[1]), static_cast<GLboolean>(arguments[2]), values);
        } else if (name == "glScissor" || name == "glViewport") {
            using Fn = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
            const GLint guest_x = static_cast<GLint>(arguments[0]);
            const GLint guest_y = static_cast<GLint>(arguments[1]);
            const GLsizei guest_width = static_cast<GLsizei>(arguments[2]);
            const GLsizei guest_height = static_cast<GLsizei>(arguments[3]);
            GLint scaled_x = 0;
            GLint scaled_y = 0;
            GLsizei scaled_width = 0;
            GLsizei scaled_height = 0;
            gl_.ScaleGuestRect(guest_x, guest_y, guest_width, guest_height,
                               scaled_x, scaled_y, scaled_width, scaled_height);
            reinterpret_cast<Fn>(function)(
                scaled_x, scaled_y, scaled_width, scaled_height);
            if (name == "glViewport")
                gl_.RememberGuestViewport(
                    guest_x, guest_y, guest_width, guest_height);
            else
                gl_.RememberGuestScissor(
                    guest_x, guest_y, guest_width, guest_height);
        } else if (name == "glBindBuffer") {
            if (arguments[0] == GL_ARRAY_BUFFER) gl_array_buffer_binding_ = static_cast<u32>(arguments[1]);
            if (arguments[0] == GL_ELEMENT_ARRAY_BUFFER) gl_element_buffer_binding_ = static_cast<u32>(arguments[1]);
            result = CallGlRaw(function, arguments, count);
        } else if (name == "glVertexAttribPointer") {
            using Fn = void (APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
            using GetIntegerFn = void (APIENTRY*)(GLenum, GLint*);
            using BindBufferFn = void (APIENTRY*)(GLenum, GLuint);
            const u32 guest_pointer = static_cast<u32>(arguments[5]);
            const bool guest_client_memory =
                guest_pointer != 0u && env_.IsMapped(guest_pointer, 1u);
            const void* pointer = guest_client_memory
                ? env_.HostPointer(guest_pointer, 1u)
                : reinterpret_cast<const void*>(arguments[5]);
            GLint actual_binding = 0;
            auto* get_integer = reinterpret_cast<GetIntegerFn>(
                gl_.Resolve("glGetIntegerv"));
            auto* bind_buffer = reinterpret_cast<BindBufferFn>(
                gl_.Resolve("glBindBuffer"));
            const GLint cached_binding =
                static_cast<GLint>(gl_array_buffer_binding_);
            if (guest_client_memory && cached_binding != 0 &&
                get_integer && bind_buffer) {
                get_integer(GL_ARRAY_BUFFER_BINDING, &actual_binding);
                gl_array_buffer_binding_ = static_cast<u32>(
                    std::max(actual_binding, 0));
                if (actual_binding != 0)
                    bind_buffer(GL_ARRAY_BUFFER, 0u);
                ++gl_client_array_rebinds_;
                if (gl_client_array_rebinds_ == 1u) {
                    log_ << "RESULT: DYNARMIC_GL_CLIENT_ARRAY_REBIND_READY "
                         << "policy=mapped-guest-pointer-overrides-vbo "
                         << "cached=" << cached_binding
                         << " actual=" << actual_binding << '\n';
                    log_.flush();
                }
            }
            reinterpret_cast<Fn>(function)(
                static_cast<GLuint>(arguments[0]),
                static_cast<GLint>(arguments[1]),
                static_cast<GLenum>(arguments[2]),
                static_cast<GLboolean>(arguments[3]),
                static_cast<GLsizei>(arguments[4]), pointer);
            if (guest_client_memory && actual_binding != 0 && bind_buffer) {
                bind_buffer(GL_ARRAY_BUFFER,
                            static_cast<GLuint>(actual_binding));
            }
        } else if (name == "glDrawElements") {
            using Fn = void (APIENTRY*)(GLenum, GLsizei, GLenum, const void*);
            using GetIntegerFn = void (APIENTRY*)(GLenum, GLint*);
            using BindBufferFn = void (APIENTRY*)(GLenum, GLuint);
            const std::size_t element_size = arguments[2] == 0x1401u ? 1u : arguments[2] == 0x1403u ? 2u : 4u;
            const std::size_t bytes = static_cast<std::size_t>(arguments[1]) * element_size;
            const u32 guest_indices = static_cast<u32>(arguments[3]);
            const bool guest_client_memory =
                guest_indices != 0u && env_.IsMapped(guest_indices, bytes ? bytes : 1u);
            const void* indices = guest_client_memory
                ? env_.HostPointer(guest_indices, bytes ? bytes : 1u)
                : reinterpret_cast<const void*>(arguments[3]);
            GLint actual_binding = 0;
            auto* get_integer = reinterpret_cast<GetIntegerFn>(
                gl_.Resolve("glGetIntegerv"));
            auto* bind_buffer = reinterpret_cast<BindBufferFn>(
                gl_.Resolve("glBindBuffer"));
            const GLint cached_binding =
                static_cast<GLint>(gl_element_buffer_binding_);
            if (guest_client_memory && cached_binding != 0 &&
                get_integer && bind_buffer) {
                get_integer(GL_ELEMENT_ARRAY_BUFFER_BINDING, &actual_binding);
                gl_element_buffer_binding_ = static_cast<u32>(
                    std::max(actual_binding, 0));
                if (actual_binding != 0)
                    bind_buffer(GL_ELEMENT_ARRAY_BUFFER, 0u);
                ++gl_client_element_rebinds_;
            }
            reinterpret_cast<Fn>(function)(
                static_cast<GLenum>(arguments[0]),
                static_cast<GLsizei>(arguments[1]),
                static_cast<GLenum>(arguments[2]), indices);
            if (guest_client_memory && actual_binding != 0 && bind_buffer) {
                bind_buffer(GL_ELEMENT_ARRAY_BUFFER,
                            static_cast<GLuint>(actual_binding));
                ++gl_client_element_rebinds_;
            }
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
        const SOCKET socket_value = ::socket(
            HostAddressFamily(static_cast<int>(family)),
            static_cast<int>(type), static_cast<int>(protocol));
        if (socket_value == INVALID_SOCKET) return SocketFailure();
        if (gd_net_set_nonblocking(socket_value, 1) != 0) {
            const int error = WSAGetLastError();
            ::closesocket(socket_value);
            SetGuestErrno(MapWsaError(error));
            return static_cast<u32>(-1);
        }
        const u32 guest_fd = RegisterSocket(socket_value);
        if (guest_fd != static_cast<u32>(-1))
            nonblocking_sockets_.insert(guest_fd);
        return guest_fd;
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

        // NetworkTest keeps the v22 beta socket genuinely nonblocking.
        // Do not wait up to 15 seconds on the Win32/UI thread.  Modern curl
        // expects EINPROGRESS and completes the connection through poll +
        // getsockopt(SO_ERROR).
        if (initial_error == WSAEWOULDBLOCK || initial_error == WSAEINPROGRESS ||
            initial_error == WSAEALREADY) {
            constexpr int kGuestEinprogress = 115;
            SetGuestErrno(kGuestEinprogress);
            if (network_log_count_++ < 192u) {
                log_ << "[host] NetworkTest socket connect fd=" << guest_fd
                     << " target=" << (address_text[0] ? address_text : "?")
                     << ':' << port << " status=in-progress errno="
                     << kGuestEinprogress << '\n';
                log_.flush();
            }
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

        const int host_length = static_cast<int>(std::min<u32>(length, INT_MAX));
        int code = ::send(socket_value, data, host_length, HostMessageFlags(flags));
        int error = code == SOCKET_ERROR ? WSAGetLastError() : 0;

        if (code == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
            SetGuestErrno(11);
            if (network_log_count_++ < 192u)
                log_ << "[host] NetworkTest socket send would-block fd="
                     << guest_fd << " errno=11\n";
            return static_cast<u32>(-1);
        }

        if (code >= 0) {
            if (socket_send_logged_.insert(guest_fd).second)
                log_ << "[host] Socket first send fd=" << guest_fd << " bytes=" << code << '\n';
            SetGuestErrno(0);
            return static_cast<u32>(code);
        }
        SetGuestErrno(MapWsaError(error));
        if (network_log_count_++ < 128u)
            log_ << "[host] Socket send failed fd=" << guest_fd << " wsa=" << error
                 << " errno=" << MapWsaError(error) << '\n';
        return static_cast<u32>(-1);
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

        const int host_length = static_cast<int>(std::min<u32>(length, INT_MAX));
        int code = ::recv(socket_value, data, host_length, HostMessageFlags(flags));
        int error = code == SOCKET_ERROR ? WSAGetLastError() : 0;

        if (code == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
            SetGuestErrno(11);
            if (network_log_count_++ < 192u)
                log_ << "[host] NetworkTest socket recv would-block fd="
                     << guest_fd << " errno=11\n";
            return static_cast<u32>(-1);
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
        u32 invalid_count = 0;
        bool have_valid_socket = false;
        for (u32 index = 0; index < count; ++index) {
            guest[index].revents = 0;
            if (guest[index].fd < 0) continue;
            const SOCKET socket_value = FindHostSocket(static_cast<u32>(guest[index].fd));
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

        // The network worker shares the UI thread.  Limit each poll to one
        // millisecond and let subsequent host frames continue the wait.
        const s32 effective_timeout = running_cooperative_worker_
            ? (timeout == 0 ? 0 : 1)
            : timeout;
        timeval timeval_value{};
        timeval* timeval_pointer = nullptr;
        if (effective_timeout >= 0) {
            timeval_value.tv_sec = effective_timeout / 1000;
            timeval_value.tv_usec = (effective_timeout % 1000) * 1000;
            timeval_pointer = &timeval_value;
        }

        int selected = 0;
        if (have_valid_socket) {
            selected = ::select(0, &readable, &writable, &exceptional, timeval_pointer);
            if (selected == SOCKET_ERROR) return SocketFailure();
        } else if (effective_timeout > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(effective_timeout));
        }

        u32 ready_count = invalid_count;
        for (u32 index = 0; index < count; ++index) {
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
        return code == 0 ? 0u : SocketFailure();
    }
    struct GuestDnsHints {
        bool present = false;
        int flags = 0;
        int family = AF_UNSPEC;
        int socktype = 0;
        int protocol = 0;
    };

    GuestDnsHints ReadGuestDnsHints(u32 hints_address) {
        GuestDnsHints output;
        if (!hints_address) return output;
        output.present = true;
        const u32 guest_flags = env_.MemoryRead32(hints_address + 0u);
        if (guest_flags & 0x001u) output.flags |= AI_PASSIVE;
        if (guest_flags & 0x002u) output.flags |= AI_CANONNAME;
        if (guest_flags & 0x004u) output.flags |= AI_NUMERICHOST;
#ifdef AI_NUMERICSERV
        if (guest_flags & 0x400u) output.flags |= AI_NUMERICSERV;
#endif
        output.family = HostAddressFamily(
            static_cast<int>(env_.MemoryRead32(hints_address + 4u)));
        output.socktype = static_cast<int>(env_.MemoryRead32(hints_address + 8u));
        output.protocol = static_cast<int>(env_.MemoryRead32(hints_address + 12u));
        return output;
    }

    static std::vector<HostAddrInfoRecord> CopyHostAddrInfo(addrinfo* results) {
        std::vector<HostAddrInfoRecord> records;
        for (addrinfo* item = results; item; item = item->ai_next) {
            if (!item->ai_addr || item->ai_addrlen <= 0 ||
                item->ai_addrlen > static_cast<int>(sizeof(sockaddr_storage)))
                continue;
            HostAddrInfoRecord record;
            record.flags = item->ai_flags;
            record.family = item->ai_family;
            record.socktype = item->ai_socktype;
            record.protocol = item->ai_protocol;
            const auto* first = reinterpret_cast<const u8*>(item->ai_addr);
            record.address.assign(first, first + item->ai_addrlen);
            if (item->ai_canonname) record.canonical = item->ai_canonname;
            records.push_back(std::move(record));
        }
        return records;
    }

    void StartAsyncDns(AsyncDnsKind kind, const std::string& node,
                       const std::string& service, const GuestDnsHints& hints) {
        auto request = std::make_shared<AsyncDnsRequest>();
        request->kind = kind;
        request->node = node;
        request->service = service;
        request->has_hints = hints.present;
        request->flags = hints.flags;
        request->family = hints.family;
        request->socktype = hints.socktype;
        request->protocol = hints.protocol;
        request->started = std::chrono::steady_clock::now();
        async_dns_ = request;
        async_dns_timeout_reported_ = false;
        async_dns_ready_reported_ = false;
        ++async_dns_queued_count_;

        log_ << "[host] Unified ARMv7 DNS queued kind="
             << (kind == AsyncDnsKind::AddrInfo ? "getaddrinfo" : "gethostbyname")
             << " node=" << (node.empty() ? "<null>" : node)
             << " service=" << (service.empty() ? "<null>" : service)
             << " worker-pc=0x" << std::hex << cpu_.Regs()[15] << std::dec << '\n';
        log_.flush();

        g_async_dns_threads_active.fetch_add(1u, std::memory_order_relaxed);
        std::thread([request] {
            addrinfo host_hints{};
            addrinfo* hints_pointer = nullptr;
            if (request->has_hints) {
                host_hints.ai_flags = request->flags;
                host_hints.ai_family = request->family;
                host_hints.ai_socktype = request->socktype;
                host_hints.ai_protocol = request->protocol;
                hints_pointer = &host_hints;
            }
            addrinfo* results = nullptr;
            const int code = ::getaddrinfo(
                request->node.empty() ? nullptr : request->node.c_str(),
                request->service.empty() ? nullptr : request->service.c_str(),
                hints_pointer, &results);
            std::vector<HostAddrInfoRecord> copied;
            if (code == 0 && results) copied = CopyHostAddrInfo(results);
            if (results) ::freeaddrinfo(results);
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->records = std::move(copied);
                request->code.store(code, std::memory_order_relaxed);
            }
            request->state.store(1, std::memory_order_release);
            g_async_dns_threads_active.fetch_sub(1u, std::memory_order_relaxed);
        }).detach();
    }

    bool AsyncDnsMatches(AsyncDnsKind kind, const std::string& node,
                         const std::string& service,
                         const GuestDnsHints& hints) const {
        if (!async_dns_) return false;
        return async_dns_->kind == kind && async_dns_->node == node &&
               async_dns_->service == service &&
               async_dns_->has_hints == hints.present &&
               async_dns_->flags == hints.flags &&
               async_dns_->family == hints.family &&
               async_dns_->socktype == hints.socktype &&
               async_dns_->protocol == hints.protocol;
    }

    bool AsyncDnsTimedOut() const {
        return async_dns_ && async_dns_->state.load(std::memory_order_acquire) == 0 &&
               std::chrono::steady_clock::now() - async_dns_->started >=
                   std::chrono::seconds(8);
    }

    void UpdateAsyncDnsWorkerState() {
        if (!async_dns_) return;
        const bool finished =
            async_dns_->state.load(std::memory_order_acquire) != 0;
        const bool timed_out = AsyncDnsTimedOut();
        if (!finished && !timed_out) return;
        cooperative_worker_runnable_ = cooperative_worker_.valid;
        if (finished && !async_dns_ready_reported_) {
            async_dns_ready_reported_ = true;
            log_ << "[host] Unified ARMv7 DNS completion ready code="
                 << async_dns_->code.load(std::memory_order_relaxed)
                 << " resume-next-frame=1\n";
            log_.flush();
        } else if (timed_out && !async_dns_timeout_reported_) {
            async_dns_timeout_reported_ = true;
            log_ << "[host] Unified ARMv7 DNS timeout after 8000 ms; "
                    "resume guest with EAI_AGAIN\n";
            log_.flush();
        }
    }

    u32 CommitGuestAddrInfoRecords(const std::vector<HostAddrInfoRecord>& records,
                                   u32 result_address) {
        if (!result_address) return static_cast<u32>(EAI_FAIL);
        env_.MemoryWrite32(result_address, 0u);
        GuestAddrInfoAllocation allocation;
        u32 first = 0u, previous = 0u;
        for (const HostAddrInfoRecord& item : records) {
            if (item.address.empty() ||
                item.address.size() > sizeof(sockaddr_storage))
                continue;
            const u32 node_block = Allocate(32u);
            const u32 address_block = Allocate(
                static_cast<u32>(item.address.size()));
            if (!node_block || !address_block) break;
            allocation.blocks.push_back(node_block);
            allocation.blocks.push_back(address_block);
            std::vector<u8> address_bytes = item.address;
            const u16 family = static_cast<u16>(GuestAddressFamily(item.family));
            if (address_bytes.size() >= sizeof(family))
                std::memcpy(address_bytes.data(), &family, sizeof(family));
            env_.WriteBytes(address_block, address_bytes.data(),
                            address_bytes.size());
            u32 canonical = 0u;
            if (!item.canonical.empty()) {
                canonical = AllocateString(item.canonical);
                if (canonical) allocation.blocks.push_back(canonical);
            }
            env_.MemoryWrite32(node_block + 0u, static_cast<u32>(item.flags));
            env_.MemoryWrite32(node_block + 4u,
                               static_cast<u32>(GuestAddressFamily(item.family)));
            env_.MemoryWrite32(node_block + 8u,
                               static_cast<u32>(item.socktype));
            env_.MemoryWrite32(node_block + 12u,
                               static_cast<u32>(item.protocol));
            env_.MemoryWrite32(node_block + 16u,
                               static_cast<u32>(item.address.size()));
            env_.MemoryWrite32(node_block + 20u, canonical);
            env_.MemoryWrite32(node_block + 24u, address_block);
            env_.MemoryWrite32(node_block + 28u, 0u);
            if (!first) first = node_block;
            if (previous) env_.MemoryWrite32(previous + 28u, node_block);
            previous = node_block;
        }
        if (!first) {
            for (u32 block : allocation.blocks) Free(block);
            return static_cast<u32>(EAI_MEMORY);
        }
        guest_addrinfo_[first] = std::move(allocation);
        env_.MemoryWrite32(result_address, first);
        return 0u;
    }

    u32 CommitGuestHostEntRecords(const std::string& name,
                                  const std::vector<HostAddrInfoRecord>& records) {
        FreeGuestHostEnt();
        std::vector<std::array<u8, 4>> addresses;
        for (const HostAddrInfoRecord& item : records) {
            if (item.family != AF_INET ||
                item.address.size() < sizeof(sockaddr_in))
                continue;
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(
                item.address.data());
            std::array<u8, 4> bytes{};
            std::memcpy(bytes.data(), &ipv4->sin_addr, bytes.size());
            if (std::find(addresses.begin(), addresses.end(), bytes) ==
                addresses.end())
                addresses.push_back(bytes);
        }
        if (addresses.empty()) {
            SetGuestErrno(2);
            return 0u;
        }
        auto allocate_block = [&](u32 size) -> u32 {
            const u32 block = Allocate(size);
            if (block) guest_hostent_.blocks.push_back(block);
            return block;
        };
        const u32 canonical = AllocateString(name);
        if (canonical) guest_hostent_.blocks.push_back(canonical);
        const u32 aliases = allocate_block(4u);
        const u32 address_list = allocate_block(
            static_cast<u32>((addresses.size() + 1u) * sizeof(u32)));
        const u32 hostent = allocate_block(20u);
        if (!canonical || !aliases || !address_list || !hostent) {
            FreeGuestHostEnt();
            SetGuestErrno(12);
            return 0u;
        }
        env_.MemoryWrite32(aliases, 0u);
        for (std::size_t index = 0; index < addresses.size(); ++index) {
            const u32 address_block = allocate_block(4u);
            if (!address_block) {
                FreeGuestHostEnt();
                SetGuestErrno(12);
                return 0u;
            }
            env_.WriteBytes(address_block, addresses[index].data(), 4u);
            env_.MemoryWrite32(
                address_list + static_cast<u32>(index * 4u), address_block);
        }
        env_.MemoryWrite32(
            address_list + static_cast<u32>(addresses.size() * 4u), 0u);
        env_.MemoryWrite32(hostent + 0u, canonical);
        env_.MemoryWrite32(hostent + 4u, aliases);
        env_.MemoryWrite32(hostent + 8u, 2u);
        env_.MemoryWrite32(hostent + 12u, 4u);
        env_.MemoryWrite32(hostent + 16u, address_list);
        guest_hostent_address_ = hostent;
        SetGuestErrno(0);
        return hostent;
    }

    bool DispatchAsyncGetAddrInfo(u32 node_address, u32 service_address,
                                  u32 hints_address, u32 result_address,
                                  u32& result) {
        const std::string node = ReadCString(node_address);
        const std::string service = ReadCString(service_address);
        const GuestDnsHints hints = ReadGuestDnsHints(hints_address);
        if (!AsyncDnsMatches(AsyncDnsKind::AddrInfo, node, service, hints)) {
            if (async_dns_ &&
                async_dns_->state.load(std::memory_order_acquire) == 0 &&
                !AsyncDnsTimedOut()) {
                cooperative_worker_yielded_ = true;
                cooperative_worker_runnable_ = false;
                return false;
            }
            async_dns_.reset();
            StartAsyncDns(AsyncDnsKind::AddrInfo, node, service, hints);
            cooperative_worker_yielded_ = true;
            cooperative_worker_runnable_ = false;
            return false;
        }
        if (AsyncDnsTimedOut()) {
            result = static_cast<u32>(EAI_AGAIN);
            ++async_dns_timeout_count_;
            async_dns_.reset();
            return true;
        }
        if (async_dns_->state.load(std::memory_order_acquire) == 0) {
            cooperative_worker_yielded_ = true;
            cooperative_worker_runnable_ = false;
            return false;
        }
        int code = EAI_FAIL;
        std::vector<HostAddrInfoRecord> records;
        {
            std::lock_guard<std::mutex> lock(async_dns_->mutex);
            code = async_dns_->code.load(std::memory_order_relaxed);
            records = async_dns_->records;
        }
        if (code == 0) result = CommitGuestAddrInfoRecords(records, result_address);
        else result = static_cast<u32>(code);
        log_ << "[host] Unified ARMv7 DNS delivered kind=getaddrinfo node="
             << (node.empty() ? "<null>" : node) << " result="
             << static_cast<s32>(result) << " records=" << records.size() << '\n';
        log_.flush();
        ++async_dns_completed_count_;
        async_dns_.reset();
        return true;
    }

    bool DispatchAsyncGetHostByName(u32 name_address, u32& result) {
        const std::string node = ReadCString(name_address);
        if (node.empty()) {
            SetGuestErrno(22);
            result = 0u;
            return true;
        }
        GuestDnsHints hints;
        hints.present = true;
        hints.family = AF_INET;
        hints.socktype = SOCK_STREAM;
        if (!AsyncDnsMatches(AsyncDnsKind::HostByName, node, {}, hints)) {
            if (async_dns_ &&
                async_dns_->state.load(std::memory_order_acquire) == 0 &&
                !AsyncDnsTimedOut()) {
                cooperative_worker_yielded_ = true;
                cooperative_worker_runnable_ = false;
                return false;
            }
            async_dns_.reset();
            StartAsyncDns(AsyncDnsKind::HostByName, node, {}, hints);
            cooperative_worker_yielded_ = true;
            cooperative_worker_runnable_ = false;
            return false;
        }
        if (AsyncDnsTimedOut()) {
            SetGuestErrno(2);
            result = 0u;
            ++async_dns_timeout_count_;
            async_dns_.reset();
            return true;
        }
        if (async_dns_->state.load(std::memory_order_acquire) == 0) {
            cooperative_worker_yielded_ = true;
            cooperative_worker_runnable_ = false;
            return false;
        }
        int code = EAI_FAIL;
        std::vector<HostAddrInfoRecord> records;
        {
            std::lock_guard<std::mutex> lock(async_dns_->mutex);
            code = async_dns_->code.load(std::memory_order_relaxed);
            records = async_dns_->records;
        }
        result = code == 0 ? CommitGuestHostEntRecords(node, records) : 0u;
        if (code != 0) SetGuestErrno(2);
        log_ << "[host] Unified ARMv7 DNS delivered kind=gethostbyname node="
             << node << " result=" << code << " records=" << records.size()
             << '\n';
        log_.flush();
        ++async_dns_completed_count_;
        async_dns_.reset();
        return true;
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
        const int code = ::getaddrinfo(node.empty() ? nullptr : node.c_str(),
                                       service.empty() ? nullptr : service.c_str(), hints, &host_results);
        if (network_log_count_++ < 64u) {
            log_ << "[host] DNS getaddrinfo node=" << (node.empty() ? "<null>" : node)
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
    void FreeGuestHostEnt() {
        for (u32 block : guest_hostent_.blocks) Free(block);
        guest_hostent_.blocks.clear();
        guest_hostent_address_ = 0u;
    }
    u32 GuestGetHostByName(u32 name_address) {
        FreeGuestHostEnt();
        const std::string name = ReadCString(name_address);
        if (name.empty()) { SetGuestErrno(22); return 0u; }

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* results = nullptr;
        const int code = ::getaddrinfo(name.c_str(), nullptr, &hints, &results);
        if (network_log_count_++ < 96u)
            log_ << "[host] DNS gethostbyname node=" << name
                 << " result=" << code << '\n';
        if (code != 0 || !results) { SetGuestErrno(2); return 0u; }
        ScopeExit cleanup([&] { ::freeaddrinfo(results); });

        std::vector<std::array<u8, 4>> addresses;
        for (addrinfo* item = results; item; item = item->ai_next) {
            if (item->ai_family != AF_INET ||
                item->ai_addrlen < static_cast<int>(sizeof(sockaddr_in)))
                continue;
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(item->ai_addr);
            std::array<u8, 4> bytes{};
            std::memcpy(bytes.data(), &ipv4->sin_addr, bytes.size());
            if (std::find(addresses.begin(), addresses.end(), bytes) == addresses.end())
                addresses.push_back(bytes);
        }
        if (addresses.empty()) { SetGuestErrno(2); return 0u; }

        auto allocate_block = [&](u32 size) -> u32 {
            const u32 block = Allocate(size);
            if (block) guest_hostent_.blocks.push_back(block);
            return block;
        };
        const u32 canonical = AllocateString(name);
        if (canonical) guest_hostent_.blocks.push_back(canonical);
        const u32 aliases = allocate_block(4u);
        const u32 address_list = allocate_block(
            static_cast<u32>((addresses.size() + 1u) * sizeof(u32)));
        const u32 hostent = allocate_block(20u);
        if (!canonical || !aliases || !address_list || !hostent) {
            FreeGuestHostEnt();
            SetGuestErrno(12);
            return 0u;
        }
        env_.MemoryWrite32(aliases, 0u);
        for (std::size_t index = 0; index < addresses.size(); ++index) {
            const u32 address_block = allocate_block(4u);
            if (!address_block) {
                FreeGuestHostEnt();
                SetGuestErrno(12);
                return 0u;
            }
            env_.WriteBytes(address_block, addresses[index].data(), 4u);
            env_.MemoryWrite32(address_list + static_cast<u32>(index * 4u),
                               address_block);
        }
        env_.MemoryWrite32(address_list +
                           static_cast<u32>(addresses.size() * 4u), 0u);
        // 32-bit bionic hostent: name, aliases, addrtype, length, addr_list.
        env_.MemoryWrite32(hostent + 0u, canonical);
        env_.MemoryWrite32(hostent + 4u, aliases);
        env_.MemoryWrite32(hostent + 8u, 2u); // AF_INET in bionic
        env_.MemoryWrite32(hostent + 12u, 4u);
        env_.MemoryWrite32(hostent + 16u, address_list);
        guest_hostent_address_ = hostent;
        SetGuestErrno(0);
        return hostent;
    }
    u32 GuestGetNameInfo(u32 address, u32 length, u32 host_address,
                         u32 host_length, u32 service_address,
                         u32 service_length, u32 guest_flags) {
        sockaddr_storage storage{};
        int storage_length = 0;
        if (!ReadGuestSockaddr(address, length, storage, storage_length))
            return static_cast<u32>(EAI_FAIL);
        int flags = 0;
        if (running_cooperative_worker_)
            flags |= NI_NUMERICHOST | NI_NUMERICSERV;
        if (guest_flags & 0x01u) flags |= NI_NOFQDN;
        if (guest_flags & 0x02u) flags |= NI_NUMERICHOST;
        if (guest_flags & 0x04u) flags |= NI_NAMEREQD;
        if (guest_flags & 0x08u) flags |= NI_NUMERICSERV;
        if (guest_flags & 0x10u) flags |= NI_DGRAM;
        std::vector<char> host(host_length ? host_length : 1u, 0);
        std::vector<char> service(service_length ? service_length : 1u, 0);
        const int code = ::getnameinfo(
            reinterpret_cast<const sockaddr*>(&storage), storage_length,
            host_address && host_length ? host.data() : nullptr,
            host_address && host_length ? static_cast<DWORD>(host_length) : 0,
            service_address && service_length ? service.data() : nullptr,
            service_address && service_length ? static_cast<DWORD>(service_length) : 0,
            flags);
        if (code == 0) {
            if (host_address && host_length)
                env_.WriteBytes(host_address, host.data(), host.size());
            if (service_address && service_length)
                env_.WriteBytes(service_address, service.data(), service.size());
        }
        return static_cast<u32>(code);
    }
    u32 GuestShutdown(u32 guest_fd, u32 how) {
        const SOCKET socket_value = FindHostSocket(guest_fd);
        if (socket_value == INVALID_SOCKET) {
            SetGuestErrno(88);
            return static_cast<u32>(-1);
        }
        const int code = ::shutdown(socket_value, static_cast<int>(how));
        if (code == 0) { SetGuestErrno(0); return 0u; }
        return SocketFailure();
    }
    u32 GuestCreateSocketPair(u32 pair_address) {
        if (!pair_address || !winsock_initialized_) {
            SetGuestErrno(22);
            return static_cast<u32>(-1);
        }
        SOCKET listener = INVALID_SOCKET;
        SOCKET client = INVALID_SOCKET;
        SOCKET server = INVALID_SOCKET;
        ScopeExit cleanup([&] {
            if (listener != INVALID_SOCKET) closesocket(listener);
            if (client != INVALID_SOCKET) closesocket(client);
            if (server != INVALID_SOCKET) closesocket(server);
        });
        listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) return SocketFailure();
        sockaddr_in loopback{};
        loopback.sin_family = AF_INET;
        loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        loopback.sin_port = 0;
        if (::bind(listener, reinterpret_cast<const sockaddr*>(&loopback),
                   sizeof(loopback)) == SOCKET_ERROR ||
            ::listen(listener, 1) == SOCKET_ERROR)
            return SocketFailure();
        int loopback_length = sizeof(loopback);
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&loopback),
                          &loopback_length) == SOCKET_ERROR)
            return SocketFailure();
        client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET) return SocketFailure();
        if (::connect(client, reinterpret_cast<const sockaddr*>(&loopback),
                      sizeof(loopback)) == SOCKET_ERROR)
            return SocketFailure();
        server = ::accept(listener, nullptr, nullptr);
        if (server == INVALID_SOCKET) return SocketFailure();
        closesocket(listener);
        listener = INVALID_SOCKET;
        const u32 first = RegisterSocket(client);
        client = INVALID_SOCKET;
        const u32 second = RegisterSocket(server);
        server = INVALID_SOCKET;
        if (first == static_cast<u32>(-1) || second == static_cast<u32>(-1))
            return static_cast<u32>(-1);
        env_.MemoryWrite32(pair_address + 0u, first);
        env_.MemoryWrite32(pair_address + 4u, second);
        SetGuestErrno(0);
        return 0u;
    }
    u32 GuestWriteV(u32 guest_fd, u32 vectors_address, u32 vector_count) {
        if (vector_count > 1024u || (vector_count && !vectors_address)) {
            SetGuestErrno(22);
            return static_cast<u32>(-1);
        }
        u64 total = 0u;
        for (u32 index = 0; index < vector_count; ++index) {
            GuestIovec vector{};
            if (!env_.ReadBytes(vectors_address + index * sizeof(GuestIovec),
                                &vector, sizeof(vector))) {
                SetGuestErrno(14);
                return total ? static_cast<u32>(total) : static_cast<u32>(-1);
            }
            if (!vector.length) continue;
            const u32 written = IsGuestSocket(guest_fd)
                ? GuestSend(guest_fd, vector.base, vector.length, 0u)
                : WriteGuestFile(vector.base, 1u, vector.length, guest_fd);
            if (written == static_cast<u32>(-1))
                return total ? static_cast<u32>(total) : written;
            total += written;
            if (written < vector.length || total > std::numeric_limits<u32>::max())
                break;
        }
        return static_cast<u32>(std::min<u64>(
            total, std::numeric_limits<u32>::max()));
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
        cooperative_worker_runnable_ = true;
        cooperative_worker_yielded_ = false;
        cooperative_worker_done_ = false;
        if (thread_address) env_.MemoryWrite32(thread_address, next_thread_id_++);
        ++cooperative_worker_registered_count_;
        log_ << "[host] Unified ARMv7 guest worker registered at 0x" << std::hex << start_routine
             << " arg=0x" << argument << std::dec
             << " scheduling=deferred-frame-pump+safe-stub-return+wall-watchdog+forensic-trace" << '\n';
        log_.flush();
        return true;
    }

    bool PumpCooperativeWorkerSlice(const char* trigger = "frame-pump") {
        if (!cooperative_worker_.valid || running_cooperative_worker_ ||
            !cooperative_worker_runnable_) return true;

        // NetworkTest2 bounded translated ARM ticks and total slice wall time, but
        // either limit is only observed after Dynarmic::A32::Jit::Run() returns.
        // The v22 CCHttpClient worker can remain in one translated guest block long
        // enough to freeze the window.  A separate host watchdog therefore uses
        // Dynarmic's asynchronous HaltExecution path to regain control even when a
        // guest block does not naturally reach the tick callback in time.
        constexpr u32 kTicksPerRun = 50000u;
        constexpr u32 kMaximumRunsPerSlice = 24u;
        constexpr auto kMaximumSlice = std::chrono::milliseconds(4);
        constexpr auto kHardRunWatchdog = std::chrono::milliseconds(8);
        const u64 resume_number = ++cooperative_worker_resume_count_;
        if (resume_number <= 128u) {
            log_ << "[host] Unified ARMv7 worker slice #" << resume_number
                 << " trigger=" << (trigger ? trigger : "unspecified")
                 << " pc=0x" << std::hex << cooperative_worker_.regs[15]
                 << " lr=0x" << cooperative_worker_.regs[14] << std::dec << '\n';
            log_.flush();
        }

        const auto saved_regs = cpu_.Regs();
        const auto saved_ext = cpu_.ExtRegs();
        const u32 saved_cpsr = cpu_.Cpsr();
        const u32 saved_fpscr = cpu_.Fpscr();
        ScopeExit restore([&] {
            cpu_.Regs() = saved_regs;
            cpu_.ExtRegs() = saved_ext;
            cpu_.SetCpsr(saved_cpsr);
            cpu_.SetFpscr(saved_fpscr);
            running_cooperative_worker_ = false;
        });

        cpu_.Regs() = cooperative_worker_.regs;
        cpu_.ExtRegs() = cooperative_worker_.ext_regs;
        cpu_.SetCpsr(cooperative_worker_.cpsr);
        cpu_.SetFpscr(cooperative_worker_.fpscr);
        running_cooperative_worker_ = true;
        CompletePendingStubReturn("slice-entry-recovery");
        cooperative_worker_yielded_ = false;
        cooperative_worker_done_ = false;

        const auto started = std::chrono::steady_clock::now();
        u32 runs = 0u;
        bool watchdog_preempted = false;
        while (!cooperative_worker_yielded_ && !cooperative_worker_done_ &&
               runs < kMaximumRunsPerSlice &&
               std::chrono::steady_clock::now() - started < kMaximumSlice) {
            ++runs;
            env_.ResetStopState();
            env_.ticks_left = kTicksPerRun;

            std::mutex watchdog_mutex;
            std::condition_variable watchdog_cv;
            bool watchdog_cancelled = false;
            std::atomic<bool> watchdog_fired{false};
            std::thread watchdog([&] {
                std::unique_lock<std::mutex> lock(watchdog_mutex);
                if (!watchdog_cv.wait_for(lock, kHardRunWatchdog,
                                          [&] { return watchdog_cancelled; })) {
                    watchdog_fired.store(true, std::memory_order_release);
                    cpu_.HaltExecution(kCallbackHalt);
                }
            });
            auto stop_watchdog = [&] {
                {
                    std::lock_guard<std::mutex> lock(watchdog_mutex);
                    watchdog_cancelled = true;
                }
                watchdog_cv.notify_one();
                if (watchdog.joinable()) watchdog.join();
                cpu_.ClearHalt(kCallbackHalt);
            };
            ScopeExit watchdog_cleanup(stop_watchdog);

            cpu_.Run();
            stop_watchdog();

            if (env_.invalid_access) return Fail("Unified ARMv7 worker invalid guest memory");
            if (env_.interpreter_fallback) return Fail("Unified ARMv7 worker interpreter fallback");
            if (env_.exception_seen) return Fail("Unified ARMv7 worker guest exception");
            if (watchdog_fired.load(std::memory_order_acquire)) {
                watchdog_preempted = true;
                ++cooperative_worker_watchdog_count_;
                if (cooperative_worker_watchdog_count_ <= 128u) {
                    log_ << "[host] Unified ARMv7 worker watchdog preempted run=" << runs
                         << " pc=0x" << std::hex << cpu_.Regs()[15]
                         << " lr=0x" << cpu_.Regs()[14] << std::dec
                         << " pc-desc=" << DescribeAddress(cpu_.Regs()[15])
                         << " caller=" << DescribeAddress(cpu_.Regs()[14])
                         << " hard_limit_ms=" << kHardRunWatchdog.count() << '\n';
                    DumpImportTrace("worker-watchdog", 128u);
                    log_.flush();
                }
                break;
            }
            if (env_.svc_pending) {
                if (env_.pending_svc == kSvcReturn) {
                    cooperative_worker_done_ = true;
                    break;
                }
                if (!HandleSvc(env_.pending_svc, "Unified ARMv7 CCHttpClient worker"))
                    return false;
                // HandleSvc leaves PC at the second half of the synthetic SVC/BX-LR
                // trampoline.  NetworkTest2-4 could save that transient PC at a
                // timeslice boundary.  Resuming there was the deterministic freeze
                // seen in CRYPTO_malloc/CRYPTO_zalloc.  Retire BX LR atomically.
                CompletePendingStubReturn("after-worker-svc");
            } else if (env_.ticks_left != 0u) {
                return Fail("Unified ARMv7 worker stopped without a trap");
            }
        }

        CompletePendingStubReturn("before-slice-save");
        if (IsImportStubReturnPc(cpu_.Regs()[15]) || IsVmOrJniStubReturnPc(cpu_.Regs()[15])) {
            DumpImportTrace("unsafe-worker-save-pc", 128u);
            return Fail("Unified ARMv7 refused to save worker inside synthetic stub");
        }
        cooperative_worker_.regs = cpu_.Regs();
        cooperative_worker_.ext_regs = cpu_.ExtRegs();
        cooperative_worker_.cpsr = cpu_.Cpsr();
        cooperative_worker_.fpscr = cpu_.Fpscr();

        if (cooperative_worker_done_) {
            cooperative_worker_.valid = false;
            cooperative_worker_runnable_ = false;
            if (cooperative_worker_done_count_++ < 16u)
                log_ << "[host] Unified ARMv7 worker exited\n";
        } else if (cooperative_worker_yielded_) {
            cooperative_worker_runnable_ = false;
            ++cooperative_worker_yield_count_;
            if (network_worker_runs_++ < 128u)
                log_ << "[host] Unified ARMv7 worker waiting for signal"
                     << " yields=" << cooperative_worker_yield_count_ << '\n';
        } else {
            cooperative_worker_runnable_ = true;
            ++cooperative_worker_slice_yield_count_;
            if (cooperative_worker_slice_yield_count_ <= 128u) {
                const double elapsed_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                log_ << "[host] Unified ARMv7 worker timeslice yield runs=" << runs
                     << " elapsed_ms=" << std::fixed << std::setprecision(2)
                     << elapsed_ms
                     << " watchdog=" << (watchdog_preempted ? 1 : 0)
                     << " saved-pc=0x" << std::hex << cooperative_worker_.regs[15]
                     << " saved-lr=0x" << cooperative_worker_.regs[14] << std::dec
                     << " pc-desc=" << DescribeAddress(cooperative_worker_.regs[15]) << '\n';
                if (cooperative_worker_slice_yield_count_ <= 16u)
                    DumpImportTrace("worker-slice-yield", 96u);
            }
        }
        log_.flush();
        return true;
    }

    u32 HostGetFileDataFromZip(u32 zip_path_address, u32 member_address, u32 size_address) {
        if (size_address) env_.MemoryWrite32(size_address, 0u);
        const std::string zip_path = ReadCString(zip_path_address);
        const std::string member = ReadCString(member_address);
        if (member.empty()) return 0;
        if (!zip_path.empty() && TranslatePath(zip_path) != apk_path_ &&
            zip_path != "game.apk" && !zip_path.ends_with("/game.apk")) return 0;
        auto bytes = apk_member_cache_.Load(member);
        if (!bytes) {
            std::string resolved_member;
            bytes = LoadExtensionResourceFallback(member, &resolved_member);
            if (bytes) {
                log_ << "[host] APK ZIP extension path fallback: " << member
                     << " -> " << resolved_member
                     << " bytes=" << bytes->size() << '\n';
            }
        }
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
        const std::string member = ReadCString(member_address);
        if (apk_member_cache_.Exists(member)) return 1u;
        return ExtensionResourceExistsFallback(member) ? 1u : 0u;
    }

    enum class FmodObjectKind { System, Sound, Stream, Channel, Dsp };
    struct FmodObjectState {
        FmodObjectKind kind = FmodObjectKind::System;
        std::string path;
        u32 mode = 0;
        unsigned host_id = 0;
        float volume = 1.0f;
        bool background = false;
        bool loop = false;
        bool paused = false;
        bool playing = false;
        bool started = false;
        bool stopped = false;
        bool mixer_paused = false;
        bool observed_playing = false;
        bool callback_sent = false;
        bool initialized = false;
        bool suspended = false;
        bool input_metering_enabled = false;
        bool output_metering_enabled = false;
        u32 callback_address = 0;
        u32 position_ms = 0;
        u32 stream_buffer_size = 16384;
        u32 stream_buffer_type = 0x00000008u; // FMOD_TIMEUNIT_RAWBYTES
        u32 output_type = 0;
        u32 software_rate = 44100;
        u32 software_speaker_mode = 3; // FMOD_SPEAKERMODE_STEREO
        u32 software_raw_speakers = 0;
    };

    static bool IsFmodImport(const std::string& name) {
        return IsFmodImportName(name);
    }

    static bool FmodPathHasExtension(const std::string& path,
                                     std::string_view extension) {
        if (path.size() < extension.size()) return false;
        const std::size_t offset = path.size() - extension.size();
        for (std::size_t index = 0; index < extension.size(); ++index) {
            char left = path[offset + index];
            char right = extension[index];
            if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
            if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
            if (left != right) return false;
        }
        return true;
    }

    u32 NewFmodObject(FmodObjectKind kind, const std::string& path = {}) {
        const u32 address = Allocate(64u);
        if (!address) return 0;
        void* memory = env_.HostPointer(address, 64u);
        if (memory) std::memset(memory, 0, 64u);
        FmodObjectState state;
        state.kind = kind;
        state.path = path;
        state.background = kind == FmodObjectKind::Stream ||
                           FmodPathHasExtension(path, ".mp3");
        if (state.background && FmodPathHasExtension(path, ".mp3") &&
            path.find("menuLoop.mp3") == std::string::npos)
            v22_recent_music_path_ = path;
        if (kind == FmodObjectKind::System) {
            state.stream_buffer_size = 16384u;
            state.stream_buffer_type = 0x00000008u;
            state.software_rate = 44100u;
            state.software_speaker_mode = 3u;
        }
        fmod_objects_[address] = std::move(state);
        return address;
    }

    FmodObjectState* FindFmodObject(u32 address) {
        const auto found = fmod_objects_.find(address);
        return found == fmod_objects_.end() ? nullptr : &found->second;
    }

    bool FmodBackendPlaying(FmodObjectState* channel) {
        if (!channel || channel->kind != FmodObjectKind::Channel || channel->stopped)
            return false;
        if (!channel->started) return channel->paused;
        if (channel->paused) return true;
        if (channel->background) return audio_is_background_playing() != 0;
        return channel->host_id && audio_is_effect_playing(channel->host_id) != 0;
    }

    void StartFmodBackground(FmodObjectState& channel) {
        if (!channel.background || channel.started || channel.stopped ||
            channel.path.empty()) return;
        audio_play_background(channel.path.c_str(), channel.loop ? 1 : 0);
        channel.started = true;
        channel.playing = true;
        if (channel.position_ms) {
            audio_set_background_time(static_cast<float>(channel.position_ms) / 1000.0f);
        }
        audio_set_background_volume(channel.volume);
        log_ << "[host] FMOD bridge: released deferred music channel path="
             << SanitizeLogText(channel.path) << " position_ms="
             << channel.position_ms << " loop=" << (channel.loop ? 1 : 0) << '\n';
    }

    void SetFmodChannelPaused(FmodObjectState& channel, bool paused) {
        if (channel.kind != FmodObjectKind::Channel || channel.stopped) return;
        if (channel.background && !channel.started) {
            channel.paused = paused;
            if (!paused) StartFmodBackground(channel);
            return;
        }
        if (channel.paused == paused) return;
        if (channel.background) {
            if (paused) {
                const float seconds = audio_get_background_time();
                if (seconds >= 0.0f)
                    channel.position_ms = static_cast<u32>(seconds * 1000.0f + 0.5f);
                audio_pause_background();
            } else {
                audio_resume_background_from(
                    static_cast<float>(channel.position_ms) / 1000.0f);
            }
        } else if (channel.host_id) {
            if (paused) audio_pause_effect(channel.host_id);
            else audio_resume_effect(channel.host_id);
        }
        channel.paused = paused;
    }

    void FillFmodMeteringInfo(u32 address, float peak) {
        if (!address) return;
        // FMOD_DSP_METERING_INFO in the 1.05 ABI is 264 bytes:
        // int numsamples; float peak[32]; float rms[32]; short channels/pad.
        std::array<u8, 264> bytes{};
        if (!env_.WriteBytes(address, bytes.data(), bytes.size())) return;
        env_.MemoryWrite32(address + 0u, peak > 0.0f ? 1u : 0u);
        env_.MemoryWrite32(address + 4u, FloatToWord(peak));
        env_.MemoryWrite32(address + 8u, FloatToWord(peak));
        const float rms = peak * 0.70710678f;
        env_.MemoryWrite32(address + 132u, FloatToWord(rms));
        env_.MemoryWrite32(address + 136u, FloatToWord(rms));
        env_.MemoryWrite16(address + 260u, 2u);
    }

    bool DispatchFmod(ImportRecord& import) {
        const std::string& name = import.name;
        const u32 r0 = cpu_.Regs()[0], r1 = cpu_.Regs()[1], r2 = cpu_.Regs()[2], r3 = cpu_.Regs()[3];
        constexpr u32 kFmodOk = 0;
        constexpr u32 kFmodInvalidParam = 31;
        constexpr u32 kFmodLoopNormal = 0x00000002u;
        auto finish = [&](u32 value) {
            cpu_.Regs()[0] = value;
            ResumeAfterStub(import.address);
            return true;
        };
        if (fmod_call_log_count_ < 96u) {
            ++fmod_call_log_count_;
            log_ << "[host] FMOD guest call #" << fmod_call_log_count_
                 << " " << name << " r0=0x" << std::hex << r0
                 << " r1=0x" << r1 << " r2=0x" << r2
                 << " r3=0x" << r3 << std::dec << '\n';
        }
        if (name == "FMOD_System_Create") {
            const u32 system = NewFmodObject(FmodObjectKind::System);
            if (r0) env_.MemoryWrite32(r0, system);
            if (import.calls == 1)
                log_ << "RESULT: DYNARMIC_V22_FMOD_BRIDGE_READY system=0x"
                     << std::hex << system << std::dec
                     << " version=0x00010504 deferred-music=1\n";
            return finish(system ? kFmodOk : kFmodInvalidParam);
        }
        if (name == "_ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE" ||
            name == "_ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE") {
            const bool stream = name.find("createStream") != std::string::npos;
            const std::string path = ReadCString(r1);
            const u32 object = NewFmodObject(stream ? FmodObjectKind::Stream : FmodObjectKind::Sound, path);
            if (FmodObjectState* state = FindFmodObject(object)) {
                state->mode = r2;
                state->loop = (r2 & kFmodLoopNormal) != 0;
            }
            const u32 output = ArgWord(4);
            if (output) env_.MemoryWrite32(output, object);
            if (stream || FmodPathHasExtension(path, ".mp3"))
                audio_preload_background(path.c_str());
            else
                audio_preload_effect(path.c_str());
            return finish(object ? kFmodOk : kFmodInvalidParam);
        }
        if (name == "_ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE") {
            FmodObjectState* sound = FindFmodObject(r1);
            if (!sound) return finish(kFmodInvalidParam);
            const u32 channel_address = NewFmodObject(FmodObjectKind::Channel, sound->path);
            FmodObjectState* channel = FindFmodObject(channel_address);
            if (channel) {
                channel->background = sound->background;
                channel->mode = sound->mode;
                channel->loop = (sound->mode & kFmodLoopNormal) != 0;
                channel->paused = r3 != 0;
                channel->playing = true;
                channel->stopped = false;
                if (channel->background) {
                    channel->volume = audio_get_background_volume();
                    if (channel->paused) {
                        log_ << "[host] FMOD bridge: music armed paused path="
                             << SanitizeLogText(channel->path) << '\n';
                    } else {
                        StartFmodBackground(*channel);
                    }
                } else {
                    channel->volume = audio_get_effects_volume();
                    channel->host_id = audio_play_effect(channel->path.c_str(), channel->loop ? 1 : 0);
                    channel->started = channel->host_id != 0;
                    if (channel->paused && channel->host_id)
                        audio_pause_effect(channel->host_id);
                }
            }
            const u32 output = ArgWord(4);
            if (output) env_.MemoryWrite32(output, channel_address);
            return finish(channel_address ? kFmodOk : kFmodInvalidParam);
        }
        if (name == "_ZN4FMOD6System19setStreamBufferSizeEjj") {
            if (FmodObjectState* state = FindFmodObject(r0)) {
                state->stream_buffer_size = r1;
                state->stream_buffer_type = r2;
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System19getStreamBufferSizeEPjS1_") {
            FmodObjectState* state = FindFmodObject(r0);
            if (r1) env_.MemoryWrite32(r1, state ? state->stream_buffer_size : 16384u);
            if (r2) env_.MemoryWrite32(r2, state ? state->stream_buffer_type : 0x00000008u);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System10getVersionEPj") {
            if (r1) env_.MemoryWrite32(r1, 0x00010504u);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE") {
            if (FmodObjectState* state = FindFmodObject(r0)) state->output_type = r1;
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi") {
            if (FmodObjectState* state = FindFmodObject(r0)) {
                state->software_rate = r1 ? r1 : 44100u;
                state->software_speaker_mode = r2 ? r2 : 3u;
                state->software_raw_speakers = r3;
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System17getSoftwareFormatEPiP16FMOD_SPEAKERMODES1_") {
            FmodObjectState* state = FindFmodObject(r0);
            if (r1) env_.MemoryWrite32(r1, state ? state->software_rate : 44100u);
            if (r2) env_.MemoryWrite32(r2, state ? state->software_speaker_mode : 3u);
            if (r3) env_.MemoryWrite32(r3, state ? state->software_raw_speakers : 0u);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System4initEijPv") {
            if (FmodObjectState* state = FindFmodObject(r0)) state->initialized = true;
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System6updateEv") {
            // Keep cached position/playing state coherent. Callback delivery is
            // intentionally deferred until a context-preserving guest callback
            // trampoline is available; the game does not need it for playback.
            for (auto& [address, state] : fmod_objects_) {
                (void)address;
                if (state.kind != FmodObjectKind::Channel || state.stopped) continue;
                const bool now_playing = FmodBackendPlaying(&state);
                if (state.background && state.started) {
                    const float seconds = audio_get_background_time();
                    if (seconds >= 0.0f)
                        state.position_ms = static_cast<u32>(seconds * 1000.0f + 0.5f);
                }
                state.observed_playing = now_playing;
                state.playing = now_playing;
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System12mixerSuspendEv" ||
            name == "_ZN4FMOD6System11mixerResumeEv") {
            const bool suspend = name.find("Suspend") != std::string::npos;
            if (FmodObjectState* system = FindFmodObject(r0)) system->suspended = suspend;
            for (auto& [address, channel] : fmod_objects_) {
                (void)address;
                if (channel.kind != FmodObjectKind::Channel || channel.stopped) continue;
                if (suspend && !channel.paused) {
                    SetFmodChannelPaused(channel, true);
                    channel.mixer_paused = true;
                } else if (!suspend && channel.mixer_paused) {
                    channel.mixer_paused = false;
                    SetFmodChannelPaused(channel, false);
                }
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System5closeEv") {
            audio_stop_background();
            audio_stop_all_effects();
            if (FmodObjectState* state = FindFmodObject(r0)) state->initialized = false;
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl9setVolumeEf") {
            if (FmodObjectState* state = FindFmodObject(r0)) {
                state->volume = WordToFloat(r1);
                if (state->background) audio_set_background_volume(state->volume);
                else if (state->host_id) audio_set_effect_volume(state->host_id, state->volume);
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl9getVolumeEPf") {
            FmodObjectState* state = FindFmodObject(r0);
            if (r1) env_.MemoryWrite32(r1, FloatToWord(state ? state->volume : 1.0f));
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl9setPausedEb") {
            if (FmodObjectState* state = FindFmodObject(r0))
                SetFmodChannelPaused(*state, r1 != 0);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl9getPausedEPb") {
            FmodObjectState* state = FindFmodObject(r0);
            if (r1) env_.MemoryWrite8(r1, state && state->paused ? 1u : 0u);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl9isPlayingEPb") {
            const bool playing = FmodBackendPlaying(FindFmodObject(r0));
            if (r1) env_.MemoryWrite8(r1, playing ? 1u : 0u);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl4stopEv") {
            if (FmodObjectState* state = FindFmodObject(r0)) {
                if (state->background && state->started) audio_stop_background();
                else if (state->host_id) audio_stop_effect(state->host_id);
                state->stopped = true;
                state->playing = false;
                state->paused = false;
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl7setModeEj") {
            if (FmodObjectState* state = FindFmodObject(r0)) {
                const bool old_loop = state->loop;
                state->mode = r1;
                state->loop = (r1 & kFmodLoopNormal) != 0;
                if (state->background && state->started && !state->stopped && old_loop != state->loop) {
                    const float seconds = audio_get_background_time();
                    if (seconds >= 0.0f)
                        state->position_ms = static_cast<u32>(seconds * 1000.0f + 0.5f);
                    audio_play_background(state->path.c_str(), state->loop ? 1 : 0);
                    if (state->position_ms)
                        audio_set_background_time(static_cast<float>(state->position_ms) / 1000.0f);
                    if (state->paused) audio_pause_background();
                }
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl11setCallbackEPF11FMOD_RESULTP19FMOD_CHANNELCONTROL24FMOD_CHANNELCONTROL_TYPE33FMOD_CHANNELCONTROL_CALLBACK_TYPEPvS6_E") {
            if (FmodObjectState* state = FindFmodObject(r0)) state->callback_address = r1;
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD7Channel11getPositionEPjj") {
            u32 position = 0;
            if (FmodObjectState* state = FindFmodObject(r0)) {
                position = state->position_ms;
                if (state->background && state->started) {
                    const float seconds = audio_get_background_time();
                    if (seconds >= 0.0f) {
                        position = static_cast<u32>(seconds * 1000.0f + 0.5f);
                        state->position_ms = position;
                    }
                }
            }
            if (r1) env_.MemoryWrite32(r1, position);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD7Channel11setPositionEjj") {
            if (FmodObjectState* state = FindFmodObject(r0)) {
                state->position_ms = r1;
                if (state->background && state->started)
                    audio_set_background_time(static_cast<float>(r1) / 1000.0f);
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD14ChannelControl6getDSPEiPPNS_3DSPE") {
            if (!fmod_dsp_address_) fmod_dsp_address_ = NewFmodObject(FmodObjectKind::Dsp);
            if (r2) env_.MemoryWrite32(r2, fmod_dsp_address_);
            return finish(fmod_dsp_address_ ? kFmodOk : kFmodInvalidParam);
        }
        if (name == "_ZN4FMOD3DSP18setMeteringEnabledEbb") {
            if (FmodObjectState* state = FindFmodObject(r0)) {
                state->input_metering_enabled = r1 != 0;
                state->output_metering_enabled = r2 != 0;
            }
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD3DSP15getMeteringInfoEP22FMOD_DSP_METERING_INFOS2_") {
            FmodObjectState* state = FindFmodObject(r0);
            const float peak = state && (state->input_metering_enabled || state->output_metering_enabled)
                                   ? audio_get_output_peak() : 0.0f;
            FillFmodMeteringInfo(r1, state && state->input_metering_enabled ? peak : 0.0f);
            FillFmodMeteringInfo(r2, state && state->output_metering_enabled ? peak : 0.0f);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD5Sound7releaseEv") {
            fmod_objects_.erase(r0);
            Free(r0);
            return finish(kFmodOk);
        }
        if (name == "_ZN4FMOD6System7releaseEv") {
            fmod_objects_.erase(r0);
            Free(r0);
            return finish(kFmodOk);
        }
        return finish(kFmodOk);
    }

    bool DispatchImport(ImportRecord& import) {
        const std::string& name = import.name;
        const u32 r0 = cpu_.Regs()[0], r1 = cpu_.Regs()[1], r2 = cpu_.Regs()[2], r3 = cpu_.Regs()[3];
        u32 result = 0;
        bool result_set = true;

        if (import.is_gl) return DispatchGl(import);
        if (IsFmodImport(name)) return DispatchFmod(import);

        auto finish_hot = [&](u32 value) {
            cpu_.Regs()[0] = value;
            ResumeAfterStub(import.address);
            return true;
        };
        // These dominate APK/level loading. Keep them ahead of the large generic
        // import chain and avoid temporary allocations on every SVC crossing.
        if (name == "_ZN11HookManager7do_hookEPvS0_S0_b" ||
            name == "__dynarmic_v22_hookwrap") {
            if (!HostV22HookManagerDoHook(r0, r1, r2)) return false;
            return finish_hot(0u);
        }
        if (name == "__dynarmic_v22_level_settings_from_string") {
            u32 value = 0u;
            if (!HostV22LevelSettingsFromString(value)) return false;
            return finish_hot(value);
        }
        if (name == "__dynarmic_v22_prepare_level_setup")
            return HostV22PrepareLevelSetup();
        if (name == "__dynarmic_v22_editor_visibility") {
            if (!HostV22EditorVisibility(r0, r1)) return false;
            return finish_hot(0u);
        }
        if (name == "__dynarmic_v22_play_visibility") {
            if (!HostV22PlayVisibility(r0)) return false;
            return finish_hot(0u);
        }
        if (name == "__dynarmic_v22_update_camera_background") {
            u32 suppress = 0u;
            if (!HostV22UpdateCameraBackground(r0, r1, suppress)) return false;
            return finish_hot(suppress);
        }
        if (name == "__dynarmic_v22_reject_null_batch_texture") {
            if (!HostV22RejectNullBatchTexture(r0)) return false;
            return finish_hot(0u);
        }
        if (name == "__dynarmic_v22_batch_update_blend") {
            if (!HostV22BatchUpdateBlend(r0)) return false;
            return finish_hot(0u);
        }
        if (name == "__dynarmic_v22_edit_level_onEdit") {
            const bool ok = HostV22EditLevelButton();
            if (!ok) return false;
            return finish_hot(cpu_.Regs()[0]);
        }
        if (name == "__dynarmic_v22_pause_onEdit") {
            const bool ok = HostV22GameplayEditorButton("pause-menu");
            if (!ok) return false;
            return finish_hot(cpu_.Regs()[0]);
        }
        if (name == "__dynarmic_v22_end_onEdit") {
            const bool ok = HostV22GameplayEditorButton("end-level");
            if (!ok) return false;
            return finish_hot(cpu_.Regs()[0]);
        }
        if (name == "__dynarmic_v22_creator_editor_unlock") {
            if (!v22_creator_callback_address_)
                return Fail("MenuLayer creator callback address is unavailable");
            ++v22_creator_unlock_calls_;
            log_ << "[host] V22 full-version button redirected through normal CreatorLayer path call="
                 << v22_creator_unlock_calls_ << '\n';
            log_.flush();
            cpu_.Regs()[15] = v22_creator_callback_address_ & ~1u;
            cpu_.SetCpsr((cpu_.Cpsr() & ~0x20u) |
                         ((v22_creator_callback_address_ & 1u) ? 0x20u : 0u));
            return true;
        }
        if (name == "__dynarmic_ziputils_ccInflateMemory") {
            u32 value = 0;
            if (!HostV22InflateMemory(r0, r1, r2, value)) return false;
            return finish_hot(value);
        }
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
        if (name == "__dynarmic_v22_native_http_send") {
            if (!QueueNativeHttpRequest(r0, r1)) return false;
            return finish_hot(0u);
        }
        if (name == "__aeabi_memcpy" || name == "__aeabi_memcpy4" ||
            name == "__aeabi_memcpy8" || name == "__aeabi_memmove" ||
            name == "__aeabi_memmove4" || name == "__aeabi_memmove8")
            return finish_hot(CopyGuest(r0, r1, r2) ? r0 : 0u);
        if (name == "__aeabi_memclr" || name == "__aeabi_memclr4" ||
            name == "__aeabi_memclr8") {
            void* destination = env_.HostPointer(r0, r1);
            if (destination) std::memset(destination, 0, r1);
            return finish_hot(destination ? r0 : 0u);
        }
        if (name == "__aeabi_memset") {
            void* destination = env_.HostPointer(r0, r1);
            if (destination)
                std::memset(destination, static_cast<int>(r2 & 0xffu), r1);
            return finish_hot(destination ? r0 : 0u);
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
        } else if (name == "writev") {
#ifdef _WIN32
            result = GuestWriteV(r0, r1, r2);
#else
            result = static_cast<u32>(-1);
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
            // NetworkTest never runs a guest worker recursively inside the
            // foreground import.  Register it and let the host frame pump give
            // it short slices, keeping the Win32 client responsive.
            result = InitializeCooperativeWorker(r0, r2, r3)
                ? 0u : static_cast<u32>(-1);
        } else if (name == "pthread_exit") {
            if (running_cooperative_worker_) {
                cooperative_worker_done_ = true;
                return true;
            }
            result = 0;
        } else if (name == "sem_init") {
            semaphores_[r0] = r2;
            result = 0;
        } else if (name == "sem_destroy") {
            semaphores_.erase(r0);
            result = 0;
        } else if (name == "sem_wait") {
            u32& count = semaphores_[r0];
            if (count) {
                --count;
                result = 0;
            } else if (running_cooperative_worker_) {
                cooperative_worker_yielded_ = true;
                cooperative_worker_runnable_ = false;
                return true; // Keep PC on sem_wait until sem_post supplies work.
            } else {
                SetGuestErrno(11);
                result = static_cast<u32>(-1);
            }
        } else if (name == "sem_post") {
            ++semaphores_[r0];
            cooperative_worker_runnable_ = cooperative_worker_.valid;
            if (cooperative_condition_log_count_++ < 512u)
                log_ << "[host] Unified ARMv7 semaphore post sem=0x" << std::hex
                     << r0 << std::dec << " pending=" << semaphores_[r0]
                     << " wake=deferred-next-frame" << '\n';
            // Do not recursively run the HTTP worker inside the foreground import.
            // The main guest call must return so the button animation and UI frame
            // can complete. PumpNetworkWorkerFrame starts the worker next frame.
            result = 0u;
        } else if (name == "pthread_cond_init") {
            condition_signals_[r0] = 0u;
            result = 0;
        } else if (name == "pthread_cond_destroy") {
            condition_signals_.erase(r0);
            result = 0;
        } else if (name == "pthread_cond_signal" ||
                   name == "pthread_cond_broadcast") {
            ++condition_signals_[r0];
            cooperative_worker_runnable_ = cooperative_worker_.valid;
            if (cooperative_condition_log_count_++ < 512u)
                log_ << "[host] Unified ARMv7 condition signal cond=0x" << std::hex
                     << r0 << std::dec << " pending=" << condition_signals_[r0]
                     << " worker-valid=" << (cooperative_worker_.valid ? 1 : 0)
                     << " wake=deferred-next-frame" << '\n';
            result = 0u;
        } else if (name == "pthread_cond_wait") {
            u32& pending = condition_signals_[r0];
            if (pending) {
                --pending;
                result = 0;
            } else if (running_cooperative_worker_) {
                cooperative_worker_yielded_ = true;
                cooperative_worker_runnable_ = false;
                if (cooperative_condition_log_count_++ < 128u)
                    log_ << "[host] Unified ARMv7 worker cond-wait cond=0x"
                         << std::hex << r0 << std::dec << '\n';
                return true; // Re-execute after a future signal token appears.
            } else {
                // Never block the single foreground host thread.
                result = 0;
            }
        } else if (name == "pthread_detach" || name == "pthread_mutex_init" ||
                   name == "pthread_mutex_destroy" || name == "pthread_mutex_lock" ||
                   name == "pthread_mutex_unlock") result = 0;
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
        } else if (name == "isalnum") result = std::isalnum(static_cast<unsigned char>(r0)) != 0;
        else if (name == "isalpha") result = std::isalpha(static_cast<unsigned char>(r0)) != 0;
        else if (name == "isxdigit") result = std::isxdigit(static_cast<unsigned char>(r0)) != 0;
        else if (name == "isprint") result = std::isprint(static_cast<unsigned char>(r0)) != 0;
        else if (name == "isspace") result = std::isspace(static_cast<unsigned char>(r0)) != 0;
        else if (name == "tolower" || name == "towlower") result = static_cast<u32>(std::tolower(static_cast<unsigned char>(r0)));
        else if (name == "toupper" || name == "towupper") result = static_cast<u32>(std::toupper(static_cast<unsigned char>(r0)));
        else if (name == "setlocale") result = c_locale_address_;
        else if (name == "getenv") result = 0;
        else if (name == "geteuid" || name == "getuid") result = 1000;
        else if (name == "getegid" || name == "getgid") result = 1000;
        else if (name == "getpid") result = 4242;
        else if (name == "pthread_self") result = 1;
        else if (name == "pthread_equal") result = r0 == r1 ? 1u : 0u;
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
        else if (name == "srand") { random_state_ = r0 ? r0 : 1u; result = 0; }
        else if (name == "rand") { random_state_ = random_state_ * 1103515245ull + 12345ull; result = static_cast<u32>((random_state_ >> 16) & 0x7fffu); }
        else if (name == "srand48") { random_state_ = r0 ? r0 : 1u; result = 0; }
        else if (name == "lrand48") { random_state_ = random_state_ * 25214903917ull + 11ull; result = static_cast<u32>((random_state_ >> 17) & 0x7fffffffu); }
        else if (name == "atof") { ReturnDouble(std::strtod(ReadCString(r0).c_str(), nullptr)); result_set=false; }
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
        else if (name == "acosf") result = FloatToWord(std::acos(WordToFloat(r0)));
        else if (name == "asinf") result = FloatToWord(std::asin(WordToFloat(r0)));
        else if (name == "atan2f") result = FloatToWord(std::atan2(WordToFloat(r0),WordToFloat(r1)));
        else if (name == "ceilf") result = FloatToWord(std::ceil(WordToFloat(r0)));
        else if (name == "cosf") result = FloatToWord(std::cos(WordToFloat(r0)));
        else if (name == "expf") result = FloatToWord(std::exp(WordToFloat(r0)));
        else if (name == "floorf") result = FloatToWord(std::floor(WordToFloat(r0)));
        else if (name == "fmodf") result = FloatToWord(std::fmod(WordToFloat(r0),WordToFloat(r1)));
        else if (name == "logf") result = FloatToWord(std::log(WordToFloat(r0)));
        else if (name == "powf") result = FloatToWord(std::pow(WordToFloat(r0),WordToFloat(r1)));
        else if (name == "sinf") result = FloatToWord(std::sin(WordToFloat(r0)));
        else if (name == "sqrtf") result = FloatToWord(std::sqrt(WordToFloat(r0)));
        else if (name == "tanf") result = FloatToWord(std::tan(WordToFloat(r0)));
        else if (name == "lroundf") result = static_cast<u32>(std::lround(WordToFloat(r0)));
        else if (name == "__isnanf") result = std::isnan(WordToFloat(r0)) ? 1u : 0u;
        else if (name == "pow") { ReturnDouble(std::pow(WordsToDouble(r0,r1),WordsToDouble(r2,r3))); result_set=false; }
        else if (name == "fmod") { ReturnDouble(std::fmod(WordsToDouble(r0,r1),WordsToDouble(r2,r3))); result_set=false; }
        else if (name == "exp") { ReturnDouble(std::exp(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "log") { ReturnDouble(std::log(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "log10") { ReturnDouble(std::log10(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "sinh") { ReturnDouble(std::sinh(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "cosh") { ReturnDouble(std::cosh(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "tanh") { ReturnDouble(std::tanh(WordsToDouble(r0,r1))); result_set=false; }
        else if (name == "asinh") { ReturnDouble(std::asinh(WordsToDouble(r0,r1))); result_set=false; }
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
            const bool request_like = text.find("gameVersion=") != std::string::npos ||
                text.find("songID=") != std::string::npos ||
                text.find("levelID=") != std::string::npos ||
                text.find("secret=") != std::string::npos ||
                text.find("upload") != std::string::npos ||
                text.find("download") != std::string::npos ||
                text.find("http://") != std::string::npos ||
                text.find("https://") != std::string::npos;
            if (request_like) {
                ++network_request_marker_count_;
                log_ << "[trace] Unified ARMv7 request-marker #" << network_request_marker_count_
                     << " text=\"" << SanitizeLogText(text) << "\""
                     << " worker-valid=" << (cooperative_worker_.valid ? 1 : 0)
#ifdef _WIN32
                     << " sockets=" << sockets_.size()
#endif
                     << '\n';
                DumpImportTrace("request-marker", 96u);
            }
            result=static_cast<u32>(text.size());
        } else if (name == "__assert2") {
            log_ << "WARNING: guest __assert2 file=" << ReadCString(r0) << " line=" << r1
                 << " function=" << ReadCString(r2) << " expression=" << ReadCString(r3) << '\n';
            result = 0;
        } else if (name == "__gnu_Unwind_Find_exidx") { if(r1)env_.MemoryWrite32(r1,0); result=0; }
        else if (name == "dlopen" || name == "dlsym" || name == "dlclose" || name == "dlerror") result=0;
        else if (name == "access") { std::error_code ec; result=std::filesystem::exists(TranslatePath(ReadCString(r0)),ec)?0u:static_cast<u32>(-1); }
        else if (name == "mkdir") { std::error_code ec; const bool made=std::filesystem::create_directories(TranslatePath(ReadCString(r0)),ec); result=(!ec&&(made||std::filesystem::exists(TranslatePath(ReadCString(r0)))))?0u:static_cast<u32>(-1); }
        else if (name == "remove") { std::error_code ec; result=std::filesystem::remove(TranslatePath(ReadCString(r0)),ec)?0u:static_cast<u32>(-1); }
        else if (name == "rename") { std::error_code ec; std::filesystem::rename(TranslatePath(ReadCString(r0)),TranslatePath(ReadCString(r1)),ec); result=ec?static_cast<u32>(-1):0u; }
        else if (name == "sysconf") result = r0 == 30u ? kPageSize : 1u;
        else if (name == "gai_strerror") result = AllocateString("address resolution error");
        else if (name == "if_nametoindex") result = 1u;
        else if (name == "initgroups" || name == "setgid" || name == "setuid" || name == "umask") result = 0;
        else if (name == "pipe") {
#ifdef _WIN32
            result = GuestCreateSocketPair(r0);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "socketpair") {
#ifdef _WIN32
            result = GuestCreateSocketPair(r3);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "fork" || name == "execl" || name == "kill" || name == "waitpid") result = static_cast<u32>(-1);
        else if (name == "dup2") result = r1;
        else if (name == "syslog" || name == "swprintf") result = 0;
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
            if (running_cooperative_worker_) {
                if (!DispatchAsyncGetAddrInfo(r0, r1, r2, r3, result))
                    return true; // Keep PC on import stub until native DNS completes.
            } else {
                result = GuestGetAddrInfo(r0, r1, r2, r3);
            }
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
        } else if (name == "gethostbyname") {
#ifdef _WIN32
            if (running_cooperative_worker_) {
                if (!DispatchAsyncGetHostByName(r0, result))
                    return true; // Re-execute after async resolver completion.
            } else {
                result = GuestGetHostByName(r0);
            }
#else
            result = 0;
#endif
        } else if (name == "getnameinfo") {
#ifdef _WIN32
            result = GuestGetNameInfo(r0, r1, r2, r3, ArgWord(4),
                                      ArgWord(5), ArgWord(6));
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "shutdown") {
#ifdef _WIN32
            result = GuestShutdown(r0, r1);
#else
            result = static_cast<u32>(-1);
#endif
        } else if (name == "alarm" || name == "raise" || name == "sigaction" ||
                   name == "sigprocmask" || name == "sched_yield" || name == "usleep" ||
                   name == "pthread_join" || name == "pthread_attr_init" ||
                   name == "pthread_rwlock_init" ||
                   name == "pthread_rwlock_destroy" || name == "pthread_rwlock_rdlock" ||
                   name == "pthread_rwlock_wrlock" || name == "pthread_rwlock_unlock" ||
                   name == "__google_potentially_blocking_region_begin" ||
                   name == "__google_potentially_blocking_region_end" || name == "mlock" ||
                   name == "mprotect" || name == "chmod" || name == "bsd_signal") result = 0;
        else if (name == "dup") {
            result = r0;
        } else if (name == "system") {
            result = static_cast<u32>(-1);
        }
        else if (name == "ferror") {
            GuestFile* file=FindGuestFile(r0); result=file&&file->stream?static_cast<u32>(std::ferror(file->stream)):0;
        } else if (name == "setbuf") {
            GuestFile* file=FindGuestFile(r0); if(file&&file->stream)std::setbuf(file->stream,nullptr); result=0;
        } else if (name == "strcspn") {
            const std::string a=ReadCString(r0),b=ReadCString(r1); const std::size_t n=a.find_first_of(b); result=static_cast<u32>(n==std::string::npos?a.size():n);
        } else if (name == "strspn") {
            const std::string a=ReadCString(r0),b=ReadCString(r1); std::size_t n=0; while(n<a.size()&&b.find(a[n])!=std::string::npos)++n; result=static_cast<u32>(n);
        } else if (name == "strpbrk") {
            const std::string a=ReadCString(r0),b=ReadCString(r1); const std::size_t n=a.find_first_of(b); result=n==std::string::npos?0:r0+static_cast<u32>(n);
        } else if (name == "memmem") {
            const u8* h=static_cast<const u8*>(env_.HostPointer(r0,r1)); const u8* n=static_cast<const u8*>(env_.HostPointer(r2,r3)); result=0; if(h&&n&&r3<=r1){for(u32 i=0;i<=r1-r3;++i){if(std::memcmp(h+i,n,r3)==0){result=r0+i;break;}}}
        } else if (name == "closedir" || name == "opendir" || name == "readdir" ||
                   name == "syscall") {
            result=0;
        } else {
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
    Dynarmic::ExclusiveMonitor global_monitor_;
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
    std::unordered_map<u32,u32> condition_signals_;
    CooperativeWorkerContext cooperative_worker_;
    bool running_cooperative_worker_=false;
    bool cooperative_worker_runnable_=false;
    bool cooperative_worker_yielded_=false;
    bool cooperative_worker_done_=false;
    u64 cooperative_worker_registered_count_=0;
    u64 cooperative_worker_resume_count_=0;
    u64 cooperative_worker_yield_count_=0;
    u64 cooperative_worker_done_count_=0;
    u64 cooperative_worker_slice_yield_count_=0;
    u64 cooperative_worker_watchdog_count_=0;
    u64 cooperative_worker_immediate_wake_count_=0;
    u64 cooperative_worker_stub_return_count_=0;
    u64 cooperative_condition_log_count_=0;
    u64 network_worker_runs_=0;
    u64 network_poll_log_count_=0;
    u64 random_state_=1;
    unsigned call_depth_=0;
    u64 permissive_stub_calls_=0;
    u64 total_import_calls_=0;
    std::array<ImportTraceEntry, 512> import_trace_{};
    std::size_t import_trace_cursor_=0;
    std::size_t import_trace_count_=0;
    u64 import_trace_sequence_=0;
    std::chrono::steady_clock::time_point forensic_call_started_{};
    std::chrono::steady_clock::time_point forensic_next_heartbeat_{};
    std::string forensic_call_label_;
    u64 network_request_marker_count_=0;
    u64 jni_svc_calls_=0;
    u64 gl_calls_=0;
    u64 gl_draw_calls_=0;
    u64 gl_draw_vertices_=0;
    u64 gl_buffer_upload_bytes_=0;
    u64 gl_texture_upload_bytes_=0;
    GuestCallMetrics last_call_metrics_;
    std::set<std::string> permissive_names_;
    std::unordered_map<u32, FmodObjectState> fmod_objects_;
    u32 fmod_dsp_address_=0;
    u64 fmod_call_log_count_=0;
    u64 v22_decompress_successes_=0;
    u64 v22_decompress_failures_=0;
    u64 v22_decompress_log_count_=0;
    u64 v22_level_payload_caches_=0;
    u64 v22_level_setup_repairs_=0;
    u64 v22_level_setup_passthroughs_=0;
    u64 v22_level_settings_native_successes_=0;
    u64 v22_level_settings_native_failures_=0;
    u64 v22_level_settings_fallback_successes_=0;
    u64 v22_editor_entries_=0;
    u32 v22_editor_visual_layer_=0;
    u32 v22_draw_grid_layer_=0;
    u64 v22_editor_visibility_passes_=0;
    u64 v22_editor_overlay_frames_=0;
    bool v22_editor_overlay_playtest_active_=false;
    bool v22_editor_level_settings_refreshed_=false;
    bool v22_editor_background_suppression_logged_=false;
    u64 v22_editor_background_updates_suppressed_=0;
    u64 v22_batch_blend_repairs_=0;
    u64 v22_missing_batch_texture_fallbacks_=0;
    u64 v22_null_batch_texture_rejections_=0;
    std::unordered_set<u32> v22_editor_visualized_objects_;
    u32 v22_editor_hide_variable_address_=0;
    u32 v22_platformer_ui_logged_=0;
    bool v22_mouse_platformer_jump_down_=false;
    bool v22_mouse_platformer_playtest_fallback_ = false;
    u32 v22_mouse_platformer_touch_ui_=0;
    u32 v22_pause_hidden_item_=0;
    u64 v22_companion_hooks_installed_=0;
    u64 v22_companion_hooks_skipped_=0;
    u64 v22_null_ccstring_float_recoveries_=0;
    u32 v22_ccstring_float_value_=0;
    u32 v22_ccstring_float_value_size_=0;
    u64 v22_level_catalog_decodes_=0;
    u32 v22_hook_thunk_cursor_=kV22ThunkBase + 0x100u;
    std::optional<std::string> v22_pending_level_setup_;
    std::unordered_map<s32,std::string> v22_level_data_encoded_;
    std::unordered_map<s32,std::string> v22_level_data_decoded_;
    std::string v22_recent_music_path_;
    u32 v22_creator_callback_address_=0;
    u64 v22_creator_unlock_calls_=0;
    bool refresh_rate_bridge_logged_=false;
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
    u32 native_http_ccobject_ctor_=0;
    u32 native_http_ccobject_release_=0;
    u32 native_http_response_vtable_=0;
    u64 native_http_next_id_=0;
    u64 native_http_queued_count_=0;
    u64 native_http_completed_count_=0;
    u64 native_http_callback_count_=0;
    std::atomic<u64> native_http_active_count_{0};
#ifdef _WIN32
    std::mutex native_http_mutex_;
    std::deque<std::string> native_http_trace_;
    std::deque<NativeHttpResult> native_http_results_;
    std::vector<std::thread> native_http_threads_;
    bool winsock_initialized_=false;
    u32 next_socket_fd_=0x4000u;
    std::unordered_map<u32, SOCKET> sockets_;
    std::unordered_set<u32> nonblocking_sockets_;
    std::unordered_set<u32> socket_send_logged_;
    std::unordered_set<u32> socket_receive_logged_;
    std::unordered_map<u32, GuestAddrInfoAllocation> guest_addrinfo_;
    std::shared_ptr<AsyncDnsRequest> async_dns_;
    bool async_dns_timeout_reported_=false;
    bool async_dns_ready_reported_=false;
    u64 async_dns_queued_count_=0;
    u64 async_dns_completed_count_=0;
    u64 async_dns_timeout_count_=0;
    GuestAddrInfoAllocation guest_hostent_;
    u32 guest_hostent_address_=0;
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
    u64 gl_client_array_rebinds_=0;
    u64 gl_client_element_rebinds_=0;
    u32 gl_framebuffer_binding_=0;
    u32 edge_clip_normalization_logs_=0;
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
        file << "Geometry Dash ARM wrapper 0.9.6-gdpstweaks6 debug-everything profile\n";
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

static void RunArmv7FeatureSmoke() {
    ProbeEnvironment env;
    env.Map(kSmokeBase,kPageSize,true);
    // Thumb-2 + VFPv3 + NEON sequence assembled for ARMv7-A, softfp.
    static constexpr std::array<u8, 34> code = {
        0x41,0xF2,0x34,0x20, 0xC0,0xF2,0x01,0x00,
        0xB7,0xEE,0x00,0x0A, 0x30,0xEE,0x00,0x0A,
        0x10,0xEE,0x10,0x1A, 0x80,0xEF,0x11,0x10,
        0x21,0xEF,0x01,0x18, 0x53,0xEC,0x11,0x2B,
        0xFE,0xE7
    };
    if (!env.WriteBytes(kSmokeBase, code.data(), code.size()))
        throw std::runtime_error("could not write ARMv7 feature smoke code");
    Dynarmic::A32::UserConfig config;
    config.callbacks=&env;
    config.arch_version=DynarmicArmv7ArchVersion<Dynarmic::A32::ArchVersion>();
    Dynarmic::ExclusiveMonitor global_monitor{1};
    config.global_monitor=&global_monitor;
    config.processor_id=0;
    config.check_halt_on_memory_access=true;
    Dynarmic::A32::Jit cpu{config};
    env.AttachCpu(&cpu);
    cpu.Regs().fill(0);
    cpu.Regs()[15]=kSmokeBase;
    cpu.SetCpsr(0x30u);
    env.ticks_left=16;
    cpu.Run();
    if(env.invalid_access||env.interpreter_fallback||env.exception_seen||
       cpu.Regs()[0]!=0x00011234u||cpu.Regs()[1]!=0x40000000u||
       cpu.Regs()[2]!=2u||cpu.Regs()[3]!=2u) {
        std::ostringstream error;
        error << "Dynarmic ARMv7 feature smoke failed r0=0x" << std::hex
              << cpu.Regs()[0] << " r1=0x" << cpu.Regs()[1]
              << " r2=0x" << cpu.Regs()[2] << " r3=0x" << cpu.Regs()[3];
        throw std::runtime_error(error.str());
    }

    // Constructor 1 in both beta families uses this same atomic increment
    // shape. The previous branch provided a global monitor but omitted the
    // callbacks which perform the monitored compare-and-write, causing every
    // STREX to fail forever.
    static constexpr std::array<u8, 16> exclusive_code = {
        0x50,0xE8,0x00,0x1F, // ldrex r1, [r0]
        0x01,0x31,           // adds  r1, #1
        0x40,0xE8,0x00,0x12, // strex r2, r1, [r0]
        0x00,0x2A,           // cmp   r2, #0
        0xFB,0xD1,           // bne   strex
        0xFE,0xE7            // b     .
    };
    constexpr u32 exclusive_pc = kSmokeBase + 0x100u;
    constexpr u32 exclusive_data = kSmokeBase + 0x200u;
    if (!env.WriteBytes(exclusive_pc, exclusive_code.data(),
                        exclusive_code.size()))
        throw std::runtime_error("could not write ARMv7 exclusive smoke code");
    env.MemoryWrite32(exclusive_data, 41u);
    env.ResetStopState();
    cpu.Regs().fill(0);
    cpu.Regs()[0] = exclusive_data;
    cpu.Regs()[15] = exclusive_pc;
    cpu.SetCpsr(0x30u);
    env.ticks_left = 32u;
    cpu.Run();
    if (env.invalid_access || env.interpreter_fallback || env.exception_seen ||
        env.MemoryRead32(exclusive_data) != 42u || cpu.Regs()[1] != 42u ||
        cpu.Regs()[2] != 0u) {
        std::ostringstream error;
        error << "Dynarmic ARMv7 exclusive smoke failed value=" << std::dec
              << env.MemoryRead32(exclusive_data) << " r1=" << cpu.Regs()[1]
              << " strex_status=" << cpu.Regs()[2];
        throw std::runtime_error(error.str());
    }
}




static void WriteV22ImportManifest(const ElfRuntime& runtime,
                                   const std::string& path) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) throw std::runtime_error("could not create import manifest: " + path);
    output << "Geometry Dash 2.2 beta ARMv7 native import manifest\n";
    output << "imports=" << runtime.imports.size() << " objects=" << runtime.objects.size() << "\n\n";
    output << "RUNTIME ORDER (the SVC/import trampoline mapping actually used by Dynarmic)\n";
    output << "INDEX\tSVC\tSTUB\tGROUP\tNAME\n";
    for (std::size_t index = 0; index < runtime.imports.size(); ++index) {
        const ImportRecord& import = runtime.imports[index];
        const std::string& name = import.name;
        const char* group = name.rfind("gl", 0) == 0 ? "GL" :
                            (name == "FMOD_System_Create" || name.rfind("_ZN4FMOD", 0) == 0) ? "FMOD" :
                            name.rfind("Java_", 0) == 0 ? "JNI" : "LIBC";
        output << index << '\t' << import.svc << "\t0x" << std::hex
               << import.address << std::dec << '\t' << group << '\t' << name << '\n';
    }
    output << "\nALPHABETICAL INDEX\n";
    std::vector<std::string> names;
    names.reserve(runtime.imports.size());
    for (const ImportRecord& import : runtime.imports) names.push_back(import.name);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const char* group = name.rfind("gl", 0) == 0 ? "GL" :
                            (name == "FMOD_System_Create" || name.rfind("_ZN4FMOD", 0) == 0) ? "FMOD" :
                            name.rfind("Java_", 0) == 0 ? "JNI" : "LIBC";
        output << group << '\t' << name << '\n';
    }
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
    (void)gd_enable_application_dpi_awareness();
    if (!gd_settings_i_lost_the_game()) {
#ifdef _WIN32
        MessageBoxA(nullptr, "I_LOST_THE_GAME is false. You lost the game.\n\nLaunch through a RUN_AUTO batch file.", "Geometry Dash Wrapper", MB_OK | MB_ICONINFORMATION);
#endif
        return 69;
    }
    std::string log_path = "gd-dynarmic-interactive.log";
    std::string profile_path = "gd-dynarmic-profile.csv";
    std::string profile_summary_path = "gd-dynarmic-profile-summary.txt";
    double slow_frame_ms = 20.0;
    bool profile_enabled = true;
    bool static_audit_only = false;
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
        else if (argument == "--static-audit-only")
            static_audit_only = true;
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
        emit(std::string("Geometry Dash Wrapper ") + GD_WRAPPER_VERSION + " backend=" + GD_ARMV7_BACKEND_NAME);
        emit("Unified ARMv7: concurrent direct WinHTTP bridge with independent request threads, stage tracing, short timeouts, and automatic form POST headers");
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
                "The ARM Dynarmic backend must be compiled as a 64-bit executable");
        if (!static_audit_only) {
            RunArmv7FeatureSmoke();
            emit("RESULT: DYNARMIC_X64_ARMV7_FEATURE_SMOKE_OK thumb2=1 vfpv3=1 neon=1 exclusive=1 guest=v7A host=x86_64");
        } else {
            emit("RESULT: DYNARMIC_V22_STATIC_AUDIT_MODE execution=disabled");
        }
        emit("RESULT: DYNARMIC_GUEST_PAGE_LOOKUP_READY pages=1048576 typed-access=single-copy");
        emit("RESULT: DYNARMIC_V22_BETA_UNIFIED_ARMV7_READY raw-so=1 apk-armv7=1 import-manifest=1 profile=1");

        std::string input_path="libcocos2dcpp.so";
        std::string import_manifest_path="gd-v22beta-imports.txt";
        std::string companion_libgame_path;
        // Desktop stabilization default: do not execute optional APK companion hooks.
        // A user can still opt in explicitly with --companion-hooks=safe/all.
        V22CompanionHookMode companion_hook_mode = V22CompanionHookMode::Off;
        bool probe_only=false;
        bool probe_only_explicit=false;
        int width=1280,height=720,max_frames=0;
        for(int i=1;i<argc;++i){
            const std::string_view argument(argv[i]);
            if(argument=="--probe-only") { probe_only=true; probe_only_explicit=true; }
            else if(argument=="--static-audit-only") {}
            else if(argument=="--debug-everything") {}
            else if(argument.rfind("--dump-imports=",0)==0)
                import_manifest_path=std::string(argument.substr(15));
            else if(argument.rfind("--frames=",0)==0)
                max_frames=std::max(
                    1,std::stoi(std::string(argument.substr(9))));
            else if(argument.rfind("--width=",0)==0)
                width=std::max(
                    320,std::stoi(std::string(argument.substr(8))));
            else if(argument.rfind("--height=",0)==0)
                height=std::max(
                    240,std::stoi(std::string(argument.substr(9))));
            else if(argument.rfind("--companion-libgame=",0)==0)
                companion_libgame_path=std::string(argument.substr(20));
            else if(argument.rfind("--companion-hooks=",0)==0) {
                constexpr std::string_view prefix = "--companion-hooks=";
                std::string mode(argument.substr(prefix.size()));
                // Be tolerant of launchers that accidentally preserve whitespace
                // or one layer of quote characters around an option value.
                while (!mode.empty() &&
                       (std::isspace(static_cast<unsigned char>(mode.front())) ||
                        mode.front() == '\"' || mode.front() == '\'')) {
                    mode.erase(mode.begin());
                }
                while (!mode.empty() &&
                       (std::isspace(static_cast<unsigned char>(mode.back())) ||
                        mode.back() == '\"' || mode.back() == '\'')) {
                    mode.pop_back();
                }
                if (mode == "off") companion_hook_mode = V22CompanionHookMode::Off;
                else if (mode == "safe") companion_hook_mode = V22CompanionHookMode::Safe;
                else if (mode == "all") companion_hook_mode = V22CompanionHookMode::All;
                else throw std::runtime_error(
                    "--companion-hooks must be off, safe, or all; received '" +
                    mode + "'");
            }
            else if(!argument.empty()&&argument[0]!='-')
                input_path=std::string(argument);
        }
        const std::filesystem::path absolute_input=
            std::filesystem::absolute(input_path);
        const char* configured_save = std::getenv("GD_SAVE_DIR");
        const std::filesystem::path writable =
            configured_save && *configured_save
                ? std::filesystem::absolute(configured_save)
                : std::filesystem::absolute("save");
        emit("Input file: "+absolute_input.string());
        const std::vector<u8> input_bytes=ReadFile(absolute_input.string());
        emit("Input bytes: "+std::to_string(input_bytes.size()));
        const bool input_is_apk=!IsElf32ArmImage(input_bytes);
        std::vector<u8> apk;
        std::vector<u8> libgame;
        std::vector<u8> companion_libgame;
        std::string companion_libgame_source;
        u32 companion_crc = 0u;
        bool companion_hooking_present = false;
        bool companion_dobby_present = false;
        bool legacy_gdkit_present = false;
        bool gauntlet_assets_present = false;
        std::string library_member;
        if(input_is_apk){
            apk=input_bytes;
            libgame=ExtractV22NativeLibrary(apk,library_member);
            emit("Input type: APK");
            emit("Extracted "+library_member+": "+
                 std::to_string(libgame.size())+" bytes");
            const std::vector<ZipNativeLibraryInfo> native_libraries =
                ListZipNativeLibraries(apk);
            {
                std::ostringstream line;
                line << "RESULT: DYNARMIC_V22_NATIVE_LIBRARY_INVENTORY count="
                     << native_libraries.size() << " libs=";
                for (std::size_t index = 0; index < native_libraries.size();
                     ++index) {
                    if (index) line << ',';
                    const std::filesystem::path member(native_libraries[index].name);
                    line << member.filename().string() << ':'
                         << native_libraries[index].uncompressed_size;
                }
                line << " policy=map-primary+validated-libgame "
                        "other-libraries=audit-only-no-constructors";
                emit(line.str());
            }
            try {
                companion_libgame = ExtractZipMember(
                    apk, "lib/armeabi-v7a/libgame.so");
                if (!companion_libgame.empty())
                    companion_libgame_source = "input-apk";
            } catch (const std::exception&) {
                companion_libgame.clear();
            }
            const auto has_apk_member =
                [&apk](std::string_view name) {
                    try {
                        return !ExtractZipMember(apk, name).empty();
                    } catch (const std::exception&) {
                        return false;
                    }
                };
            companion_hooking_present = has_apk_member(
                "lib/armeabi-v7a/libhooking.so");
            companion_dobby_present = has_apk_member(
                "lib/armeabi-v7a/libdobby.so");
            legacy_gdkit_present = has_apk_member(
                "lib/armeabi-v7a/libgdkit.so");
            gauntlet_assets_present =
                (has_apk_member("assets/GauntletSheet.png") &&
                 has_apk_member("assets/GauntletSheet.plist")) ||
                (has_apk_member("assets/GauntletSheet-hd.png") &&
                 has_apk_member("assets/GauntletSheet-hd.plist"));
            if (companion_libgame.empty()) {
                std::filesystem::path sidecar;
                if (!companion_libgame_path.empty())
                    sidecar = std::filesystem::absolute(companion_libgame_path);
                else {
                    const std::filesystem::path beside_input =
                        absolute_input.parent_path() / "libgame.so";
                    const std::filesystem::path beside_executable =
                        std::filesystem::absolute("libgame.so");
                    if (std::filesystem::exists(beside_input))
                        sidecar = beside_input;
                    else if (std::filesystem::exists(beside_executable))
                        sidecar = beside_executable;
                }
                if (!sidecar.empty()) {
                    companion_libgame = ReadFile(sidecar.string());
                    if (!IsElf32ArmImage(companion_libgame))
                        throw std::runtime_error(
                            "external companion libgame.so is not ARM ELF32: " +
                            sidecar.string());
                    companion_libgame_source = sidecar.string();
                }
            }
            if (!companion_libgame.empty()) {
                companion_crc = static_cast<u32>(crc32(
                    0, reinterpret_cast<const Bytef*>(
                           companion_libgame.data()),
                    static_cast<uInt>(companion_libgame.size())));
                std::ostringstream line;
                line << "RESULT: DYNARMIC_V22_COMPANION_LIBGAME_DETECTED bytes="
                     << companion_libgame.size() << " crc32=0x" << std::hex
                     << std::setw(8) << std::setfill('0') << companion_crc
                     << std::dec << " source=" << companion_libgame_source
                     << " role=editor-and-feature-extension "
                        "hook-engine=host-redirection execution=runtime-profile";
                emit(line.str());
            } else {
                emit("RESULT: DYNARMIC_V22_COMPANION_LIBGAME_ABSENT "
                     "editor-unlock=main-library-force-true");
            }
            emit(
                "RESULT: DYNARMIC_V22_APK_EXTENSION_MANIFEST "
                "libgame=" +
                std::to_string(!companion_libgame.empty()) +
                " libhooking=" +
                std::to_string(companion_hooking_present) +
                " libdobby=" +
                std::to_string(companion_dobby_present) +
                " libgdkit=" +
                std::to_string(legacy_gdkit_present) +
                " discovered-features=" +
                std::string(
                    !companion_libgame.empty()
                        ? "editor-init,DPAD,CollisionFix,ShaderFix,"
                          "SpeedrunTimer,GDPSManager,Options"
                        : legacy_gdkit_present
                            ? "legacy-touch,update,menu,unlock"
                            : "none") +
                " execution=constructors+selected-ApplyHooks-no-dobby-loader");
        }else{
            libgame=input_bytes;
            library_member="raw:libcocos2dcpp.so";
            emit("Input type: raw ARMv7 ELF shared library");
            if(!probe_only_explicit) probe_only=true;
        }
        ProbeEnvironment env;
        ElfRuntime runtime=MapAndRelocateElf(libgame,env);
        ElfRuntime companion_runtime{};
        V22VisualHookCounts visual_hooks{};
        if (!companion_libgame.empty())
            companion_runtime = MapAndRelocateV22CompanionElf(
                companion_libgame, env, runtime);
        const std::size_t zip_hooks=InstallCcFileUtilsZipHooks(runtime,env);
        if(zip_hooks!=1u)
            throw std::runtime_error(
                "required 2.2 cocos2d getFileDataFromZip hook was not found");
        const std::size_t browser_hooks=
            InstallCcApplicationOpenUrlHook(runtime,env);
        if(browser_hooks!=1u)
            throw std::runtime_error(
                "required cocos2d openURL hook was not found");
        const std::size_t native_http_hooks =
            InstallV22NativeHttpSendHook(runtime, env);
        if (native_http_hooks != 1u)
            throw std::runtime_error(
                "required CCHttpClient::send native WinHTTP hook was not found");
        const std::size_t inflate_hooks=
            InstallV22InflateMemoryHook(runtime,env);
        if(inflate_hooks!=1u)
            throw std::runtime_error(
                "required cocos2d ZipUtils::ccInflateMemory hook was not found");
        const std::size_t icon_unlock_hooks =
            InstallV22ConfigurableIconUnlockHooks(runtime, env);
        const std::size_t editor_redirects = gd_settings_full_bypass()
            ? InstallV22CreatorEditorUnlock(runtime, env) : 0u;
        const std::size_t gauntlet_asset_guards =
            InstallV22MissingGauntletAssetGuard(
                runtime, env, gauntlet_assets_present);
        const V22GraphicsPatchCounts graphics_patches =
            InstallV22HighestGraphicsHooks(runtime, env);
        const std::size_t swing_reopen_patches =
            InstallV22PlatformerSwingReopenPatch(runtime, env);
        const std::size_t prepare_bridges =
            InstallV22PrepareLevelBridge(runtime, env);
        if (prepare_bridges != 1u)
            throw std::runtime_error(
                "required PlayLayer level setup bridge was not installed");
        const std::size_t settings_parser_bridges =
            InstallV22LevelSettingsParserBridge(runtime, env);
        if (settings_parser_bridges != 1u)
            throw std::runtime_error(
                "required LevelSettingsObject parser bridge was not installed");
        const V22DesktopTextInputPatchCounts desktop_text_input =
            InstallV22DesktopTextInputPatches(runtime, env);

        const bool late_beta_layout =
            runtime.v22_play_layer_level_offset == 820u &&
            runtime.v22_game_level_id_offset == 272u;
        const bool early_beta_layout =
            runtime.v22_play_layer_level_offset == 644u &&
            runtime.v22_game_level_id_offset == 260u;
        const bool primary_editor_initializer =
            HasV22PrimaryEditorInitializer(runtime, env);
        runtime.v22_wrapper_editor_profile =
            DetectV22EditorRestoreProfile(runtime, env);
        runtime.v22_companion_editor_init_enabled =
            HasCompatibleV22CompanionEditorInitializer(
                runtime, env, late_beta_layout);
        // Preserve the proven late-beta visibility repair when the 2023
        // companion exists, but stock-editor restoration itself never depends
        // on that library. 2019 already has a real updateVisibility function.
        if (runtime.v22_companion_editor_init_enabled) {
            visual_hooks = InstallV22SafeVisualHooks(runtime, env);
        } else if (runtime.v22_wrapper_editor_profile ==
                       V22EditorRestoreProfile::Late2022 ||
                   runtime.v22_wrapper_editor_profile ==
                       V22EditorRestoreProfile::Late2023) {
            const auto stock_visibility =
                InstallV22StockEditorVisibilityBridge(runtime, env);
            if (stock_visibility.first + stock_visibility.second == 0u)
                throw std::runtime_error(
                    "stock late-beta editor visibility bridge was not installed");
        }

        ResolveV22InputBridgeSymbols(runtime);
        const bool install_editor_bridge =
            runtime.v22_wrapper_editor_profile != V22EditorRestoreProfile::None ||
            runtime.v22_companion_editor_init_enabled ||
            primary_editor_initializer;
        const std::size_t edit_button_pointers = install_editor_bridge
            ? InstallV22EditButtonBridge(runtime, env) : 0u;
        if (install_editor_bridge &&
            edit_button_pointers == 0u)
            throw std::runtime_error(
                "capable beta EditLevelLayer callback pointer was not found");
        const auto gameplay_edit_pointers = install_editor_bridge
            ? InstallV22GameplayEditButtonBridges(runtime, env)
            : std::pair<std::size_t, std::size_t>{};
        emit(std::string("RESULT: DYNARMIC_V22_STOCK_EDITOR_PROFILE profile=") +
             V22EditorRestoreProfileName(runtime.v22_wrapper_editor_profile) +
             " primary-bytes=" + std::to_string(runtime.primary_file_bytes) +
             " bridge=" + (install_editor_bridge ? "enabled" : "disabled"));
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
        emit("RESULT: DYNARMIC_CCAPPLICATION_OPENURL_HOOK_READY count="+
             std::to_string(browser_hooks));
        emit("RESULT: DYNARMIC_V22_NATIVE_HTTP_BRIDGE_READY count="+
             std::to_string(native_http_hooks)+
             " boundary=CCHttpClient::send transport=WinHTTP "
             "concurrency=per-request proxy=none "
             "form-content-type=automatic guest-pthread=unused "
             "guest-curl=unused guest-openssl=unused");
        emit("RESULT: DYNARMIC_V22_LOW_LEVEL_INFLATE_HOOK_READY count="+
             std::to_string(inflate_hooks)+
             " codec=gzip+zlib+raw original-cpp-decompress=1 hidden-sret=guest-native");
        emit("RESULT: DYNARMIC_V22_FULL_VERSION_BYPASS enabled="+
             std::string(gd_settings_full_bypass() ? "1" : "0")+
             " patches="+std::to_string(editor_redirects)+
             " mode=best-effort-menu-creator+online-capability-no-global-button-remap");
        emit("RESULT: DYNARMIC_V22_GAUNTLET_ASSET_GUARD assets="+
             std::string(gauntlet_assets_present ? "1" : "0")+
             " callbacks="+std::to_string(gauntlet_asset_guards)+
             " policy=missing-assets-safe-noop");
        emit("RESULT: UNIFIED_LAUNCH_SETTINGS server="+
             std::string(gd_settings_server())+
             " hack-icons="+(gd_settings_hack_icons() ? "true" : "false")+
             " icon-color-hooks="+std::to_string(icon_unlock_hooks)+
             " full-bypass="+
             (gd_settings_full_bypass() ? "true" : "false")+
             " highest-graphics="+
             (gd_settings_force_highest_graphics() ? "true" : "false")+
             " hd-hooks="+std::to_string(graphics_patches.hd)+
             " low-memory-hooks="+
             std::to_string(graphics_patches.low_memory)+
             " music-pulse-max="+
             std::to_string(gd_settings_music_pulse_max())+
             " remove-pause-button="+
             (gd_settings_remove_pause_button() ? "true" : "false")+
             " hide-cursor-when-playing="+
             (gd_settings_hide_cursor_when_playing() ? "true" : "false")+
             " practice-zx="+
             ((runtime.v22_ui_on_check && runtime.v22_ui_on_delete_check &&
                runtime.v22_practice_mode_offset)
                  ? "ready-practice-guarded" : "disabled-unproven")+
             " exact-editor-visibility="+
             (gd_settings_v22_exact_editor_visibility()
                  ? "true" : "false"));
        emit("RESULT: DYNARMIC_V22_PLATFORMER_SWING_REOPEN_PATCH count="+
             std::to_string(swing_reopen_patches)+
             " policy=hide-on-toggle-reappear-on-menu-reopen");
        emit("RESULT: DYNARMIC_V22_FRAME_CLIP_RESET_DISABLED "
             "reason=guest-editor-state-authoritative");
        emit("RESULT: DYNARMIC_V22_LEVEL_SETUP_BRIDGE_READY callsites="+
             std::to_string(prepare_bridges)+
             " source=guest-valid-first+vtable-level-scan+apk-catalog+"
             "music-fallback+latest-inflate+native-passthrough "
             "guest-string-builder=bringup9-compatible-cow "
             "play-level-offset="+
             std::to_string(runtime.v22_play_layer_level_offset)+
             " level-id-offset="+
             std::to_string(runtime.v22_game_level_id_offset));
        emit("RESULT: DYNARMIC_V22_LEVEL_SETTINGS_FALLBACK_READY callsites="+
             std::to_string(settings_parser_bridges)+
             " mode=native-first+strip-kS38+minimal-default+default-object");
        emit("RESULT: DYNARMIC_V22_DESKTOP_TEXT_INPUT_READY keyboard-callbacks=" +
             std::to_string(desktop_text_input.keyboard_callbacks) +
             " offset-delegates=" +
             std::to_string(desktop_text_input.offset_delegates) +
             " software-keyboard-pan=disabled");
        if (!companion_libgame.empty()) {
            std::ostringstream line;
            line << "RESULT: DYNARMIC_V22_COMPANION_EDITOR_RUNTIME_READY image=0x"
                 << std::hex << companion_runtime.image_min << "-0x"
                 << companion_runtime.image_max << " executable=0x"
                 << runtime.v22_companion_executable_min << "-0x"
                 << runtime.v22_companion_executable_max << " initH=0x"
                 << runtime.v22_companion_editor_init << std::dec
                 << " constructors=" << companion_runtime.constructors.size()
                 << " initH-enabled="
                 << (runtime.v22_companion_editor_init_enabled ? 1 : 0)
                 << " hook-profile="
                 << V22CompanionHookModeName(companion_hook_mode)
                 << " source=" << companion_libgame_source;
            emit(line.str());
            if (runtime.v22_companion_editor_init_enabled) {
                emit(
                    "RESULT: DYNARMIC_V22_EDITOR_NATIVE_VISUAL_HOOK_READY "
                    "count=1 init-hook=disabled visibility-mode=" +
                    std::string(
                        visual_hooks.exact_companion_editor
                            ? "exact-companion"
                            : "host-camera-cull+client-array+background-suppression") +
                    " visibility-pointers=" +
                    std::to_string(visual_hooks.editor_visibility.first) +
                    " visibility-calls=" +
                    std::to_string(visual_hooks.editor_visibility.second) +
                    " background-pointers=" +
                    std::to_string(visual_hooks.camera_background.first) +
                    " background-calls=" +
                    std::to_string(visual_hooks.camera_background.second) +
                    " blend-pointers=" +
                    std::to_string(visual_hooks.batch_blend.first) +
                    " blend-calls=" +
                    std::to_string(visual_hooks.batch_blend.second) +
                    " init-guard-pointers=" +
                    std::to_string(visual_hooks.batch_init_guard.first) +
                    " init-guard-calls=" +
                    std::to_string(visual_hooks.batch_init_guard.second));
                emit(
                    "RESULT: DYNARMIC_V22_ART_ASSET_LIMITS ground=" +
                    std::to_string(visual_hooks.ground_asset_max) +
                    " background=" +
                    std::to_string(visual_hooks.background_asset_max) +
                    " policy=apk-native-assets-only");
                emit(
                    "RESULT: DYNARMIC_V22_PLATFORMER_SAFE_VISUAL_HOOK_READY "
                    "count=1 touch-hooks=disabled companion-gdps=disabled "
                    "visibility-pointers=" +
                    std::to_string(visual_hooks.play_visibility.first) +
                    " visibility-calls=" +
                    std::to_string(visual_hooks.play_visibility.second));
            } else {
                const std::string reason =
                    !runtime.v22_companion_editor_init
                        ? "initH-absent"
                        : !late_beta_layout
                            ? "primary-layout-mismatch"
                            : "initH-abi-validation-failed";
                emit(
                    "RESULT: DYNARMIC_V22_COMPANION_EDITOR_EXTENSION_SKIPPED "
                    "reason=" + reason +
                    " action=continue-primary-game");
            }
        }
        if (edit_button_pointers) {
            emit("RESULT: DYNARMIC_V22_EDIT_BUTTON_BRIDGE_READY pointers="+
                 std::to_string(edit_button_pointers)+
                 " source=EditLevelLayer::onEdit "
                 "target=LevelEditorLayer::create+" +
                 std::string(runtime.v22_companion_editor_init_enabled
                                 ? "validated-companion-initH"
                                 : "native-primary-init"));
        } else {
            emit(
                "RESULT: DYNARMIC_V22_EDIT_BUTTON_BRIDGE_UNAVAILABLE reason=" +
                std::string(
                    install_editor_bridge
                        ? "callback-pointer-not-found"
                        : (late_beta_layout || early_beta_layout)
                            ? "primary-editor-init-is-base-only-stub"
                            : "unknown-layout-native-callback-preserved"));
        }
        if (gameplay_edit_pointers.first || gameplay_edit_pointers.second) {
            emit("RESULT: DYNARMIC_V22_GAMEPLAY_EDIT_BUTTON_BRIDGE_READY "
                 "pause-pointers=" +
                 std::to_string(gameplay_edit_pointers.first) +
                 " end-pointers=" +
                 std::to_string(gameplay_edit_pointers.second) +
                 " target=validated-editor-entry-no-global-hooks");
        } else {
            emit("RESULT: DYNARMIC_V22_GAMEPLAY_EDIT_BUTTON_BRIDGE_UNAVAILABLE "
                 "reason=no-compatible-editor-initializer-or-callback-pointer");
        }
        emit("RESULT: DYNARMIC_V22_EDITOR_CAPABILITIES layout="+
             std::string(late_beta_layout
                             ? "late"
                             : early_beta_layout ? "early" : "unknown")+
             " creator-entry-unlock="+
             std::to_string(editor_redirects != 0u)+
             " primary-full-init="+
             std::to_string(primary_editor_initializer)+
             " companion-compatible-init="+
             std::to_string(runtime.v22_companion_editor_init_enabled)+
             " edit-level-bridge="+
             std::to_string(edit_button_pointers != 0u)+
             " gameplay-edit-bridge="+
             std::to_string(gameplay_edit_pointers.first != 0u ||
                            gameplay_edit_pointers.second != 0u));
        emit("RESULT: DYNARMIC_V22_COMPANION_HOOK_POLICY mode=" +
             std::string(V22CompanionHookModeName(companion_hook_mode)) +
             " safe=MenuLayer,Options,EditLevelLayer,LevelEditorLayer,"
             "EditorPauseLayer,CollisionFix,ShaderFix,SpeedrunTimer,"
             "MoreSearch,SwingIconFix,AbbreviatedLabels,Emojis,"
             "AdvancedLevelInfo all-adds=DPAD,GDPSManager,Servers,Hacks,DevDebug "
             "loader=disabled libhooking=host-implemented libdobby=not-run");
        emit("RESULT: DYNARMIC_V22_PLATFORMER_WINDOWS_INPUT_READY "
             "mouse=native-touch-id-ownership+host-jump "
             "keyboard=Space,Up,A,D,Left,Right "
             "editor-playtest=LevelEditorLayer-queueButton "
             "button-visuals=UILayer-key-path "
             "buttons=jump:1,left:2,right:3 queueButton="+
             std::to_string(runtime.v22_gjbase_queue_button != 0u)+
             " ui-key="+
             std::to_string(runtime.v22_ui_key_down != 0u)+
             " unsafe-editor-ui-key-path=disabled");
        emit("RESULT: DYNARMIC_V22_AUDIO_PATH music=MCI effects=waveOut imports="+
             std::to_string(std::count_if(
                 runtime.imports.begin(),runtime.imports.end(),
                 [](const ImportRecord& import){return GuestExecutor::IsFmodImportName(import.name);})) +
             " simpleaudio-hooks=not-required");
        WriteV22ImportManifest(runtime, import_manifest_path);
        emit("RESULT: DYNARMIC_V22_IMPORT_MANIFEST_WRITTEN path="+import_manifest_path+
             " imports="+std::to_string(runtime.imports.size()));
        if (static_audit_only) {
            emit("RESULT: DYNARMIC_V22_STATIC_AUDIT_OK constructors="+
                 std::to_string(runtime.constructors.size())+
                 " imports="+std::to_string(runtime.imports.size())+
                 " relocations="+std::to_string(runtime.relocation_count));
            return 0;
        }
        GuestExecutor executor(env,runtime,log_file);
        executor.ConfigureHost(absolute_input.string(),writable.string(),apk,input_is_apk);
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
            emit(std::string("RESULT: DYNARMIC_V22_BETA_PROBE_ONLY_OK input=")+
                 (input_is_apk ? "apk" : "raw-so"));
            return 0;
        }
        if(!input_is_apk)
            throw std::runtime_error("raw libcocos2dcpp.so has no Java/assets; pass a complete 2.2 beta APK for nativeInit");

        const u32 apk_ref=executor.NewStringRef(absolute_input.string());
        if(!apk_ref||!executor.RunFunction(
                runtime.native_set_paths,{kEnvObject,0u,apk_ref},
                &result,"nativeSetPaths"))
            throw std::runtime_error(executor.LastError());
        emit("RESULT: DYNARMIC_PATHS_SET");

        bool companion_runtime_initialized = false;
        if (!companion_libgame.empty() && late_beta_layout &&
            companion_hook_mode != V22CompanionHookMode::Off) {
            companion_runtime_initialized = true;
            emit("Running " +
                 std::to_string(companion_runtime.constructors.size()) +
                 " companion libgame.so constructors through Dynarmic");
            for (std::size_t index = 0;
                 index < companion_runtime.constructors.size(); ++index) {
                const u32 entry = companion_runtime.constructors[index];
                if (entry == 0u || entry == std::numeric_limits<u32>::max())
                    continue;
                u32 ignored = 0u;
                if (!executor.RunFunction(
                        entry, {}, &ignored,
                        "companion constructor " +
                            std::to_string(index + 1u),
                        0u, std::chrono::milliseconds(30000))) {
                    emit("RESULT: DYNARMIC_V22_COMPANION_CONSTRUCTOR_FAILED index=" +
                         std::to_string(index + 1u) +
                         " action=disable-companion-hooks");
                    companion_runtime_initialized = false;
                    break;
                }
            }
            if (companion_runtime_initialized) {
                emit("RESULT: DYNARMIC_V22_COMPANION_CONSTRUCTORS_OK count=" +
                     std::to_string(companion_runtime.constructors.size()));
                std::size_t features_ok = 0u;
                std::size_t features_failed = 0u;
                std::size_t features_absent = 0u;
                for (const V22CompanionFeatureSpec& feature :
                     kV22CompanionFeatures) {
                    if (companion_hook_mode == V22CompanionHookMode::Safe &&
                        !feature.safe)
                        continue;
                    const SymbolRecord* apply =
                        FindSymbol(runtime, feature.symbol);
                    if (!apply ||
                        apply->address < runtime.v22_companion_executable_min ||
                        apply->address >= runtime.v22_companion_executable_max) {
                        ++features_absent;
                        emit(std::string(
                                 "RESULT: DYNARMIC_V22_COMPANION_FEATURE_ABSENT name=") +
                             feature.label);
                        continue;
                    }
                    const u64 hooks_before =
                        executor.CompanionHooksInstalled();
                    const u64 skipped_before =
                        executor.CompanionHooksSkipped();
                    u32 ignored = 0u;
                    const bool ok = executor.RunFunction(
                        apply->address, {}, &ignored,
                        std::string("companion ") + feature.label +
                            "::ApplyHooks",
                        0u, std::chrono::milliseconds(30000));
                    if (ok) {
                        ++features_ok;
                        emit(std::string(
                                 "RESULT: DYNARMIC_V22_COMPANION_FEATURE_ENABLED name=") +
                             feature.label + " hooks=" +
                             std::to_string(
                                 executor.CompanionHooksInstalled() -
                                 hooks_before) +
                             " skipped=" +
                             std::to_string(
                                 executor.CompanionHooksSkipped() -
                                 skipped_before));
                    } else {
                        ++features_failed;
                        emit(std::string(
                                 "RESULT: DYNARMIC_V22_COMPANION_FEATURE_FAILED name=") +
                             feature.label + " action=continue-primary-game");
                    }
                }
                executor.ClearGuestCodeCache("companion-hooks-installed");
                emit("RESULT: DYNARMIC_V22_COMPANION_FEATURE_TOTALS mode=" +
                     std::string(V22CompanionHookModeName(
                         companion_hook_mode)) +
                     " enabled=" + std::to_string(features_ok) +
                     " failed=" + std::to_string(features_failed) +
                     " absent=" + std::to_string(features_absent) +
                     " hooks=" +
                     std::to_string(executor.CompanionHooksInstalled()) +
                     " skipped=" +
                     std::to_string(executor.CompanionHooksSkipped()));
            }
        } else if (!companion_libgame.empty() && !late_beta_layout) {
            emit("RESULT: DYNARMIC_V22_COMPANION_RUNTIME_SKIPPED "
                 "reason=primary-layout-not-late-beta action=preserve-game");
        } else if (companion_hook_mode == V22CompanionHookMode::Off) {
            emit("RESULT: DYNARMIC_V22_COMPANION_RUNTIME_SKIPPED "
                 "reason=hook-profile-off");
        }

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
                    if(!native_paused) {
                        ok=executor.PrepareV22MousePlatformerTouch();
                        if(ok)
                            ok=executor.SendTouchPoint(
                                runtime.native_touch_begin,event.x,event.y,
                                "nativeTouchesBegin");
                        if(ok)
                            ok=executor.SyncV22MousePlatformerJump(true);
                    }
                    break;
                case HostEventType::TouchMove:
                    if(!native_paused)
                        ok=executor.SendTouchMove(
                            runtime.native_touch_move,event.x,event.y);
                    break;
                case HostEventType::TouchEnd:
                    if(!native_paused) {
                        ok=executor.SendTouchPoint(
                            runtime.native_touch_end,event.x,event.y,
                            "nativeTouchesEnd");
                        if(ok)
                            ok=executor.SyncV22MousePlatformerJump(false);
                    }
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
                case HostEventType::PracticeCheckpoint:
                    if(!native_paused)
                        ok=executor.SendPracticeCheckpoint(event.value != 0u);
                    break;
                case HostEventType::EditorCommand:
                    if(!native_paused) ok=executor.SendEditorCommand(event.value);
                    break;
                case HostEventType::ExtrasAction:
                    if(!native_paused) ok=executor.HandleExtrasAction(event.value);
                    break;
                case HostEventType::PlatformButton:
                    if(!native_paused)
                        ok=executor.SendPlatformerButton(
                            event.value,event.pressed);
                    break;
                case HostEventType::Pause:
                    if(!native_paused){
                        ok=executor.SyncV22MousePlatformerJump(false);
                        if(ok)
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
            if(!executor.PumpNetworkWorkerFrame())
                throw std::runtime_error(executor.LastError());
            if(native_paused||!executor.WindowActive()){
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            const auto render_start=std::chrono::steady_clock::now();
            executor.ResetFrameClipState();
            if(!executor.UpdateV22EditorOverlayFrame())
                throw std::runtime_error(executor.LastError());
            if(profile_enabled) executor.BeginGpuFrame(frame_count+1u);
            if(!executor.RunFunction(
                    runtime.native_render,{kEnvObject,0u},&result,
                    "nativeRender",0u,
                    std::chrono::milliseconds(30000)))
                throw std::runtime_error(executor.LastError());
            if(profile_enabled) executor.EndGpuFrame();
            executor.RefreshExtrasMenuVisibility();
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
                const char* configured_title = std::getenv("GD_GAME_TITLE");
                executor.SetWindowTitle(
                    configured_title && *configured_title
                        ? configured_title : "Geometry Dash");
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
        emit("Cooperative network totals: "+
             executor.DescribeCooperativeNetwork());
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
        emit("RESULT: DYNARMIC_V22_BETA_UNIFIED_ARMV7_OK");
        return 0;
    } catch(const std::exception& error){
        emit(std::string("ERROR: ")+error.what());
        emit("RESULT: DYNARMIC_V22_BETA_UNIFIED_ARMV7_FAILED");
        return 1;
    }
}

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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <direct.h>
#endif

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"

extern "C" {
#include "zlib.h"
#include "storage_win.h"
}

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

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
constexpr u32 kHeapSize = 0x08000000u;
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

struct MemoryRegion {
    u32 base = 0;
    std::vector<u8> data;
    bool executable = false;
};

class ProbeEnvironment final : public Dynarmic::A32::UserCallbacks {
public:
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
        if (size == 0 || size > std::numeric_limits<u32>::max()) throw std::runtime_error("invalid guest mapping size");
        const u64 end = static_cast<u64>(base) + size;
        if (end > 0x100000000ull) throw std::runtime_error("guest mapping exceeds 32-bit address space");
        for (const auto& region : regions_) {
            const u64 existing_end = static_cast<u64>(region.base) + region.data.size();
            if (!(end <= region.base || base >= existing_end)) throw std::runtime_error("overlapping guest mapping");
        }
        regions_.push_back(MemoryRegion{base, std::vector<u8>(size), executable});
        std::sort(regions_.begin(), regions_.end(), [](const MemoryRegion& lhs, const MemoryRegion& rhs) { return lhs.base < rhs.base; });
    }
    void CopyIn(u32 address, const u8* source, std::size_t size) {
        MemoryRegion* region = FindMutable(address, size);
        if (!region) throw std::runtime_error("CopyIn outside mapped guest memory");
        std::memcpy(region->data.data() + (address - region->base), source, size);
    }
    bool ReadBytes(u32 address, void* output, std::size_t size) const {
        const MemoryRegion* region = Find(address, size);
        if (!region) return false;
        std::memcpy(output, region->data.data() + (address - region->base), size);
        return true;
    }
    bool WriteBytes(u32 address, const void* source, std::size_t size) {
        MemoryRegion* region = FindMutable(address, size);
        if (!region) return false;
        std::memcpy(region->data.data() + (address - region->base), source, size);
        return true;
    }
    bool ReadCString(u32 address, std::string& output, std::size_t maximum = 1u << 20) const {
        output.clear();
        for (std::size_t i = 0; i < maximum; ++i) {
            u8 value = 0;
            if (!ReadBytes(address + static_cast<u32>(i), &value, 1)) return false;
            if (value == 0) return true;
            output.push_back(static_cast<char>(value));
        }
        return false;
    }
    void* HostPointer(u32 address, std::size_t size) {
        MemoryRegion* region = FindMutable(address, size);
        return region ? static_cast<void*>(region->data.data() + (address - region->base)) : nullptr;
    }
    const void* HostPointer(u32 address, std::size_t size) const {
        const MemoryRegion* region = Find(address, size);
        return region ? static_cast<const void*>(region->data.data() + (address - region->base)) : nullptr;
    }
    bool IsMapped(u32 address, std::size_t size = 1) const { return Find(address, size) != nullptr; }
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
        const MemoryRegion* region = Find(vaddr, 1);
        if (!region) { invalid_access = true; fault_address = vaddr; RequestHalt(); return 0; }
        return region->data[vaddr - region->base];
    }
    u16 MemoryRead16(u32 vaddr) override {
        return static_cast<u16>(MemoryRead8(vaddr)) | static_cast<u16>(static_cast<u16>(MemoryRead8(vaddr + 1)) << 8);
    }
    u32 MemoryRead32(u32 vaddr) override {
        return static_cast<u32>(MemoryRead16(vaddr)) | (static_cast<u32>(MemoryRead16(vaddr + 2)) << 16);
    }
    u64 MemoryRead64(u32 vaddr) override {
        return static_cast<u64>(MemoryRead32(vaddr)) | (static_cast<u64>(MemoryRead32(vaddr + 4)) << 32);
    }
    void MemoryWrite8(u32 vaddr, u8 value) override {
        MemoryRegion* region = FindMutable(vaddr, 1);
        if (!region) { invalid_access = true; fault_address = vaddr; RequestHalt(); return; }
        region->data[vaddr - region->base] = value;
    }
    void MemoryWrite16(u32 vaddr, u16 value) override {
        MemoryWrite8(vaddr, static_cast<u8>(value));
        MemoryWrite8(vaddr + 1, static_cast<u8>(value >> 8));
    }
    void MemoryWrite32(u32 vaddr, u32 value) override {
        MemoryWrite16(vaddr, static_cast<u16>(value));
        MemoryWrite16(vaddr + 2, static_cast<u16>(value >> 16));
    }
    void MemoryWrite64(u32 vaddr, u64 value) override {
        MemoryWrite32(vaddr, static_cast<u32>(value));
        MemoryWrite32(vaddr + 4, static_cast<u32>(value >> 32));
    }
    void InterpreterFallback(u32 pc, std::size_t count) override {
        interpreter_fallback = true; fallback_pc = pc; fallback_count = count; RequestHalt();
    }
    void CallSVC(u32 swi) override {
        svc_pending = true; pending_svc = swi; RequestHalt();
    }
    void ExceptionRaised(u32 pc, Dynarmic::A32::Exception) override {
        exception_seen = true; exception_pc = pc; RequestHalt();
    }
    void AddTicks(u64 ticks) override { ticks_left = ticks > ticks_left ? 0 : ticks_left - ticks; }
    u64 GetTicksRemaining() override { return ticks_left; }

private:
    void RequestHalt() {
        if (attached_cpu_) attached_cpu_->HaltExecution(kCallbackHalt);
        ticks_left = 0;
    }
    const MemoryRegion* Find(u32 address, std::size_t size) const {
        const u64 end = static_cast<u64>(address) + size;
        for (const auto& region : regions_) {
            const u64 region_end = static_cast<u64>(region.base) + region.data.size();
            if (address >= region.base && end <= region_end) return &region;
        }
        return nullptr;
    }
    MemoryRegion* FindMutable(u32 address, std::size_t size) {
        const u64 end = static_cast<u64>(address) + size;
        for (auto& region : regions_) {
            const u64 region_end = static_cast<u64>(region.base) + region.data.size();
            if (address >= region.base && end <= region_end) return &region;
        }
        return nullptr;
    }
    std::vector<MemoryRegion> regions_;
    Dynarmic::A32::Jit* attached_cpu_ = nullptr;
};

struct ImportRecord {
    std::string name;
    u32 address = 0;
    u32 svc = 0;
    u64 calls = 0;
    bool warned = false;
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
    runtime.imports.push_back(ImportRecord{name, address, svc, 0, false});
    WriteArmSvcStub(env, address, svc);
    return address;
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
    if (runtime.jni_onload == 0 || runtime.native_set_paths == 0 || runtime.native_init == 0 || runtime.native_render == 0) throw std::runtime_error("required JNI exports were not found in libgame.so");
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
    bool standard = false;
    std::string path;
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
using GLsizeiptr_ = std::ptrdiff_t;
using GLintptr_ = std::ptrdiff_t;

class WinGlHost {
public:
    ~WinGlHost() { Destroy(); }

    bool Create(int width, int height, std::ostream& log) {
        log_ = &log;
        instance_ = GetModuleHandleA(nullptr);
        const char* class_name = "GeometryDashDynarmicTest3Window";
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
        window_ = CreateWindowExA(0, class_name, "Geometry Dash ARM - Dynarmic x64 Test3",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  rectangle.right - rectangle.left,
                                  rectangle.bottom - rectangle.top,
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
        opengl_ = LoadLibraryA("opengl32.dll");
        if (!opengl_) return Fail("opengl32.dll could not be loaded");

        using SwapInterval = BOOL (WINAPI*)(int);
        if (auto* swap = reinterpret_cast<SwapInterval>(Resolve("wglSwapIntervalEXT"))) swap(1);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        return true;
    }

    void Destroy() {
        if (context_) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context_); context_ = nullptr; }
        if (device_ && window_) { ReleaseDC(window_, device_); device_ = nullptr; }
        if (window_) { DestroyWindow(window_); window_ = nullptr; }
        if (opengl_) { FreeLibrary(opengl_); opengl_ = nullptr; }
        functions_.clear();
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
            if (message.message == WM_QUIT) return false;
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        return !closed_;
    }
    void Swap() { if (device_) SwapBuffers(device_); }
    bool Ready() const { return context_ != nullptr; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        WinGlHost* self = reinterpret_cast<WinGlHost*>(GetWindowLongPtrA(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTA*>(lparam);
            self = static_cast<WinGlHost*>(create->lpCreateParams);
            SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (message == WM_CLOSE) {
            if (self) self->closed_ = true;
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            if (self) self->closed_ = true;
            PostQuitMessage(0);
            return 0;
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
    std::unordered_map<std::string, void*> functions_;
};
#else
class WinGlHost {
public:
    bool Create(int, int, std::ostream&) { return false; }
    void Destroy() {}
    void* Resolve(const char*) { return nullptr; }
    bool PumpMessages() { return false; }
    void Swap() {}
    bool Ready() const { return false; }
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
        storage_shutdown();
    }

    void ConfigureHost(const std::string& apk_path, const std::string& writable_path) {
        apk_path_ = apk_path;
        writable_path_ = writable_path;
#ifdef _WIN32
        CreateDirectoryA(writable_path_.c_str(), nullptr);
#endif
        storage_initialize(writable_path_.c_str());
        const u32 stdin_handle = NewGuestFile(stdin, true, "stdin");
        const u32 stdout_handle = NewGuestFile(stdout, true, "stdout");
        const u32 stderr_handle = NewGuestFile(stderr, true, "stderr");
        for (const auto& object : runtime_.objects) {
            if (object.name == "__sF") {
                env_.MemoryWrite32(object.address + 0u, stdin_handle);
                env_.MemoryWrite32(object.address + 84u, stdout_handle);
                env_.MemoryWrite32(object.address + 168u, stderr_handle);
            }
        }
    }

    bool CreateOpenGlWindow(int width, int height) {
        return gl_.Create(width, height, log_);
    }
    bool PumpMessages() { return gl_.PumpMessages(); }
    void SwapBuffersHost() { gl_.Swap(); }
    double FrameInterval() const { return frame_interval_; }
    u64 PermissiveStubCalls() const { return permissive_stub_calls_; }
    const std::set<std::string>& PermissiveNames() const { return permissive_names_; }
    const std::string& LastError() const { return last_error_; }

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
        bool returned = false;
        const auto started = std::chrono::steady_clock::now();
        auto next_progress = started + std::chrono::seconds(5);

        while ((unlimited_ticks || budget != 0) && !returned) {
            const auto before_run = std::chrono::steady_clock::now();
            if (wall_budget.count() > 0 && before_run - started >= wall_budget) {
                const std::string diagnostic = BuildExecutionDiagnostic(
                    label + " exceeded wall-clock guard", estimated_ticks, before_run - started);
                --call_depth_;
                return Fail(diagnostic);
            }

            const u64 chunk = unlimited_ticks ? 5000000u : std::min<u64>(budget, 5000000u);
            env_.ResetStopState();
            env_.ticks_left = chunk;
            const Dynarmic::HaltReason halt_reason = cpu_.Run();
            cpu_.ClearHalt(kCallbackHalt);

            if (env_.invalid_access) {
                std::ostringstream error;
                error << label << " invalid guest memory at 0x" << std::hex << env_.fault_address
                      << " PC=0x" << cpu_.Regs()[15] << " (" << DescribeAddress(cpu_.Regs()[15]) << ')'
                      << " LR=0x" << cpu_.Regs()[14] << " (" << DescribeAddress(cpu_.Regs()[14]) << ')';
                --call_depth_;
                return Fail(error.str());
            }
            if (env_.interpreter_fallback) {
                std::ostringstream error;
                error << label << " interpreter fallback at 0x" << std::hex << env_.fallback_pc
                      << " (" << DescribeAddress(env_.fallback_pc) << ") count=" << std::dec << env_.fallback_count;
                --call_depth_;
                return Fail(error.str());
            }
            if (env_.exception_seen) {
                std::ostringstream error;
                error << label << " exception at 0x" << std::hex << env_.exception_pc
                      << " (" << DescribeAddress(env_.exception_pc) << ')';
                --call_depth_;
                return Fail(error.str());
            }
            if (env_.svc_pending) {
                estimated_ticks += 1024u;
                if (env_.pending_svc == kSvcReturn) {
                    returned = true;
                    break;
                }
                if (!HandleSvc(env_.pending_svc, label)) {
                    --call_depth_;
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
                --call_depth_;
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
            --call_depth_;
            return Fail(diagnostic);
        }
        if (result) *result = cpu_.Regs()[0];
        --call_depth_;
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
        RememberEvent("import:" + import.name);
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

    u32 Allocate(u32 requested) {
        const u32 size = std::max<u32>(requested, 1u);
        const u32 aligned = AlignUp(size, 16u);
        if (heap_cursor_ > kHeapBase + kHeapSize - aligned) return 0;
        const u32 address = heap_cursor_;
        heap_cursor_ += aligned;
        allocations_[address] = aligned;
        return address;
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
        std::string text;
        return env_.ReadCString(address, text) ? static_cast<u32>(text.size()) : 0;
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
        log_.flush();
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
        log_.flush();
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
            log_.flush();
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
        if (name == "isBackgroundMusicPlaying") return background_music_playing_ ? 1u : 0u;
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
        if (name == "playEffect") { (void)arguments.Word(); (void)arguments.Word(); return next_effect_id_++; }
        return 0;
    }
    u32 DispatchJniFloat(GuestRef* method, ArgCursor& arguments) {
        if (!method) return 0;
        LogFirstMethodCall(method);
        float result = 0.0f;
        if (method->name == "getFloatForKey") {
            const std::string key = RefString(arguments.Word());
            result = storage_get_float(key.c_str(), arguments.FloatArgument());
        } else if (method->name == "getBackgroundMusicVolume") result = background_volume_;
        else if (method->name == "getEffectsVolume") result = effects_volume_;
        else if (method->name == "getBackgroundMusicTime") result = 0.0f;
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
            background_music_playing_ = true;
            log_ << "Audio stub: playBackgroundMusic " << path << " loop=" << loop << '\n';
        } else if (name == "stopBackgroundMusic") background_music_playing_ = false;
        else if (name == "pauseBackgroundMusic") background_music_playing_ = false;
        else if (name == "resumeBackgroundMusic") background_music_playing_ = true;
        else if (name == "setBackgroundMusicVolume") {
            const u64 bits = arguments.U64();
            background_volume_ = static_cast<float>(WordsToDouble(static_cast<u32>(bits), static_cast<u32>(bits >> 32)));
        } else if (name == "setEffectsVolume") effects_volume_ = arguments.FloatArgument();
        else if (name == "preloadBackgroundMusic" || name == "preloadEffect" || name == "unloadEffect") {
            const std::string path = RefString(arguments.Word());
            log_ << "Audio stub: " << name << ' ' << path << '\n';
        } else if (name == "showMessageBox") {
            const std::string title = RefString(arguments.Word());
            const std::string text = RefString(arguments.Word());
            log_ << "JNI message box: " << title << " | " << text << '\n';
        } else {
            // The remaining Android activity calls are safe no-ops for the first-frame milestone.
        }
        log_.flush();
    }

    bool HandleJni(u32 index) {
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
        std::string a = ReadCString(left);
        std::string b = ReadCString(right);
        if (limited) {
            a.resize(std::min<std::size_t>(a.size(), maximum));
            b.resize(std::min<std::size_t>(b.size(), maximum));
        }
        if (insensitive) {
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
        return a < b ? -1 : a > b ? 1 : 0;
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

    u32 NewGuestFile(std::FILE* stream, bool standard, const std::string& path) {
        const u32 handle = 0x23000000u + next_file_id_++ * 0x100u;
        files_[handle] = GuestFile{handle, stream, standard, path};
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
                if (offset < 84u) return FindGuestFile(0x23000100u);
                if (offset < 168u) return FindGuestFile(0x23000200u);
                return FindGuestFile(0x23000300u);
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
        std::FILE* stream = std::fopen(host_path.c_str(), mode.c_str());
        if (!stream) {
            env_.MemoryWrite32(errno_address_, static_cast<u32>(errno));
            if (logged_file_failures_.insert(guest_path).second) log_ << "Dynarmic file open failed: " << guest_path << " mode=" << mode << '\n';
            return 0;
        }
        log_ << "Dynarmic file open: " << guest_path << " -> " << host_path << " mode=" << mode << '\n';
        log_.flush();
        return NewGuestFile(stream, false, host_path);
    }
    u32 ReadGuestFile(u32 destination, u32 element_size, u32 count, u32 handle) {
        GuestFile* file = FindGuestFile(handle);
        const u64 requested = static_cast<u64>(element_size) * count;
        if (!file || !file->stream || requested > 512ull * 1024ull * 1024ull) return 0;
        std::vector<u8> buffer(static_cast<std::size_t>(requested));
        const std::size_t read = std::fread(buffer.data(), element_size, count, file->stream);
        const std::size_t bytes = read * element_size;
        if (bytes && !env_.WriteBytes(destination, buffer.data(), bytes)) return 0;
        return static_cast<u32>(read);
    }
    u32 WriteGuestFile(u32 source, u32 element_size, u32 count, u32 handle) {
        GuestFile* file = FindGuestFile(handle);
        const u64 requested = static_cast<u64>(element_size) * count;
        if (!file || !file->stream || requested > 512ull * 1024ull * 1024ull) return 0;
        std::vector<u8> buffer(static_cast<std::size_t>(requested));
        if (requested && !env_.ReadBytes(source, buffer.data(), buffer.size())) return 0;
        return static_cast<u32>(std::fwrite(buffer.data(), element_size, count, file->stream));
    }
    u32 CloseGuestFile(u32 handle) {
        auto found = files_.find(handle);
        if (found == files_.end()) return static_cast<u32>(-1);
        if (!found->second.standard && found->second.stream) std::fclose(found->second.stream);
        files_.erase(found);
        return 0;
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
        if (name == "glClearDepthf") {
            void* clear_depth = gl_.Resolve("glClearDepth");
            if (!clear_depth) return Fail("OpenGL function unavailable: glClearDepth");
            reinterpret_cast<void (APIENTRY*)(double)>(clear_depth)(static_cast<double>(WordToFloat(ArgWord(0))));
            cpu_.Regs()[0] = 0;
            ResumeAfterStub(import.address);
            return true;
        }
        void* function = gl_.Resolve(name);
        if (!function) return Fail("OpenGL function unavailable: " + name);
        std::array<GlWord, 9> arguments{};
        const unsigned count = GlArgumentCount(name);
        if (count == std::numeric_limits<unsigned>::max()) return Fail("OpenGL argument descriptor missing: " + name);
        for (unsigned i = 0; i < count; ++i) arguments[i] = ArgWord(i);
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
        for (u32 i = 1; i < count; ++i) {
            if (!env_.ReadBytes(base + i * size, pivot.data(), size)) return false;
            u32 j = i;
            while (j > 0) {
                const u32 scratch = Allocate(size);
                if (!scratch || !env_.WriteBytes(scratch, pivot.data(), size)) return false;
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

    bool DispatchImport(ImportRecord& import) {
        const std::string& name = import.name;
        const u32 r0 = cpu_.Regs()[0], r1 = cpu_.Regs()[1], r2 = cpu_.Regs()[2], r3 = cpu_.Regs()[3];
        u32 result = 0;
        bool result_set = true;

        if (name.rfind("gl", 0) == 0) return DispatchGl(import);

        if (name == "malloc") result = Allocate(r0);
        else if (name == "calloc") {
            const u64 total = static_cast<u64>(r0) * r1;
            result = total <= std::numeric_limits<u32>::max() ? Allocate(static_cast<u32>(total)) : 0;
            if (result && total) std::memset(env_.HostPointer(result, static_cast<std::size_t>(total)), 0, static_cast<std::size_t>(total));
        } else if (name == "realloc") {
            if (!r0) result = Allocate(r1);
            else if (!r1) result = 0;
            else {
                const auto found = allocations_.find(r0);
                const u32 old_size = found == allocations_.end() ? 0u : found->second;
                if (old_size >= r1) result = r0;
                else {
                    result = Allocate(r1);
                    if (result && old_size) CopyGuest(result, r0, old_size);
                }
            }
        } else if (name == "free" || name == "__cxa_finalize") result = 0;
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
        else if (name == "fseek") { GuestFile* file = FindGuestFile(r0); result = file && file->stream ? static_cast<u32>(std::fseek(file->stream, static_cast<s32>(r1), static_cast<int>(r2))) : static_cast<u32>(-1); }
        else if (name == "ftell") { GuestFile* file = FindGuestFile(r0); result = file && file->stream ? static_cast<u32>(std::ftell(file->stream)) : static_cast<u32>(-1); }
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
        } else if (name == "read") result = ReadGuestFile(r1, 1, r2, r0);
        else if (name == "write") result = WriteGuestFile(r1, 1, r2, r0);
        else if (name == "lseek") { GuestFile* file = FindGuestFile(r0); if (file && file->stream && std::fseek(file->stream, static_cast<s32>(r1), static_cast<int>(r2)) == 0) result = static_cast<u32>(std::ftell(file->stream)); else result = static_cast<u32>(-1); }
        else if (name == "close") result = CloseGuestFile(r0);
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
        else if (name == "__stack_chk_fail" || name == "abort" || name == "exit" || name == "longjmp" || name == "siglongjmp") return Fail("guest called fatal import " + name);
        else if (name == "setjmp" || name == "sigsetjmp") result = 0;
        else if (name == "pthread_once") {
            const u32 state = env_.MemoryRead32(r0);
            if (state == 0 && !CallNestedInitializer(r0, r1)) return false;
            result = 0;
        } else if (name == "pthread_key_create") { const u32 key = next_pthread_key_++; if (r0) env_.MemoryWrite32(r0, key); result = 0; }
        else if (name == "pthread_key_delete") { thread_values_.erase(r0); result = 0; }
        else if (name == "pthread_setspecific") { thread_values_[r0] = r1; result = 0; }
        else if (name == "pthread_getspecific") result = thread_values_[r0];
        else if (name == "pthread_create") { if (r0) env_.MemoryWrite32(r0, next_thread_id_++); result = 0; }
        else if (name == "pthread_exit") result = 0;
        else if (name == "pthread_detach" || name == "pthread_mutex_init" || name == "pthread_mutex_destroy" || name == "pthread_mutex_lock" || name == "pthread_mutex_unlock" || name == "pthread_cond_broadcast" || name == "pthread_cond_wait" || name == "sem_init" || name == "sem_destroy" || name == "sem_post" || name == "sem_wait") result = 0;
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
            if (r0 && r1) env_.WriteBytes(r0, output.data(), output.size()); result = static_cast<u32>(written);
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
        else if (name == "gethostname") { const char host[]="dynarmic-win64"; if (r0 && r1) { const std::size_t n=std::min<std::size_t>(sizeof(host),r1); env_.WriteBytes(r0,host,n); } result=0; }
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
            FormatCursor cursor{*this,3u,0u}; const std::string text=FormatGuestString(r2,cursor); log_ << "android log: " << text << '\n'; result=static_cast<u32>(text.size());
        } else if (name == "__gnu_Unwind_Find_exidx") { if(r1)env_.MemoryWrite32(r1,0); result=0; }
        else if (name == "dlopen" || name == "dlsym" || name == "dlclose" || name == "dlerror") result=0;
        else if (name == "mmap") result=Allocate(r1);
        else if (name == "munmap") result=0;
        else if (name == "socket" || name == "accept" || name == "bind" || name == "connect" || name == "listen" || name == "recv" || name == "recvfrom" || name == "send" || name == "sendto" || name == "setsockopt" || name == "getsockopt" || name == "getpeername" || name == "getsockname" || name == "poll" || name == "ioctl" || name == "fcntl" || name == "getaddrinfo" || name == "freeaddrinfo" || name == "inet_ntop" || name == "inet_pton" || name == "alarm" || name == "raise" || name == "sigaction") result = name == "freeaddrinfo" ? 0u : static_cast<u32>(-1);
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
    u32 errno_address_=0;
    u32 c_locale_address_=0;
    u32 tm_address_=0;
    u32 time_zone_address_=0;
    u32 next_pthread_key_=1;
    u32 next_thread_id_=1;
    std::unordered_map<u32,u32> thread_values_;
    u64 random_state_=1;
    unsigned call_depth_=0;
    u64 permissive_stub_calls_=0;
    std::set<std::string> permissive_names_;
    std::deque<std::string> recent_events_;
    std::string last_error_;
    std::vector<GuestRef> refs_;
    std::set<u32> unimplemented_jni_slots_;
    std::unordered_map<u32,GuestFile> files_;
    u32 next_file_id_=1;
    std::set<std::string> logged_file_failures_;
    u32 strtok_state_=0;
    std::unordered_map<u32,GuestZStream> zstreams_;
    u64 zlib_init_logs_=0;
    std::string apk_path_;
    std::string writable_path_;
    WinGlHost gl_;
    double frame_interval_=1.0/60.0;
    std::unordered_map<u32,u32> gl_string_cache_;
    u32 gl_array_buffer_binding_=0;
    u32 gl_element_buffer_binding_=0;
    u64 logged_guest_stdio_=0;
    bool background_music_playing_=false;
    float background_volume_=1.0f;
    float effects_volume_=1.0f;
    u32 next_effect_id_=1;
};

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

extern "C" void runtime_log(const char* format, ...) {
    if (!format) return;
    std::fprintf(stderr, "[storage] ");
    va_list args; va_start(args,format); std::vfprintf(stderr,format,args); va_end(args);
    std::fputc('\n',stderr);
}

int main(int argc,char** argv) {
    std::string log_path = "gd-dynarmic-probe.log";
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument.rfind("--log=", 0) == 0 && argument.size() > 6u)
            log_path = std::string(argument.substr(6));
    }
    std::ofstream log_file(log_path,std::ios::trunc);
    auto emit=[&](const std::string& line){std::cout<<line<<'\n';log_file<<line<<'\n';log_file.flush();};
    try {
        emit("Geometry Dash ARM wrapper 0.9.4-arm-dynarmictest3-fix1");
        emit("Milestone: x64 Dynarmic nativeInit wall guard and execution diagnostics");
        emit("Log file: " + log_path);
        emit("Host pointer bits: "+std::to_string(sizeof(void*)*8));
        if(sizeof(void*)!=8) throw std::runtime_error("DynarmicTest3 must be compiled as a 64-bit executable");
        RunThumbSmoke();
        emit("RESULT: DYNARMIC_X64_THUMB_SMOKE_OK r0=8 guest=v5TE host=x86_64");

        std::string apk_path="game.apk";
        bool probe_only=false;
        int width=1280,height=720,frames=180;
        for(int i=1;i<argc;++i){
            const std::string_view argument(argv[i]);
            if(argument=="--probe-only") probe_only=true;
            else if(argument.rfind("--frames=",0)==0) frames=std::max(1,std::stoi(std::string(argument.substr(9))));
            else if(argument.rfind("--width=",0)==0) width=std::max(320,std::stoi(std::string(argument.substr(8))));
            else if(argument.rfind("--height=",0)==0) height=std::max(240,std::stoi(std::string(argument.substr(9))));
            else if(!argument.empty()&&argument[0]!='-') apk_path=std::string(argument);
        }
        const std::filesystem::path absolute_apk=std::filesystem::absolute(apk_path);
        const std::filesystem::path writable=std::filesystem::absolute("save");
        emit("Input APK: "+absolute_apk.string());
        const std::vector<u8> apk=ReadFile(absolute_apk.string());
        emit("APK bytes: "+std::to_string(apk.size()));
        const std::vector<u8> libgame=ExtractZipMember(apk,"lib/armeabi/libgame.so");
        emit("Extracted lib/armeabi/libgame.so: "+std::to_string(libgame.size())+" bytes");
        ProbeEnvironment env;
        ElfRuntime runtime=MapAndRelocateElf(libgame,env);
        {
            std::ostringstream line; line<<"Image: 0x"<<std::hex<<runtime.image_min<<"-0x"<<runtime.image_max<<" entry=0x"<<runtime.entry<<std::dec; emit(line.str());
        }
        emit("Authentic ARM constructors: "+std::to_string(runtime.constructors.size()));
        emit("Dynarmic relocation targets: function-imports="+std::to_string(runtime.imports.size())+" objects="+std::to_string(runtime.objects.size()));
        {
            std::ostringstream line; line<<"Exports: JNI_OnLoad=0x"<<std::hex<<runtime.jni_onload<<" nativeSetPaths=0x"<<runtime.native_set_paths<<" nativeInit=0x"<<runtime.native_init<<" nativeRender=0x"<<runtime.native_render<<std::dec; emit(line.str());
        }
        emit("RESULT: DYNARMIC_RELOCATION_OK");
        GuestExecutor executor(env,runtime,log_file);
        executor.ConfigureHost(absolute_apk.string(),writable.string());
        emit("Running "+std::to_string(runtime.constructors.size())+" authentic ARM constructors through Dynarmic");
        for(std::size_t index=0;index<runtime.constructors.size();++index){
            const u32 entry=runtime.constructors[index]; if(entry==0||entry==std::numeric_limits<u32>::max())continue;
            if(index<8||((index+1u)%32u)==0u||index+1u==runtime.constructors.size()){
                std::ostringstream line; line<<"constructor "<<(index+1u)<<'/'<<runtime.constructors.size()<<": guest 0x"<<std::hex<<entry<<std::dec;emit(line.str());
            }
            u32 ignored=0; if(!executor.RunFunction(entry,{},&ignored,"constructor "+std::to_string(index+1u))){emit("RESULT: DYNARMIC_CONSTRUCTOR_FAILED index="+std::to_string(index+1u));throw std::runtime_error(executor.LastError());}
        }
        emit("RESULT: DYNARMIC_CONSTRUCTORS_OK count="+std::to_string(runtime.constructors.size()));
        u32 result=0;
        if(!executor.RunFunction(runtime.jni_onload,{kVmObject,0u},&result,"JNI_OnLoad")) throw std::runtime_error(executor.LastError());
        {std::ostringstream line;line<<"Dynarmic JNI_OnLoad returned 0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<result<<std::dec;emit(line.str());}
        if(result!=kJniVersion14) throw std::runtime_error("JNI_OnLoad returned unexpected version");
        emit("RESULT: DYNARMIC_JNI_ONLOAD_OK result=0x00010004");
        if(probe_only){emit("RESULT: DYNARMIC_BRINGUP3_FIX1_PROBE_ONLY_OK");return 0;}

        const u32 apk_ref=executor.NewStringRef(absolute_apk.string());
        if(!apk_ref||!executor.RunFunction(runtime.native_set_paths,{kEnvObject,0u,apk_ref},&result,"nativeSetPaths")) throw std::runtime_error(executor.LastError());
        emit("RESULT: DYNARMIC_PATHS_SET");
        if(!executor.CreateOpenGlWindow(width,height)) throw std::runtime_error("could not create Win32 OpenGL host window");
        emit("RESULT: DYNARMIC_OPENGL_HOST_OK");
        const auto init_start=std::chrono::steady_clock::now();
        if(!executor.RunFunction(runtime.native_init,{kEnvObject,0u,static_cast<u32>(width),static_cast<u32>(height)},&result,"nativeInit",0u,std::chrono::milliseconds(120000))) throw std::runtime_error(executor.LastError());
        const double init_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-init_start).count();
        emit("RESULT: DYNARMIC_NATIVE_INIT_RETURNED time_ms="+std::to_string(init_ms));
        emit("RESULT: DYNARMIC_RENDER_LOOP_ENTERED");
        bool first_frame=false;
        for(int frame=0;frame<frames&&executor.PumpMessages();++frame){
            const auto start=std::chrono::steady_clock::now();
            if(!executor.RunFunction(runtime.native_render,{kEnvObject,0u},&result,"nativeRender frame "+std::to_string(frame+1),0u,std::chrono::milliseconds(30000))) throw std::runtime_error(executor.LastError());
            executor.SwapBuffersHost();
            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(!first_frame){first_frame=true;emit("RESULT: DYNARMIC_FIRST_FRAME_OK frame_ms="+std::to_string(elapsed));}
            if((frame+1)%60==0) emit("Dynarmic render progress: frame="+std::to_string(frame+1)+" last_ms="+std::to_string(elapsed));
        }
        if(!first_frame) throw std::runtime_error("render loop ended before the first frame");
        emit("Permissive runtime import calls: "+std::to_string(executor.PermissiveStubCalls())+" unique="+std::to_string(executor.PermissiveNames().size()));
        if(!executor.PermissiveNames().empty()){std::ostringstream names;names<<"Permissive imports:";for(const auto& name:executor.PermissiveNames())names<<' '<<name;emit(names.str());}
        emit("RESULT: DYNARMIC_BRINGUP3_FIX1_OK");
        return 0;
    } catch(const std::exception& error){
        emit(std::string("ERROR: ")+error.what());
        emit("RESULT: DYNARMIC_BRINGUP3_FIX1_FAILED");
        return 1;
    }
}

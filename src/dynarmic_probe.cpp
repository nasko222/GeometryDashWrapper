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

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"

extern "C" {
#include "zlib.h"
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
    u32 native_init = 0;
    u32 native_render = 0;
    std::vector<u32> constructors;
    std::vector<ImportRecord> imports;
    std::vector<ObjectRecord> objects;
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
            if (name == "JNI_OnLoad") runtime.jni_onload = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit") runtime.native_init = address;
            else if (name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender") runtime.native_render = address;
        }
    }

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
    if (runtime.jni_onload == 0 || runtime.native_init == 0 || runtime.native_render == 0) throw std::runtime_error("required JNI exports were not found in libgame.so");
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

class GuestExecutor {
public:
    GuestExecutor(ProbeEnvironment& env, ElfRuntime& runtime, std::ostream& log)
        : env_(env), runtime_(runtime), log_(log), cpu_(MakeConfig(env)) {
        env_.AttachCpu(&cpu_);
        InitializeControlTraps();
        heap_cursor_ = kHeapBase + 0x1000u;
        errno_address_ = kObjectBase + kObjectRegionSize - 0x1000u;
        env_.MemoryWrite32(errno_address_, 0u);
        const char c_locale[] = "C";
        c_locale_address_ = Allocate(sizeof(c_locale));
        env_.WriteBytes(c_locale_address_, c_locale, sizeof(c_locale));
    }

    bool RunFunction(u32 address, const std::vector<u32>& arguments, u32* result, const std::string& label) {
        if (call_depth_ >= 16) return Fail("guest call recursion limit reached in " + label);
        ++call_depth_;
        cpu_.Regs().fill(0);
        cpu_.ExtRegs().fill(0);
        for (std::size_t i = 0; i < arguments.size() && i < 4; ++i) cpu_.Regs()[i] = arguments[i];
        const u32 stack_top = kStackBase + kStackSize - static_cast<u32>(call_depth_) * 0x00080000u;
        cpu_.Regs()[13] = stack_top - 0x100u;
        cpu_.Regs()[14] = kReturnStub;
        cpu_.Regs()[15] = address & ~1u;
        cpu_.SetCpsr(0x00000010u | ((address & 1u) ? 0x20u : 0u));
        cpu_.SetFpscr(0u);
        u64 budget = kGuestCallTickBudget;
        bool returned = false;
        while (budget != 0 && !returned) {
            const u64 chunk = std::min<u64>(budget, 5000000u);
            env_.ResetStopState();
            env_.ticks_left = chunk;
            const Dynarmic::HaltReason halt_reason = cpu_.Run();
            cpu_.ClearHalt(kCallbackHalt);
            if (env_.invalid_access) {
                std::ostringstream error; error << label << " invalid guest memory at 0x" << std::hex << env_.fault_address;
                --call_depth_; return Fail(error.str());
            }
            if (env_.interpreter_fallback) {
                std::ostringstream error; error << label << " interpreter fallback at 0x" << std::hex << env_.fallback_pc << " count=" << std::dec << env_.fallback_count;
                --call_depth_; return Fail(error.str());
            }
            if (env_.exception_seen) {
                std::ostringstream error; error << label << " exception at 0x" << std::hex << env_.exception_pc;
                --call_depth_; return Fail(error.str());
            }
            if (env_.svc_pending) {
                if (env_.pending_svc == kSvcReturn) {
                    returned = true;
                    break;
                }
                if (!HandleSvc(env_.pending_svc, label)) { --call_depth_; return false; }
                budget = budget > 1024 ? budget - 1024 : 0;
                continue;
            }
            if (env_.ticks_left == 0) budget = budget > chunk ? budget - chunk : 0;
            else {
                std::ostringstream error;
                error << label << " stopped without a trap at PC=0x" << std::hex << cpu_.Regs()[15]
                      << " LR=0x" << cpu_.Regs()[14] << " SP=0x" << cpu_.Regs()[13]
                      << " CPSR=0x" << cpu_.Cpsr() << " halt=0x" << static_cast<u64>(halt_reason);
                --call_depth_; return Fail(error.str());
            }
        }
        if (!returned) { --call_depth_; return Fail(label + " exceeded guest tick budget"); }
        if (result) *result = cpu_.Regs()[0];
        --call_depth_;
        return true;
    }

    u64 PermissiveStubCalls() const { return permissive_stub_calls_; }
    const std::set<std::string>& PermissiveNames() const { return permissive_names_; }
    const std::string& LastError() const { return last_error_; }

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
        if (svc >= kSvcVmBase && svc < kSvcVmBase + 8u) return HandleVm(svc - kSvcVmBase);
        if (svc >= kSvcJniBase && svc < kSvcJniBase + kJniTableSize) return HandleJni(svc - kSvcJniBase);
        if (svc == 0 || svc > runtime_.imports.size()) {
            std::ostringstream error; error << label << " unknown SVC 0x" << std::hex << svc;
            return Fail(error.str());
        }
        ImportRecord& import = runtime_.imports[svc - 1u];
        ++import.calls;
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
    bool HandleJni(u32 index) {
        u32 result = 0;
        switch (index) {
        case 4: result = kJniVersion14; break; // GetVersion
        case 6: result = NextFakeRef(); break; // FindClass
        case 15: result = 0; break;            // ExceptionOccurred
        case 17: result = 0; break;            // ExceptionClear
        case 21: result = cpu_.Regs()[1]; break; // NewGlobalRef
        case 23: result = 0; break;            // DeleteLocalRef
        case 33: case 94: case 113: case 144: result = NextFakeRef(); break;
        case 215: result = 0; break;            // RegisterNatives
        case 216: result = 0; break;            // UnregisterNatives
        case 219:
            if (cpu_.Regs()[1]) env_.MemoryWrite32(cpu_.Regs()[1], kVmObject);
            result = 0;
            break;
        case 228: result = 0; break;            // ExceptionCheck
        default: result = 0; break;
        }
        cpu_.Regs()[0] = result;
        ResumeAfterStub(kEnvStubs + index * 8u);
        return true;
    }
    u32 NextFakeRef() {
        const u32 value = kFakeRefBase + fake_ref_counter_ * 0x10u;
        ++fake_ref_counter_;
        return value;
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
    bool CopyGuest(u32 destination, u32 source, u32 size, bool overlap) {
        if (size == 0) return true;
        std::vector<u8> temporary(size);
        if (!env_.ReadBytes(source, temporary.data(), size)) return false;
        if (overlap) return env_.WriteBytes(destination, temporary.data(), size);
        return env_.WriteBytes(destination, temporary.data(), size);
    }
    u32 CStringLength(u32 address) {
        std::string text;
        if (!env_.ReadCString(address, text)) return 0;
        return static_cast<u32>(text.size());
    }
    int CompareStrings(u32 left, u32 right, u32 maximum, bool limited, bool insensitive) {
        std::string a, b;
        if (!env_.ReadCString(left, a) || !env_.ReadCString(right, b)) return 0;
        if (limited) { a.resize(std::min<std::size_t>(a.size(), maximum)); b.resize(std::min<std::size_t>(b.size(), maximum)); }
        if (insensitive) {
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        }
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
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
    bool DispatchImport(ImportRecord& import) {
        const std::string& name = import.name;
        const u32 r0 = cpu_.Regs()[0], r1 = cpu_.Regs()[1], r2 = cpu_.Regs()[2], r3 = cpu_.Regs()[3];
        u32 result = 0;
        bool result_set = true;
        if (name == "malloc") result = Allocate(r0);
        else if (name == "calloc") {
            const u64 total = static_cast<u64>(r0) * r1;
            result = total <= std::numeric_limits<u32>::max() ? Allocate(static_cast<u32>(total)) : 0;
            if (result && total) { std::vector<u8> zeros(static_cast<std::size_t>(total)); env_.WriteBytes(result, zeros.data(), zeros.size()); }
        } else if (name == "realloc") {
            if (!r0) result = Allocate(r1);
            else if (!r1) result = 0;
            else {
                const auto found = allocations_.find(r0);
                const u32 old_size = found == allocations_.end() ? 0u : found->second;
                if (old_size >= r1) result = r0;
                else { result = Allocate(r1); if (result && old_size) CopyGuest(result, r0, old_size, true); }
            }
        } else if (name == "free" || name == "__cxa_finalize") result = 0;
        else if (name == "memcpy" || name == "memmove") result = CopyGuest(r0, r1, r2, name == "memmove") ? r0 : 0;
        else if (name == "memset") {
            std::vector<u8> fill(r2, static_cast<u8>(r1));
            result = env_.WriteBytes(r0, fill.data(), fill.size()) ? r0 : 0;
        } else if (name == "memcmp") {
            std::vector<u8> a(r2), b(r2);
            result = (env_.ReadBytes(r0, a.data(), a.size()) && env_.ReadBytes(r1, b.data(), b.size())) ? static_cast<u32>(std::memcmp(a.data(), b.data(), a.size())) : 0;
        } else if (name == "strlen") result = CStringLength(r0);
        else if (name == "strcmp") result = static_cast<u32>(CompareStrings(r0, r1, 0, false, false));
        else if (name == "strncmp") result = static_cast<u32>(CompareStrings(r0, r1, r2, true, false));
        else if (name == "strcasecmp") result = static_cast<u32>(CompareStrings(r0, r1, 0, false, true));
        else if (name == "strncasecmp") result = static_cast<u32>(CompareStrings(r0, r1, r2, true, true));
        else if (name == "strcpy" || name == "strcat" || name == "strncpy" || name == "strlcat") {
            std::string source;
            if (!env_.ReadCString(r1, source)) result = 0;
            else {
                std::string destination;
                if (name == "strcat" || name == "strlcat") env_.ReadCString(r0, destination);
                std::string combined = destination + source;
                if (name == "strncpy") combined.resize(std::min<std::size_t>(combined.size(), r2));
                if (name == "strlcat" && r2 != 0 && combined.size() >= r2) combined.resize(r2 - 1u);
                env_.WriteBytes(r0, combined.c_str(), combined.size() + 1u);
                result = name == "strlcat" ? static_cast<u32>(destination.size() + source.size()) : r0;
            }
        } else if (name == "strdup") {
            std::string source;
            if (env_.ReadCString(r0, source)) { result = Allocate(static_cast<u32>(source.size() + 1u)); if (result) env_.WriteBytes(result, source.c_str(), source.size() + 1u); }
        } else if (name == "memchr" || name == "memrchr") {
            std::vector<u8> bytes(r2);
            if (env_.ReadBytes(r0, bytes.data(), bytes.size())) {
                if (name == "memchr") {
                    const auto it = std::find(bytes.begin(), bytes.end(), static_cast<u8>(r1));
                    result = it == bytes.end() ? 0 : r0 + static_cast<u32>(it - bytes.begin());
                } else {
                    const auto it = std::find(bytes.rbegin(), bytes.rend(), static_cast<u8>(r1));
                    result = it == bytes.rend() ? 0 : r0 + static_cast<u32>(bytes.size() - 1u - static_cast<std::size_t>(it - bytes.rbegin()));
                }
            }
        } else if (name == "__cxa_atexit") result = 0;
        else if (name == "__errno") result = errno_address_;
        else if (name == "__stack_chk_fail" || name == "abort" || name == "exit") return Fail("guest called fatal import " + name);
        else if (name == "pthread_once") {
            const u32 state = env_.MemoryRead32(r0);
            if (state == 0 && !CallNestedInitializer(r0, r1)) return false;
            result = 0;
        } else if (name == "pthread_key_create") {
            const u32 key = next_pthread_key_++;
            if (r0) env_.MemoryWrite32(r0, key);
            result = 0;
        } else if (name == "pthread_key_delete") { thread_values_.erase(r0); result = 0; }
        else if (name == "pthread_setspecific") { thread_values_[r0] = r1; result = 0; }
        else if (name == "pthread_getspecific") { result = thread_values_[r0]; }
        else if (name == "pthread_mutex_init" || name == "pthread_mutex_destroy" || name == "pthread_mutex_lock" || name == "pthread_mutex_unlock" ||
                 name == "pthread_cond_broadcast" || name == "pthread_cond_wait" || name == "sem_init" || name == "sem_destroy" || name == "sem_post" || name == "sem_wait") result = 0;
        else if (name == "wctob") result = r0 <= 0xFFu ? r0 : static_cast<u32>(-1);
        else if (name == "btowc") result = r0 == static_cast<u32>(-1) ? static_cast<u32>(-1) : static_cast<u8>(r0);
        else if (name == "wctype") {
            std::string type;
            if (env_.ReadCString(r0, type)) {
                static const std::array<const char*, 12> classes = {"alnum","alpha","blank","cntrl","digit","graph","lower","print","punct","space","upper","xdigit"};
                for (u32 i = 0; i < classes.size(); ++i) if (type == classes[i]) { result = i + 1u; break; }
            }
        } else if (name == "iswctype") {
            const int c = r0 <= 0xFFu ? static_cast<int>(r0) : 0;
            switch (r1) {
            case 1: result = std::isalnum(c) != 0; break; case 2: result = std::isalpha(c) != 0; break;
            case 3: result = c == ' ' || c == '\t'; break; case 4: result = std::iscntrl(c) != 0; break;
            case 5: result = std::isdigit(c) != 0; break; case 6: result = std::isgraph(c) != 0; break;
            case 7: result = std::islower(c) != 0; break; case 8: result = std::isprint(c) != 0; break;
            case 9: result = std::ispunct(c) != 0; break; case 10: result = std::isspace(c) != 0; break;
            case 11: result = std::isupper(c) != 0; break; case 12: result = std::isxdigit(c) != 0; break;
            default: result = 0; break;
            }
        } else if (name == "tolower" || name == "towlower") result = static_cast<u32>(std::tolower(static_cast<unsigned char>(r0)));
        else if (name == "toupper" || name == "towupper") result = static_cast<u32>(std::toupper(static_cast<unsigned char>(r0)));
        else if (name == "setlocale") result = c_locale_address_;
        else if (name == "__android_log_print" || name == "printf" || name == "fprintf" || name == "vfprintf" || name == "fputs" || name == "fputc" || name == "putc") result = 0;
        else if (name == "getenv") result = 0;
        else if (name == "arc4random") result = 0x13579BDFu + static_cast<u32>(import.calls * 1103515245u);
        else if (name == "srand48") { random_state_ = r0 ? r0 : 1u; result = 0; }
        else if (name == "lrand48") { random_state_ = random_state_ * 25214903917ull + 11ull; result = static_cast<u32>((random_state_ >> 17) & 0x7FFFFFFFu); }
        else if (name == "sin") { ReturnDouble(std::sin(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "cos") { ReturnDouble(std::cos(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "tan") { ReturnDouble(std::tan(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "asin") { ReturnDouble(std::asin(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "acos") { ReturnDouble(std::acos(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "atan") { ReturnDouble(std::atan(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "atan2") { ReturnDouble(std::atan2(WordsToDouble(r0, r1), WordsToDouble(r2, r3))); result_set = false; }
        else if (name == "sqrt") { ReturnDouble(std::sqrt(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "floor") { ReturnDouble(std::floor(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "ceil") { ReturnDouble(std::ceil(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "round") { ReturnDouble(std::round(WordsToDouble(r0, r1))); result_set = false; }
        else if (name == "roundf") result = FloatToWord(std::round(WordToFloat(r0)));
        else if (name == "pow") { ReturnDouble(std::pow(WordsToDouble(r0, r1), WordsToDouble(r2, r3))); result_set = false; }
        else if (name == "fmod") { ReturnDouble(std::fmod(WordsToDouble(r0, r1), WordsToDouble(r2, r3))); result_set = false; }
        else if (name == "__fpclassifyd") result = static_cast<u32>(std::fpclassify(WordsToDouble(r0, r1)));
        else if (name == "__gnu_Unwind_Find_exidx") { if (r1) env_.MemoryWrite32(r1, 0u); result = 0; }
        else if (name == "dlopen" || name == "dlsym" || name == "dlclose" || name == "dlerror") result = 0;
        else {
            ++permissive_stub_calls_;
            permissive_names_.insert(name);
            if (!import.warned) {
                import.warned = true;
                log_ << "Dynarmic permissive constructor stub: " << name << " -> 0\n";
                log_.flush();
            }
            result = 0;
        }
        if (result_set) cpu_.Regs()[0] = result;
        ResumeAfterStub(import.address);
        return true;
    }

    ProbeEnvironment& env_;
    ElfRuntime& runtime_;
    std::ostream& log_;
    Dynarmic::A32::Jit cpu_;
    u32 heap_cursor_ = 0;
    std::map<u32, u32> allocations_;
    u32 errno_address_ = 0;
    u32 c_locale_address_ = 0;
    u32 next_pthread_key_ = 1;
    std::unordered_map<u32, u32> thread_values_;
    u64 random_state_ = 1;
    u32 fake_ref_counter_ = 1;
    unsigned call_depth_ = 0;
    u64 permissive_stub_calls_ = 0;
    std::set<std::string> permissive_names_;
    std::string last_error_;
};

static void RunThumbSmoke() {
    ProbeEnvironment env;
    env.Map(kSmokeBase, kPageSize, true);
    env.MemoryWrite16(kSmokeBase + 0, 0x0088u); // lsls r0, r1, #2
    env.MemoryWrite16(kSmokeBase + 2, 0xE7FEu); // b .
    Dynarmic::A32::UserConfig config;
    config.callbacks = &env;
    config.arch_version = Dynarmic::A32::ArchVersion::v5TE;
    config.check_halt_on_memory_access = true;
    Dynarmic::A32::Jit cpu{config};
    env.AttachCpu(&cpu);
    cpu.Regs().fill(0);
    cpu.Regs()[0] = 1;
    cpu.Regs()[1] = 2;
    cpu.Regs()[15] = kSmokeBase;
    cpu.SetCpsr(0x00000030u);
    env.ticks_left = 1;
    cpu.Run();
    if (env.invalid_access || env.interpreter_fallback || env.exception_seen || cpu.Regs()[0] != 8) {
        std::ostringstream message;
        message << "Dynarmic Thumb smoke failed: r0=" << cpu.Regs()[0] << " invalid=" << env.invalid_access
                << " fallback=" << env.interpreter_fallback << " exception=" << env.exception_seen;
        throw std::runtime_error(message.str());
    }
}

} // namespace

int main(int argc, char** argv) {
    std::ofstream log_file("gd-dynarmic-probe.log", std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::cout << line << '\n';
        log_file << line << '\n';
        log_file.flush();
    };
    try {
        emit("Geometry Dash ARM wrapper 0.9.4-arm-dynarmictest2-fix1");
        emit("Milestone: x64 Dynarmic relocations, import traps, constructors, and JNI_OnLoad");
        emit("Host pointer bits: " + std::to_string(sizeof(void*) * 8));
        if (sizeof(void*) != 8) throw std::runtime_error("DynarmicTest2 must be compiled as a 64-bit executable");
        RunThumbSmoke();
        emit("RESULT: DYNARMIC_X64_THUMB_SMOKE_OK r0=8 guest=v5TE host=x86_64");

        std::string apk_path = "game.apk";
        bool relocate_only = false;
        for (int index = 1; index < argc; ++index) {
            if (std::string_view(argv[index]) == "--relocate-only") relocate_only = true;
            else if (argv[index][0] != '-') apk_path = argv[index];
        }
        emit("Input APK: " + apk_path);
        const std::vector<u8> apk = ReadFile(apk_path);
        emit("APK bytes: " + std::to_string(apk.size()));
        const std::vector<u8> libgame = ExtractZipMember(apk, "lib/armeabi/libgame.so");
        emit("Extracted lib/armeabi/libgame.so: " + std::to_string(libgame.size()) + " bytes");

        ProbeEnvironment env;
        ElfRuntime runtime = MapAndRelocateElf(libgame, env);
        {
            std::ostringstream line;
            line << "Image: 0x" << std::hex << runtime.image_min << "-0x" << runtime.image_max
                 << " entry=0x" << runtime.entry << std::dec;
            emit(line.str());
        }
        emit("PT_LOAD segments: " + std::to_string(runtime.load_segments) + " executable=" + std::to_string(runtime.executable_segments));
        emit("Authentic ARM constructors: " + std::to_string(runtime.constructors.size()));
        emit("Dynamic symbols: " + std::to_string(runtime.dynsym_count) + " undefined-import symbols=" + std::to_string(runtime.undefined_symbols));
        emit("Relocations: total=" + std::to_string(runtime.relocation_count) + " relative=" + std::to_string(runtime.relative_relocations) +
             " imported=" + std::to_string(runtime.imported_relocations));
        emit("Dynarmic relocation targets: function-imports=" + std::to_string(runtime.imports.size()) + " objects=" + std::to_string(runtime.objects.size()));
        {
            std::ostringstream line;
            line << "Exports: JNI_OnLoad=0x" << std::hex << runtime.jni_onload << " nativeInit=0x" << runtime.native_init
                 << " nativeRender=0x" << runtime.native_render << std::dec;
            emit(line.str());
        }
        if (runtime.constructors.size() != 238) throw std::runtime_error("expected 238 constructors for this Geometry Dash 1.4 libgame.so");
        if (runtime.imports.size() != 277 || runtime.objects.size() != 7) {
            std::ostringstream warning;
            warning << "WARNING: expected relocation layout 277 functions/7 objects, got " << runtime.imports.size() << '/' << runtime.objects.size();
            emit(warning.str());
        }
        emit("RESULT: DYNARMIC_RELOCATION_OK");
        if (relocate_only) { emit("RESULT: DYNARMIC_BRINGUP2_RELOCATE_ONLY_OK"); return 0; }

        GuestExecutor executor(env, runtime, log_file);
        emit("Running 238 authentic ARM constructors through Dynarmic");
        for (std::size_t index = 0; index < runtime.constructors.size(); ++index) {
            const u32 entry = runtime.constructors[index];
            if (entry == 0 || entry == std::numeric_limits<u32>::max()) continue;
            if (index < 8 || ((index + 1u) % 32u) == 0u || index + 1u == runtime.constructors.size()) {
                std::ostringstream line;
                line << "constructor " << (index + 1u) << '/' << runtime.constructors.size() << ": guest 0x" << std::hex << entry
                     << " (ELF+0x" << (entry - kGameBase) << ')' << std::dec;
                emit(line.str());
            }
            u32 ignored = 0;
            if (!executor.RunFunction(entry, {}, &ignored, "constructor " + std::to_string(index + 1u))) {
                emit("DYNARMIC EXECUTION ERROR: " + executor.LastError());
                emit("RESULT: DYNARMIC_CONSTRUCTOR_FAILED index=" + std::to_string(index + 1u));
                throw std::runtime_error("constructor execution failed");
            }
        }
        emit("RESULT: DYNARMIC_CONSTRUCTORS_OK count=238");

        {
            std::ostringstream line;
            line << "Calling authentic ARM JNI_OnLoad at guest 0x" << std::hex << runtime.jni_onload << std::dec;
            emit(line.str());
        }
        u32 jni_result = 0;
        if (!executor.RunFunction(runtime.jni_onload, {kVmObject, 0u}, &jni_result, "JNI_OnLoad")) {
            emit("DYNARMIC EXECUTION ERROR: " + executor.LastError());
            emit("RESULT: DYNARMIC_JNI_ONLOAD_FAILED");
            throw std::runtime_error("JNI_OnLoad execution failed");
        }
        {
            std::ostringstream line;
            line << "Dynarmic JNI_OnLoad returned 0x" << std::hex << std::setw(8) << std::setfill('0') << jni_result << std::dec;
            emit(line.str());
        }
        if (jni_result != kJniVersion14) throw std::runtime_error("JNI_OnLoad returned an unexpected JNI version");
        emit("RESULT: DYNARMIC_JNI_ONLOAD_OK result=0x00010004");
        emit("Permissive constructor import calls: " + std::to_string(executor.PermissiveStubCalls()) +
             " unique=" + std::to_string(executor.PermissiveNames().size()));
        if (!executor.PermissiveNames().empty()) {
            std::ostringstream names;
            names << "Permissive imports:";
            for (const auto& name : executor.PermissiveNames()) names << ' ' << name;
            emit(names.str());
        }
        emit("NEXT: connect nativeSetPaths/nativeInit and the existing JNI/OpenGL/file bridges");
        emit("RESULT: DYNARMIC_BRINGUP2_OK");
        return 0;
    } catch (const std::exception& exception) {
        emit(std::string("ERROR: ") + exception.what());
        emit("RESULT: DYNARMIC_BRINGUP2_FAILED");
        return 1;
    }
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

namespace {

constexpr u32 kGameBase = 0x10000000u;
constexpr u32 kSmokeBase = 0x0F000000u;
constexpr u32 kPageSize = 0x1000u;
constexpr u32 kPtLoad = 1;
constexpr u16 kEtDyn = 3;
constexpr u16 kEmArm = 40;
constexpr u32 kShtDynsym = 11;
constexpr u32 kShtRel = 9;
constexpr u32 kShtInitArray = 14;
constexpr u16 kShnUndef = 0;
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
    if (!file) {
        throw std::runtime_error("could not open " + path);
    }
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
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }
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
            if (ReadLe32(zip, pos) == kEocdSignature) {
                eocd = pos;
                break;
            }
            if (pos == search_start) {
                break;
            }
        }
    }
    if (!eocd) {
        throw std::runtime_error("APK end-of-central-directory record not found");
    }

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
        if (next > zip.size()) {
            throw std::runtime_error("truncated APK central-directory name");
        }
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
                if (output.size() != uncompressed_size) {
                    throw std::runtime_error("stored APK member size mismatch");
                }
            } else if (method == 8) {
                output = InflateRaw(zip.data() + data_offset, compressed_size, uncompressed_size);
            } else {
                throw std::runtime_error("unsupported APK compression method " + std::to_string(method));
            }
            const u32 actual_crc = static_cast<u32>(crc32(0, reinterpret_cast<const Bytef*>(output.data()), static_cast<uInt>(output.size())));
            if (actual_crc != expected_crc) {
                throw std::runtime_error("APK member CRC mismatch");
            }
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
    u32 last_svc = 0;

    void Map(u32 base, std::size_t size, bool executable) {
        if (size == 0 || size > std::numeric_limits<u32>::max()) {
            throw std::runtime_error("invalid guest mapping size");
        }
        const u64 end = static_cast<u64>(base) + size;
        if (end > 0x100000000ull) {
            throw std::runtime_error("guest mapping exceeds 32-bit address space");
        }
        for (const auto& region : regions_) {
            const u64 existing_end = static_cast<u64>(region.base) + region.data.size();
            if (!(end <= region.base || base >= existing_end)) {
                throw std::runtime_error("overlapping guest mapping");
            }
        }
        regions_.push_back(MemoryRegion{base, std::vector<u8>(size), executable});
        std::sort(regions_.begin(), regions_.end(), [](const MemoryRegion& lhs, const MemoryRegion& rhs) {
            return lhs.base < rhs.base;
        });
    }

    void CopyIn(u32 address, const u8* source, std::size_t size) {
        MemoryRegion* region = FindMutable(address, size);
        if (!region) {
            throw std::runtime_error("CopyIn outside mapped guest memory");
        }
        std::memcpy(region->data.data() + (address - region->base), source, size);
    }

    u8 MemoryRead8(u32 vaddr) override {
        const MemoryRegion* region = Find(vaddr, 1);
        if (!region) {
            invalid_access = true;
            ticks_left = 0;
            return 0;
        }
        return region->data[vaddr - region->base];
    }

    u16 MemoryRead16(u32 vaddr) override {
        return static_cast<u16>(MemoryRead8(vaddr)) |
               static_cast<u16>(static_cast<u16>(MemoryRead8(vaddr + 1)) << 8);
    }

    u32 MemoryRead32(u32 vaddr) override {
        return static_cast<u32>(MemoryRead16(vaddr)) |
               (static_cast<u32>(MemoryRead16(vaddr + 2)) << 16);
    }

    u64 MemoryRead64(u32 vaddr) override {
        return static_cast<u64>(MemoryRead32(vaddr)) |
               (static_cast<u64>(MemoryRead32(vaddr + 4)) << 32);
    }

    void MemoryWrite8(u32 vaddr, u8 value) override {
        MemoryRegion* region = FindMutable(vaddr, 1);
        if (!region) {
            invalid_access = true;
            ticks_left = 0;
            return;
        }
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

    void InterpreterFallback(u32, std::size_t) override {
        interpreter_fallback = true;
        ticks_left = 0;
    }

    void CallSVC(u32 swi) override {
        last_svc = swi;
        ticks_left = 0;
    }

    void ExceptionRaised(u32, Dynarmic::A32::Exception) override {
        exception_seen = true;
        ticks_left = 0;
    }

    void AddTicks(u64 ticks) override {
        ticks_left = ticks > ticks_left ? 0 : ticks_left - ticks;
    }

    u64 GetTicksRemaining() override {
        return ticks_left;
    }

private:
    const MemoryRegion* Find(u32 address, std::size_t size) const {
        const u64 end = static_cast<u64>(address) + size;
        for (const auto& region : regions_) {
            const u64 region_end = static_cast<u64>(region.base) + region.data.size();
            if (address >= region.base && end <= region_end) {
                return &region;
            }
        }
        return nullptr;
    }

    MemoryRegion* FindMutable(u32 address, std::size_t size) {
        const u64 end = static_cast<u64>(address) + size;
        for (auto& region : regions_) {
            const u64 region_end = static_cast<u64>(region.base) + region.data.size();
            if (address >= region.base && end <= region_end) {
                return &region;
            }
        }
        return nullptr;
    }

    std::vector<MemoryRegion> regions_;
};

struct ElfReport {
    u32 image_min = 0;
    u32 image_max = 0;
    u32 entry = 0;
    std::size_t load_segments = 0;
    std::size_t executable_segments = 0;
    std::size_t init_array_count = 0;
    std::size_t dynsym_count = 0;
    std::size_t undefined_symbols = 0;
    std::size_t relocation_count = 0;
    std::size_t relative_relocations = 0;
    std::size_t imported_relocations = 0;
    u32 jni_onload = 0;
    u32 native_init = 0;
    u32 native_render = 0;
};

static std::size_t BoundedStringLength(const char* text, std::size_t maximum) {
    std::size_t length = 0;
    while (length < maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

static std::string SectionName(const std::vector<u8>& elf, const Elf32Shdr& shstr, u32 offset) {
    if (offset >= shstr.size || static_cast<u64>(shstr.offset) + offset >= elf.size()) {
        return {};
    }
    const char* begin = reinterpret_cast<const char*>(elf.data() + shstr.offset + offset);
    const std::size_t max_length = std::min<std::size_t>(shstr.size - offset, elf.size() - shstr.offset - offset);
    const std::size_t length = BoundedStringLength(begin, max_length);
    return std::string(begin, length);
}

static std::string StringFromTable(const std::vector<u8>& elf, const Elf32Shdr& strings, u32 offset) {
    if (offset >= strings.size || static_cast<u64>(strings.offset) + offset >= elf.size()) {
        return {};
    }
    const char* begin = reinterpret_cast<const char*>(elf.data() + strings.offset + offset);
    const std::size_t max_length = std::min<std::size_t>(strings.size - offset, elf.size() - strings.offset - offset);
    return std::string(begin, BoundedStringLength(begin, max_length));
}

static ElfReport AnalyzeAndMapElf(const std::vector<u8>& elf, ProbeEnvironment& env) {
    const Elf32Ehdr header = ReadPod<Elf32Ehdr>(elf, 0);
    if (std::memcmp(header.ident, "\x7F" "ELF", 4) != 0 || header.ident[4] != 1 || header.ident[5] != 1) {
        throw std::runtime_error("libgame.so is not a little-endian ELF32 image");
    }
    if (header.type != kEtDyn || header.machine != kEmArm) {
        throw std::runtime_error("libgame.so is not an ARM ET_DYN shared object");
    }
    if (header.phentsize != sizeof(Elf32Phdr) || header.shentsize != sizeof(Elf32Shdr)) {
        throw std::runtime_error("unexpected ELF table entry sizes");
    }
    if (static_cast<u64>(header.phoff) + static_cast<u64>(header.phnum) * sizeof(Elf32Phdr) > elf.size() ||
        static_cast<u64>(header.shoff) + static_cast<u64>(header.shnum) * sizeof(Elf32Shdr) > elf.size()) {
        throw std::runtime_error("ELF program or section table is truncated");
    }

    ElfReport report{};
    report.entry = kGameBase + header.entry;
    u32 min_vaddr = std::numeric_limits<u32>::max();
    u32 max_vaddr = 0;
    std::vector<Elf32Phdr> phdrs;
    for (u16 i = 0; i < header.phnum; ++i) {
        const Elf32Phdr ph = ReadPod<Elf32Phdr>(elf, header.phoff + static_cast<std::size_t>(i) * sizeof(Elf32Phdr));
        phdrs.push_back(ph);
        if (ph.type != kPtLoad || ph.memsz == 0) {
            continue;
        }
        if (static_cast<u64>(ph.offset) + ph.filesz > elf.size() || ph.filesz > ph.memsz) {
            throw std::runtime_error("invalid PT_LOAD segment");
        }
        ++report.load_segments;
        if (ph.flags & 1u) {
            ++report.executable_segments;
        }
        min_vaddr = std::min(min_vaddr, AlignDown(ph.vaddr, kPageSize));
        max_vaddr = std::max(max_vaddr, AlignUp(ph.vaddr + ph.memsz, kPageSize));
    }
    if (report.load_segments == 0 || max_vaddr <= min_vaddr) {
        throw std::runtime_error("ELF has no loadable image");
    }
    report.image_min = kGameBase + min_vaddr;
    report.image_max = kGameBase + max_vaddr;
    env.Map(report.image_min, static_cast<std::size_t>(report.image_max - report.image_min), true);
    for (const Elf32Phdr& ph : phdrs) {
        if (ph.type == kPtLoad && ph.filesz != 0) {
            env.CopyIn(kGameBase + ph.vaddr, elf.data() + ph.offset, ph.filesz);
        }
    }

    std::vector<Elf32Shdr> sections;
    sections.reserve(header.shnum);
    for (u16 i = 0; i < header.shnum; ++i) {
        sections.push_back(ReadPod<Elf32Shdr>(elf, header.shoff + static_cast<std::size_t>(i) * sizeof(Elf32Shdr)));
    }
    if (header.shstrndx >= sections.size()) {
        throw std::runtime_error("invalid ELF section-name table index");
    }
    const Elf32Shdr& shstr = sections[header.shstrndx];

    for (std::size_t index = 0; index < sections.size(); ++index) {
        const Elf32Shdr& section = sections[index];
        const std::string name = SectionName(elf, shstr, section.name);
        if ((section.type == kShtInitArray || name == ".init_array") && section.entsize != 0) {
            report.init_array_count = section.size / section.entsize;
        } else if ((section.type == kShtInitArray || name == ".init_array") && section.size % 4 == 0) {
            report.init_array_count = section.size / 4;
        }

        if (section.type == kShtDynsym) {
            if (section.entsize != sizeof(Elf32Sym) || section.link >= sections.size()) {
                throw std::runtime_error("invalid .dynsym metadata");
            }
            const Elf32Shdr& strings = sections[section.link];
            report.dynsym_count = section.size / sizeof(Elf32Sym);
            for (std::size_t symbol_index = 0; symbol_index < report.dynsym_count; ++symbol_index) {
                const Elf32Sym symbol = ReadPod<Elf32Sym>(elf, section.offset + symbol_index * sizeof(Elf32Sym));
                if (symbol.shndx == kShnUndef && symbol.name != 0) {
                    ++report.undefined_symbols;
                }
                if (symbol.shndx == kShnUndef || symbol.value == 0) {
                    continue;
                }
                const std::string symbol_name = StringFromTable(elf, strings, symbol.name);
                const u32 address = kGameBase + symbol.value;
                if (symbol_name == "JNI_OnLoad") {
                    report.jni_onload = address;
                } else if (symbol_name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit") {
                    report.native_init = address;
                } else if (symbol_name == "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender") {
                    report.native_render = address;
                }
            }
        }

        if (section.type == kShtRel) {
            if (section.entsize != sizeof(Elf32Rel)) {
                throw std::runtime_error("invalid ELF REL entry size");
            }
            const std::size_t count = section.size / sizeof(Elf32Rel);
            report.relocation_count += count;
            for (std::size_t rel_index = 0; rel_index < count; ++rel_index) {
                const Elf32Rel rel = ReadPod<Elf32Rel>(elf, section.offset + rel_index * sizeof(Elf32Rel));
                const u32 type = rel.info & 0xFFu;
                if (type == kRArmRelative) {
                    ++report.relative_relocations;
                } else if (type == kRArmAbs32 || type == kRArmGlobDat || type == kRArmJumpSlot) {
                    ++report.imported_relocations;
                }
            }
        }
    }

    if (report.jni_onload == 0 || report.native_init == 0 || report.native_render == 0) {
        throw std::runtime_error("required JNI exports were not found in libgame.so");
    }
    return report;
}

static void RunThumbSmoke(ProbeEnvironment& env) {
    env.Map(kSmokeBase, kPageSize, true);
    env.MemoryWrite16(kSmokeBase + 0, 0x0088u); // lsls r0, r1, #2
    env.MemoryWrite16(kSmokeBase + 2, 0xE7FEu); // b .

    Dynarmic::A32::UserConfig config;
    config.callbacks = &env;
    config.arch_version = Dynarmic::A32::ArchVersion::v5TE;
    Dynarmic::A32::Jit cpu{config};
    cpu.Regs().fill(0);
    cpu.Regs()[0] = 1;
    cpu.Regs()[1] = 2;
    cpu.Regs()[15] = kSmokeBase;
    cpu.SetCpsr(0x00000030u);
    env.ticks_left = 1;
    cpu.Run();
    if (env.invalid_access || env.interpreter_fallback || env.exception_seen || cpu.Regs()[0] != 8) {
        std::ostringstream message;
        message << "Dynarmic Thumb smoke failed: r0=" << cpu.Regs()[0]
                << " invalid=" << env.invalid_access
                << " fallback=" << env.interpreter_fallback
                << " exception=" << env.exception_seen;
        throw std::runtime_error(message.str());
    }
}

} // namespace

int main(int argc, char** argv) {
    std::ofstream log("gd-dynarmic-probe.log", std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::cout << line << '\n';
        log << line << '\n';
        log.flush();
    };

    try {
        emit("Geometry Dash ARM wrapper 0.9.4-arm-dynarmictest1");
        emit("Milestone: x64 Dynarmic backend and real libgame.so bring-up probe");
        emit("Host pointer bits: " + std::to_string(sizeof(void*) * 8));
        if (sizeof(void*) != 8) {
            throw std::runtime_error("DynarmicTest1 must be compiled as a 64-bit executable");
        }

        ProbeEnvironment env;
        RunThumbSmoke(env);
        emit("RESULT: DYNARMIC_X64_THUMB_SMOKE_OK r0=8 guest=v5TE host=x86_64");

        const std::string apk_path = argc >= 2 ? argv[1] : "game.apk";
        emit("Input APK: " + apk_path);
        const std::vector<u8> apk = ReadFile(apk_path);
        emit("APK bytes: " + std::to_string(apk.size()));
        const std::vector<u8> libgame = ExtractZipMember(apk, "lib/armeabi/libgame.so");
        emit("Extracted lib/armeabi/libgame.so: " + std::to_string(libgame.size()) + " bytes");

        const ElfReport report = AnalyzeAndMapElf(libgame, env);
        emit("ELF32 ARM image mapped into Dynarmic callbacks");
        {
            std::ostringstream line;
            line << "Image: 0x" << std::hex << report.image_min << "-0x" << report.image_max
                 << " entry=0x" << report.entry << std::dec;
            emit(line.str());
        }
        emit("PT_LOAD segments: " + std::to_string(report.load_segments) +
             " executable=" + std::to_string(report.executable_segments));
        emit("Authentic ARM constructors: " + std::to_string(report.init_array_count));
        emit("Dynamic symbols: " + std::to_string(report.dynsym_count) +
             " undefined-import symbols=" + std::to_string(report.undefined_symbols));
        emit("Relocations: total=" + std::to_string(report.relocation_count) +
             " relative=" + std::to_string(report.relative_relocations) +
             " imported=" + std::to_string(report.imported_relocations));
        {
            std::ostringstream line;
            line << "Exports: JNI_OnLoad=0x" << std::hex << report.jni_onload
                 << " nativeInit=0x" << report.native_init
                 << " nativeRender=0x" << report.native_render << std::dec;
            emit(line.str());
        }

        if (report.init_array_count != 238) {
            throw std::runtime_error("expected 238 constructors for this Geometry Dash 1.4 libgame.so");
        }
        emit("RESULT: DYNARMIC_APK_ELF_OK constructors=238 exports=ready memory=ready");
        emit("NEXT: implement relocations/import traps, then execute constructors and JNI_OnLoad");
        emit("RESULT: DYNARMIC_BRINGUP1_OK");
        return 0;
    } catch (const std::exception& exception) {
        emit(std::string("ERROR: ") + exception.what());
        emit("RESULT: DYNARMIC_BRINGUP1_FAILED");
        return 1;
    }
}

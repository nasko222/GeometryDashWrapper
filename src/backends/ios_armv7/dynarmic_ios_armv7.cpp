#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
#include "dynarmic/interface/exclusive_monitor.h"

extern "C" {
#include "zlib.h"
#include "build_info.h"
}

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

namespace {

template <typename ArchVersion>
constexpr ArchVersion DynarmicArmv7ArchVersion() {
    if constexpr (requires { ArchVersion::v7A; }) return ArchVersion::v7A;
    else if constexpr (requires { ArchVersion::v7; }) return ArchVersion::v7;
    else static_assert(!sizeof(ArchVersion), "Dynarmic ARMv7 enum not recognized");
}

constexpr u32 kPageSize = 0x1000u;
constexpr u32 kHeapBase = 0x30000000u;
constexpr u32 kHeapSize = 0x08000000u;
constexpr u32 kImportBase = 0x50000000u;
constexpr u32 kImportSize = 0x00100000u;
constexpr u32 kControlBase = 0x51000000u;
constexpr u32 kObjectBase = 0x52000000u;
constexpr u32 kObjectSize = 0x00400000u;
constexpr u32 kStackBase = 0x70000000u;
constexpr u32 kStackSize = 0x01000000u;
constexpr u32 kSvcReturn = 0x00fffffeu;
constexpr u64 kRunChunk = 5000000u;
constexpr u64 kRunBudget = 150000000u;

static u16 ReadLe16(const u8* p) { return static_cast<u16>(p[0] | (u16(p[1]) << 8)); }
static u32 ReadLe32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
static u64 ReadLe64(const u8* p) { return u64(ReadLe32(p)) | (u64(ReadLe32(p + 4)) << 32); }
static u32 ReadBe32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}
static bool RangeFits(std::size_t total, std::size_t off, std::size_t size) {
    return off <= total && size <= total - off;
}
static u32 AlignUp(u32 value, u32 alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}
static std::vector<u8> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("could not open " + path);
    f.seekg(0, std::ios::end);
    const auto length = f.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("invalid file size");
    f.seekg(0, std::ios::beg);
    std::vector<u8> out(static_cast<std::size_t>(length));
    if (!out.empty() && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size())))
        throw std::runtime_error("could not read " + path);
    return out;
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
    if (result != Z_STREAM_END || stream.total_out != output_size)
        throw std::runtime_error("deflate member did not produce expected size");
    return output;
}

struct ZipEntry {
    std::string name;
    u16 method = 0;
    u32 crc = 0;
    u32 compressed_size = 0;
    u32 uncompressed_size = 0;
    u32 local_offset = 0;
};

static std::vector<ZipEntry> ListZip(const std::vector<u8>& zip) {
    constexpr u32 eocd_sig = 0x06054b50u;
    constexpr u32 central_sig = 0x02014b50u;
    if (zip.size() < 22u) throw std::runtime_error("IPA is too small");
    const std::size_t min_pos = zip.size() > 0xffffu + 22u ? zip.size() - (0xffffu + 22u) : 0u;
    std::optional<std::size_t> eocd;
    for (std::size_t pos = zip.size() - 22u;; --pos) {
        if (ReadLe32(zip.data() + pos) == eocd_sig) { eocd = pos; break; }
        if (pos == min_pos) break;
    }
    if (!eocd) throw std::runtime_error("IPA ZIP central directory not found");
    const u16 count = ReadLe16(zip.data() + *eocd + 10u);
    std::size_t pos = ReadLe32(zip.data() + *eocd + 16u);
    std::vector<ZipEntry> out;
    out.reserve(count);
    for (u16 i = 0; i < count; ++i) {
        if (!RangeFits(zip.size(), pos, 46u) || ReadLe32(zip.data() + pos) != central_sig)
            throw std::runtime_error("invalid IPA ZIP central entry");
        const u16 name_len = ReadLe16(zip.data() + pos + 28u);
        const u16 extra_len = ReadLe16(zip.data() + pos + 30u);
        const u16 comment_len = ReadLe16(zip.data() + pos + 32u);
        const std::size_t next = pos + 46ull + name_len + extra_len + comment_len;
        if (next > zip.size()) throw std::runtime_error("truncated IPA ZIP central entry");
        ZipEntry e;
        e.method = ReadLe16(zip.data() + pos + 10u);
        e.crc = ReadLe32(zip.data() + pos + 16u);
        e.compressed_size = ReadLe32(zip.data() + pos + 20u);
        e.uncompressed_size = ReadLe32(zip.data() + pos + 24u);
        e.local_offset = ReadLe32(zip.data() + pos + 42u);
        e.name.assign(reinterpret_cast<const char*>(zip.data() + pos + 46u), name_len);
        std::replace(e.name.begin(), e.name.end(), '\\', '/');
        out.push_back(std::move(e));
        pos = next;
    }
    return out;
}

static std::vector<u8> ExtractZip(const std::vector<u8>& zip, const ZipEntry& e) {
    constexpr u32 local_sig = 0x04034b50u;
    if (!RangeFits(zip.size(), e.local_offset, 30u) || ReadLe32(zip.data() + e.local_offset) != local_sig)
        throw std::runtime_error("invalid IPA ZIP local header for " + e.name);
    const u16 name_len = ReadLe16(zip.data() + e.local_offset + 26u);
    const u16 extra_len = ReadLe16(zip.data() + e.local_offset + 28u);
    const std::size_t data_off = static_cast<std::size_t>(e.local_offset) + 30u + name_len + extra_len;
    if (!RangeFits(zip.size(), data_off, e.compressed_size)) throw std::runtime_error("truncated IPA member " + e.name);
    std::vector<u8> out;
    if (e.method == 0u) {
        out.assign(zip.begin() + static_cast<std::ptrdiff_t>(data_off),
                   zip.begin() + static_cast<std::ptrdiff_t>(data_off + e.compressed_size));
    } else if (e.method == 8u) {
        out = InflateRaw(zip.data() + data_off, e.compressed_size, e.uncompressed_size);
    } else {
        throw std::runtime_error("unsupported IPA compression method for " + e.name);
    }
    if (out.size() != e.uncompressed_size) throw std::runtime_error("IPA member size mismatch for " + e.name);
    const u32 actual = static_cast<u32>(crc32(0, reinterpret_cast<const Bytef*>(out.data()), static_cast<uInt>(out.size())));
    if (actual != e.crc) throw std::runtime_error("IPA member CRC mismatch for " + e.name);
    return out;
}

static bool IsMachOMagic(const std::vector<u8>& bytes) {
    if (bytes.size() < 4u) return false;
    const u32 le = ReadLe32(bytes.data());
    const u32 be = ReadBe32(bytes.data());
    return le == 0xfeedfaceu || le == 0xfeedfacfu || be == 0xcafebabeu || be == 0xcafebabfu;
}

struct IpaExecutable {
    std::string member;
    std::vector<u8> bytes;
};

static IpaExecutable FindAppExecutable(const std::vector<u8>& ipa) {
    const auto entries = ListZip(ipa);
    for (const auto& e : entries) {
        if (!e.name.starts_with("Payload/")) continue;
        const auto app = e.name.find(".app/");
        if (app == std::string::npos) continue;
        const std::size_t remainder = app + 5u;
        if (remainder >= e.name.size() || e.name.find('/', remainder) != std::string::npos) continue;
        if (e.uncompressed_size < 4096u || e.uncompressed_size > 128u * 1024u * 1024u) continue;
        std::vector<u8> candidate;
        try { candidate = ExtractZip(ipa, e); } catch (...) { continue; }
        if (IsMachOMagic(candidate)) return IpaExecutable{e.name, std::move(candidate)};
    }
    throw std::runtime_error("could not locate the top-level Mach-O executable in Payload/*.app");
}

constexpr u32 MH_MAGIC = 0xfeedfaceu;
constexpr u32 FAT_MAGIC = 0xcafebabeu;
constexpr u32 CPU_TYPE_ARM = 12u;
constexpr u32 CPU_SUBTYPE_ARM_V7 = 9u;
constexpr u32 LC_SEGMENT = 0x1u;
constexpr u32 LC_SYMTAB = 0x2u;
constexpr u32 LC_UNIXTHREAD = 0x5u;
constexpr u32 LC_DYSYMTAB = 0xbu;
constexpr u32 LC_DYLD_INFO_ONLY = 0x80000022u;
constexpr u32 LC_DYLD_INFO = 0x22u;
constexpr u32 LC_ENCRYPTION_INFO = 0x21u;

struct MachSection {
    std::string segname;
    std::string sectname;
    u32 addr = 0;
    u32 size = 0;
    u32 offset = 0;
    u32 flags = 0;
    u32 reserved1 = 0;
    u32 reserved2 = 0;
};
struct MachSegment {
    std::string name;
    u32 vmaddr = 0;
    u32 vmsize = 0;
    u32 fileoff = 0;
    u32 filesize = 0;
    u32 initprot = 0;
    std::vector<MachSection> sections;
};
struct DyldInfo {
    u32 rebase_off = 0, rebase_size = 0;
    u32 bind_off = 0, bind_size = 0;
    u32 weak_bind_off = 0, weak_bind_size = 0;
    u32 lazy_bind_off = 0, lazy_bind_size = 0;
};
struct MachImage {
    std::vector<u8> bytes;
    std::string arch;
    u32 entry = 0;
    bool encrypted = false;
    std::vector<MachSegment> segments;
    DyldInfo dyld;
    u32 constructor_count = 0;
};

static std::string FixedName(const u8* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len]) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

static MachImage SelectAndParseArmv7(const std::vector<u8>& full) {
    std::size_t slice_off = 0;
    std::size_t slice_size = full.size();
    std::string arch = "armv7";
    if (full.size() >= 8u && ReadBe32(full.data()) == FAT_MAGIC) {
        const u32 count = ReadBe32(full.data() + 4u);
        if (count > 64u || !RangeFits(full.size(), 8u, static_cast<std::size_t>(count) * 20u))
            throw std::runtime_error("invalid fat Mach-O architecture table");
        bool found = false;
        for (u32 i = 0; i < count; ++i) {
            const u8* a = full.data() + 8u + i * 20u;
            const u32 cpu = ReadBe32(a);
            const u32 sub = ReadBe32(a + 4u) & 0x00ffffffu;
            const u32 off = ReadBe32(a + 8u);
            const u32 size = ReadBe32(a + 12u);
            if (cpu == CPU_TYPE_ARM && sub == CPU_SUBTYPE_ARM_V7 && RangeFits(full.size(), off, size)) {
                slice_off = off; slice_size = size; arch = "armv7"; found = true; break;
            }
        }
        if (!found) {
            for (u32 i = 0; i < count; ++i) {
                const u8* a = full.data() + 8u + i * 20u;
                const u32 cpu = ReadBe32(a);
                const u32 off = ReadBe32(a + 8u);
                const u32 size = ReadBe32(a + 12u);
                if (cpu == CPU_TYPE_ARM && RangeFits(full.size(), off, size)) {
                    slice_off = off; slice_size = size; arch = "arm32"; found = true; break;
                }
            }
        }
        if (!found) throw std::runtime_error("IPA has no 32-bit ARM Mach-O slice");
    }
    if (!RangeFits(full.size(), slice_off, slice_size)) throw std::runtime_error("Mach-O slice out of range");
    MachImage image;
    image.bytes.assign(full.begin() + static_cast<std::ptrdiff_t>(slice_off),
                       full.begin() + static_cast<std::ptrdiff_t>(slice_off + slice_size));
    image.arch = arch;
    if (image.bytes.size() < 28u || ReadLe32(image.bytes.data()) != MH_MAGIC)
        throw std::runtime_error("selected iOS slice is not a little-endian 32-bit Mach-O");
    const u32 cpu = ReadLe32(image.bytes.data() + 4u);
    if (cpu != CPU_TYPE_ARM) throw std::runtime_error("selected Mach-O is not ARM32");
    const u32 ncmds = ReadLe32(image.bytes.data() + 16u);
    const u32 sizeofcmds = ReadLe32(image.bytes.data() + 20u);
    if (ncmds > 4096u || !RangeFits(image.bytes.size(), 28u, sizeofcmds)) throw std::runtime_error("invalid Mach-O load commands");
    std::size_t off = 28u;
    for (u32 ci = 0; ci < ncmds; ++ci) {
        if (!RangeFits(image.bytes.size(), off, 8u)) throw std::runtime_error("truncated Mach-O command");
        const u32 cmd = ReadLe32(image.bytes.data() + off);
        const u32 cmdsize = ReadLe32(image.bytes.data() + off + 4u);
        if (cmdsize < 8u || !RangeFits(image.bytes.size(), off, cmdsize)) throw std::runtime_error("invalid Mach-O command size");
        if (cmd == LC_SEGMENT) {
            if (cmdsize < 56u) throw std::runtime_error("short LC_SEGMENT");
            MachSegment seg;
            seg.name = FixedName(image.bytes.data() + off + 8u, 16u);
            seg.vmaddr = ReadLe32(image.bytes.data() + off + 24u);
            seg.vmsize = ReadLe32(image.bytes.data() + off + 28u);
            seg.fileoff = ReadLe32(image.bytes.data() + off + 32u);
            seg.filesize = ReadLe32(image.bytes.data() + off + 36u);
            seg.initprot = ReadLe32(image.bytes.data() + off + 44u);
            const u32 nsects = ReadLe32(image.bytes.data() + off + 48u);
            if (56ull + static_cast<u64>(nsects) * 68ull > cmdsize) throw std::runtime_error("LC_SEGMENT sections exceed command");
            for (u32 si = 0; si < nsects; ++si) {
                const std::size_t so = off + 56u + static_cast<std::size_t>(si) * 68u;
                MachSection s;
                s.sectname = FixedName(image.bytes.data() + so, 16u);
                s.segname = FixedName(image.bytes.data() + so + 16u, 16u);
                s.addr = ReadLe32(image.bytes.data() + so + 32u);
                s.size = ReadLe32(image.bytes.data() + so + 36u);
                s.offset = ReadLe32(image.bytes.data() + so + 40u);
                s.flags = ReadLe32(image.bytes.data() + so + 56u);
                s.reserved1 = ReadLe32(image.bytes.data() + so + 60u);
                s.reserved2 = ReadLe32(image.bytes.data() + so + 64u);
                if (s.sectname == "__mod_init_func") image.constructor_count = s.size / 4u;
                seg.sections.push_back(std::move(s));
            }
            image.segments.push_back(std::move(seg));
        } else if (cmd == LC_UNIXTHREAD) {
            if (cmdsize >= 8u + 8u + 17u * 4u) {
                const u32 flavor = ReadLe32(image.bytes.data() + off + 8u);
                const u32 count = ReadLe32(image.bytes.data() + off + 12u);
                if (flavor == 1u && count >= 17u) image.entry = ReadLe32(image.bytes.data() + off + 16u + 15u * 4u);
            }
        } else if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) {
            if (cmdsize >= 48u) {
                image.dyld.rebase_off = ReadLe32(image.bytes.data() + off + 8u);
                image.dyld.rebase_size = ReadLe32(image.bytes.data() + off + 12u);
                image.dyld.bind_off = ReadLe32(image.bytes.data() + off + 16u);
                image.dyld.bind_size = ReadLe32(image.bytes.data() + off + 20u);
                image.dyld.weak_bind_off = ReadLe32(image.bytes.data() + off + 24u);
                image.dyld.weak_bind_size = ReadLe32(image.bytes.data() + off + 28u);
                image.dyld.lazy_bind_off = ReadLe32(image.bytes.data() + off + 32u);
                image.dyld.lazy_bind_size = ReadLe32(image.bytes.data() + off + 36u);
            }
        } else if (cmd == LC_ENCRYPTION_INFO) {
            if (cmdsize >= 20u) image.encrypted = ReadLe32(image.bytes.data() + off + 16u) != 0u;
        }
        off += cmdsize;
    }
    if (!image.entry) throw std::runtime_error("LC_UNIXTHREAD ARM entry PC was not found");
    return image;
}

struct MemoryRegion { u32 base = 0; std::vector<u8> data; bool executable = false; };
class ProbeEnvironment final : public Dynarmic::A32::UserCallbacks {
public:
    ProbeEnvironment() : page_regions_(kGuestPageCount, kUnmappedPage) {}
    u64 ticks_left = 0;
    bool invalid_access = false, interpreter_fallback = false, exception_seen = false, svc_pending = false;
    u32 pending_svc = 0, fault_address = 0, fallback_pc = 0, exception_pc = 0;
    std::size_t fallback_count = 0;
    void Map(u32 base, std::size_t size, bool executable) {
        if (!size || size > std::numeric_limits<u32>::max()) throw std::runtime_error("invalid guest mapping");
        const u64 end = static_cast<u64>(base) + size;
        if (end > 0x100000000ull) throw std::runtime_error("guest mapping exceeds 32-bit space");
        for (const auto& r : regions_) {
            const u64 re = static_cast<u64>(r.base) + r.data.size();
            if (!(end <= r.base || base >= re)) throw std::runtime_error("overlapping guest mapping");
        }
        regions_.push_back({base, std::vector<u8>(size), executable});
        std::sort(regions_.begin(), regions_.end(), [](const auto& a, const auto& b){ return a.base < b.base; });
        RebuildPageLookup();
    }
    void AttachCpu(Dynarmic::A32::Jit* cpu) { cpu_ = cpu; }
    bool IsMapped(u32 address, std::size_t size = 1u) const { return Find(address, size) != nullptr; }
    bool ReadBytes(u32 address, void* output, std::size_t size) const {
        const MemoryRegion* r = Find(address, size);
        if (!r) return false;
        if (size) {
            std::memcpy(output, r->data.data() + (address - r->base), size);
        }
        return true;
    }
    bool WriteBytes(u32 address, const void* source, std::size_t size) {
        MemoryRegion* r = FindMutable(address, size);
        if (!r) return false;
        if (size) {
            std::memcpy(r->data.data() + (address - r->base), source, size);
        }
        return true;
    }
    bool ReadCString(u32 address, std::string& output, std::size_t max = 65536u) const {
        output.clear();
        for (std::size_t i = 0; i < max; ++i) {
            const MemoryRegion* r = Find(address + static_cast<u32>(i), 1u);
            if (!r) return false;
            const u8 c = r->data[address + static_cast<u32>(i) - r->base];
            if (!c) return true;
            output.push_back(static_cast<char>(c));
        }
        return false;
    }
    void ResetStopState() { invalid_access=false; interpreter_fallback=false; exception_seen=false; svc_pending=false; pending_svc=0; fault_address=0; fallback_pc=0; fallback_count=0; exception_pc=0; }
    u8 MemoryRead8(u32 a) override { auto* r=Find(a,1); if(!r)return ReadFault<u8>(a); return r->data[a-r->base]; }
    u16 MemoryRead16(u32 a) override { return ReadTyped<u16>(a); }
    u32 MemoryRead32(u32 a) override { return ReadTyped<u32>(a); }
    u64 MemoryRead64(u32 a) override { return ReadTyped<u64>(a); }
    void MemoryWrite8(u32 a,u8 v) override { auto* r=FindMutable(a,1); if(!r){WriteFault(a);return;} r->data[a-r->base]=v; }
    void MemoryWrite16(u32 a,u16 v) override { WriteTyped(a,v); }
    void MemoryWrite32(u32 a,u32 v) override { WriteTyped(a,v); }
    void MemoryWrite64(u32 a,u64 v) override { WriteTyped(a,v); }
    bool MemoryWriteExclusive8(u32 a,u8 v,u8 e) override { return CompareExchange(a,v,e); }
    bool MemoryWriteExclusive16(u32 a,u16 v,u16 e) override { return CompareExchange(a,v,e); }
    bool MemoryWriteExclusive32(u32 a,u32 v,u32 e) override { return CompareExchange(a,v,e); }
    bool MemoryWriteExclusive64(u32 a,u64 v,u64 e) override { return CompareExchange(a,v,e); }
    void InterpreterFallback(u32 pc,std::size_t count) override { interpreter_fallback=true;fallback_pc=pc;fallback_count=count;RequestHalt(); }
    void CallSVC(u32 swi) override { svc_pending=true;pending_svc=swi;RequestHalt(); }
    void ExceptionRaised(u32 pc,Dynarmic::A32::Exception) override { exception_seen=true;exception_pc=pc;RequestHalt(); }
    void AddTicks(u64 t) override { ticks_left = t > ticks_left ? 0 : ticks_left - t; }
    u64 GetTicksRemaining() override { return ticks_left; }
private:
    static constexpr u32 kShift=12u; static constexpr std::size_t kGuestPageCount=std::size_t{1}<<(32u-kShift); static constexpr std::int16_t kUnmappedPage=-1;
    void RequestHalt(){ if(cpu_) cpu_->HaltExecution(Dynarmic::HaltReason::UserDefined1); }
    void RebuildPageLookup(){ std::fill(page_regions_.begin(),page_regions_.end(),kUnmappedPage); if(regions_.size()>32760u)throw std::runtime_error("too many guest regions"); for(std::size_t i=0;i<regions_.size();++i){const auto& r=regions_[i];u64 b=u64(r.base)>>kShift;u64 e=(u64(r.base)+r.data.size()-1u)>>kShift;for(u64 p=b;p<=e;++p)page_regions_[static_cast<std::size_t>(p)]=static_cast<std::int16_t>(i);} }
    const MemoryRegion* FindContaining(u32 a) const { const auto i=page_regions_[a>>kShift]; if(i<0)return nullptr;const auto& r=regions_[static_cast<std::size_t>(i)];return u64(a)<u64(r.base)+r.data.size()&&a>=r.base?&r:nullptr; }
    MemoryRegion* FindContainingMutable(u32 a){ const auto i=page_regions_[a>>kShift];if(i<0)return nullptr;auto& r=regions_[static_cast<std::size_t>(i)];return u64(a)<u64(r.base)+r.data.size()&&a>=r.base?&r:nullptr; }
    const MemoryRegion* Find(u32 a,std::size_t s) const { const u64 e=u64(a)+s;if(e>0x100000000ull)return nullptr;const auto* r=FindContaining(a);return r&&e<=u64(r->base)+r->data.size()?r:nullptr; }
    MemoryRegion* FindMutable(u32 a,std::size_t s){ const u64 e=u64(a)+s;if(e>0x100000000ull)return nullptr;auto* r=FindContainingMutable(a);return r&&e<=u64(r->base)+r->data.size()?r:nullptr; }
    template<class T>T ReadFault(u32 a){invalid_access=true;fault_address=a;RequestHalt();return T{};}
    void WriteFault(u32 a){invalid_access=true;fault_address=a;RequestHalt();}
    template<class T>T ReadTyped(u32 a){const auto* r=Find(a,sizeof(T));if(!r)return ReadFault<T>(a);T v{};std::memcpy(&v,r->data.data()+(a-r->base),sizeof(v));return v;}
    template<class T>void WriteTyped(u32 a,T v){auto* r=FindMutable(a,sizeof(T));if(!r){WriteFault(a);return;}std::memcpy(r->data.data()+(a-r->base),&v,sizeof(v));}
    template<class T>bool CompareExchange(u32 a,T v,T e){auto* r=FindMutable(a,sizeof(T));if(!r){WriteFault(a);return false;}T c{};u8* p=r->data.data()+(a-r->base);std::memcpy(&c,p,sizeof(c));if(c!=e)return false;std::memcpy(p,&v,sizeof(v));return true;}
    std::vector<MemoryRegion> regions_; std::vector<std::int16_t> page_regions_; Dynarmic::A32::Jit* cpu_=nullptr;
};

struct Import { std::string name; u32 stub=0; u32 svc=0; u64 calls=0; };
struct FakeObject { std::string class_name; bool is_class=false; bool is_meta=false; std::string string_value; };
struct GuestMethod { std::string selector; u32 selector_addr=0; u32 imp=0; };
struct GuestClass { std::string name; u32 class_addr=0; u32 meta_addr=0; u32 superclass_addr=0; u32 instance_size=0; std::vector<GuestMethod> instance_methods; std::vector<GuestMethod> class_methods; };

class Logger {
public:
    explicit Logger(const std::string& path) {
        if (!path.empty()) file_.open(path, std::ios::out | std::ios::trunc);
    }
    template<class T> Logger& operator<<(const T& v){ std::cout<<v; if(file_)file_<<v; return *this; }
    Logger& operator<<(std::ostream&(*m)(std::ostream&)){ m(std::cout); if(file_)m(file_); return *this; }
    void Flush(){ std::cout.flush(); if(file_)file_.flush(); }
private: std::ofstream file_;
};

class IosBootstrap {
public:
    IosBootstrap(MachImage image, Logger& log)
        : image_(std::move(image)), log_(log), monitor_(1), cpu_(MakeConfig()) {
        env_.AttachCpu(&cpu_);
    }

    bool Prepare() {
        if (image_.encrypted) { log_ << "RESULT: IOS_ENCRYPTED_EXECUTABLE_UNSUPPORTED\n"; return false; }
        for (const auto& seg : image_.segments) {
            if (seg.name == "__PAGEZERO" || !seg.vmsize) continue;
            const u32 map_size = AlignUp(seg.vmsize, kPageSize);
            env_.Map(seg.vmaddr, map_size, (seg.initprot & 4u) != 0u);
            if (seg.filesize) {
                if (!RangeFits(image_.bytes.size(), seg.fileoff, seg.filesize)) throw std::runtime_error("Mach-O segment outside file: " + seg.name);
                if (!env_.WriteBytes(seg.vmaddr, image_.bytes.data() + seg.fileoff, seg.filesize)) throw std::runtime_error("could not map Mach-O segment");
            }
            log_ << "IOS: mapped " << seg.name << " vm=0x" << Hex(seg.vmaddr) << " size=0x" << Hex(seg.vmsize) << "\n";
        }
        env_.Map(kHeapBase, kHeapSize, false);
        env_.Map(kImportBase, kImportSize, true);
        env_.Map(kControlBase, kPageSize, true);
        env_.Map(kObjectBase, kObjectSize, false);
        env_.Map(kStackBase, kStackSize, false);
        WriteArmSvc(kControlBase, kSvcReturn);
        BindStream(image_.dyld.bind_off, image_.dyld.bind_size, "bind");
        BindStream(image_.dyld.weak_bind_off, image_.dyld.weak_bind_size, "weak-bind");
        BindStream(image_.dyld.lazy_bind_off, image_.dyld.lazy_bind_size, "lazy-bind");
        ParseGuestClasses();
        log_ << "IOS: imports bound=" << imports_.size() << " guest-objc-classes=" << classes_.size()
             << " mod-init-functions=" << image_.constructor_count << "\n";
        ReportDelegate("AppController");
        ReportDelegate("AppDelegate");
        log_.Flush();
        return true;
    }

    bool Run() {
        BuildInitialStack();
        cpu_.Regs().fill(0);
        cpu_.ExtRegs().fill(0);
        cpu_.Regs()[13] = initial_sp_;
        cpu_.Regs()[15] = image_.entry & ~1u;
        cpu_.SetCpsr(0x10u | ((image_.entry & 1u) ? 0x20u : 0u));
        cpu_.SetFpscr(0u);
        log_ << "IOS: starting real ARMv7 Mach-O execution PC=0x" << Hex(image_.entry)
             << " SP=0x" << Hex(initial_sp_) << "\n";
        u64 budget = kRunBudget;
        while (budget && !done_) {
            env_.ResetStopState();
            const u64 chunk = std::min<u64>(budget, kRunChunk);
            env_.ticks_left = chunk;
            const auto reason = cpu_.Run();
            (void)reason;
            cpu_.ClearHalt(Dynarmic::HaltReason::UserDefined1);
            // PublicTest4 charged a whole 5M-tick chunk every time an import
            // SVC halted Dynarmic, even if only a handful of guest instructions
            // had executed. Preserve GetTicksRemaining() on a halt and charge
            // only the work actually consumed.
            const u64 remaining = std::min<u64>(env_.ticks_left, chunk);
            const u64 consumed = std::max<u64>(chunk - remaining, 1u);
            budget -= std::min<u64>(budget, consumed);
            if (env_.invalid_access) {
                log_ << (delegate_launch_active_?"RESULT: IOS_DELEGATE_MEMORY_FAULT address=0x":"RESULT: IOS_BOOTSTRAP_MEMORY_FAULT address=0x") << Hex(env_.fault_address)
                     << " pc=0x" << Hex(cpu_.Regs()[15]) << " lr=0x" << Hex(cpu_.Regs()[14]) << "\n";
                return false;
            }
            if (env_.interpreter_fallback) {
                log_ << (delegate_launch_active_?"RESULT: IOS_DELEGATE_INTERPRETER_FALLBACK pc=0x":"RESULT: IOS_BOOTSTRAP_INTERPRETER_FALLBACK pc=0x") << Hex(env_.fallback_pc)
                     << " count=" << env_.fallback_count << "\n";
                return false;
            }
            if (env_.exception_seen) {
                log_ << (delegate_launch_active_?"RESULT: IOS_DELEGATE_EXCEPTION pc=0x":"RESULT: IOS_BOOTSTRAP_EXCEPTION pc=0x") << Hex(env_.exception_pc) << "\n";
                return false;
            }
            if (env_.svc_pending) {
                if (!HandleSvc(env_.pending_svc)) return false;
                continue;
            }
        }
        if(delegate_launch_returned_){
            log_<<"RESULT: IOS_DELEGATE_LAUNCH_RETURNED delegate="<<(delegate_name_.empty()?"unknown":delegate_name_)<<" r0=0x"<<Hex(delegate_return_value_)<<" unknown-imports="<<unknown_import_count_<<" objc-stubs="<<unimplemented_objc_count_<<"\n";
            log_<<"Execution status: PublicTest4 executed the real iOS application delegate launch method; rendering/event-loop work is next.\n";
            return true;
        }
        if(delegate_launch_active_ && budget==0u){
            log_<<"RESULT: IOS_DELEGATE_TICK_BUDGET_EXHAUSTED pc=0x"<<Hex(cpu_.Regs()[15])
                <<" lr=0x"<<Hex(cpu_.Regs()[14])
                <<" unknown-imports="<<unknown_import_count_
                <<" objc-stubs="<<unimplemented_objc_count_
                <<" testflight-bypasses="<<testflight_bypass_count_<<"\n";
            return false;
        }
        if(reached_ui_application_main_){
            log_<<"RESULT: IOS_BOOTSTRAP_REACHED_UIAPPLICATIONMAIN delegate="<<(delegate_name_.empty()?"unknown":delegate_name_);
            if(delegate_launch_deferred_)log_<<" delegate-launch=deferred constructors="<<image_.constructor_count;
            log_<<"\n";
            return true;
        }
        if (done_) {
            log_ << "RESULT: IOS_BOOTSTRAP_EXITED_BEFORE_UIAPPLICATIONMAIN\n";
            return false;
        }
        log_ << "RESULT: IOS_BOOTSTRAP_TICK_BUDGET_EXHAUSTED pc=0x" << Hex(cpu_.Regs()[15]) << "\n";
        return false;
    }

private:
    static std::string Hex(u64 v){ std::ostringstream s; s<<std::hex<<v; return s.str(); }
    Dynarmic::A32::UserConfig MakeConfig() {
        Dynarmic::A32::UserConfig c; c.callbacks=&env_; c.arch_version=DynarmicArmv7ArchVersion<Dynarmic::A32::ArchVersion>(); c.global_monitor=&monitor_; c.processor_id=0; c.check_halt_on_memory_access=true; return c;
    }
    void WriteArmSvc(u32 addr,u32 svc){ env_.MemoryWrite32(addr,0xef000000u|(svc&0x00ffffffu)); env_.MemoryWrite32(addr+4u,0xe12fff1eu); }
    u32 Allocate(u32 size,u32 align=8u){ heap_cursor_=AlignUp(heap_cursor_,align); if(u64(heap_cursor_)+size>u64(kHeapBase)+kHeapSize)throw std::runtime_error("iOS guest heap exhausted");u32 a=heap_cursor_;heap_cursor_+=AlignUp(std::max<u32>(size,1u),8u);return a; }
    u32 AllocateObjectBytes(u32 size){ object_cursor_=AlignUp(object_cursor_,8u); if(u64(object_cursor_)+size>u64(kObjectBase)+kObjectSize)throw std::runtime_error("iOS fake object region exhausted");u32 a=object_cursor_;object_cursor_+=AlignUp(size,8u);return a; }
    u32 AllocateCString(const std::string& s){ u32 a=Allocate(static_cast<u32>(s.size()+1u),1u); env_.WriteBytes(a,s.c_str(),s.size()+1u); return a; }

    u32 EnsureExternalClass(const std::string& name,bool meta=false){
        const std::string key=(meta?"meta:":"class:")+name;
        auto it=fake_named_.find(key); if(it!=fake_named_.end())return it->second;
        u32 class_addr=0,meta_addr=0;
        auto normal=fake_named_.find("class:"+name);
        auto metait=fake_named_.find("meta:"+name);
        if(normal!=fake_named_.end())class_addr=normal->second;
        if(metait!=fake_named_.end())meta_addr=metait->second;
        if(!class_addr)class_addr=AllocateObjectBytes(32u);
        if(!meta_addr)meta_addr=AllocateObjectBytes(32u);
        env_.MemoryWrite32(class_addr,meta_addr);
        env_.MemoryWrite32(meta_addr,meta_addr);
        fake_objects_[class_addr]=FakeObject{name,true,false,{}};
        fake_objects_[meta_addr]=FakeObject{name,true,true,{}};
        fake_named_["class:"+name]=class_addr; fake_named_["meta:"+name]=meta_addr;
        return meta?meta_addr:class_addr;
    }
    u32 NewExternalInstance(const std::string& cls){ const u32 a=Allocate(64u); const u32 c=EnsureExternalClass(cls); env_.MemoryWrite32(a,c); fake_objects_[a]=FakeObject{cls,false,false,{}}; return a; }
    u32 NewFakeString(const std::string& text){ u32 a=NewExternalInstance("NSString"); const u32 chars=AllocateCString(text); env_.MemoryWrite32(a+8u,chars); env_.MemoryWrite32(a+12u,static_cast<u32>(text.size())); fake_objects_[a].string_value=text; return a; }
    u32 AssociatedExternal(u32 owner,std::string_view key,std::string_view cls){
        const std::string k=std::to_string(owner)+":"+std::string(key);
        auto it=associated_fake_.find(k);if(it!=associated_fake_.end())return it->second;
        const u32 obj=NewExternalInstance(std::string(cls));associated_fake_[k]=obj;return obj;
    }
    bool WriteCGRect(u32 dest,float x,float y,float w,float h){
        if(!dest||!env_.IsMapped(dest,16u))return false;
        const std::array<float,4> rect{x,y,w,h};
        return env_.WriteBytes(dest,rect.data(),sizeof(rect));
    }
    void WriteGeneratedIds(u32 count,u32 ptr){
        if(!ptr||!count||count>4096u||!env_.IsMapped(ptr,std::size_t(count)*4u))return;
        for(u32 i=0;i<count;++i)env_.MemoryWrite32(ptr+i*4u,gl_object_counter_++);
    }

    u32 EnsureImport(const std::string& name){ auto it=import_by_name_.find(name); if(it!=import_by_name_.end())return imports_[it->second].stub; const std::size_t idx=imports_.size(); if((idx+1u)*8u>kImportSize)throw std::runtime_error("too many iOS imports"); Import imp{name,kImportBase+static_cast<u32>(idx*8u),static_cast<u32>(idx+1u),0}; WriteArmSvc(imp.stub,imp.svc); imports_.push_back(imp);import_by_name_[name]=idx;return imp.stub; }
    u32 ResolveSymbol(const std::string& symbol){
        constexpr std::string_view cls="_OBJC_CLASS_$_"; constexpr std::string_view meta="_OBJC_METACLASS_$_";
        if(symbol.starts_with(cls))return EnsureExternalClass(symbol.substr(cls.size()),false);
        if(symbol.starts_with(meta))return EnsureExternalClass(symbol.substr(meta.size()),true);
        if(symbol=="___CFConstantStringClassReference")return EnsureExternalClass("NSConstantString",false);
        static const std::set<std::string> known_data={"_NSDefaultRunLoopMode","_NSRunLoopCommonModes","_NSLocaleCountryCode","_NSLocaleLanguageCode","_NSLocalizedDescriptionKey","_UIApplicationDidBecomeActiveNotification","_UIApplicationDidFinishLaunchingNotification","_AVAudioSessionCategoryAmbient","_AVAudioSessionCategoryPlayback","_AVAudioSessionCategorySoloAmbient","_AVAudioSessionCategoryPlayAndRecord"};
        if(known_data.count(symbol)){ auto it=data_symbols_.find(symbol); if(it!=data_symbols_.end())return it->second; const u32 obj=NewFakeString(symbol.substr(1)); data_symbols_[symbol]=obj; return obj; }
        return EnsureImport(symbol);
    }

    static u64 ReadUleb(const std::vector<u8>& b,std::size_t& p,std::size_t end){u64 v=0;unsigned shift=0;while(p<end){u8 c=b[p++];v|=u64(c&0x7f)<<shift;if(!(c&0x80))return v;shift+=7;if(shift>63)throw std::runtime_error("ULEB overflow");}throw std::runtime_error("truncated ULEB");}
    static s64 ReadSleb(const std::vector<u8>& b,std::size_t& p,std::size_t end){s64 v=0;unsigned shift=0;u8 c=0;do{if(p>=end)throw std::runtime_error("truncated SLEB");c=b[p++];v|=s64(c&0x7f)<<shift;shift+=7;}while(c&0x80);if(shift<64&&(c&0x40))v|=-(s64(1)<<shift);return v;}
    void BindStream(u32 stream_off, u32 stream_size, const char* label) {
        if (!stream_size) return;
        if (!RangeFits(image_.bytes.size(), stream_off, stream_size)) {
            throw std::runtime_error(std::string(label) + " stream outside Mach-O");
        }

        constexpr u8 MASK = 0xf0, IMM = 0x0f;
        constexpr u8 DONE = 0x00, SET_DYLIB_IMM = 0x10, SET_DYLIB_ULEB = 0x20;
        constexpr u8 SET_DYLIB_SPECIAL = 0x30, SET_SYMBOL = 0x40, SET_TYPE = 0x50;
        constexpr u8 SET_ADDEND = 0x60, SET_SEG_OFF = 0x70, ADD_ADDR = 0x80;
        constexpr u8 DO_BIND = 0x90, DO_BIND_ADD = 0xa0, DO_BIND_IMM = 0xb0;
        constexpr u8 DO_BIND_TIMES = 0xc0;

        std::size_t p = stream_off;
        const std::size_t end = stream_off + stream_size;
        u32 seg_index = 0;
        /*
         * dyld's ARMv7 binder performs address arithmetic with uintptr_t.
         * Real 32-bit binaries can encode a large ULEB that deliberately wraps
         * to an earlier address. Geometry Dash 1.0 does this in its bind stream.
         */
        u32 seg_offset = 0;
        std::string symbol;
        s64 addend = 0;
        u8 type = 1;

        auto add_offset = [&](u64 amount) {
            seg_offset = static_cast<u32>(static_cast<u64>(seg_offset) + amount);
        };
        auto dobind = [&]() {
            if (seg_index >= image_.segments.size()) {
                throw std::runtime_error("bind segment index out of range");
            }
            const auto& seg = image_.segments[seg_index];
            const u64 addr64 = static_cast<u64>(seg.vmaddr) + seg_offset;
            if (addr64 > 0xffffffffull ||
                !env_.IsMapped(static_cast<u32>(addr64), 4u)) {
                throw std::runtime_error("bind address unmapped");
            }
            if (symbol.empty()) {
                throw std::runtime_error("Mach-O bind attempted without a symbol");
            }

            const u32 target = ResolveSymbol(symbol);
            const s64 address = static_cast<s64>(addr64);
            u32 value = 0;
            if (type == 1u || type == 2u) {
                value = static_cast<u32>(static_cast<s64>(target) + addend);
            } else if (type == 3u) {
                value = static_cast<u32>(static_cast<s64>(target) + addend - address);
            } else {
                throw std::runtime_error("unsupported Mach-O bind type");
            }
            env_.MemoryWrite32(static_cast<u32>(addr64), value);
            add_offset(4u);
        };

        while (p < end) {
            const u8 byte = image_.bytes[p++];
            const u8 op = byte & MASK;
            const u8 imm = byte & IMM;
            switch (op) {
            case DONE:
                symbol.clear();
                addend = 0;
                type = 1;
                break;
            case SET_DYLIB_IMM:
            case SET_DYLIB_SPECIAL:
                break;
            case SET_DYLIB_ULEB:
                (void)ReadUleb(image_.bytes, p, end);
                break;
            case SET_SYMBOL: {
                const std::size_t symbol_start = p;
                while (p < end && image_.bytes[p]) ++p;
                if (p >= end) {
                    throw std::runtime_error("unterminated bind symbol");
                }
                symbol.assign(
                    reinterpret_cast<const char*>(image_.bytes.data() + symbol_start),
                    p - symbol_start);
                ++p;
                break;
            }
            case SET_TYPE:
                type = imm;
                break;
            case SET_ADDEND:
                addend = ReadSleb(image_.bytes, p, end);
                break;
            case SET_SEG_OFF:
                seg_index = imm;
                seg_offset = static_cast<u32>(ReadUleb(image_.bytes, p, end));
                break;
            case ADD_ADDR:
                add_offset(ReadUleb(image_.bytes, p, end));
                break;
            case DO_BIND:
                dobind();
                break;
            case DO_BIND_ADD:
                dobind();
                add_offset(ReadUleb(image_.bytes, p, end));
                break;
            case DO_BIND_IMM:
                dobind();
                add_offset(static_cast<u64>(imm) * 4u);
                break;
            case DO_BIND_TIMES: {
                const u64 count = ReadUleb(image_.bytes, p, end);
                const u64 skip = ReadUleb(image_.bytes, p, end);
                for (u64 i = 0; i < count; ++i) {
                    dobind();
                    add_offset(skip);
                }
                break;
            }
            default:
                throw std::runtime_error("unsupported Mach-O bind opcode");
            }
        }
        log_ << "IOS: processed " << label << " stream bytes=" << stream_size << "\n";
    }

    const MachSection* FindSection(std::string_view name)const{for(const auto& seg:image_.segments)for(const auto& s:seg.sections)if(s.sectname==name)return &s;return nullptr;}
    std::vector<GuestMethod> ParseMethods(u32 list){std::vector<GuestMethod> out;if(!list||!env_.IsMapped(list,8u))return out;const u32 entsize=env_.MemoryRead32(list)&0xffffu;const u32 count=env_.MemoryRead32(list+4u);if(entsize<12u||count>20000u)return out;for(u32 i=0;i<count;++i){const u32 e=list+8u+i*entsize;if(!env_.IsMapped(e,12u))break;const u32 sel=env_.MemoryRead32(e),imp=env_.MemoryRead32(e+8u);std::string n;if(sel&&env_.ReadCString(sel,n,2048u))out.push_back({n,sel,imp});}return out;}
    void ParseGuestClasses(){const MachSection* s=FindSection("__objc_classlist");if(!s)return;for(u32 off=0;off+4u<=s->size;off+=4u){const u32 c=env_.MemoryRead32(s->addr+off);if(!c||!env_.IsMapped(c,20u))continue;const u32 meta=env_.MemoryRead32(c);const u32 superclass=env_.MemoryRead32(c+4u);const u32 data=env_.MemoryRead32(c+16u)&~3u;if(!data||!env_.IsMapped(data,40u))continue;const u32 namep=env_.MemoryRead32(data+16u),methods=env_.MemoryRead32(data+20u),instance_size=env_.MemoryRead32(data+8u);std::string name;if(!namep||!env_.ReadCString(namep,name,4096u))continue;GuestClass gc;gc.name=name;gc.class_addr=c;gc.meta_addr=meta;gc.superclass_addr=superclass;gc.instance_size=instance_size;gc.instance_methods=ParseMethods(methods);if(meta&&env_.IsMapped(meta,20u)){const u32 md=env_.MemoryRead32(meta+16u)&~3u;if(md&&env_.IsMapped(md,24u))gc.class_methods=ParseMethods(env_.MemoryRead32(md+20u));}classes_.push_back(std::move(gc));}}
    void ReportDelegate(const std::string& name){for(const auto& c:classes_)if(c.name==name){log_<<"IOS: found delegate class "<<name<<" class=0x"<<Hex(c.class_addr)<<" instanceSize="<<c.instance_size<<"\n";for(const auto& m:c.instance_methods)if(m.selector=="application:didFinishLaunchingWithOptions:"||m.selector=="applicationDidFinishLaunching:")log_<<"IOS: delegate launch method "<<m.selector<<" imp=0x"<<Hex(m.imp)<<"\n";}}

    const GuestClass* FindGuestClassByName(std::string_view name)const{for(const auto& c:classes_)if(c.name==name)return &c;return nullptr;}
    const GuestClass* FindGuestClassByClassAddress(u32 addr)const{for(const auto& c:classes_)if(c.class_addr==addr||c.meta_addr==addr)return &c;return nullptr;}
    const GuestClass* FindGuestClassForInstance(u32 object)const{if(!object||!env_.IsMapped(object,4u))return nullptr;const u32 isa=const_cast<ProbeEnvironment&>(env_).MemoryRead32(object);for(const auto& c:classes_)if(c.class_addr==isa)return &c;return nullptr;}
    const GuestMethod* FindMethod(const std::vector<GuestMethod>& methods,std::string_view selector)const{for(const auto& m:methods)if(m.selector==selector)return &m;return nullptr;}
    u32 NewGuestInstance(const GuestClass& cls){const u32 bytes=std::max<u32>(cls.instance_size,4u);const u32 object=Allocate(bytes,8u);std::vector<u8> zero(bytes);env_.WriteBytes(object,zero.data(),zero.size());env_.MemoryWrite32(object,cls.class_addr);return object;}
    void EnterGuestMethod(const GuestMethod& method){cpu_.Regs()[15]=method.imp&~1u;cpu_.SetCpsr((cpu_.Cpsr()&~0x20u)|((method.imp&1u)?0x20u:0u));}
    std::string ResolveDelegateName(u32 object){const std::string candidate=DescribeString(object);if(!candidate.empty()&&FindGuestClassByName(candidate))return candidate;for(std::string_view preferred:{std::string_view("AppDelegate"),std::string_view("AppController")})if(FindGuestClassByName(preferred))return std::string(preferred);return {};}
    bool BeginDelegateLaunch(){
        const GuestClass* cls=FindGuestClassByName(delegate_name_);
        if(!cls){log_<<"RESULT: IOS_DELEGATE_CLASS_NOT_FOUND name="<<(delegate_name_.empty()?"unknown":delegate_name_)<<"\n";done_=true;return true;}
        const GuestMethod* method=FindMethod(cls->instance_methods,"application:didFinishLaunchingWithOptions:");
        if(!method)method=FindMethod(cls->instance_methods,"applicationDidFinishLaunching:");
        if(!method){log_<<"RESULT: IOS_DELEGATE_LAUNCH_METHOD_NOT_FOUND class="<<cls->name<<"\n";done_=true;return true;}
        delegate_instance_=NewGuestInstance(*cls);
        application_instance_=NewExternalInstance("UIApplication");
        cpu_.Regs()[0]=delegate_instance_;cpu_.Regs()[1]=method->selector_addr;cpu_.Regs()[2]=application_instance_;cpu_.Regs()[3]=0u;
        cpu_.Regs()[14]=kControlBase;
        EnterGuestMethod(*method);
        delegate_launch_active_=true;delegate_launch_started_=true;
        log_<<"IOS: UIApplicationMain entering real delegate class="<<cls->name<<" selector="<<method->selector<<" imp=0x"<<Hex(method->imp)<<" self=0x"<<Hex(delegate_instance_)<<"\n";
        return true;
    }

    std::string ClassNameForAddress(u32 addr)const{auto it=fake_objects_.find(addr);if(it!=fake_objects_.end())return it->second.class_name;for(const auto& c:classes_)if(c.class_addr==addr||c.meta_addr==addr)return c.name;const GuestClass* instance=FindGuestClassForInstance(addr);return instance?instance->name:std::string{};}
    std::string DescribeString(u32 obj){if(!obj)return {};auto it=fake_objects_.find(obj);if(it!=fake_objects_.end()&&!it->second.string_value.empty())return it->second.string_value;std::string direct;if(env_.ReadCString(obj,direct,512u)&&!direct.empty()&&std::all_of(direct.begin(),direct.end(),[](unsigned char c){return c>=0x20&&c<0x7f;}))return direct;if(env_.IsMapped(obj,16u)){const u32 chars=env_.MemoryRead32(obj+8u);std::string s;if(chars&&env_.ReadCString(chars,s,512u))return s;}return {};}

    bool HandleObjcMsgSendStret(){
        const u32 dest=cpu_.Regs()[0],receiver=cpu_.Regs()[1],selector_addr=cpu_.Regs()[2];
        std::string selector;if(selector_addr)env_.ReadCString(selector_addr,selector,1024u);
        if(!dest||!env_.IsMapped(dest,16u))return true;

        if(!receiver){WriteCGRect(dest,0.0f,0.0f,0.0f,0.0f);return true;}

        const GuestClass* class_receiver=nullptr;
        for(const auto& c:classes_)if(c.class_addr==receiver||c.meta_addr==receiver){class_receiver=&c;break;}
        if(class_receiver){
            if(const GuestMethod* method=FindMethod(class_receiver->class_methods,selector)){
                if(++guest_dispatch_logs_<=64u)log_<<"IOS: objc guest stret class dispatch "<<class_receiver->name<<" +"<<selector<<" imp=0x"<<Hex(method->imp)<<"\n";
                EnterGuestMethod(*method);return true;
            }
        }
        if(const GuestClass* instance_class=FindGuestClassForInstance(receiver)){
            if(const GuestMethod* method=FindMethod(instance_class->instance_methods,selector)){
                if(++guest_dispatch_logs_<=64u)log_<<"IOS: objc guest stret instance dispatch "<<instance_class->name<<" -"<<selector<<" imp=0x"<<Hex(method->imp)<<"\n";
                EnterGuestMethod(*method);return true;
            }
        }

        const std::string cls=ClassNameForAddress(receiver);
        if(selector=="bounds"||selector=="frame"||selector=="applicationFrame"){
            WriteCGRect(dest,0.0f,0.0f,320.0f,480.0f);
            if(++stret_stub_logs_<=16u)
                log_<<"IOS: UIKit CGRect bootstrap class="<<(cls.empty()?"unknown":cls)<<" selector="<<selector<<" -> 320x480\n";
            return true;
        }
        WriteCGRect(dest,0.0f,0.0f,0.0f,0.0f);
        if(++stret_stub_logs_<=16u)
            log_<<"IOS: objc stret bootstrap stub class="<<(cls.empty()?"unknown":cls)<<" selector="<<(selector.empty()?"<unknown>":selector)<<" -> zero-rect\n";
        return true;
    }

    bool HandleObjcMsgSend(){
        const u32 receiver=cpu_.Regs()[0],selector_addr=cpu_.Regs()[1];
        std::string selector;if(selector_addr)env_.ReadCString(selector_addr,selector,1024u);
        if(!receiver){cpu_.Regs()[0]=0;return true;}

        // Dispatch Objective-C messages back into real classes that live in the
        // Mach-O. objc_msgSend is a tail-call, so preserving LR and replacing PC
        // with the IMP gives guest methods the same return path the real runtime
        // would have provided.
        const GuestClass* guest_class_receiver=nullptr;
        bool guest_meta_receiver=false;
        for(const auto& c:classes_){if(c.class_addr==receiver){guest_class_receiver=&c;break;}if(c.meta_addr==receiver){guest_class_receiver=&c;guest_meta_receiver=true;break;}}

        // Forlorn 1.9c bundles the pre-Apple TestFlight SDK.  Its +takeOff:
        // startup performs telemetry/cache/network initialization before the
        // game itself starts.  None of that SDK is required for gameplay, and
        // emulating it first would only hide the next real UIKit/game blocker.
        // Treat the call as a successful no-op, exactly like an unavailable
        // analytics service.
        if(guest_class_receiver && guest_class_receiver->name=="TestFlight" && selector=="takeOff:"){
            ++testflight_bypass_count_;
            if(testflight_bypass_count_<=4u)
                log_<<"IOS: bypassing legacy TestFlight +takeOff: count="<<testflight_bypass_count_<<" policy=telemetry-disabled\n";
            cpu_.Regs()[0]=0u;
            return true;
        }

        if(guest_class_receiver&&selector=="class"){cpu_.Regs()[0]=receiver;return true;}
        if(guest_class_receiver&&!guest_meta_receiver&&(selector=="alloc"||selector=="new")){cpu_.Regs()[0]=NewGuestInstance(*guest_class_receiver);return true;}
        if(guest_class_receiver){const GuestMethod* method=FindMethod(guest_class_receiver->class_methods,selector);if(method){if(++guest_dispatch_logs_<=64u)log_<<"IOS: objc guest class dispatch "<<guest_class_receiver->name<<" +"<<selector<<" imp=0x"<<Hex(method->imp)<<"\n";EnterGuestMethod(*method);return true;}}
        if(const GuestClass* instance_class=FindGuestClassForInstance(receiver)){
            const GuestMethod* method=FindMethod(instance_class->instance_methods,selector);
            if(method){if(++guest_dispatch_logs_<=64u)log_<<"IOS: objc guest instance dispatch "<<instance_class->name<<" -"<<selector<<" imp=0x"<<Hex(method->imp)<<"\n";EnterGuestMethod(*method);return true;}
            if(selector=="class"){cpu_.Regs()[0]=instance_class->class_addr;return true;}
            if(selector=="layer"&&(instance_class->name=="EAGLView"||instance_class->name=="UIView")){cpu_.Regs()[0]=AssociatedExternal(receiver,"layer","CAEAGLLayer");return true;}
            if(selector=="init"||selector.starts_with("initWith")||selector=="retain"||selector=="autorelease"||selector=="copy"||selector=="mutableCopy"){cpu_.Regs()[0]=receiver;return true;}
            if(selector=="release"||selector=="dealloc"){cpu_.Regs()[0]=0;return true;}
        }

        const auto fit=fake_objects_.find(receiver);
        if(fit!=fake_objects_.end()){
            const auto& fo=fit->second;
            if((fo.is_class||fo.is_meta)&&(selector=="alloc"||selector=="new")){cpu_.Regs()[0]=NewExternalInstance(fo.class_name);return true;}
            if((fo.is_class||fo.is_meta)&&selector=="class"){cpu_.Regs()[0]=receiver;return true;}
            if(fo.is_class&&(selector=="sharedApplication"||selector=="sharedInstance"||selector=="defaultManager"||selector=="defaultCenter"||selector=="standardUserDefaults"||selector=="mainBundle"||selector=="mainScreen"||selector=="currentDevice")){cpu_.Regs()[0]=NewExternalInstance(fo.class_name);return true;}
            if(fo.is_class&&(selector.starts_with("numberWith")||selector.starts_with("valueWith")||selector.starts_with("arrayWith")||selector.starts_with("dictionaryWith")||selector.starts_with("setWith")||selector.starts_with("URLWith")||selector.starts_with("dataWith")||selector.starts_with("colorWith")||selector.starts_with("fontWith")||selector.starts_with("imageNamed"))){cpu_.Regs()[0]=NewExternalInstance(fo.class_name);return true;}
            if(fo.is_class&&selector=="setCurrentContext:"){cpu_.Regs()[0]=1u;return true;}
            if(!fo.is_class&&(selector=="init"||selector.starts_with("initWith"))){cpu_.Regs()[0]=receiver;return true;}
            if(!fo.is_class&&selector=="layer"){cpu_.Regs()[0]=AssociatedExternal(receiver,"layer","CAEAGLLayer");return true;}
            if(!fo.is_class&&(selector=="presentRenderbuffer:"||selector=="renderbufferStorage:fromDrawable:")){cpu_.Regs()[0]=1u;return true;}
            if(!fo.is_class&&(selector=="drawableWidth"||selector=="width")){cpu_.Regs()[0]=320u;return true;}
            if(!fo.is_class&&(selector=="drawableHeight"||selector=="height")){cpu_.Regs()[0]=480u;return true;}
            if(selector=="retain"||selector=="autorelease"||selector=="copy"||selector=="mutableCopy"){cpu_.Regs()[0]=receiver;return true;}
            if(selector=="release"||selector=="dealloc"){cpu_.Regs()[0]=0;return true;}
            if(selector=="class"){cpu_.Regs()[0]=fo.is_class?receiver:env_.MemoryRead32(receiver);return true;}
            if(selector=="UTF8String"||selector=="cStringUsingEncoding:"){cpu_.Regs()[0]=fo.string_value.empty()?0u:AllocateCString(fo.string_value);return true;}
            if(selector=="length"){cpu_.Regs()[0]=static_cast<u32>(fo.string_value.size());return true;}
            if(selector=="respondsToSelector:"||selector=="isKindOfClass:"||selector=="isMemberOfClass:"){cpu_.Regs()[0]=1u;return true;}
        }
        if(++unimplemented_objc_count_<=64u){const std::string class_name=ClassNameForAddress(receiver);log_<<"IOS: objc bootstrap stub receiver=0x"<<Hex(receiver)<<" class="<<(class_name.empty()?"unknown":class_name)<<" selector="<<(selector.empty()?"<unknown>":selector)<<" -> nil/0\n";}
        cpu_.Regs()[0]=0;return true;
    }

    bool HandleObjcMsgSendSuper2(){
        const u32 super_ptr=cpu_.Regs()[0],selector_addr=cpu_.Regs()[1];std::string selector;if(selector_addr)env_.ReadCString(selector_addr,selector,1024u);
        if(!super_ptr||!env_.IsMapped(super_ptr,8u)){cpu_.Regs()[0]=0;return true;}
        const u32 receiver=env_.MemoryRead32(super_ptr);const u32 current_class=env_.MemoryRead32(super_ptr+4u);
        const GuestClass* current=FindGuestClassByClassAddress(current_class);
        if(current){if(const GuestClass* parent=FindGuestClassByClassAddress(current->superclass_addr)){if(const GuestMethod* method=FindMethod(parent->instance_methods,selector)){cpu_.Regs()[0]=receiver;EnterGuestMethod(*method);return true;}}}
        // The common superclass calls during bootstrap are NSObject/UIKit init,
        // retain and release operations. Returning the original receiver is the
        // safe Objective-C identity behavior for those initializers.
        if(selector=="init"||selector.starts_with("initWith")||selector=="retain"||selector=="autorelease"){cpu_.Regs()[0]=receiver;return true;}
        if(selector=="release"||selector=="dealloc"){cpu_.Regs()[0]=0;return true;}
        if(++unimplemented_objc_count_<=64u)log_<<"IOS: objc super bootstrap stub class="<<(current?current->name:"unknown")<<" selector="<<(selector.empty()?"<unknown>":selector)<<" -> receiver/0\n";
        cpu_.Regs()[0]=receiver;return true;
    }

    bool HandleSvc(u32 svc){if(svc==kSvcReturn){if(delegate_launch_active_){delegate_launch_active_=false;delegate_launch_returned_=true;delegate_return_value_=cpu_.Regs()[0];log_<<"IOS: real delegate launch returned r0=0x"<<Hex(delegate_return_value_)<<"\n";}done_=true;return true;}if(svc==0||svc>imports_.size()){log_<<"RESULT: IOS_UNKNOWN_SVC id="<<svc<<"\n";return false;}Import& imp=imports_[svc-1u];++imp.calls;std::string name=imp.name;if(!name.empty()&&name[0]=='_')name.erase(name.begin());if(imp.calls==1u)log_<<"IOS IMPORT: "<<imp.name<<" r0=0x"<<Hex(cpu_.Regs()[0])<<" r1=0x"<<Hex(cpu_.Regs()[1])<<"\n";
        if(name=="objc_msgSend")return HandleObjcMsgSend();
        if(name=="objc_msgSend_stret")return HandleObjcMsgSendStret();
        if(name=="objc_msgSendSuper2")return HandleObjcMsgSendSuper2();
        if(name=="objc_msgSendSuper2_stret"){if(cpu_.Regs()[0]&&env_.IsMapped(cpu_.Regs()[0],16u))WriteCGRect(cpu_.Regs()[0],0.0f,0.0f,0.0f,0.0f);return true;}
        if(name=="objc_retain"||name=="objc_autorelease"||name=="objc_retainAutoreleasedReturnValue"||name=="objc_autoreleaseReturnValue"){return true;}
        if(name=="objc_release"){cpu_.Regs()[0]=0;return true;}
        if(name=="objc_getClass"){std::string n;if(cpu_.Regs()[0])env_.ReadCString(cpu_.Regs()[0],n,512u);for(const auto& c:classes_)if(c.name==n){cpu_.Regs()[0]=c.class_addr;return true;}cpu_.Regs()[0]=EnsureExternalClass(n.empty()?"NSObject":n);return true;}
        if(name=="sel_registerName"||name=="sel_getUid"){return true;}
        if(name=="NSStringFromClass"){const std::string n=ClassNameForAddress(cpu_.Regs()[0]);cpu_.Regs()[0]=NewFakeString(n.empty()?"NSObject":n);return true;}
        if(name=="NSClassFromString"){const std::string n=DescribeString(cpu_.Regs()[0]);for(const auto& c:classes_)if(c.name==n){cpu_.Regs()[0]=c.class_addr;return true;}cpu_.Regs()[0]=EnsureExternalClass(n.empty()?"NSObject":n);return true;}
        if(name=="UIApplicationMain"){
            reached_ui_application_main_=true;delegate_name_=ResolveDelegateName(cpu_.Regs()[3]);
            log_<<"IOS: UIApplicationMain reached argc="<<cpu_.Regs()[0]<<" delegate="<<(delegate_name_.empty()?"unknown":delegate_name_)<<" constructors="<<image_.constructor_count<<"\n";
            if(image_.constructor_count!=0u){delegate_launch_deferred_=true;done_=true;cpu_.Regs()[0]=0;log_<<"IOS: delegate launch deferred until Mach-O static constructors are implemented\n";return true;}
            return BeginDelegateLaunch();
        }
        // Minimal OpenGL ES/OpenAL bootstrap results. These do not render yet;
        // they only provide the success/query values cocos2d expects while
        // constructing EAGLView.
        if(name=="glGetError"||name=="alGetError"||name=="alcGetError"){cpu_.Regs()[0]=0u;return true;}
        if(name=="glCheckFramebufferStatusOES"){cpu_.Regs()[0]=0x8cd5u;return true;} // GL_FRAMEBUFFER_COMPLETE
        if(name=="glGenFramebuffersOES"||name=="glGenRenderbuffersOES"||name=="glGenBuffers"||name=="glGenTextures"||name=="alGenBuffers"||name=="alGenSources"){
            WriteGeneratedIds(cpu_.Regs()[0],cpu_.Regs()[1]);cpu_.Regs()[0]=0u;return true;
        }
        if(name=="glGetRenderbufferParameterivOES"){
            const u32 pname=cpu_.Regs()[1],ptr=cpu_.Regs()[2];
            if(ptr&&env_.IsMapped(ptr,4u))env_.MemoryWrite32(ptr,pname==0x8d43u?480u:320u);
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="glGetString"){cpu_.Regs()[0]=AllocateCString("GeometryDashWrapper iOS OpenGL ES bootstrap");return true;}
        if(name=="alcOpenDevice"||name=="alcCreateContext"){cpu_.Regs()[0]=Allocate(16u);return true;}
        if(name=="alcMakeContextCurrent"){cpu_.Regs()[0]=1u;return true;}
        if(name.starts_with("gl")||name.starts_with("al")||name.starts_with("alc")){
            if(++graphics_stub_logs_<=32u)log_<<"IOS: graphics/audio bootstrap stub "<<imp.name<<"\n";
            cpu_.Regs()[0]=0u;return true;
        }

        if(name=="exit"||name=="_exit"){done_=true;exit_code_=cpu_.Regs()[0];return true;}
        if(name=="malloc"){cpu_.Regs()[0]=Allocate(std::max<u32>(cpu_.Regs()[0],1u));return true;}
        if(name=="calloc"){const u64 n=u64(cpu_.Regs()[0])*cpu_.Regs()[1];if(n>0x1000000u){cpu_.Regs()[0]=0;return true;}const u32 a=Allocate(static_cast<u32>(std::max<u64>(n,1u)));std::vector<u8> z(static_cast<std::size_t>(n));if(n)env_.WriteBytes(a,z.data(),z.size());cpu_.Regs()[0]=a;return true;}
        if(name=="free"){cpu_.Regs()[0]=0;return true;}
        if(name=="memset"){const u32 dst=cpu_.Regs()[0],value=cpu_.Regs()[1]&0xffu,n=cpu_.Regs()[2];if(n&&env_.IsMapped(dst,n)){std::vector<u8> v(n,static_cast<u8>(value));env_.WriteBytes(dst,v.data(),v.size());}cpu_.Regs()[0]=dst;return true;}
        if(name=="memcpy"||name=="memmove"){const u32 dst=cpu_.Regs()[0],src=cpu_.Regs()[1],n=cpu_.Regs()[2];if(n&&env_.IsMapped(dst,n)&&env_.IsMapped(src,n)){std::vector<u8> tmp(n);env_.ReadBytes(src,tmp.data(),n);env_.WriteBytes(dst,tmp.data(),n);}cpu_.Regs()[0]=dst;return true;}
        if(name=="strlen"){std::string s;if(!env_.ReadCString(cpu_.Regs()[0],s,1u<<20)){cpu_.Regs()[0]=0;}else cpu_.Regs()[0]=static_cast<u32>(s.size());return true;}
        if(name=="__stack_chk_fail"){log_<<"RESULT: IOS_STACK_CHECK_FAILED\n";done_=true;return false;}
        if (++unknown_import_count_ <= 64u) {
            log_ << "IOS: bootstrap import stub " << imp.name << " -> 0\n";
        }
        cpu_.Regs()[0] = 0;
        return true;
    }

    void BuildInitialStack(){const std::string argv0="GeometryDashWrapper-iOS";const u32 arg=AllocateCString(argv0);u32 sp=kStackBase+kStackSize-0x1000u;sp&=~7u;sp-=24u;env_.MemoryWrite32(sp+0u,1u);env_.MemoryWrite32(sp+4u,arg);env_.MemoryWrite32(sp+8u,0u);env_.MemoryWrite32(sp+12u,0u);env_.MemoryWrite32(sp+16u,0u);env_.MemoryWrite32(sp+20u,0u);initial_sp_=sp;}

    MachImage image_; Logger& log_; ProbeEnvironment env_; Dynarmic::ExclusiveMonitor monitor_; Dynarmic::A32::Jit cpu_;
    std::vector<Import> imports_; std::unordered_map<std::string,std::size_t> import_by_name_; std::unordered_map<u32,FakeObject> fake_objects_; std::unordered_map<std::string,u32> fake_named_; std::unordered_map<std::string,u32> data_symbols_; std::unordered_map<std::string,u32> associated_fake_; std::vector<GuestClass> classes_;
    u32 heap_cursor_=kHeapBase+0x1000u,object_cursor_=kObjectBase+0x1000u,initial_sp_=0;u32 exit_code_=0;u32 delegate_instance_=0,application_instance_=0,delegate_return_value_=0,gl_object_counter_=1u;u64 unknown_import_count_=0,unimplemented_objc_count_=0,guest_dispatch_logs_=0,testflight_bypass_count_=0,stret_stub_logs_=0,graphics_stub_logs_=0;bool done_=false,reached_ui_application_main_=false,delegate_launch_started_=false,delegate_launch_active_=false,delegate_launch_returned_=false,delegate_launch_deferred_=false;std::string delegate_name_;
};

static std::string GetLogPath(int argc,char** argv){for(int i=2;i<argc;++i){std::string a=argv[i];if(a.starts_with("--log="))return a.substr(6);}const char* env=std::getenv("GD_LOG_PATH");return env?env:"ios-armv7.log";}

} // namespace

int main(int argc,char** argv){
    if(argc<2){std::cerr<<"Usage: RobTopIOSArmV7.exe game.ipa [--log=file]\n";return 2;}
    Logger log(GetLogPath(argc,argv));
    try{
        log<<"Geometry Dash Wrapper "<<GD_WRAPPER_VERSION<<" - iOS ARMv7 bootstrap\n";
        log<<"IPA: "<<argv[1]<<"\n";
        const auto ipa=ReadFile(argv[1]);
        auto exe=FindAppExecutable(ipa);
        log<<"Executable member: "<<exe.member<<" ("<<exe.bytes.size()<<" bytes)\n";
        auto image=SelectAndParseArmv7(exe.bytes);
        log<<"Mach-O: "<<image.arch<<" entry=0x"<<std::hex<<image.entry<<std::dec<<" encrypted="<<(image.encrypted?1:0)<<"\n";
        IosBootstrap runtime(std::move(image),log);
        if(!runtime.Prepare())return 3;
        return runtime.Run()?0:4;
    }catch(const std::exception& e){log<<"RESULT: IOS_BOOTSTRAP_FATAL error="<<e.what()<<"\n";return 5;}
}

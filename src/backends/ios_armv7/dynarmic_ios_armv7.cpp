#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
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
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#endif

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
constexpr u64 kRunBudget = 600000000u;
constexpr u32 kFrameProbeCount = 0u; // 0 = run until the user closes the host window
constexpr u64 kSyntheticFrameUsec = 16667u;

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
static float FloatFromBits(u32 bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
static u32 FloatToBits(float value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
static double DoubleFromRegs(u32 lo, u32 hi) {
    const u64 bits = u64(lo) | (u64(hi) << 32);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
static void DoubleToRegs(double value, u32& lo, u32& hi) {
    u64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    lo = static_cast<u32>(bits);
    hi = static_cast<u32>(bits >> 32);
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


struct DecodedPng {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;
};

static u8 PaethByte(u8 a,u8 b,u8 c) {
    const int p=int(a)+int(b)-int(c);
    const int pa=std::abs(p-int(a)),pb=std::abs(p-int(b)),pc=std::abs(p-int(c));
    return pa<=pb&&pa<=pc?a:(pb<=pc?b:c);
}

static bool DecodeIosPngRgba(const std::vector<u8>& png, DecodedPng& out) {
    static constexpr u8 sig[8]={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    if(png.size()<33u||std::memcmp(png.data(),sig,8u)!=0)return false;
    bool cgbi=false;
    u32 width=0,height=0;
    u8 bit_depth=0,color_type=0,interlace=0;
    std::vector<u8> compressed;
    std::size_t pos=8u;
    while(pos+12u<=png.size()){
        const u32 len=ReadBe32(png.data()+pos);
        if(!RangeFits(png.size(),pos+8u,std::size_t(len)+4u))return false;
        const char* type=reinterpret_cast<const char*>(png.data()+pos+4u);
        const u8* data=png.data()+pos+8u;
        if(std::memcmp(type,"CgBI",4u)==0)cgbi=true;
        else if(std::memcmp(type,"IHDR",4u)==0){
            if(len!=13u)return false;
            width=ReadBe32(data);height=ReadBe32(data+4u);
            bit_depth=data[8];color_type=data[9];interlace=data[12];
        }else if(std::memcmp(type,"IDAT",4u)==0){
            compressed.insert(compressed.end(),data,data+len);
        }else if(std::memcmp(type,"IEND",4u)==0)break;
        pos+=12u+len;
    }
    if(!width||!height||width>8192u||height>8192u||bit_depth!=8u||color_type!=6u||interlace!=0u||compressed.empty())return false;
    const std::size_t stride=std::size_t(width)*4u;
    const std::size_t raw_size=(stride+1u)*height;
    if(raw_size>256u*1024u*1024u)return false;
    std::vector<u8> raw(raw_size);
    z_stream stream{};
    stream.next_in=const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in=static_cast<uInt>(compressed.size());
    stream.next_out=reinterpret_cast<Bytef*>(raw.data());
    stream.avail_out=static_cast<uInt>(raw.size());
    if(inflateInit2(&stream,cgbi?-MAX_WBITS:MAX_WBITS)!=Z_OK)return false;
    const int result=inflate(&stream,Z_FINISH);
    inflateEnd(&stream);
    if(result!=Z_STREAM_END||stream.total_out!=raw_size)return false;

    std::vector<u8> recon(std::size_t(width)*height*4u);
    std::vector<u8> prev(stride,0u),row(stride,0u);
    std::size_t rp=0u;
    for(u32 y=0;y<height;++y){
        const u8 filter=raw[rp++];
        const u8* src=raw.data()+rp;rp+=stride;
        for(std::size_t x=0;x<stride;++x){
            const u8 left=x>=4u?row[x-4u]:0u;
            const u8 up=prev[x];
            const u8 ul=x>=4u?prev[x-4u]:0u;
            u8 value=src[x];
            if(filter==1u)value=static_cast<u8>(value+left);
            else if(filter==2u)value=static_cast<u8>(value+up);
            else if(filter==3u)value=static_cast<u8>(value+u8((u16(left)+u16(up))/2u));
            else if(filter==4u)value=static_cast<u8>(value+PaethByte(left,up,ul));
            else if(filter!=0u)return false;
            row[x]=value;
        }
        std::memcpy(recon.data()+std::size_t(y)*stride,row.data(),stride);
        prev.swap(row);
    }

    out.width=width;out.height=height;out.rgba.resize(std::size_t(width)*height*4u);
    for(std::size_t i=0;i<out.rgba.size();i+=4u){
        if(cgbi){
            u32 b=recon[i+0],g=recon[i+1],r=recon[i+2],a=recon[i+3];
            if(a&&a<255u){
                r=std::min<u32>(255u,(r*255u+a/2u)/a);
                g=std::min<u32>(255u,(g*255u+a/2u)/a);
                b=std::min<u32>(255u,(b*255u+a/2u)/a);
            }
            out.rgba[i+0]=static_cast<u8>(r);out.rgba[i+1]=static_cast<u8>(g);
            out.rgba[i+2]=static_cast<u8>(b);out.rgba[i+3]=static_cast<u8>(a);
        }else{
            std::memcpy(out.rgba.data()+i,recon.data()+i,4u);
        }
    }
    return true;
}

static std::string LowerAscii(std::string s){
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}


struct PlistValue {
    enum class Kind { Null, Boolean, Integer, Real, String, Array, Dict };
    Kind kind = Kind::Null;
    bool boolean = false;
    s64 integer = 0;
    double real = 0.0;
    std::string string;
    std::vector<PlistValue> array;
    std::vector<std::string> dict_keys;
    std::vector<PlistValue> dict_values;
};

class BinaryPlistParser {
public:
    explicit BinaryPlistParser(const std::vector<u8>& bytes):bytes_(bytes){}

    bool Parse(PlistValue& out) {
        if(bytes_.size()<40u||std::memcmp(bytes_.data(),"bplist00",8u)!=0)return false;
        const std::size_t trailer=bytes_.size()-32u;
        offset_size_=bytes_[trailer+6u];
        ref_size_=bytes_[trailer+7u];
        num_objects_=ReadUInt(trailer+8u,8u);
        top_object_=ReadUInt(trailer+16u,8u);
        offset_table_=ReadUInt(trailer+24u,8u);
        if(!offset_size_||offset_size_>8u||!ref_size_||ref_size_>8u||!num_objects_||
           top_object_>=num_objects_||offset_table_>=bytes_.size()||
           num_objects_>(1u<<20))return false;
        if(offset_table_>bytes_.size()||num_objects_>(bytes_.size()-offset_table_)/offset_size_)return false;
        cache_.resize(static_cast<std::size_t>(num_objects_));
        active_.resize(static_cast<std::size_t>(num_objects_),false);
        return ParseObject(top_object_,out,0u);
    }

private:
    u64 ReadUInt(std::size_t pos,std::size_t bytes)const {
        if(!bytes||bytes>8u||!RangeFits(bytes_.size(),pos,bytes))return 0u;
        u64 value=0u;
        for(std::size_t i=0;i<bytes;++i)value=(value<<8u)|bytes_[pos+i];
        return value;
    }
    bool ObjectOffset(u64 object,std::size_t& off)const {
        if(object>=num_objects_)return false;
        const u64 raw=ReadUInt(static_cast<std::size_t>(offset_table_+object*offset_size_),offset_size_);
        if(raw>=bytes_.size())return false;
        off=static_cast<std::size_t>(raw);return true;
    }
    bool Count(std::size_t object_off,u8 marker,u64& count,std::size_t& payload)const {
        count=marker&0x0fu;payload=object_off+1u;
        if(count!=0x0fu)return payload<=bytes_.size();
        if(payload>=bytes_.size())return false;
        const u8 int_marker=bytes_[payload++];
        if((int_marker>>4u)!=0x1u)return false;
        const u8 power=int_marker&0x0fu;
        if(power>3u)return false;
        const std::size_t int_bytes=std::size_t(1u)<<power;
        if(!RangeFits(bytes_.size(),payload,int_bytes))return false;
        count=ReadUInt(payload,int_bytes);payload+=int_bytes;
        return true;
    }
    static void AppendUtf8(std::string& out,u32 cp){
        if(cp<=0x7fu)out.push_back(static_cast<char>(cp));
        else if(cp<=0x7ffu){
            out.push_back(static_cast<char>(0xc0u|(cp>>6u)));
            out.push_back(static_cast<char>(0x80u|(cp&0x3fu)));
        }else{
            out.push_back(static_cast<char>(0xe0u|(cp>>12u)));
            out.push_back(static_cast<char>(0x80u|((cp>>6u)&0x3fu)));
            out.push_back(static_cast<char>(0x80u|(cp&0x3fu)));
        }
    }
    bool ParseObject(u64 object,PlistValue& out,u32 depth){
        if(depth>64u||object>=num_objects_)return false;
        if(cache_[object]){out=*cache_[object];return true;}
        if(active_[object])return false;
        active_[object]=true;

        std::size_t off=0;
        if(!ObjectOffset(object,off)){active_[object]=false;return false;}
        const u8 marker=bytes_[off],type=marker>>4u,info=marker&0x0fu;
        PlistValue value;

        if(type==0x0u){
            if(info==0x8u){value.kind=PlistValue::Kind::Boolean;value.boolean=false;}
            else if(info==0x9u){value.kind=PlistValue::Kind::Boolean;value.boolean=true;}
            else value.kind=PlistValue::Kind::Null;
        }else if(type==0x1u){
            if(info>3u){active_[object]=false;return false;}
            const std::size_t n=std::size_t(1u)<<info;
            if(!RangeFits(bytes_.size(),off+1u,n)){active_[object]=false;return false;}
            u64 raw=ReadUInt(off+1u,n);
            if(n<8u&&(raw&(u64(1)<<(n*8u-1u))))raw|=(~u64(0))<<(n*8u);
            value.kind=PlistValue::Kind::Integer;value.integer=static_cast<s64>(raw);
        }else if(type==0x2u){
            const std::size_t n=std::size_t(1u)<<info;
            if((n!=4u&&n!=8u)||!RangeFits(bytes_.size(),off+1u,n)){active_[object]=false;return false;}
            if(n==4u){
                const u32 bits=static_cast<u32>(ReadUInt(off+1u,4u));
                float f=0.0f;std::memcpy(&f,&bits,sizeof(f));value.real=f;
            }else{
                const u64 bits=ReadUInt(off+1u,8u);
                double d=0.0;std::memcpy(&d,&bits,sizeof(d));value.real=d;
            }
            value.kind=PlistValue::Kind::Real;
        }else if(type==0x5u||type==0x6u){
            u64 count=0;std::size_t payload=0;
            if(!Count(off,marker,count,payload)||count>(1u<<24)){active_[object]=false;return false;}
            value.kind=PlistValue::Kind::String;
            if(type==0x5u){
                if(count>bytes_.size()||!RangeFits(bytes_.size(),payload,static_cast<std::size_t>(count))){active_[object]=false;return false;}
                value.string.assign(reinterpret_cast<const char*>(bytes_.data()+payload),static_cast<std::size_t>(count));
            }else{
                if(count>bytes_.size()/2u||!RangeFits(bytes_.size(),payload,static_cast<std::size_t>(count)*2u)){active_[object]=false;return false;}
                for(u64 i=0;i<count;++i)AppendUtf8(value.string,static_cast<u32>(ReadUInt(payload+static_cast<std::size_t>(i)*2u,2u)));
            }
        }else if(type==0xau||type==0xcu){
            u64 count=0;std::size_t payload=0;
            if(!Count(off,marker,count,payload)||count>(1u<<20)||count>(bytes_.size()/ref_size_)){active_[object]=false;return false;}
            if(!RangeFits(bytes_.size(),payload,static_cast<std::size_t>(count)*ref_size_)){active_[object]=false;return false;}
            value.kind=PlistValue::Kind::Array;value.array.reserve(static_cast<std::size_t>(count));
            for(u64 i=0;i<count;++i){
                const u64 ref=ReadUInt(payload+static_cast<std::size_t>(i)*ref_size_,ref_size_);
                PlistValue child;if(!ParseObject(ref,child,depth+1u)){active_[object]=false;return false;}
                value.array.push_back(std::move(child));
            }
        }else if(type==0xdu){
            u64 count=0;std::size_t payload=0;
            if(!Count(off,marker,count,payload)||count>(1u<<20)||count>(bytes_.size()/(2u*ref_size_))){active_[object]=false;return false;}
            const std::size_t refs=static_cast<std::size_t>(count)*ref_size_;
            if(!RangeFits(bytes_.size(),payload,refs*2u)){active_[object]=false;return false;}
            value.kind=PlistValue::Kind::Dict;value.dict_keys.reserve(static_cast<std::size_t>(count));value.dict_values.reserve(static_cast<std::size_t>(count));
            for(u64 i=0;i<count;++i){
                const u64 key_ref=ReadUInt(payload+static_cast<std::size_t>(i)*ref_size_,ref_size_);
                const u64 val_ref=ReadUInt(payload+refs+static_cast<std::size_t>(i)*ref_size_,ref_size_);
                PlistValue key,val;
                if(!ParseObject(key_ref,key,depth+1u)||key.kind!=PlistValue::Kind::String||
                   !ParseObject(val_ref,val,depth+1u)){active_[object]=false;return false;}
                value.dict_keys.push_back(key.string);value.dict_values.push_back(std::move(val));
            }
        }else{
            active_[object]=false;return false;
        }

        active_[object]=false;
        cache_[object]=value;
        out=std::move(value);
        return true;
    }

    const std::vector<u8>& bytes_;
    u8 offset_size_=0,ref_size_=0;
    u64 num_objects_=0,top_object_=0,offset_table_=0;
    std::vector<std::optional<PlistValue>> cache_;
    std::vector<bool> active_;
};

static bool ExtractFloats(std::string_view text,float* out,std::size_t count){
    std::string copy(text);
    const char* p=copy.c_str();
    for(std::size_t i=0;i<count;++i){
        while(*p&&!(std::isdigit(static_cast<unsigned char>(*p))||*p=='-'||*p=='+'||*p=='.'))++p;
        if(!*p)return false;
        char* end=nullptr;out[i]=std::strtof(p,&end);
        if(end==p)return false;
        p=end;
    }
    return true;
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
    const void* HostPointer(u32 address, std::size_t size = 1u) const {
        if(!address)return nullptr;
        const MemoryRegion* r=Find(address,size);
        return r ? static_cast<const void*>(r->data.data()+(address-r->base)) : nullptr;
    }
    void* HostPointerMutable(u32 address, std::size_t size = 1u) {
        if(!address)return nullptr;
        MemoryRegion* r=FindMutable(address,size);
        return r ? static_cast<void*>(r->data.data()+(address-r->base)) : nullptr;
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
struct FakeObject { std::string class_name; bool is_class=false; bool is_meta=false; std::string string_value; std::string resource_value; u32 aux0=0,aux1=0,aux2=0; };
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

struct HostTouchEvent {
    u32 phase = 0; // UIKit phases: began=0, moved=1, ended=3, cancelled=4
    float x = 0.0f;
    float y = 0.0f;
};

class HostOpenGLWindow {
public:
    HostOpenGLWindow() = default;
    HostOpenGLWindow(const HostOpenGLWindow&) = delete;
    HostOpenGLWindow& operator=(const HostOpenGLWindow&) = delete;
    ~HostOpenGLWindow(){ Shutdown(); }

    bool Initialize(Logger& log) {
#ifdef _WIN32
        if(ready_)return true;
        instance_=GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.style=CS_OWNDC;
        wc.lpfnWndProc=&HostOpenGLWindow::WndProc;
        wc.hInstance=instance_;
        wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        wc.lpszClassName=L"GDW_IOS_ARMV7_GL";
        RegisterClassW(&wc);

        // Keep the wrapper UI consistent with the other backends: a 16:9
        // host window. The old iPhone game itself remains native 3:2 and
        // is pillarboxed inside this window after rotating the portrait
        // iOS renderbuffer 90 degrees counter-clockwise.
        RECT rect{0,0,960,540};
        AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        hwnd_=CreateWindowExW(
            0,wc.lpszClassName,L"Forlorn - Geometry Dash Wrapper iOS",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,
            nullptr,nullptr,instance_,this);
        if(!hwnd_){log<<"IOS HOSTGL: CreateWindowExW failed error="<<GetLastError()<<"\n";return false;}
        dc_=GetDC(hwnd_);
        if(!dc_){log<<"IOS HOSTGL: GetDC failed\n";Shutdown();return false;}

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize=sizeof(pfd);
        pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pfd.iPixelType=PFD_TYPE_RGBA;
        pfd.cColorBits=32;
        pfd.cDepthBits=24;
        pfd.cStencilBits=8;
        pfd.iLayerType=PFD_MAIN_PLANE;
        const int pf=ChoosePixelFormat(dc_,&pfd);
        if(!pf||!SetPixelFormat(dc_,pf,&pfd)){log<<"IOS HOSTGL: pixel format setup failed error="<<GetLastError()<<"\n";Shutdown();return false;}
        rc_=wglCreateContext(dc_);
        if(!rc_||!wglMakeCurrent(dc_,rc_)){log<<"IOS HOSTGL: wglCreateContext/wglMakeCurrent failed\n";Shutdown();return false;}

        LoadExtensions();
        if(!CreatePortraitFramebuffer(log)){Shutdown();return false;}
        glViewport(0,0,kPortraitWidth,kPortraitHeight);
        glClearColor(0.0f,0.0f,0.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        ready_=true;
        Present();
        frame_clock_=std::chrono::steady_clock::now();
        log<<"IOS HOSTGL: Win32 OpenGL window ready client=960x540 logical=320x480 offscreen="
           <<kPortraitWidth<<"x"<<kPortraitHeight
           <<" presentation=CCW90 content=3:2 pillarboxed-in=16:9 input=mouse-to-uitouch vbo="<<(glGenBuffers_?1:0)<<"\n";
        return true;
#else
        (void)log;
        return false;
#endif
    }

    void Shutdown() {
#ifdef _WIN32
        if(rc_&&dc_)wglMakeCurrent(dc_,rc_);
        if(offscreen_depth_&&glDeleteRenderbuffers_){glDeleteRenderbuffers_(1,&offscreen_depth_);offscreen_depth_=0;}
        if(offscreen_fbo_&&glDeleteFramebuffers_){glDeleteFramebuffers_(1,&offscreen_fbo_);offscreen_fbo_=0;}
        if(offscreen_texture_){glDeleteTextures(1,&offscreen_texture_);offscreen_texture_=0;}
        if(rc_){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(rc_);rc_=nullptr;}
        if(hwnd_&&dc_){ReleaseDC(hwnd_,dc_);dc_=nullptr;}
        if(hwnd_){DestroyWindow(hwnd_);hwnd_=nullptr;}
#endif
        ready_=false;
    }

    bool Ready() const { return ready_; }
    bool Closed() const { return closed_; }
    u64 PresentCount() const { return present_count_; }

    bool PopTouchEvent(HostTouchEvent& out) {
        if(touch_events_.empty())return false;
        out=touch_events_.front();
        touch_events_.erase(touch_events_.begin());
        return true;
    }

    bool PumpMessages() {
#ifdef _WIN32
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT){closed_=true;return false;}
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
#endif
        return !closed_;
    }

    void Present() {
#ifdef _WIN32
        if(!dc_||!rc_)return;
        if(!offscreen_fbo_||!offscreen_texture_||!glBindFramebuffer_) {
            SwapBuffers(dc_);++present_count_;return;
        }

        // The guest renders exactly like an old iPhone: portrait 320x480
        // renderbuffer, scaled here to 480x720. Landscape apps rotate their
        // scene inside that portrait surface. A physical iPhone then rotates
        // the display itself; Windows does not, so rotate the final renderbuffer
        // counter-clockwise here instead of distorting every guest GL call.
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        GLint previous_matrix_mode=GL_MODELVIEW;
        glGetIntegerv(GL_MATRIX_MODE,&previous_matrix_mode);

        glBindFramebuffer_(kGlFramebuffer,0);
        RECT client{};
        GetClientRect(hwnd_,&client);
        const int client_w=static_cast<int>(std::max<LONG>(1,client.right-client.left));
        const int client_h=static_cast<int>(std::max<LONG>(1,client.bottom-client.top));

        // Native Forlorn content is 480x320 = 3:2. Fit it into the 16:9
        // wrapper window without stretching, leaving black pillarboxes.
        constexpr double content_aspect=1.5;
        int view_w=client_w;
        int view_h=static_cast<int>(double(view_w)/content_aspect+0.5);
        if(view_h>client_h){
            view_h=client_h;
            view_w=static_cast<int>(double(view_h)*content_aspect+0.5);
        }
        const int view_x=(client_w-view_w)/2;
        const int view_y=(client_h-view_h)/2;

        glViewport(0,0,client_w,client_h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glClearColor(0.0f,0.0f,0.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(view_x,view_y,view_w,view_h);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0,1.0,0.0,1.0,-1.0,1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,offscreen_texture_);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glColor4f(1.0f,1.0f,1.0f,1.0f);

        // Rotate CCW 90 degrees: source top-left becomes destination bottom-left.
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f,1.0f); glVertex2f(0.0f,0.0f);
        glTexCoord2f(0.0f,0.0f); glVertex2f(1.0f,0.0f);
        glTexCoord2f(1.0f,0.0f); glVertex2f(1.0f,1.0f);
        glTexCoord2f(1.0f,1.0f); glVertex2f(0.0f,1.0f);
        glEnd();

        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(previous_matrix_mode);
        glPopClientAttrib();
        glPopAttrib();

        SwapBuffers(dc_);
        ++present_count_;

        // FBO binding is not part of the legacy attribute stack.
        glBindFramebuffer_(kGlFramebuffer,offscreen_fbo_);
#endif
    }

    void Pace60() {
#ifdef _WIN32
        if(!ready_)return;
        const auto target=frame_clock_+std::chrono::microseconds(16667);
        const auto now=std::chrono::steady_clock::now();
        if(now<target)std::this_thread::sleep_for(target-now);
        frame_clock_=std::chrono::steady_clock::now();
#endif
    }

#ifdef _WIN32
    using PFNGLACTIVETEXTUREPROC_ = void (APIENTRY*)(GLenum);
    using PFNGLGENBUFFERSPROC_ = void (APIENTRY*)(GLsizei,GLuint*);
    using PFNGLBINDBUFFERPROC_ = void (APIENTRY*)(GLenum,GLuint);
    using PFNGLBUFFERDATAPROC_ = void (APIENTRY*)(GLenum,std::ptrdiff_t,const void*,GLenum);
    using PFNGLBUFFERSUBDATAPROC_ = void (APIENTRY*)(GLenum,std::ptrdiff_t,std::ptrdiff_t,const void*);
    using PFNGLDELETEBUFFERSPROC_ = void (APIENTRY*)(GLsizei,const GLuint*);
    using PFNGLGENERATEMIPMAPPROC_ = void (APIENTRY*)(GLenum);
    using PFNGLGENFRAMEBUFFERSPROC_ = void (APIENTRY*)(GLsizei,GLuint*);
    using PFNGLBINDFRAMEBUFFERPROC_ = void (APIENTRY*)(GLenum,GLuint);
    using PFNGLDELETEFRAMEBUFFERSPROC_ = void (APIENTRY*)(GLsizei,const GLuint*);
    using PFNGLFRAMEBUFFERTEXTURE2DPROC_ = void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint);
    using PFNGLCHECKFRAMEBUFFERSTATUSPROC_ = GLenum (APIENTRY*)(GLenum);
    using PFNGLGENRENDERBUFFERSPROC_ = void (APIENTRY*)(GLsizei,GLuint*);
    using PFNGLBINDRENDERBUFFERPROC_ = void (APIENTRY*)(GLenum,GLuint);
    using PFNGLDELETERENDERBUFFERSPROC_ = void (APIENTRY*)(GLsizei,const GLuint*);
    using PFNGLRENDERBUFFERSTORAGEPROC_ = void (APIENTRY*)(GLenum,GLenum,GLsizei,GLsizei);
    using PFNGLFRAMEBUFFERRENDERBUFFERPROC_ = void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint);

    PFNGLACTIVETEXTUREPROC_ glActiveTexture_ = nullptr;
    PFNGLGENBUFFERSPROC_ glGenBuffers_ = nullptr;
    PFNGLBINDBUFFERPROC_ glBindBuffer_ = nullptr;
    PFNGLBUFFERDATAPROC_ glBufferData_ = nullptr;
    PFNGLBUFFERSUBDATAPROC_ glBufferSubData_ = nullptr;
    PFNGLDELETEBUFFERSPROC_ glDeleteBuffers_ = nullptr;
    PFNGLGENERATEMIPMAPPROC_ glGenerateMipmap_ = nullptr;
    PFNGLGENFRAMEBUFFERSPROC_ glGenFramebuffers_ = nullptr;
    PFNGLBINDFRAMEBUFFERPROC_ glBindFramebuffer_ = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC_ glDeleteFramebuffers_ = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC_ glFramebufferTexture2D_ = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC_ glCheckFramebufferStatus_ = nullptr;
    PFNGLGENRENDERBUFFERSPROC_ glGenRenderbuffers_ = nullptr;
    PFNGLBINDRENDERBUFFERPROC_ glBindRenderbuffer_ = nullptr;
    PFNGLDELETERENDERBUFFERSPROC_ glDeleteRenderbuffers_ = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC_ glRenderbufferStorage_ = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC_ glFramebufferRenderbuffer_ = nullptr;
#endif

private:
#ifdef _WIN32
    template<class T>
    static T LoadGlProc(const char* name,const char* fallback=nullptr){
        PROC p=wglGetProcAddress(name);
        if((!p||p==(PROC)1||p==(PROC)2||p==(PROC)3||p==(PROC)-1)&&fallback)p=wglGetProcAddress(fallback);
        return reinterpret_cast<T>(p);
    }
    void LoadExtensions(){
        glActiveTexture_=LoadGlProc<PFNGLACTIVETEXTUREPROC_>("glActiveTexture","glActiveTextureARB");
        glGenBuffers_=LoadGlProc<PFNGLGENBUFFERSPROC_>("glGenBuffers","glGenBuffersARB");
        glBindBuffer_=LoadGlProc<PFNGLBINDBUFFERPROC_>("glBindBuffer","glBindBufferARB");
        glBufferData_=LoadGlProc<PFNGLBUFFERDATAPROC_>("glBufferData","glBufferDataARB");
        glBufferSubData_=LoadGlProc<PFNGLBUFFERSUBDATAPROC_>("glBufferSubData","glBufferSubDataARB");
        glDeleteBuffers_=LoadGlProc<PFNGLDELETEBUFFERSPROC_>("glDeleteBuffers","glDeleteBuffersARB");
        glGenerateMipmap_=LoadGlProc<PFNGLGENERATEMIPMAPPROC_>("glGenerateMipmap","glGenerateMipmapEXT");
        glGenFramebuffers_=LoadGlProc<PFNGLGENFRAMEBUFFERSPROC_>("glGenFramebuffers","glGenFramebuffersEXT");
        glBindFramebuffer_=LoadGlProc<PFNGLBINDFRAMEBUFFERPROC_>("glBindFramebuffer","glBindFramebufferEXT");
        glDeleteFramebuffers_=LoadGlProc<PFNGLDELETEFRAMEBUFFERSPROC_>("glDeleteFramebuffers","glDeleteFramebuffersEXT");
        glFramebufferTexture2D_=LoadGlProc<PFNGLFRAMEBUFFERTEXTURE2DPROC_>("glFramebufferTexture2D","glFramebufferTexture2DEXT");
        glCheckFramebufferStatus_=LoadGlProc<PFNGLCHECKFRAMEBUFFERSTATUSPROC_>("glCheckFramebufferStatus","glCheckFramebufferStatusEXT");
        glGenRenderbuffers_=LoadGlProc<PFNGLGENRENDERBUFFERSPROC_>("glGenRenderbuffers","glGenRenderbuffersEXT");
        glBindRenderbuffer_=LoadGlProc<PFNGLBINDRENDERBUFFERPROC_>("glBindRenderbuffer","glBindRenderbufferEXT");
        glDeleteRenderbuffers_=LoadGlProc<PFNGLDELETERENDERBUFFERSPROC_>("glDeleteRenderbuffers","glDeleteRenderbuffersEXT");
        glRenderbufferStorage_=LoadGlProc<PFNGLRENDERBUFFERSTORAGEPROC_>("glRenderbufferStorage","glRenderbufferStorageEXT");
        glFramebufferRenderbuffer_=LoadGlProc<PFNGLFRAMEBUFFERRENDERBUFFERPROC_>("glFramebufferRenderbuffer","glFramebufferRenderbufferEXT");
    }

    bool CreatePortraitFramebuffer(Logger& log){
        if(!glGenFramebuffers_||!glBindFramebuffer_||!glDeleteFramebuffers_||
           !glFramebufferTexture2D_||!glCheckFramebufferStatus_||
           !glGenRenderbuffers_||!glBindRenderbuffer_||!glDeleteRenderbuffers_||
           !glRenderbufferStorage_||!glFramebufferRenderbuffer_){
            log<<"IOS HOSTGL: framebuffer-object extension unavailable; cannot rotate iOS portrait surface safely\n";
            return false;
        }

        glGenTextures(1,&offscreen_texture_);
        glBindTexture(GL_TEXTURE_2D,offscreen_texture_);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,kPortraitWidth,kPortraitHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);

        glGenFramebuffers_(1,&offscreen_fbo_);
        glBindFramebuffer_(kGlFramebuffer,offscreen_fbo_);
        glFramebufferTexture2D_(kGlFramebuffer,kGlColorAttachment0,GL_TEXTURE_2D,offscreen_texture_,0);

        glGenRenderbuffers_(1,&offscreen_depth_);
        glBindRenderbuffer_(kGlRenderbuffer,offscreen_depth_);
        glRenderbufferStorage_(kGlRenderbuffer,kGlDepthComponent24,kPortraitWidth,kPortraitHeight);
        glFramebufferRenderbuffer_(kGlFramebuffer,kGlDepthAttachment,kGlRenderbuffer,offscreen_depth_);

        const GLenum status=glCheckFramebufferStatus_(kGlFramebuffer);
        if(status!=kGlFramebufferComplete){
            log<<"IOS HOSTGL: portrait framebuffer incomplete status=0x"<<std::hex<<status<<std::dec<<"\n";
            return false;
        }
        glBindTexture(GL_TEXTURE_2D,0);
        return true;
    }
    bool ClientToGuestTouch(LPARAM lp,float& gx,float& gy,bool clamp_to_content=false) const {
        if(!hwnd_)return false;
        RECT client{};GetClientRect(hwnd_,&client);
        const int client_w=static_cast<int>(std::max<LONG>(1,client.right-client.left));
        const int client_h=static_cast<int>(std::max<LONG>(1,client.bottom-client.top));
        constexpr double content_aspect=1.5;
        int view_w=client_w;
        int view_h=static_cast<int>(double(view_w)/content_aspect+0.5);
        if(view_h>client_h){view_h=client_h;view_w=static_cast<int>(double(view_h)*content_aspect+0.5);}
        const int view_x=(client_w-view_w)/2;
        const int view_y=(client_h-view_h)/2;
        int x=static_cast<int>(static_cast<short>(LOWORD(lp)));
        int y=static_cast<int>(static_cast<short>(HIWORD(lp)));
        if(!clamp_to_content&&(x<view_x||x>=view_x+view_w||y<view_y||y>=view_y+view_h))return false;
        x=std::clamp(x,view_x,view_x+view_w-1);
        y=std::clamp(y,view_y,view_y+view_h-1);
        const float lx=float(x-view_x)/float(std::max(1,view_w));
        const float ly=float(y-view_y)/float(std::max(1,view_h));
        // Inverse of the final CCW90 presentation:
        // 480x320 landscape top-left -> 320x480 UIKit portrait coordinates.
        gx=(1.0f-ly)*320.0f;
        gy=lx*480.0f;
        return true;
    }
    void QueueTouch(u32 phase,LPARAM lp,bool clamp_to_content=false){
        float x=0.0f,y=0.0f;
        if(!ClientToGuestTouch(lp,x,y,clamp_to_content))return;
        last_touch_x_=x;last_touch_y_=y;
        if(touch_events_.size()>=64u)touch_events_.erase(touch_events_.begin());
        touch_events_.push_back(HostTouchEvent{phase,x,y});
    }

    static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        HostOpenGLWindow* self=reinterpret_cast<HostOpenGLWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){
            auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
            self=reinterpret_cast<HostOpenGLWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        switch(msg){
            case WM_LBUTTONDOWN:
                if(self){
                    self->mouse_down_=true;
                    SetFocus(hwnd);SetCapture(hwnd);
                    self->QueueTouch(0u,lp,false);
                }
                return 0;
            case WM_MOUSEMOVE:
                if(self&&self->mouse_down_)self->QueueTouch(1u,lp,true);
                return 0;
            case WM_LBUTTONUP:
                if(self&&self->mouse_down_){
                    self->QueueTouch(3u,lp,true);
                    self->mouse_down_=false;
                    if(GetCapture()==hwnd)ReleaseCapture();
                }
                return 0;
            case WM_CAPTURECHANGED:
                if(self&&self->mouse_down_){
                    self->mouse_down_=false;
                    if(self->touch_events_.size()>=64u)self->touch_events_.erase(self->touch_events_.begin());
                    self->touch_events_.push_back(HostTouchEvent{4u,self->last_touch_x_,self->last_touch_y_});
                }
                return 0;
            case WM_CLOSE:
                if(self)self->closed_=true;
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                if(self){self->closed_=true;self->hwnd_=nullptr;}
                PostQuitMessage(0);
                return 0;
            default: return DefWindowProcW(hwnd,msg,wp,lp);
        }
    }
    static constexpr GLsizei kPortraitWidth=320;
    static constexpr GLsizei kPortraitHeight=480;
    static constexpr GLenum kGlFramebuffer=0x8d40u;
    static constexpr GLenum kGlRenderbuffer=0x8d41u;
    static constexpr GLenum kGlColorAttachment0=0x8ce0u;
    static constexpr GLenum kGlDepthAttachment=0x8d00u;
    static constexpr GLenum kGlDepthComponent24=0x81a6u;
    static constexpr GLenum kGlFramebufferComplete=0x8cd5u;

    HINSTANCE instance_=nullptr;
    HWND hwnd_=nullptr;
    HDC dc_=nullptr;
    HGLRC rc_=nullptr;
    GLuint offscreen_texture_=0;
    GLuint offscreen_fbo_=0;
    GLuint offscreen_depth_=0;
#endif
    bool ready_=false;
    bool closed_=false;
    bool mouse_down_=false;
    float last_touch_x_=0.0f,last_touch_y_=0.0f;
    std::vector<HostTouchEvent> touch_events_;
    u64 present_count_=0;
    std::chrono::steady_clock::time_point frame_clock_{};
};

class IosBootstrap {
public:
    IosBootstrap(MachImage image, Logger& log, const std::vector<u8>& ipa, std::string app_root)
        : image_(std::move(image)), log_(log), ipa_(&ipa), app_root_(std::move(app_root)), monitor_(1), cpu_(MakeConfig()) {
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
        BuildAssetIndex();
        ParseGuestClasses();
        ParseGuestCategories();
        log_ << "IOS: imports bound=" << imports_.size() << " guest-objc-classes=" << classes_.size()
             << " guest-objc-category-methods=" << guest_category_method_count_
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
                log_ << (frame_pump_active_?"RESULT: IOS_FRAME_MEMORY_FAULT address=0x":(delegate_launch_active_?"RESULT: IOS_DELEGATE_MEMORY_FAULT address=0x":"RESULT: IOS_BOOTSTRAP_MEMORY_FAULT address=0x")) << Hex(env_.fault_address)
                     << " pc=0x" << Hex(cpu_.Regs()[15]) << " lr=0x" << Hex(cpu_.Regs()[14]) << "\n";
                if(frame_pump_active_&&cpu_.Regs()[15]>=0x1000u&&cpu_.Regs()[15]<0x2000u)
                    log_<<"IOS: frame fault entered Mach-O header page; likely invalid cached IMP/function callback\n";
                return false;
            }
            if (env_.interpreter_fallback) {
                log_ << (frame_pump_active_?"RESULT: IOS_FRAME_INTERPRETER_FALLBACK pc=0x":(delegate_launch_active_?"RESULT: IOS_DELEGATE_INTERPRETER_FALLBACK pc=0x":"RESULT: IOS_BOOTSTRAP_INTERPRETER_FALLBACK pc=0x")) << Hex(env_.fallback_pc)
                     << " count=" << env_.fallback_count << "\n";
                return false;
            }
            if (env_.exception_seen) {
                log_ << (frame_pump_active_?"RESULT: IOS_FRAME_EXCEPTION pc=0x":(delegate_launch_active_?"RESULT: IOS_DELEGATE_EXCEPTION pc=0x":"RESULT: IOS_BOOTSTRAP_EXCEPTION pc=0x")) << Hex(env_.exception_pc) << "\n";
                return false;
            }
            if (env_.svc_pending) {
                if (!HandleSvc(env_.pending_svc)) return false;
                continue;
            }
        }
        if(frame_probe_completed_){
            log_<<(host_window_closed_?"RESULT: IOS_HOST_WINDOW_CLOSED":"RESULT: IOS_HOST_OPENGL_PROBE_OK")
                <<" frames="<<frame_count_
                <<" director=0x"<<Hex(director_instance_)
                <<" running-scene=0x"<<Hex(running_scene_)
                <<" running-scene-class="<<(FindGuestClassForInstance(running_scene_)?FindGuestClassForInstance(running_scene_)->name:(running_scene_?"unknown":"nil"))
                <<" presents="<<host_window_.PresentCount()
                <<" placeholder-textures="<<placeholder_texture_uploads_<<" real-asset-draws="<<real_asset_draws_<<" touches="<<touch_dispatch_count_
                <<" unknown-imports="<<unknown_import_count_
                <<" objc-stubs="<<unimplemented_objc_count_
                <<" category-methods="<<guest_category_method_count_<<"\n";
            log_<<"Execution status: PublicTest13 runs the real Forlorn cocos2d loop with IPA-backed stdio/resource loading until the user closes the window.\n";
            return true;
        }
        if(delegate_launch_returned_){
            log_<<"RESULT: IOS_DELEGATE_LAUNCH_RETURNED_NO_FRAME_PUMP delegate="<<(delegate_name_.empty()?"unknown":delegate_name_)<<" r0=0x"<<Hex(delegate_return_value_)<<" unknown-imports="<<unknown_import_count_<<" objc-stubs="<<unimplemented_objc_count_<<"\n";
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
    bool WriteCGPoint(u32 dest,float x,float y){
        if(!dest||!env_.IsMapped(dest,8u))return false;
        const std::array<float,2> point{x,y};
        return env_.WriteBytes(dest,point.data(),sizeof(point));
    }
    void WriteGeneratedIds(u32 count,u32 ptr){
        if(!ptr||!count||count>4096u||!env_.IsMapped(ptr,std::size_t(count)*4u))return;
        for(u32 i=0;i<count;++i)env_.MemoryWrite32(ptr+i*4u,gl_object_counter_++);
    }

    void BuildAssetIndex(){
        if(!ipa_)return;
        try{
            for(const auto& e:ListZip(*ipa_)){
                if(!e.name.starts_with(app_root_)||e.name.size()<=app_root_.size())continue;
                const std::string rel=e.name.substr(app_root_.size());
                if(rel.empty()||rel.back()=='/'||e.uncompressed_size>64u*1024u*1024u)continue;
                const std::size_t index=asset_entries_.size();
                asset_entries_.push_back(e);
                asset_relative_.push_back(rel);
                auto add=[&](std::string key){key=LowerAscii(std::move(key));if(!key.empty()&&!asset_lookup_.count(key))asset_lookup_[key]=index;};
                add(rel);
                const auto slash=rel.find_last_of('/');
                add(slash==std::string::npos?rel:rel.substr(slash+1u));
            }
            log_<<"IOS ASSET: indexed "<<asset_entries_.size()<<" app resources from "<<app_root_<<"\n";
        }catch(const std::exception& e){
            log_<<"IOS ASSET: index failed error="<<e.what()<<"\n";
        }
    }
    std::string NormalizeAssetRequest(std::string request)const{
        std::replace(request.begin(),request.end(),'\\','/');
        if(request.starts_with("ipa://"))request.erase(0,6u);
        const auto app=request.find(".app/");
        if(app!=std::string::npos)request=request.substr(app+5u);
        while(!request.empty()&&(request.front()=='/'||request.front()=='.'))request.erase(request.begin());
        return request;
    }
    std::string ResolveAssetRelative(std::string request)const{
        request=NormalizeAssetRequest(std::move(request));
        if(request.empty())return {};
        auto find_key=[&](const std::string& key)->std::string{
            auto it=asset_lookup_.find(LowerAscii(key));
            return it==asset_lookup_.end()?std::string{}:asset_relative_[it->second];
        };
        if(auto v=find_key(request);!v.empty())return v;
        const auto slash=request.find_last_of('/');
        if(slash!=std::string::npos)if(auto v=find_key(request.substr(slash+1u));!v.empty())return v;
        return {};
    }
    const DecodedPng* DecodeAsset(std::string relative){
        relative=ResolveAssetRelative(std::move(relative));
        if(relative.empty()||!ipa_)return nullptr;
        auto cached=decoded_assets_.find(relative);
        if(cached!=decoded_assets_.end())return &cached->second;
        auto it=asset_lookup_.find(LowerAscii(relative));
        if(it==asset_lookup_.end())return nullptr;
        try{
            const auto bytes=ExtractZip(*ipa_,asset_entries_[it->second]);
            DecodedPng decoded;
            if(!DecodeIosPngRgba(bytes,decoded))return nullptr;
            auto [pos,ok]=decoded_assets_.emplace(relative,std::move(decoded));
            if(ok&&++asset_decode_logs_<=16u)
                log_<<"IOS ASSET: decoded "<<relative<<" "<<pos->second.width<<"x"<<pos->second.height<<" RGBA\n";
            return &pos->second;
        }catch(const std::exception& e){
            if(++asset_failure_logs_<=8u)log_<<"IOS ASSET: decode failed "<<relative<<" error="<<e.what()<<"\n";
            return nullptr;
        }
    }
    u32 NewImageForAsset(const std::string& request){
        const std::string rel=ResolveAssetRelative(request);
        const u32 image=NewExternalInstance("UIImage");
        fake_objects_[image].resource_value=rel;
        if(!rel.empty()&&++asset_resolve_logs_<=16u)log_<<"IOS ASSET: UIImage "<<request<<" -> "<<rel<<"\n";
        return image;
    }
    struct VirtualFile {
        std::string relative;
        std::vector<u8> bytes;
        std::size_t pos=0;
        bool gzip=false;
    };
    bool ReadAssetBytes(std::string request,std::string& relative,std::vector<u8>& bytes){
        relative=ResolveAssetRelative(std::move(request));
        if(relative.empty()||!ipa_)return false;
        auto it=asset_lookup_.find(LowerAscii(relative));
        if(it==asset_lookup_.end())return false;
        try{bytes=ExtractZip(*ipa_,asset_entries_[it->second]);return true;}
        catch(const std::exception& e){
            if(++asset_failure_logs_<=8u)log_<<"IOS FILE: extract failed "<<relative<<" error="<<e.what()<<"\\n";
            return false;
        }
    }
    bool InflateGzip(const std::vector<u8>& compressed,std::vector<u8>& output){
        z_stream stream{};
        stream.next_in=const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
        stream.avail_in=static_cast<uInt>(compressed.size());
        if(inflateInit2(&stream,15+32)!=Z_OK)return false;
        std::array<u8,32768> chunk{};
        int rc=Z_OK;
        while(rc==Z_OK){
            stream.next_out=reinterpret_cast<Bytef*>(chunk.data());
            stream.avail_out=static_cast<uInt>(chunk.size());
            rc=inflate(&stream,Z_NO_FLUSH);
            const std::size_t produced=chunk.size()-stream.avail_out;
            if(produced)output.insert(output.end(),chunk.data(),chunk.data()+produced);
            if(output.size()>128u*1024u*1024u){inflateEnd(&stream);return false;}
        }
        inflateEnd(&stream);
        return rc==Z_STREAM_END;
    }
    u32 OpenVirtualAsset(std::string request,bool gzip){
        std::string relative;std::vector<u8> bytes;
        if(!ReadAssetBytes(std::move(request),relative,bytes))return 0u;
        if(gzip){std::vector<u8> decoded;if(!InflateGzip(bytes,decoded))return 0u;bytes=std::move(decoded);}
        const u32 handle=AllocateObjectBytes(16u);
        virtual_files_[handle]=VirtualFile{relative,std::move(bytes),0u,gzip};
        if(++file_open_logs_<=32u)log_<<"IOS FILE: open "<<relative<<" bytes="<<virtual_files_[handle].bytes.size()<<" handle=0x"<<Hex(handle)<<(gzip?" gzip":"")<<"\\n";
        return handle;
    }
    VirtualFile* FindVirtualFile(u32 handle){auto it=virtual_files_.find(handle);return it==virtual_files_.end()?nullptr:&it->second;}
    u32 ReadVirtualFile(u32 handle,u32 dest,u32 bytes_requested){
        VirtualFile* file=FindVirtualFile(handle);
        if(!file||!dest||!bytes_requested)return 0u;
        const std::size_t remaining=file->pos<file->bytes.size()?file->bytes.size()-file->pos:0u;
        const std::size_t amount=std::min<std::size_t>(remaining,bytes_requested);
        if(amount&&!env_.IsMapped(dest,amount))return 0u;
        if(amount)env_.WriteBytes(dest,file->bytes.data()+file->pos,amount);
        file->pos+=amount;
        return static_cast<u32>(amount);
    }
    u32 NewFakeNumber(double value){
        const u32 obj=NewExternalInstance("NSNumber");
        fake_numbers_[obj]=value;return obj;
    }
    u32 MakeFakePlistObject(const PlistValue& value){
        switch(value.kind){
        case PlistValue::Kind::Null:return 0u;
        case PlistValue::Kind::Boolean:return NewFakeNumber(value.boolean?1.0:0.0);
        case PlistValue::Kind::Integer:return NewFakeNumber(static_cast<double>(value.integer));
        case PlistValue::Kind::Real:return NewFakeNumber(value.real);
        case PlistValue::Kind::String:return NewFakeString(value.string);
        case PlistValue::Kind::Array:{
            const u32 obj=NewExternalInstance("NSArray");
            auto& values=fake_collections_[obj];values.reserve(value.array.size());
            for(const auto& child:value.array)values.push_back(MakeFakePlistObject(child));
            return obj;
        }
        case PlistValue::Kind::Dict:{
            const u32 obj=NewExternalInstance("NSDictionary");
            auto& dict=fake_dictionaries_[obj];
            auto& keys=fake_dictionary_keys_[obj];
            for(std::size_t i=0;i<value.dict_keys.size()&&i<value.dict_values.size();++i){
                const std::string& key=value.dict_keys[i];
                const u32 child_obj=MakeFakePlistObject(value.dict_values[i]);
                dict[key]=child_obj;keys.push_back(NewFakeString(key));
            }
            return obj;
        }}
        return 0u;
    }
    u32 LoadFakePlist(std::string request){
        const std::string relative=ResolveAssetRelative(request);
        if(relative.empty())return 0u;
        auto cached=plist_roots_.find(relative);
        if(cached!=plist_roots_.end())return cached->second;
        std::string resolved;std::vector<u8> bytes;
        if(!ReadAssetBytes(relative,resolved,bytes))return 0u;
        PlistValue root;BinaryPlistParser parser(bytes);
        if(!parser.Parse(root)||root.kind!=PlistValue::Kind::Dict){
            if(++plist_failure_logs_<=8u)log_<<"IOS PLIST: parse failed "<<relative<<"\n";
            return 0u;
        }
        const u32 obj=MakeFakePlistObject(root);plist_roots_[relative]=obj;
        const auto dit=fake_dictionaries_.find(obj);
        if(++plist_load_logs_<=24u)log_<<"IOS PLIST: loaded "<<relative<<" root-keys="<<(dit==fake_dictionaries_.end()?0u:dit->second.size())<<"\n";
        return obj;
    }
    const std::vector<u32>* FastEnumerationValues(u32 receiver,std::vector<u32>& scratch){
        auto cit=fake_collections_.find(receiver);
        if(cit!=fake_collections_.end())return &cit->second;
        auto kit=fake_dictionary_keys_.find(receiver);
        if(kit!=fake_dictionary_keys_.end())return &kit->second;
        return nullptr;
    }
    bool HandleFastEnumeration(u32 receiver){
        const u32 state=cpu_.Regs()[2],buffer=cpu_.Regs()[3],capacity=StackArg(0);
        if(!state||!buffer||!capacity||capacity>4096u||!env_.IsMapped(state,32u)||!env_.IsMapped(buffer,std::size_t(capacity)*4u)){cpu_.Regs()[0]=0u;return true;}
        std::vector<u32> scratch;
        const std::vector<u32>* values=FastEnumerationValues(receiver,scratch);
        if(!values){cpu_.Regs()[0]=0u;return true;}
        const u32 start=env_.MemoryRead32(state);
        if(start>=values->size()){cpu_.Regs()[0]=0u;return true;}
        const u32 count=std::min<u32>(capacity,static_cast<u32>(values->size()-start));
        env_.WriteBytes(buffer,values->data()+start,std::size_t(count)*4u);
        if(!fast_enum_mutation_addr_){fast_enum_mutation_addr_=Allocate(4u,4u);env_.MemoryWrite32(fast_enum_mutation_addr_,1u);}
        env_.MemoryWrite32(state,start+count);
        env_.MemoryWrite32(state+4u,buffer);
        env_.MemoryWrite32(state+8u,fast_enum_mutation_addr_);
        cpu_.Regs()[0]=count;return true;
    }

    u32 EnsureFloatData(const std::string& symbol,std::initializer_list<float> values){
        auto it=data_symbols_.find(symbol);if(it!=data_symbols_.end())return it->second;
        const u32 addr=Allocate(static_cast<u32>(values.size()*sizeof(float)),4u);
        std::vector<float> data(values);
        env_.WriteBytes(addr,data.data(),data.size()*sizeof(float));
        data_symbols_[symbol]=addr;return addr;
    }
    struct Affine { float a=1.0f,b=0.0f,c=0.0f,d=1.0f,tx=0.0f,ty=0.0f; };
    Affine ReadAffineCallArg(){
        return Affine{FloatFromBits(cpu_.Regs()[1]),FloatFromBits(cpu_.Regs()[2]),FloatFromBits(cpu_.Regs()[3]),
                      FloatFromBits(StackArg(0)),FloatFromBits(StackArg(1)),FloatFromBits(StackArg(2))};
    }
    bool WriteAffine(u32 dest,const Affine& t){
        const std::array<float,6> v{t.a,t.b,t.c,t.d,t.tx,t.ty};
        return dest&&env_.IsMapped(dest,sizeof(v))&&env_.WriteBytes(dest,v.data(),sizeof(v));
    }
    static Affine AffineConcat(const Affine& a,const Affine& b){
        return Affine{
            a.a*b.a+a.c*b.b,
            a.b*b.a+a.d*b.b,
            a.a*b.c+a.c*b.d,
            a.b*b.c+a.d*b.d,
            a.a*b.tx+a.c*b.ty+a.tx,
            a.b*b.tx+a.d*b.ty+a.ty
        };
    }

    u32 EnsureImport(const std::string& name){ auto it=import_by_name_.find(name); if(it!=import_by_name_.end())return imports_[it->second].stub; const std::size_t idx=imports_.size(); if((idx+1u)*8u>kImportSize)throw std::runtime_error("too many iOS imports"); Import imp{name,kImportBase+static_cast<u32>(idx*8u),static_cast<u32>(idx+1u),0}; WriteArmSvc(imp.stub,imp.svc); imports_.push_back(imp);import_by_name_[name]=idx;return imp.stub; }
    u32 ResolveSymbol(const std::string& symbol){
        constexpr std::string_view cls="_OBJC_CLASS_$_"; constexpr std::string_view meta="_OBJC_METACLASS_$_";
        if(symbol.starts_with(cls))return EnsureExternalClass(symbol.substr(cls.size()),false);
        if(symbol.starts_with(meta))return EnsureExternalClass(symbol.substr(meta.size()),true);
        if(symbol=="___CFConstantStringClassReference")return EnsureExternalClass("NSConstantString",false);
        if(symbol=="_CGAffineTransformIdentity")return EnsureFloatData(symbol,{1.0f,0.0f,0.0f,1.0f,0.0f,0.0f});
        if(symbol=="_CGPointZero"||symbol=="_CGSizeZero")return EnsureFloatData(symbol,{0.0f,0.0f});
        if(symbol=="_CGRectZero")return EnsureFloatData(symbol,{0.0f,0.0f,0.0f,0.0f});
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

    GuestClass* FindGuestClassByClassAddressMutable(u32 addr){for(auto& c:classes_)if(c.class_addr==addr||c.meta_addr==addr)return &c;return nullptr;}
    const GuestClass* FindGuestClassByName(std::string_view name)const{for(const auto& c:classes_)if(c.name==name)return &c;return nullptr;}
    const GuestClass* FindGuestClassByClassAddress(u32 addr)const{for(const auto& c:classes_)if(c.class_addr==addr||c.meta_addr==addr)return &c;return nullptr;}
    const GuestClass* FindGuestClassForInstance(u32 object)const{if(!object||!env_.IsMapped(object,4u))return nullptr;const u32 isa=const_cast<ProbeEnvironment&>(env_).MemoryRead32(object);for(const auto& c:classes_)if(c.class_addr==isa)return &c;return nullptr;}
    const GuestMethod* FindMethod(const std::vector<GuestMethod>& methods,std::string_view selector)const{for(const auto& m:methods)if(m.selector==selector)return &m;return nullptr;}
    const GuestMethod* FindInstanceMethodRecursive(const GuestClass* cls,std::string_view selector)const{
        std::set<u32> seen;
        while(cls&&seen.insert(cls->class_addr).second){
            if(const GuestMethod* m=FindMethod(cls->instance_methods,selector))return m;
            cls=FindGuestClassByClassAddress(cls->superclass_addr);
        }
        return nullptr;
    }
    const GuestMethod* FindClassMethodRecursive(const GuestClass* cls,std::string_view selector)const{
        std::set<u32> seen;
        while(cls&&seen.insert(cls->class_addr).second){
            if(const GuestMethod* m=FindMethod(cls->class_methods,selector))return m;
            cls=FindGuestClassByClassAddress(cls->superclass_addr);
        }
        return nullptr;
    }
    void MergeCategoryMethods(std::vector<GuestMethod>& target,const std::vector<GuestMethod>& extra){
        for(const auto& m:extra){
            auto it=std::find_if(target.begin(),target.end(),[&](const GuestMethod& e){return e.selector==m.selector;});
            if(it!=target.end())*it=m; else target.push_back(m);
            ++guest_category_method_count_;
        }
    }
    void ParseGuestCategories(){
        const MachSection* s=FindSection("__objc_catlist");if(!s)return;
        for(u32 off=0;off+4u<=s->size;off+=4u){
            const u32 cat=env_.MemoryRead32(s->addr+off);
            if(!cat||!env_.IsMapped(cat,16u))continue;
            const u32 cls_addr=env_.MemoryRead32(cat+4u);
            GuestClass* cls=FindGuestClassByClassAddressMutable(cls_addr);
            if(!cls)continue;
            MergeCategoryMethods(cls->instance_methods,ParseMethods(env_.MemoryRead32(cat+8u)));
            MergeCategoryMethods(cls->class_methods,ParseMethods(env_.MemoryRead32(cat+12u)));
        }
    }
    u32 NewGuestInstance(const GuestClass& cls){const u32 bytes=std::max<u32>(cls.instance_size,4u);const u32 object=Allocate(bytes,8u);std::vector<u8> zero(bytes);env_.WriteBytes(object,zero.data(),zero.size());env_.MemoryWrite32(object,cls.class_addr);return object;}
    void EnterGuestMethod(const GuestMethod& method){cpu_.Regs()[15]=method.imp&~1u;cpu_.SetCpsr((cpu_.Cpsr()&~0x20u)|((method.imp&1u)?0x20u:0u));}
    std::string ResolveDelegateName(u32 object){const std::string candidate=DescribeString(object);if(!candidate.empty()&&FindGuestClassByName(candidate))return candidate;for(std::string_view preferred:{std::string_view("AppDelegate"),std::string_view("AppController")})if(FindGuestClassByName(preferred))return std::string(preferred);return {};}
    std::string SelectorName(u32 selector_addr){
        std::string selector;
        if(selector_addr)env_.ReadCString(selector_addr,selector,1024u);
        return selector;
    }
    u32 GuestMethodImpForSelector(const GuestClass* cls,bool class_method,u32 selector_addr){
        if(!cls||!selector_addr)return 0u;
        const std::string selector=SelectorName(selector_addr);
        if(selector.empty())return 0u;
        const GuestMethod* method=class_method
            ? FindClassMethodRecursive(cls,selector)
            : FindInstanceMethodRecursive(cls,selector);
        return method?method->imp:0u;
    }
    bool IsKindOfGuestClass(const GuestClass* cls,u32 wanted)const{
        std::set<u32> seen;
        while(cls&&seen.insert(cls->class_addr).second){
            if(cls->class_addr==wanted||cls->meta_addr==wanted)return true;
            cls=FindGuestClassByClassAddress(cls->superclass_addr);
        }
        return false;
    }
    bool ShouldTraceSceneMessage(std::string_view class_name,std::string_view selector)const{
        (void)class_name;
        return selector=="node"||selector=="runWithScene:"||selector=="replaceScene:"||
               selector=="pushScene:"||selector=="popScene"||selector=="runningScene"||
               selector=="schedule:"||selector=="scheduleUpdate"||selector=="methodForSelector:"||
               selector=="onEnter"||selector=="onEnterTransitionDidFinish"||selector=="loadingFinished"||selector=="loadManagers";
    }
    void TraceSceneMessage(std::string_view kind,std::string_view class_name,std::string_view selector,u32 value=0u){
        if(++scene_trace_count_<=128u)
            log_<<"IOS SCENE: "<<kind<<" class="<<class_name<<" selector="<<selector
                <<" value=0x"<<Hex(value)<<"\n";
    }

    u32 StackArg(u32 index) {
        const u32 sp=cpu_.Regs()[13];
        return (sp&&env_.IsMapped(sp+index*4u,4u))?const_cast<ProbeEnvironment&>(env_).MemoryRead32(sp+index*4u):0u;
    }
    bool EnsureHostGL(){
        if(host_window_.Ready())return true;
        if(host_window_attempted_)return false;
        host_window_attempted_=true;
        return host_window_.Initialize(log_);
    }
    const void* GuestGlPointer(u32 address,bool element=false) const {
#ifdef _WIN32
        const bool vbo=element ? (bound_element_array_buffer_!=0u) : (bound_array_buffer_!=0u);
        if(vbo)return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address));
#endif
        return env_.HostPointer(address,1u);
    }
    bool HandleHostGraphicsImport(const std::string& name){
#ifdef _WIN32
        if(!EnsureHostGL())return false;
        auto& r=cpu_.Regs();

        if(name=="glGetError"){r[0]=static_cast<u32>(glGetError());return true;}
        if(name=="glGetString"){
            const GLubyte* p=glGetString(static_cast<GLenum>(r[0]));
            std::string value=p?reinterpret_cast<const char*>(p):"";
            if(r[0]==0x1f03u)value+=" GL_OES_framebuffer_object GL_EXT_discard_framebuffer";
            r[0]=AllocateCString(value.empty()?"GeometryDashWrapper HostGL":value);
            return true;
        }
        if(name=="glGetIntegerv"){
            GLint values[16]{};
            glGetIntegerv(static_cast<GLenum>(r[0]),values);
            const u32 count=(r[0]==0x0ba2u||r[0]==0x0c10u)?4u:1u;
            if(r[1]&&env_.IsMapped(r[1],count*4u))env_.WriteBytes(r[1],values,count*4u);
            r[0]=0u;return true;
        }
        if(name=="glGetFloatv"){
            GLfloat values[16]{};
            glGetFloatv(static_cast<GLenum>(r[0]),values);
            const u32 count=(r[0]==0x0ba6u||r[0]==0x0ba7u||r[0]==0x0ba8u)?16u:1u;
            if(r[1]&&env_.IsMapped(r[1],count*4u))env_.WriteBytes(r[1],values,count*4u);
            r[0]=0u;return true;
        }
        if(name=="glIsEnabled"){r[0]=glIsEnabled(static_cast<GLenum>(r[0]))?1u:0u;return true;}

        if(name=="glClear"){glClear(static_cast<GLbitfield>(r[0]));r[0]=0u;return true;}
        if(name=="glClearColor"){glClearColor(FloatFromBits(r[0]),FloatFromBits(r[1]),FloatFromBits(r[2]),FloatFromBits(r[3]));r[0]=0u;return true;}
        if(name=="glClearDepthf"){glClearDepth(static_cast<GLclampd>(FloatFromBits(r[0])));r[0]=0u;return true;}
        if(name=="glEnable"){glEnable(static_cast<GLenum>(r[0]));r[0]=0u;return true;}
        if(name=="glDisable"){glDisable(static_cast<GLenum>(r[0]));r[0]=0u;return true;}
        if(name=="glBlendFunc"){glBlendFunc(static_cast<GLenum>(r[0]),static_cast<GLenum>(r[1]));r[0]=0u;return true;}
        if(name=="glDepthFunc"){glDepthFunc(static_cast<GLenum>(r[0]));r[0]=0u;return true;}
        if(name=="glAlphaFunc"){glAlphaFunc(static_cast<GLenum>(r[0]),FloatFromBits(r[1]));r[0]=0u;return true;}
        if(name=="glHint"){glHint(static_cast<GLenum>(r[0]),static_cast<GLenum>(r[1]));r[0]=0u;return true;}
        if(name=="glLineWidth"){glLineWidth(FloatFromBits(r[0]));r[0]=0u;return true;}
        if(name=="glPointSize"){glPointSize(FloatFromBits(r[0]));r[0]=0u;return true;}
        if(name=="glColor4f"){glColor4f(FloatFromBits(r[0]),FloatFromBits(r[1]),FloatFromBits(r[2]),FloatFromBits(r[3]));r[0]=0u;return true;}
        if(name=="glColor4ub"){glColor4ub(static_cast<GLubyte>(r[0]),static_cast<GLubyte>(r[1]),static_cast<GLubyte>(r[2]),static_cast<GLubyte>(r[3]));r[0]=0u;return true;}
        if(name=="glViewport"){glViewport(static_cast<GLint>(r[0]),static_cast<GLint>(r[1]),static_cast<GLsizei>(r[2]),static_cast<GLsizei>(r[3]));r[0]=0u;return true;}
        if(name=="glScissor"){glScissor(static_cast<GLint>(r[0]),static_cast<GLint>(r[1]),static_cast<GLsizei>(r[2]),static_cast<GLsizei>(r[3]));r[0]=0u;return true;}
        if(name=="glMatrixMode"){glMatrixMode(static_cast<GLenum>(r[0]));r[0]=0u;return true;}
        if(name=="glLoadIdentity"){glLoadIdentity();r[0]=0u;return true;}
        if(name=="glPushMatrix"){glPushMatrix();r[0]=0u;return true;}
        if(name=="glPopMatrix"){glPopMatrix();r[0]=0u;return true;}
        if(name=="glMultMatrixf"){
            const auto* p=static_cast<const GLfloat*>(env_.HostPointer(r[0],16u*sizeof(GLfloat)));
            if(p)glMultMatrixf(p);
            r[0]=0u;return true;
        }
        if(name=="glTranslatef"){glTranslatef(FloatFromBits(r[0]),FloatFromBits(r[1]),FloatFromBits(r[2]));r[0]=0u;return true;}
        if(name=="glRotatef"){glRotatef(FloatFromBits(r[0]),FloatFromBits(r[1]),FloatFromBits(r[2]),FloatFromBits(r[3]));r[0]=0u;return true;}
        if(name=="glFrustumf"){
            glFrustum(FloatFromBits(r[0]),FloatFromBits(r[1]),FloatFromBits(r[2]),FloatFromBits(r[3]),FloatFromBits(StackArg(0)),FloatFromBits(StackArg(1)));
            r[0]=0u;return true;
        }
        if(name=="glOrthof"){
            glOrtho(FloatFromBits(r[0]),FloatFromBits(r[1]),FloatFromBits(r[2]),FloatFromBits(r[3]),FloatFromBits(StackArg(0)),FloatFromBits(StackArg(1)));
            r[0]=0u;return true;
        }

        if(name=="glActiveTexture"){
            if(host_window_.glActiveTexture_)host_window_.glActiveTexture_(static_cast<GLenum>(r[0]));
            r[0]=0u;return true;
        }
        if(name=="glBindTexture"){glBindTexture(static_cast<GLenum>(r[0]),static_cast<GLuint>(r[1]));bound_texture_=r[1];r[0]=0u;return true;}
        if(name=="glGenTextures"){
            const u32 count=r[0],ptr=r[1];
            if(count&&count<4096u&&ptr&&env_.IsMapped(ptr,count*4u)){
                std::vector<GLuint> ids(count);glGenTextures(static_cast<GLsizei>(count),ids.data());env_.WriteBytes(ptr,ids.data(),count*4u);
            }
            r[0]=0u;return true;
        }
        if(name=="glDeleteTextures"){
            const u32 count=r[0],ptr=r[1];
            if(count&&count<4096u&&ptr&&env_.IsMapped(ptr,count*4u)){
                std::vector<GLuint> ids(count);env_.ReadBytes(ptr,ids.data(),count*4u);glDeleteTextures(static_cast<GLsizei>(count),ids.data());
            }
            r[0]=0u;return true;
        }
        if(name=="glTexParameteri"){glTexParameteri(static_cast<GLenum>(r[0]),static_cast<GLenum>(r[1]),static_cast<GLint>(r[2]));r[0]=0u;return true;}
        if(name=="glTexEnvi"){glTexEnvi(static_cast<GLenum>(r[0]),static_cast<GLenum>(r[1]),static_cast<GLint>(r[2]));r[0]=0u;return true;}
        if(name=="glPixelStorei"){glPixelStorei(static_cast<GLenum>(r[0]),static_cast<GLint>(r[1]));r[0]=0u;return true;}
        if(name=="glTexImage2D"){
            const GLsizei width=static_cast<GLsizei>(r[3]);
            const GLsizei height=static_cast<GLsizei>(StackArg(0));
            const GLint border=static_cast<GLint>(StackArg(1));
            const GLenum format=static_cast<GLenum>(StackArg(2));
            const GLenum type=static_cast<GLenum>(StackArg(3));
            const u32 pixels_addr=StackArg(4);
            const std::size_t approx=(width>0&&height>0&&width<16384&&height<16384)?std::size_t(width)*height*4u:1u;
            const void* pixels=pixels_addr?env_.HostPointer(pixels_addr,std::min<std::size_t>(approx,64u*1024u*1024u)):nullptr;
            glTexImage2D(static_cast<GLenum>(r[0]),static_cast<GLint>(r[1]),static_cast<GLint>(r[2]),width,height,border,format,type,pixels);
            if(++texture_upload_logs_<=24u)log_<<"IOS TEX: glTexImage2D bound="<<bound_texture_<<" "<<width<<"x"<<height<<" format=0x"<<Hex(format)<<" type=0x"<<Hex(type)<<" pixels=0x"<<Hex(pixels_addr)<<"\n";
            r[0]=0u;return true;
        }
        if(name=="glCompressedTexImage2D"){
            const GLint level=static_cast<GLint>(r[1]);
            const GLsizei width=static_cast<GLsizei>(r[3]);
            const GLsizei height=static_cast<GLsizei>(StackArg(0));
            const GLsizei safe_w=(width>0&&width<=4096)?width:4;
            const GLsizei safe_h=(height>0&&height<=4096)?height:4;
            std::vector<GLubyte> checker(std::size_t(safe_w)*safe_h*4u);
            for(GLsizei y=0;y<safe_h;++y)for(GLsizei x=0;x<safe_w;++x){
                const bool mag=(((x>>4)^(y>>4))&1)==0;const std::size_t o=(std::size_t(y)*safe_w+x)*4u;
                checker[o+0]=255u;checker[o+1]=mag?0u:255u;checker[o+2]=255u;checker[o+3]=255u;
            }
            glTexImage2D(static_cast<GLenum>(r[0]),level,GL_RGBA,safe_w,safe_h,0,GL_RGBA,GL_UNSIGNED_BYTE,checker.data());
            if(++placeholder_texture_uploads_<=8u)log_<<"IOS HOSTGL: substituted unsupported compressed texture with sized checker "<<safe_w<<"x"<<safe_h<<" level="<<level<<" bound="<<bound_texture_<<"\n";
            r[0]=0u;return true;
        }
        if(name=="glGenerateMipmapOES"){
            if(host_window_.glGenerateMipmap_)host_window_.glGenerateMipmap_(static_cast<GLenum>(r[0]));
            r[0]=0u;return true;
        }

        if(name=="glGenBuffers"){
            const u32 count=r[0],ptr=r[1];
            if(host_window_.glGenBuffers_&&count&&count<4096u&&ptr&&env_.IsMapped(ptr,count*4u)){
                std::vector<GLuint> ids(count);host_window_.glGenBuffers_(static_cast<GLsizei>(count),ids.data());env_.WriteBytes(ptr,ids.data(),count*4u);
            }else WriteGeneratedIds(count,ptr);
            r[0]=0u;return true;
        }
        if(name=="glDeleteBuffers"){
            const u32 count=r[0],ptr=r[1];
            if(host_window_.glDeleteBuffers_&&count&&count<4096u&&ptr&&env_.IsMapped(ptr,count*4u)){
                std::vector<GLuint> ids(count);env_.ReadBytes(ptr,ids.data(),count*4u);host_window_.glDeleteBuffers_(static_cast<GLsizei>(count),ids.data());
            }
            r[0]=0u;return true;
        }
        if(name=="glBindBuffer"){
            const GLenum target=static_cast<GLenum>(r[0]);const GLuint id=static_cast<GLuint>(r[1]);
            if(target==0x8892u)bound_array_buffer_=id;else if(target==0x8893u)bound_element_array_buffer_=id;
            if(host_window_.glBindBuffer_)host_window_.glBindBuffer_(target,id);
            r[0]=0u;return true;
        }
        if(name=="glBufferData"){
            if(host_window_.glBufferData_){
                const std::size_t size=r[1];
                const void* data=r[2]?env_.HostPointer(r[2],std::max<std::size_t>(size,1u)):nullptr;
                if(!r[2]||data)host_window_.glBufferData_(static_cast<GLenum>(r[0]),static_cast<std::ptrdiff_t>(size),data,static_cast<GLenum>(r[3]));
            }
            r[0]=0u;return true;
        }
        if(name=="glBufferSubData"){
            if(host_window_.glBufferSubData_){
                const std::size_t size=r[2];const void* data=r[3]?env_.HostPointer(r[3],std::max<std::size_t>(size,1u)):nullptr;
                if(!r[3]||data)host_window_.glBufferSubData_(static_cast<GLenum>(r[0]),static_cast<std::ptrdiff_t>(r[1]),static_cast<std::ptrdiff_t>(size),data);
            }
            r[0]=0u;return true;
        }

        if(name=="glEnableClientState"){glEnableClientState(static_cast<GLenum>(r[0]));r[0]=0u;return true;}
        if(name=="glDisableClientState"){glDisableClientState(static_cast<GLenum>(r[0]));r[0]=0u;return true;}
        if(name=="glVertexPointer"){
            const void* p=GuestGlPointer(r[3],false);
            if(p||bound_array_buffer_)glVertexPointer(static_cast<GLint>(r[0]),static_cast<GLenum>(r[1]),static_cast<GLsizei>(r[2]),p);
            r[0]=0u;return true;
        }
        if(name=="glTexCoordPointer"){
            const void* p=GuestGlPointer(r[3],false);
            if(p||bound_array_buffer_)glTexCoordPointer(static_cast<GLint>(r[0]),static_cast<GLenum>(r[1]),static_cast<GLsizei>(r[2]),p);
            r[0]=0u;return true;
        }
        if(name=="glColorPointer"){
            const void* p=GuestGlPointer(r[3],false);
            if(p||bound_array_buffer_)glColorPointer(static_cast<GLint>(r[0]),static_cast<GLenum>(r[1]),static_cast<GLsizei>(r[2]),p);
            r[0]=0u;return true;
        }
        if(name=="glPointSizePointerOES"){r[0]=0u;return true;}
        if(name=="glDrawArrays"){glDrawArrays(static_cast<GLenum>(r[0]),static_cast<GLint>(r[1]),static_cast<GLsizei>(r[2]));r[0]=0u;return true;}
        if(name=="glDrawElements"){
            const void* p=GuestGlPointer(r[3],true);
            if(p||bound_element_array_buffer_)glDrawElements(static_cast<GLenum>(r[0]),static_cast<GLsizei>(r[1]),static_cast<GLenum>(r[2]),p);
            r[0]=0u;return true;
        }

        // iOS renderbuffer/FBO objects are collapsed onto the Win32 default
        // framebuffer for the first visible-host test.
        if(name=="glGenFramebuffersOES"||name=="glGenRenderbuffersOES"){WriteGeneratedIds(r[0],r[1]);r[0]=0u;return true;}
        if(name=="glBindFramebufferOES"||name=="glBindRenderbufferOES"||name=="glFramebufferRenderbufferOES"||name=="glFramebufferTexture2DOES"||
           name=="glDeleteFramebuffersOES"||name=="glDeleteRenderbuffersOES"||name=="glRenderbufferStorageOES"||
           name=="glRenderbufferStorageMultisampleAPPLE"||name=="glResolveMultisampleFramebufferAPPLE"||name=="glDiscardFramebufferEXT"){
            r[0]=0u;return true;
        }
        if(name=="glCheckFramebufferStatusOES"){r[0]=0x8cd5u;return true;}
        if(name=="glGetRenderbufferParameterivOES"){
            const u32 pname=r[1],ptr=r[2];
            if(ptr&&env_.IsMapped(ptr,4u))env_.MemoryWrite32(ptr,pname==0x8d43u?480u:320u);
            r[0]=0u;return true;
        }
        if(name=="glReadPixels"){
            const GLsizei width=static_cast<GLsizei>(r[2]),height=static_cast<GLsizei>(r[3]);
            const GLenum format=static_cast<GLenum>(StackArg(0)),type=static_cast<GLenum>(StackArg(1));const u32 ptr=StackArg(2);
            const std::size_t size=(width>0&&height>0&&width<8192&&height<8192)?std::size_t(width)*height*4u:0u;
            void* out=(ptr&&size)?env_.HostPointerMutable(ptr,size):nullptr;
            if(out)glReadPixels(static_cast<GLint>(r[0]),static_cast<GLint>(r[1]),width,height,format,type,out);
            r[0]=0u;return true;
        }
        return false;
#else
        (void)name;
        return false;
#endif
    }

    void EnsureTouchObjects(){
        if(touch_object_)return;
        touch_object_=NewExternalInstance("UITouch");
        touch_set_=NewExternalInstance("NSSet");
        touch_event_=NewExternalInstance("UIEvent");
        fake_collections_[touch_set_]={touch_object_};
        fake_objects_[touch_event_].aux0=touch_set_;
    }
    bool BeginTouchDispatch(){
        if(!eagl_view_instance_)return false;
        HostTouchEvent event{};
        if(!host_window_.PopTouchEvent(event))return false;
        EnsureTouchObjects();
        previous_touch_x_=touch_x_;previous_touch_y_=touch_y_;
        touch_x_=event.x;touch_y_=event.y;touch_phase_=event.phase;
        const GuestClass* cls=FindGuestClassForInstance(eagl_view_instance_);
        if(!cls)return false;
        const char* selector=event.phase==0u?"touchesBegan:withEvent:":
                             event.phase==1u?"touchesMoved:withEvent:":
                             event.phase==3u?"touchesEnded:withEvent:":
                                             "touchesCancelled:withEvent:";
        const GuestMethod* method=FindInstanceMethodRecursive(cls,selector);
        if(!method)return false;
        cpu_.Regs()[0]=eagl_view_instance_;cpu_.Regs()[1]=method->selector_addr;
        cpu_.Regs()[2]=touch_set_;cpu_.Regs()[3]=touch_event_;cpu_.Regs()[14]=kControlBase;
        EnterGuestMethod(*method);host_call_stage_=HostCallStage::Touch;touch_dispatch_active_=true;
        if(++touch_log_count_<=32u)
            log_<<"IOS INPUT: "<<selector<<" guest=("<<std::fixed<<std::setprecision(1)<<touch_x_<<","<<touch_y_
                <<") view=0x"<<Hex(eagl_view_instance_)<<"\n";
        return true;
    }

    bool BeginDelegateLaunch(){
        const GuestClass* cls=FindGuestClassByName(delegate_name_);
        if(!cls){log_<<"RESULT: IOS_DELEGATE_CLASS_NOT_FOUND name="<<(delegate_name_.empty()?"unknown":delegate_name_)<<"\n";done_=true;return true;}
        const GuestMethod* method=FindInstanceMethodRecursive(cls,"application:didFinishLaunchingWithOptions:");
        if(!method)method=FindInstanceMethodRecursive(cls,"applicationDidFinishLaunching:");
        if(!method){log_<<"RESULT: IOS_DELEGATE_LAUNCH_METHOD_NOT_FOUND class="<<cls->name<<"\n";done_=true;return true;}
        delegate_instance_=NewGuestInstance(*cls);
        application_instance_=NewExternalInstance("UIApplication");
        EnsureHostGL();
        cpu_.Regs()[0]=delegate_instance_;cpu_.Regs()[1]=method->selector_addr;cpu_.Regs()[2]=application_instance_;cpu_.Regs()[3]=0u;
        cpu_.Regs()[14]=kControlBase;
        EnterGuestMethod(*method);
        delegate_launch_active_=true;delegate_launch_started_=true;host_call_stage_=HostCallStage::Delegate;
        log_<<"IOS: UIApplicationMain entering real delegate class="<<cls->name<<" selector="<<method->selector<<" imp=0x"<<Hex(method->imp)<<" self=0x"<<Hex(delegate_instance_)<<"\n";
        return true;
    }

    bool BeginDirectorAcquire(){
        const GuestClass* cls=FindGuestClassByName("CCDirector");
        if(!cls){log_<<"RESULT: IOS_FRAME_PUMP_NO_CCDIRECTOR_CLASS\n";done_=true;return true;}
        const GuestMethod* method=FindClassMethodRecursive(cls,"sharedDirector");
        if(!method){log_<<"RESULT: IOS_FRAME_PUMP_NO_SHARED_DIRECTOR\n";done_=true;return true;}
        cpu_.Regs()[0]=cls->class_addr;cpu_.Regs()[1]=method->selector_addr;cpu_.Regs()[2]=0u;cpu_.Regs()[3]=0u;cpu_.Regs()[14]=kControlBase;
        EnterGuestMethod(*method);host_call_stage_=HostCallStage::AcquireDirector;
        log_<<"IOS: UIApplicationMain continuing into cocos2d event loop: acquiring CCDirector\n";
        return true;
    }

    bool BeginRunningSceneQuery(){
        const GuestClass* cls=FindGuestClassForInstance(director_instance_);
        if(!cls){log_<<"RESULT: IOS_FRAME_PUMP_BAD_DIRECTOR object=0x"<<Hex(director_instance_)<<"\n";done_=true;return true;}
        const GuestMethod* method=FindInstanceMethodRecursive(cls,"runningScene");
        if(!method)return BeginFrameProbe();
        cpu_.Regs()[0]=director_instance_;cpu_.Regs()[1]=method->selector_addr;cpu_.Regs()[2]=0u;cpu_.Regs()[3]=0u;cpu_.Regs()[14]=kControlBase;
        EnterGuestMethod(*method);host_call_stage_=HostCallStage::QueryRunningScene;
        return true;
    }

    bool BeginFrameProbe(){
        if(host_window_.Ready()&&!host_window_.PumpMessages()){
            host_window_closed_=true;frame_probe_completed_=true;done_=true;
            log_<<"IOS HOSTGL: window closed by user after frames="<<frame_count_<<"\n";
            return true;
        }
        if(BeginTouchDispatch())return true;
        const GuestClass* cls=FindGuestClassForInstance(director_instance_);
        if(!cls){log_<<"RESULT: IOS_FRAME_PUMP_BAD_DIRECTOR object=0x"<<Hex(director_instance_)<<"\n";done_=true;return true;}
        const GuestMethod* method=FindInstanceMethodRecursive(cls,"drawScene");
        if(!method){log_<<"RESULT: IOS_FRAME_PUMP_NO_DRAWSCENE director-class="<<cls->name<<"\n";done_=true;return true;}
        virtual_time_usec_+=kSyntheticFrameUsec;
        cpu_.Regs()[0]=director_instance_;cpu_.Regs()[1]=method->selector_addr;cpu_.Regs()[2]=0u;cpu_.Regs()[3]=0u;cpu_.Regs()[14]=kControlBase;
        frame_present_start_=host_window_.PresentCount();
        EnterGuestMethod(*method);host_call_stage_=HostCallStage::Frame;frame_pump_active_=true;
        if(frame_count_<3u||frame_count_%120u==0u)
            log_<<"IOS: frame pump begin frame="<<(frame_count_+1u)<<" director="<<cls->name<<" drawScene=0x"<<Hex(method->imp)<<"\n";
        return true;
    }

    std::string ClassNameForAddress(u32 addr)const{auto it=fake_objects_.find(addr);if(it!=fake_objects_.end())return it->second.class_name;for(const auto& c:classes_)if(c.class_addr==addr||c.meta_addr==addr)return c.name;const GuestClass* instance=FindGuestClassForInstance(addr);return instance?instance->name:std::string{};}
    std::string DescribeString(u32 obj){if(!obj)return {};auto it=fake_objects_.find(obj);if(it!=fake_objects_.end()&&!it->second.string_value.empty())return it->second.string_value;std::string direct;if(env_.ReadCString(obj,direct,512u)&&!direct.empty()&&std::all_of(direct.begin(),direct.end(),[](unsigned char c){return c>=0x20&&c<0x7f;}))return direct;if(env_.IsMapped(obj,16u)){const u32 chars=env_.MemoryRead32(obj+8u);std::string s;if(chars&&env_.ReadCString(chars,s,512u))return s;}return {};}

    bool HandleObjcMsgSendStret(){
        const u32 dest=cpu_.Regs()[0],receiver=cpu_.Regs()[1],selector_addr=cpu_.Regs()[2];
        std::string selector;if(selector_addr)env_.ReadCString(selector_addr,selector,1024u);
        if(!dest||!env_.IsMapped(dest,16u))return true;

        if(!receiver){WriteCGRect(dest,0.0f,0.0f,0.0f,0.0f);return true;}
        if(auto fit=fake_objects_.find(receiver);fit!=fake_objects_.end()&&fit->second.class_name=="UITouch"){
            if(selector=="locationInView:"){WriteCGPoint(dest,touch_x_,touch_y_);return true;}
            if(selector=="previousLocationInView:"){WriteCGPoint(dest,previous_touch_x_,previous_touch_y_);return true;}
        }

        const GuestClass* class_receiver=nullptr;
        for(const auto& c:classes_)if(c.class_addr==receiver||c.meta_addr==receiver){class_receiver=&c;break;}
        if(class_receiver){
            if(const GuestMethod* method=FindClassMethodRecursive(class_receiver,selector)){
                if(++guest_dispatch_logs_<=64u)log_<<"IOS: objc guest stret class dispatch "<<class_receiver->name<<" +"<<selector<<" imp=0x"<<Hex(method->imp)<<"\n";
                EnterGuestMethod(*method);return true;
            }
        }
        if(const GuestClass* instance_class=FindGuestClassForInstance(receiver)){
            if(const GuestMethod* method=FindInstanceMethodRecursive(instance_class,selector)){
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
        if(guest_class_receiver&&(selector=="respondsToSelector:"||selector=="instancesRespondToSelector:")){
            const u32 imp=GuestMethodImpForSelector(guest_class_receiver,selector=="respondsToSelector:",cpu_.Regs()[2]);
            cpu_.Regs()[0]=imp?1u:0u;return true;
        }
        if(guest_class_receiver&&(selector=="methodForSelector:"||selector=="instanceMethodForSelector:")){
            const bool class_method=selector=="methodForSelector:";
            cpu_.Regs()[0]=GuestMethodImpForSelector(guest_class_receiver,class_method,cpu_.Regs()[2]);
            if(ShouldTraceSceneMessage(guest_class_receiver->name,selector))
                TraceSceneMessage("class-runtime",guest_class_receiver->name,selector,cpu_.Regs()[0]);
            return true;
        }
        if(guest_class_receiver&&!guest_meta_receiver&&(selector=="alloc"||selector=="new")){cpu_.Regs()[0]=NewGuestInstance(*guest_class_receiver);return true;}
        if(guest_class_receiver){
            const GuestMethod* method=FindClassMethodRecursive(guest_class_receiver,selector);
            if(method){
                if(++guest_dispatch_logs_<=160u||ShouldTraceSceneMessage(guest_class_receiver->name,selector))
                    log_<<"IOS: objc guest class dispatch "<<guest_class_receiver->name<<" +"<<selector<<" imp=0x"<<Hex(method->imp)<<"\n";
                if(ShouldTraceSceneMessage(guest_class_receiver->name,selector))
                    TraceSceneMessage("class-dispatch",guest_class_receiver->name,selector,method->imp);
                EnterGuestMethod(*method);return true;
            }
        }
        if(const GuestClass* instance_class=FindGuestClassForInstance(receiver)){
            if(selector=="setOpenGLView:"&&cpu_.Regs()[2]){
                eagl_view_instance_=cpu_.Regs()[2];
                log_<<"IOS INPUT: captured EAGLView=0x"<<Hex(eagl_view_instance_)<<"\n";
            }
            if((selector=="runWithScene:"||selector=="replaceScene:"||selector=="pushScene:")&&cpu_.Regs()[2]){
                observed_scene_=cpu_.Regs()[2];
                const GuestClass* scene_cls=FindGuestClassForInstance(observed_scene_);
                log_<<"IOS: observed "<<selector<<" scene=0x"<<Hex(observed_scene_)<<" class="<<(scene_cls?scene_cls->name:"unknown")<<"\n";
            }
            if(selector=="methodForSelector:"){
                cpu_.Regs()[0]=GuestMethodImpForSelector(instance_class,false,cpu_.Regs()[2]);
                TraceSceneMessage("instance-runtime",instance_class->name,selector,cpu_.Regs()[0]);
                return true;
            }
            if(selector=="respondsToSelector:"){
                cpu_.Regs()[0]=GuestMethodImpForSelector(instance_class,false,cpu_.Regs()[2])?1u:0u;return true;
            }
            if(selector=="isKindOfClass:"||selector=="isMemberOfClass:"){
                cpu_.Regs()[0]=(selector=="isMemberOfClass:")
                    ? (instance_class->class_addr==cpu_.Regs()[2]?1u:0u)
                    : (IsKindOfGuestClass(instance_class,cpu_.Regs()[2])?1u:0u);
                return true;
            }
            if(selector=="isEqual:"){cpu_.Regs()[0]=receiver==cpu_.Regs()[2]?1u:0u;return true;}
            if(selector=="performSelector:"||selector=="performSelector:withObject:"||selector=="performSelectorOnMainThread:withObject:waitUntilDone:"){
                const u32 target_sel=cpu_.Regs()[2];
                const std::string target_name=SelectorName(target_sel);
                const GuestMethod* target=FindInstanceMethodRecursive(instance_class,target_name);
                if(target){
                    const u32 object_arg=(selector=="performSelector:")?0u:cpu_.Regs()[3];
                    cpu_.Regs()[1]=target_sel;cpu_.Regs()[2]=object_arg;
                    if(ShouldTraceSceneMessage(instance_class->name,target_name))
                        TraceSceneMessage("perform-selector",instance_class->name,target_name,target->imp);
                    EnterGuestMethod(*target);return true;
                }
                cpu_.Regs()[0]=0u;return true;
            }
            const GuestMethod* method=FindInstanceMethodRecursive(instance_class,selector);
            if(method){
                if(++guest_dispatch_logs_<=160u||ShouldTraceSceneMessage(instance_class->name,selector))
                    log_<<"IOS: objc guest instance dispatch "<<instance_class->name<<" -"<<selector<<" imp=0x"<<Hex(method->imp)<<"\n";
                if(ShouldTraceSceneMessage(instance_class->name,selector))
                    TraceSceneMessage("instance-dispatch",instance_class->name,selector,method->imp);
                EnterGuestMethod(*method);return true;
            }
            if(selector=="class"){cpu_.Regs()[0]=instance_class->class_addr;return true;}
            if(selector=="layer"&&(instance_class->name=="EAGLView"||instance_class->name=="UIView")){cpu_.Regs()[0]=AssociatedExternal(receiver,"layer","CAEAGLLayer");return true;}
            if(selector=="init"||selector.starts_with("initWith")||selector=="retain"||selector=="autorelease"||selector=="copy"||selector=="mutableCopy"){cpu_.Regs()[0]=receiver;return true;}
            if(selector=="release"||selector=="dealloc"){cpu_.Regs()[0]=0;return true;}
        }

        const auto fit=fake_objects_.find(receiver);
        if(fit!=fake_objects_.end()){
            const auto& fo=fit->second;
            if((fo.is_class||fo.is_meta)&&(selector=="alloc"||selector=="new")){
                cpu_.Regs()[0]=NewExternalInstance(fo.class_name);
                if(fo.class_name=="UIWindow")window_instance_=cpu_.Regs()[0];
                return true;
            }
            if((fo.is_class||fo.is_meta)&&selector=="class"){cpu_.Regs()[0]=receiver;return true;}
            if(fo.is_class&&fo.class_name=="UIApplication"&&selector=="sharedApplication"){
                if(!application_instance_)application_instance_=NewExternalInstance("UIApplication");
                cpu_.Regs()[0]=application_instance_;return true;
            }
            if(fo.is_class&&fo.class_name=="NSThread"&&selector=="currentThread"){
                if(!main_thread_instance_)main_thread_instance_=NewExternalInstance("NSThread");
                cpu_.Regs()[0]=main_thread_instance_;return true;
            }
            if(fo.is_class&&fo.class_name=="NSThread"&&selector=="isMainThread"){cpu_.Regs()[0]=1u;return true;}
            if(fo.is_class&&fo.class_name=="NSThread"&&selector=="detachNewThreadSelector:toTarget:withObject:"){
                const u32 target_sel=cpu_.Regs()[2],target_obj=cpu_.Regs()[3],object_arg=StackArg(0);
                if(const GuestClass* target_cls=FindGuestClassForInstance(target_obj)){
                    const std::string target_name=SelectorName(target_sel);
                    if(const GuestMethod* target=FindInstanceMethodRecursive(target_cls,target_name)){
                        log_<<"IOS THREAD: detach selector="<<target_name<<" target="<<target_cls->name<<" policy=synchronous\n";
                        cpu_.Regs()[0]=target_obj;cpu_.Regs()[1]=target_sel;cpu_.Regs()[2]=object_arg;
                        EnterGuestMethod(*target);return true;
                    }
                }
                cpu_.Regs()[0]=0u;return true;
            }
            if(fo.is_class&&(selector=="sharedInstance"||selector=="defaultManager"||selector=="defaultCenter"||selector=="standardUserDefaults"||selector=="mainBundle"||selector=="mainScreen"||selector=="currentDevice")){cpu_.Regs()[0]=NewExternalInstance(fo.class_name);return true;}
            if(fo.is_class&&fo.class_name=="NSString"&&(selector=="stringWithCString:encoding:"||selector=="stringWithUTF8String:")){
                std::string value;env_.ReadCString(cpu_.Regs()[2],value,1u<<20);cpu_.Regs()[0]=NewFakeString(value);return true;
            }
            if(fo.is_class&&fo.class_name=="UIImage"&&(selector=="imageNamed:"||selector=="imageWithContentsOfFile:")){
                cpu_.Regs()[0]=NewImageForAsset(DescribeString(cpu_.Regs()[2]));return true;
            }
            if(fo.is_class&&(fo.class_name=="NSDictionary"||fo.class_name=="NSMutableDictionary")&&selector=="dictionaryWithContentsOfFile:"){
                cpu_.Regs()[0]=LoadFakePlist(DescribeString(cpu_.Regs()[2]));return true;
            }
            if(fo.is_class&&(fo.class_name=="NSDictionary"||fo.class_name=="NSMutableDictionary")&&
               (selector=="dictionary"||selector=="dictionaryWithCapacity:")){
                const u32 obj=NewExternalInstance(fo.class_name);fake_dictionaries_[obj];fake_dictionary_keys_[obj];cpu_.Regs()[0]=obj;return true;
            }
            if(fo.is_class&&(fo.class_name=="NSArray"||fo.class_name=="NSMutableArray")&&
               (selector=="array"||selector=="arrayWithCapacity:")){
                const u32 obj=NewExternalInstance(fo.class_name);fake_collections_[obj];cpu_.Regs()[0]=obj;return true;
            }
            if(fo.is_class&&fo.class_name=="NSNumber"&&selector.starts_with("numberWith")){
                double value=0.0;
                if(selector=="numberWithFloat:")value=FloatFromBits(cpu_.Regs()[2]);
                else if(selector=="numberWithDouble:")value=DoubleFromRegs(cpu_.Regs()[2],cpu_.Regs()[3]);
                else value=static_cast<s32>(cpu_.Regs()[2]);
                cpu_.Regs()[0]=NewFakeNumber(value);return true;
            }
            if(fo.is_class&&(selector.starts_with("numberWith")||selector.starts_with("valueWith")||selector.starts_with("arrayWith")||selector.starts_with("dictionaryWith")||selector.starts_with("setWith")||selector.starts_with("URLWith")||selector.starts_with("dataWith")||selector.starts_with("colorWith")||selector.starts_with("fontWith")||selector.starts_with("imageNamed"))){cpu_.Regs()[0]=NewExternalInstance(fo.class_name);return true;}
            if(fo.is_class&&selector=="setCurrentContext:"){cpu_.Regs()[0]=1u;return true;}
            if(!fo.is_class&&fo.class_name=="NSBundle"&&(selector=="pathForResource:ofType:"||selector=="pathForResource:ofType:inDirectory:")){
                std::string base=DescribeString(cpu_.Regs()[2]),ext=DescribeString(cpu_.Regs()[3]);
                std::string candidate=base;
                if(!ext.empty()&&!candidate.ends_with("."+ext))candidate+="."+ext;
                if(selector=="pathForResource:ofType:inDirectory:"){
                    const std::string dir=DescribeString(StackArg(0));
                    if(!dir.empty())candidate=dir+"/"+candidate;
                }
                const std::string rel=ResolveAssetRelative(candidate);
                cpu_.Regs()[0]=rel.empty()?0u:NewFakeString("ipa://"+rel);
                if(!rel.empty()&&++asset_resolve_logs_<=16u)log_<<"IOS ASSET: NSBundle "<<candidate<<" -> "<<rel<<"\n";
                return true;
            }
            if(!fo.is_class&&fo.class_name=="UIImage"&&selector=="initWithContentsOfFile:"){
                fake_objects_[receiver].resource_value=ResolveAssetRelative(DescribeString(cpu_.Regs()[2]));
                cpu_.Regs()[0]=receiver;return true;
            }
            if(!fo.is_class&&fo.class_name=="NSThread"&&selector=="initWithTarget:selector:object:"){
                fake_objects_[receiver].aux0=cpu_.Regs()[2];
                fake_objects_[receiver].aux1=cpu_.Regs()[3];
                fake_objects_[receiver].aux2=StackArg(0);
                cpu_.Regs()[0]=receiver;return true;
            }
            if(!fo.is_class&&fo.class_name=="NSThread"&&selector=="start"){
                const u32 target_obj=fo.aux0,target_sel=fo.aux1,object_arg=fo.aux2;
                if(const GuestClass* target_cls=FindGuestClassForInstance(target_obj)){
                    const std::string target_name=SelectorName(target_sel);
                    if(const GuestMethod* target=FindInstanceMethodRecursive(target_cls,target_name)){
                        log_<<"IOS THREAD: NSThread start selector="<<target_name<<" target="<<target_cls->name<<" policy=synchronous\n";
                        cpu_.Regs()[0]=target_obj;cpu_.Regs()[1]=target_sel;cpu_.Regs()[2]=object_arg;
                        EnterGuestMethod(*target);return true;
                    }
                }
                cpu_.Regs()[0]=0u;return true;
            }
            if(!fo.is_class&&fo.class_name=="UITouch"){
                if(selector=="locationInView:"){cpu_.Regs()[0]=FloatToBits(touch_x_);cpu_.Regs()[1]=FloatToBits(touch_y_);return true;}
                if(selector=="previousLocationInView:"){cpu_.Regs()[0]=FloatToBits(previous_touch_x_);cpu_.Regs()[1]=FloatToBits(previous_touch_y_);return true;}
                if(selector=="tapCount"){cpu_.Regs()[0]=1u;return true;}
                if(selector=="phase"){cpu_.Regs()[0]=touch_phase_;return true;}
                if(selector=="view"){cpu_.Regs()[0]=eagl_view_instance_;return true;}
                if(selector=="window"){cpu_.Regs()[0]=window_instance_;return true;}
            }
            if(!fo.is_class&&fo.class_name=="UIEvent"&&selector=="allTouches"){cpu_.Regs()[0]=touch_set_;return true;}
            if(!fo.is_class&&(fo.class_name=="NSDictionary"||fo.class_name=="NSMutableDictionary")){
                auto dit=fake_dictionaries_.find(receiver);
                if(selector=="count"){cpu_.Regs()[0]=dit==fake_dictionaries_.end()?0u:static_cast<u32>(dit->second.size());return true;}
                if(selector=="objectForKey:"||selector=="valueForKey:"||selector=="objectForKeyedSubscript:"){
                    const std::string key=DescribeString(cpu_.Regs()[2]);
                    if(dit==fake_dictionaries_.end()){cpu_.Regs()[0]=0u;return true;}
                    auto vit=dit->second.find(key);cpu_.Regs()[0]=vit==dit->second.end()?0u:vit->second;return true;
                }
                if(selector=="setObject:forKey:"||selector=="setValue:forKey:"||selector=="setObject:forKeyedSubscript:"){
                    const u32 value=cpu_.Regs()[2];const std::string key=DescribeString(cpu_.Regs()[3]);
                    auto& dict=fake_dictionaries_[receiver];
                    if(!dict.count(key))fake_dictionary_keys_[receiver].push_back(NewFakeString(key));
                    dict[key]=value;cpu_.Regs()[0]=0u;return true;
                }
                if(selector=="removeObjectForKey:"){
                    const std::string key=DescribeString(cpu_.Regs()[2]);fake_dictionaries_[receiver].erase(key);
                    auto& keys=fake_dictionary_keys_[receiver];
                    keys.erase(std::remove_if(keys.begin(),keys.end(),[&](u32 obj){return DescribeString(obj)==key;}),keys.end());
                    cpu_.Regs()[0]=0u;return true;
                }
                if(selector=="removeAllObjects"){fake_dictionaries_[receiver].clear();fake_dictionary_keys_[receiver].clear();cpu_.Regs()[0]=0u;return true;}
                if(selector=="allKeys"){
                    const u32 array=NewExternalInstance("NSArray");fake_collections_[array]=fake_dictionary_keys_[receiver];cpu_.Regs()[0]=array;return true;
                }
                if(selector=="keyEnumerator"){
                    const u32 enumerator=NewExternalInstance("NSEnumerator");fake_collections_[enumerator]=fake_dictionary_keys_[receiver];fake_objects_[enumerator].aux0=0u;cpu_.Regs()[0]=enumerator;return true;
                }
                if(selector=="countByEnumeratingWithState:objects:count:")return HandleFastEnumeration(receiver);
            }
            if(!fo.is_class&&(fo.class_name=="NSSet"||fo.class_name=="NSArray"||fo.class_name=="NSMutableArray")){
                auto it=fake_collections_.find(receiver);
                const std::vector<u32>* values=it==fake_collections_.end()?nullptr:&it->second;
                if(selector=="count"){cpu_.Regs()[0]=values?static_cast<u32>(values->size()):0u;return true;}
                if(selector=="anyObject"||selector=="firstObject"||selector=="lastObject"){cpu_.Regs()[0]=(values&&!values->empty())?values->front():0u;return true;}
                if(selector=="objectAtIndex:"||selector=="objectAtIndexedSubscript:"){
                    const u32 idx=cpu_.Regs()[2];cpu_.Regs()[0]=(values&&idx<values->size())?(*values)[idx]:0u;return true;
                }
                if(selector=="addObject:"){fake_collections_[receiver].push_back(cpu_.Regs()[2]);cpu_.Regs()[0]=0u;return true;}
                if(selector=="insertObject:atIndex:"){
                    auto& v=fake_collections_[receiver];const u32 idx=cpu_.Regs()[3];
                    v.insert(v.begin()+std::min<std::size_t>(idx,v.size()),cpu_.Regs()[2]);cpu_.Regs()[0]=0u;return true;
                }
                if(selector=="removeObject:"){
                    auto& v=fake_collections_[receiver];v.erase(std::remove(v.begin(),v.end(),cpu_.Regs()[2]),v.end());cpu_.Regs()[0]=0u;return true;
                }
                if(selector=="removeObjectAtIndex:"){
                    auto& v=fake_collections_[receiver];const u32 idx=cpu_.Regs()[2];if(idx<v.size())v.erase(v.begin()+idx);cpu_.Regs()[0]=0u;return true;
                }
                if(selector=="removeAllObjects"){fake_collections_[receiver].clear();cpu_.Regs()[0]=0u;return true;}
                if(selector=="allObjects"){
                    const u32 array=NewExternalInstance("NSArray");
                    if(values)fake_collections_[array]=*values;
                    cpu_.Regs()[0]=array;return true;
                }
                if(selector=="objectEnumerator"){
                    const u32 enumerator=NewExternalInstance("NSEnumerator");if(values)fake_collections_[enumerator]=*values;fake_objects_[enumerator].aux0=0u;cpu_.Regs()[0]=enumerator;return true;
                }
                if(selector=="containsObject:"){
                    cpu_.Regs()[0]=(values&&std::find(values->begin(),values->end(),cpu_.Regs()[2])!=values->end())?1u:0u;return true;
                }
                if(selector=="countByEnumeratingWithState:objects:count:")return HandleFastEnumeration(receiver);
            }
            if(!fo.is_class&&fo.class_name=="NSEnumerator"&&selector=="nextObject"){
                auto& values=fake_collections_[receiver];u32& index=fake_objects_[receiver].aux0;
                cpu_.Regs()[0]=index<values.size()?values[index++]:0u;return true;
            }
            if(!fo.is_class&&fo.class_name=="NSNumber"){
                const double value=fake_numbers_.count(receiver)?fake_numbers_[receiver]:0.0;
                if(selector=="boolValue"){cpu_.Regs()[0]=value!=0.0?1u:0u;return true;}
                if(selector=="intValue"||selector=="integerValue"||selector=="unsignedIntValue"||selector=="unsignedIntegerValue"||selector=="longValue"||selector=="shortValue"){cpu_.Regs()[0]=static_cast<u32>(static_cast<s32>(value));return true;}
                if(selector=="floatValue"){cpu_.Regs()[0]=FloatToBits(static_cast<float>(value));return true;}
                if(selector=="doubleValue"){DoubleToRegs(value,cpu_.Regs()[0],cpu_.Regs()[1]);return true;}
                if(selector=="stringValue"){cpu_.Regs()[0]=NewFakeString(std::to_string(value));return true;}
            }
            if(!fo.is_class&&(selector=="init"||selector.starts_with("initWith"))){cpu_.Regs()[0]=receiver;return true;}
            if(!fo.is_class&&fo.class_name=="NSLock"&&(selector=="lock"||selector=="unlock"||selector=="tryLock")){cpu_.Regs()[0]=selector=="tryLock"?1u:0u;return true;}
            if(!fo.is_class&&selector=="layer"){cpu_.Regs()[0]=AssociatedExternal(receiver,"layer","CAEAGLLayer");return true;}
            if(!fo.is_class&&fo.class_name=="EAGLContext"&&selector=="presentRenderbuffer:"){host_window_.Present();cpu_.Regs()[0]=1u;return true;}
            if(!fo.is_class&&fo.class_name=="EAGLContext"&&selector=="renderbufferStorage:fromDrawable:"){cpu_.Regs()[0]=1u;return true;}
            if(!fo.is_class&&fo.class_name=="UIImage"&&selector=="CGImage"){
                cpu_.Regs()[0]=AssociatedExternal(receiver,"cgimage","CGImage");
                fake_objects_[cpu_.Regs()[0]].resource_value=fo.resource_value;
                fake_cgimages_.insert(cpu_.Regs()[0]);
                return true;
            }
            if(!fo.is_class&&(selector=="drawableWidth"||selector=="width")){cpu_.Regs()[0]=320u;return true;}
            if(!fo.is_class&&(selector=="drawableHeight"||selector=="height")){cpu_.Regs()[0]=480u;return true;}
            if(!fo.is_class&&fo.class_name=="UIApplication"&&selector=="delegate"){cpu_.Regs()[0]=delegate_instance_;return true;}
            if(!fo.is_class&&fo.class_name=="UIApplication"&&selector=="setDelegate:"){delegate_instance_=cpu_.Regs()[2];cpu_.Regs()[0]=0u;return true;}
            if(!fo.is_class&&fo.class_name=="UIDevice"&&selector=="systemVersion"){cpu_.Regs()[0]=NewFakeString("6.1");return true;}
            if(!fo.is_class&&fo.class_name=="UIDevice"&&selector=="userInterfaceIdiom"){cpu_.Regs()[0]=0u;return true;}
            if(!fo.is_class&&fo.class_name=="NSThread"&&selector=="isMainThread"){cpu_.Regs()[0]=1u;return true;}
            if(selector=="retain"||selector=="autorelease"||selector=="copy"||selector=="mutableCopy"){cpu_.Regs()[0]=receiver;return true;}
            if(selector=="release"||selector=="dealloc"){cpu_.Regs()[0]=0;return true;}
            if(selector=="class"){cpu_.Regs()[0]=fo.is_class?receiver:env_.MemoryRead32(receiver);return true;}
            if(selector=="UTF8String"||selector=="cStringUsingEncoding:"){cpu_.Regs()[0]=fo.string_value.empty()?0u:AllocateCString(fo.string_value);return true;}
            if(selector=="length"){cpu_.Regs()[0]=static_cast<u32>(fo.string_value.size());return true;}
            if(selector=="respondsToSelector:"||selector=="isKindOfClass:"||selector=="isMemberOfClass:"){cpu_.Regs()[0]=1u;return true;}
        }
        const std::string string_value=DescribeString(receiver);
        if(!string_value.empty()){
            if(selector=="length"){cpu_.Regs()[0]=static_cast<u32>(string_value.size());return true;}
            if(selector=="UTF8String"||selector=="cStringUsingEncoding:"){cpu_.Regs()[0]=AllocateCString(string_value);return true;}
            if(selector=="lowercaseString"){std::string v=string_value;std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});cpu_.Regs()[0]=NewFakeString(v);return true;}
            if(selector=="isEqualToString:"){cpu_.Regs()[0]=string_value==DescribeString(cpu_.Regs()[2])?1u:0u;return true;}
            if(selector=="compare:"||selector=="caseInsensitiveCompare:"||selector=="compare:options:"){
                std::string rhs=DescribeString(cpu_.Regs()[2]),lhs=string_value;
                if(selector=="caseInsensitiveCompare:"||(selector=="compare:options:"&&(cpu_.Regs()[3]&1u))){
                    lhs=LowerAscii(lhs);rhs=LowerAscii(rhs);
                }
                cpu_.Regs()[0]=lhs<rhs?static_cast<u32>(-1):(lhs>rhs?1u:0u);return true;
            }
            if(selector=="intValue"||selector=="integerValue"){cpu_.Regs()[0]=static_cast<u32>(std::strtol(string_value.c_str(),nullptr,10));return true;}
            if(selector=="boolValue"){cpu_.Regs()[0]=(!string_value.empty()&&string_value!="0"&&LowerAscii(string_value)!="false")?1u:0u;return true;}
            if(selector=="floatValue"){cpu_.Regs()[0]=FloatToBits(std::strtof(string_value.c_str(),nullptr));return true;}
            if(selector=="hasPrefix:"){const std::string rhs=DescribeString(cpu_.Regs()[2]);cpu_.Regs()[0]=string_value.rfind(rhs,0u)==0u?1u:0u;return true;}
            if(selector=="hasSuffix:"){const std::string rhs=DescribeString(cpu_.Regs()[2]);cpu_.Regs()[0]=(rhs.size()<=string_value.size()&&string_value.compare(string_value.size()-rhs.size(),rhs.size(),rhs)==0)?1u:0u;return true;}
            if(selector=="stringByAppendingString:"){cpu_.Regs()[0]=NewFakeString(string_value+DescribeString(cpu_.Regs()[2]));return true;}
            if(selector=="isAbsolutePath"){cpu_.Regs()[0]=(!string_value.empty()&&(string_value[0]=='/'||(string_value.size()>2&&string_value[1]==':')))?1u:0u;return true;}
            if(selector=="lastPathComponent"){const auto p=string_value.find_last_of("/\\");cpu_.Regs()[0]=NewFakeString(p==std::string::npos?string_value:string_value.substr(p+1u));return true;}
            if(selector=="stringByDeletingLastPathComponent"){const auto p=string_value.find_last_of("/\\");cpu_.Regs()[0]=NewFakeString(p==std::string::npos?std::string{}:string_value.substr(0,p));return true;}
        }
        if(++unimplemented_objc_count_<=64u){const std::string class_name=ClassNameForAddress(receiver);log_<<"IOS: objc bootstrap stub receiver=0x"<<Hex(receiver)<<" class="<<(class_name.empty()?"unknown":class_name)<<" selector="<<(selector.empty()?"<unknown>":selector)<<" -> nil/0\n";}
        cpu_.Regs()[0]=0;return true;
    }

    bool HandleObjcMsgSendSuper2(){
        const u32 super_ptr=cpu_.Regs()[0],selector_addr=cpu_.Regs()[1];std::string selector;if(selector_addr)env_.ReadCString(selector_addr,selector,1024u);
        if(!super_ptr||!env_.IsMapped(super_ptr,8u)){cpu_.Regs()[0]=0;return true;}
        const u32 receiver=env_.MemoryRead32(super_ptr);const u32 current_class=env_.MemoryRead32(super_ptr+4u);
        const GuestClass* current=FindGuestClassByClassAddress(current_class);
        if(current){if(const GuestClass* parent=FindGuestClassByClassAddress(current->superclass_addr)){if(const GuestMethod* method=FindInstanceMethodRecursive(parent,selector)){cpu_.Regs()[0]=receiver;EnterGuestMethod(*method);return true;}}}
        // The common superclass calls during bootstrap are NSObject/UIKit init,
        // retain and release operations. Returning the original receiver is the
        // safe Objective-C identity behavior for those initializers.
        if(selector=="init"||selector.starts_with("initWith")||selector=="retain"||selector=="autorelease"){cpu_.Regs()[0]=receiver;return true;}
        if(selector=="release"||selector=="dealloc"){cpu_.Regs()[0]=0;return true;}
        if(++unimplemented_objc_count_<=64u)log_<<"IOS: objc super bootstrap stub class="<<(current?current->name:"unknown")<<" selector="<<(selector.empty()?"<unknown>":selector)<<" -> receiver/0\n";
        cpu_.Regs()[0]=receiver;return true;
    }

    bool HandleSvc(u32 svc){if(svc==kSvcReturn){
        if(host_call_stage_==HostCallStage::Delegate){
            delegate_launch_active_=false;delegate_launch_returned_=true;delegate_return_value_=cpu_.Regs()[0];
            log_<<"IOS: real delegate launch returned r0=0x"<<Hex(delegate_return_value_)<<"\n";
            host_call_stage_=HostCallStage::None;
            return BeginDirectorAcquire();
        }
        if(host_call_stage_==HostCallStage::AcquireDirector){
            director_instance_=cpu_.Regs()[0];host_call_stage_=HostCallStage::None;
            const GuestClass* cls=FindGuestClassForInstance(director_instance_);
            log_<<"IOS: CCDirector acquired object=0x"<<Hex(director_instance_)<<" class="<<(cls?cls->name:"unknown")<<"\n";
            return BeginRunningSceneQuery();
        }
        if(host_call_stage_==HostCallStage::QueryRunningScene){
            running_scene_=cpu_.Regs()[0];host_call_stage_=HostCallStage::None;
            const GuestClass* scene_cls=FindGuestClassForInstance(running_scene_);
            log_<<"IOS: CCDirector runningScene=0x"<<Hex(running_scene_)<<" class="<<(scene_cls?scene_cls->name:(running_scene_?"unknown":"nil"))<<"\n";
            return BeginFrameProbe();
        }
        if(host_call_stage_==HostCallStage::Touch){
            host_call_stage_=HostCallStage::None;touch_dispatch_active_=false;++touch_dispatch_count_;
            return BeginFrameProbe();
        }
        if(host_call_stage_==HostCallStage::Frame){
            host_call_stage_=HostCallStage::None;frame_pump_active_=false;++frame_count_;
            if(observed_scene_)running_scene_=observed_scene_;
            if(host_window_.Ready()){
                if(host_window_.PresentCount()==frame_present_start_)host_window_.Present();
                host_window_.Pace60();
            }
            if(kFrameProbeCount!=0u&&frame_count_>=kFrameProbeCount){
                frame_probe_completed_=true;done_=true;
                log_<<"IOS: cocos2d visible-host probe completed frames="<<frame_count_<<"\n";
                return true;
            }
            return BeginFrameProbe();
        }
        done_=true;return true;
    }if(svc==0||svc>imports_.size()){log_<<"RESULT: IOS_UNKNOWN_SVC id="<<svc<<"\n";return false;}Import& imp=imports_[svc-1u];++imp.calls;std::string name=imp.name;if(!name.empty()&&name[0]=='_')name.erase(name.begin());if(imp.calls==1u)log_<<"IOS IMPORT: "<<imp.name<<" r0=0x"<<Hex(cpu_.Regs()[0])<<" r1=0x"<<Hex(cpu_.Regs()[1])<<"\n";
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
        if(name=="objc_getProperty"){
            const u32 self=cpu_.Regs()[0],offset=cpu_.Regs()[2];
            cpu_.Regs()[0]=(self&&env_.IsMapped(self+offset,4u))?env_.MemoryRead32(self+offset):0u;
            return true;
        }
        if(name=="objc_setProperty"||name=="objc_setProperty_nonatomic"||name=="objc_setProperty_nonatomic_copy"){
            const u32 self=cpu_.Regs()[0],offset=cpu_.Regs()[2],value=cpu_.Regs()[3];
            if(self&&env_.IsMapped(self+offset,4u))env_.MemoryWrite32(self+offset,value);
            cpu_.Regs()[0]=value;return true;
        }
        if(name=="objc_sync_enter"||name=="objc_sync_exit"||name=="__Unwind_SjLj_Register"||name=="__Unwind_SjLj_Unregister"){
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="floor"||name=="ceil"||name=="sin"||name=="cos"||name=="atan2"){
            const double a=DoubleFromRegs(cpu_.Regs()[0],cpu_.Regs()[1]);
            double result=0.0;
            if(name=="floor")result=std::floor(a);
            else if(name=="ceil")result=std::ceil(a);
            else if(name=="sin")result=std::sin(a);
            else if(name=="cos")result=std::cos(a);
            else result=std::atan2(a,DoubleFromRegs(cpu_.Regs()[2],cpu_.Regs()[3]));
            DoubleToRegs(result,cpu_.Regs()[0],cpu_.Regs()[1]);return true;
        }
        if(name=="floorf"||name=="ceilf"||name=="sinf"||name=="cosf"||name=="tanf"||name=="atanf"||name=="atan2f"||name=="powf"){
            const float a=FloatFromBits(cpu_.Regs()[0]);
            float result=0.0f;
            if(name=="floorf")result=std::floor(a);
            else if(name=="ceilf")result=std::ceil(a);
            else if(name=="sinf")result=std::sin(a);
            else if(name=="cosf")result=std::cos(a);
            else if(name=="tanf")result=std::tan(a);
            else if(name=="atanf")result=std::atan(a);
            else if(name=="atan2f")result=std::atan2(a,FloatFromBits(cpu_.Regs()[1]));
            else result=std::pow(a,FloatFromBits(cpu_.Regs()[1]));
            cpu_.Regs()[0]=FloatToBits(result);return true;
        }
        if(name=="CGPointFromString"||name=="CGSizeFromString"){
            float v[2]{};
            if(ExtractFloats(DescribeString(cpu_.Regs()[1]),v,2u))WriteCGPoint(cpu_.Regs()[0],v[0],v[1]);
            else WriteCGPoint(cpu_.Regs()[0],0.0f,0.0f);
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGRectFromString"){
            float v[4]{};
            if(ExtractFloats(DescribeString(cpu_.Regs()[1]),v,4u))WriteCGRect(cpu_.Regs()[0],v[0],v[1],v[2],v[3]);
            else WriteCGRect(cpu_.Regs()[0],0.0f,0.0f,0.0f,0.0f);
            cpu_.Regs()[0]=0u;return true;
        }

        if(name=="CGAffineTransformMakeRotation"){
            const float angle=FloatFromBits(cpu_.Regs()[1]);const float c=std::cos(angle),s=std::sin(angle);
            WriteAffine(cpu_.Regs()[0],Affine{c,s,-s,c,0.0f,0.0f});cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGAffineTransformMakeScale"){
            WriteAffine(cpu_.Regs()[0],Affine{FloatFromBits(cpu_.Regs()[1]),0.0f,0.0f,FloatFromBits(cpu_.Regs()[2]),0.0f,0.0f});cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGAffineTransformTranslate"){
            Affine t=ReadAffineCallArg();const float x=FloatFromBits(StackArg(3)),y=FloatFromBits(StackArg(4));
            t.tx+=x*t.a+y*t.c;t.ty+=x*t.b+y*t.d;WriteAffine(cpu_.Regs()[0],t);cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGAffineTransformScale"){
            Affine t=ReadAffineCallArg();const float x=FloatFromBits(StackArg(3)),y=FloatFromBits(StackArg(4));
            t.a*=x;t.b*=x;t.c*=y;t.d*=y;WriteAffine(cpu_.Regs()[0],t);cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGAffineTransformRotate"){
            const Affine t=ReadAffineCallArg();const float angle=FloatFromBits(StackArg(3)),c=std::cos(angle),s=std::sin(angle);
            WriteAffine(cpu_.Regs()[0],Affine{t.a*c+t.c*s,t.b*c+t.d*s,-t.a*s+t.c*c,-t.b*s+t.d*c,t.tx,t.ty});
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGAffineTransformInvert"){
            const Affine t=ReadAffineCallArg();const float det=t.a*t.d-t.b*t.c;
            Affine v{};
            if(std::abs(det)>1.0e-12f){
                v.a=t.d/det;v.b=-t.b/det;v.c=-t.c/det;v.d=t.a/det;
                v.tx=(t.c*t.ty-t.d*t.tx)/det;v.ty=(t.b*t.tx-t.a*t.ty)/det;
            }
            WriteAffine(cpu_.Regs()[0],v);cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGAffineTransformConcat"){
            const Affine a=ReadAffineCallArg();
            const Affine b{FloatFromBits(StackArg(3)),FloatFromBits(StackArg(4)),FloatFromBits(StackArg(5)),
                           FloatFromBits(StackArg(6)),FloatFromBits(StackArg(7)),FloatFromBits(StackArg(8))};
            WriteAffine(cpu_.Regs()[0],AffineConcat(a,b));cpu_.Regs()[0]=0u;return true;
        }

        if(name=="CGImageGetWidth"||name=="CGImageGetHeight"){
            last_cgimage_=cpu_.Regs()[0];
            u32 value=2u;
            auto fit=fake_objects_.find(last_cgimage_);
            if(fit!=fake_objects_.end()&&!fit->second.resource_value.empty())
                if(const DecodedPng* img=DecodeAsset(fit->second.resource_value))
                    value=name=="CGImageGetWidth"?img->width:img->height;
            cpu_.Regs()[0]=value;return true;
        }
        if(name=="CGImageGetBitsPerComponent"){last_cgimage_=cpu_.Regs()[0];cpu_.Regs()[0]=8u;return true;}
        if(name=="CGImageGetBytesPerRow"){
            last_cgimage_=cpu_.Regs()[0];u32 width=2u;
            auto fit=fake_objects_.find(last_cgimage_);
            if(fit!=fake_objects_.end()&&!fit->second.resource_value.empty())
                if(const DecodedPng* img=DecodeAsset(fit->second.resource_value))width=img->width;
            cpu_.Regs()[0]=width*4u;return true;
        }
        if(name=="CGImageGetAlphaInfo"){cpu_.Regs()[0]=1u;return true;}
        if(name=="CGImageGetColorSpace"||name=="CGColorSpaceCreateDeviceRGB"||name=="CGColorSpaceCreateDeviceGray"){
            cpu_.Regs()[0]=Allocate(16u);return true;
        }
        if(name=="CGColorSpaceRelease"||name=="CGImageRelease"||name=="CGContextRelease"||name=="CGDataProviderRelease"){cpu_.Regs()[0]=0u;return true;}
        if(name=="CGBitmapContextCreate"){
            const u32 data=cpu_.Regs()[0],width=cpu_.Regs()[1],height=cpu_.Regs()[2],bits=cpu_.Regs()[3];
            const u32 bytes_per_row=StackArg(0);
            const u32 ctx=Allocate(32u);
            bitmap_contexts_[ctx]=std::array<u32,6>{data,width,height,bits,bytes_per_row,last_cgimage_};
            cpu_.Regs()[0]=ctx;return true;
        }
        if(name=="CGContextDrawImage"){
            const u32 ctx=cpu_.Regs()[0];
            auto it=bitmap_contexts_.find(ctx);
            if(it!=bitmap_contexts_.end()){
                const u32 data=it->second[0],width=it->second[1],height=it->second[2],bpr=it->second[4],cgimage=it->second[5];
                if(data&&width&&height&&bpr&&u64(bpr)*height<=64u*1024u*1024u&&env_.IsMapped(data,std::size_t(bpr)*height)){
                    const DecodedPng* source=nullptr;
                    auto fit=fake_objects_.find(cgimage);
                    if(fit!=fake_objects_.end()&&!fit->second.resource_value.empty())source=DecodeAsset(fit->second.resource_value);
                    std::vector<u8> pixels(std::size_t(bpr)*height,0);
                    for(u32 y=0;y<height;++y)for(u32 x=0;x<width;++x){
                        const std::size_t o=std::size_t(y)*bpr+x*4u;
                        if(o+3u>=pixels.size())continue;
                        if(source&&!source->rgba.empty()){
                            const u32 sx=std::min<u32>(source->width-1u,u32((u64(x)*source->width)/std::max<u32>(width,1u)));
                            const u32 sy=std::min<u32>(source->height-1u,u32((u64(y)*source->height)/std::max<u32>(height,1u)));
                            const std::size_t so=(std::size_t(sy)*source->width+sx)*4u;
                            std::memcpy(pixels.data()+o,source->rgba.data()+so,4u);
                        }else{
                            const bool mag=(((x>>4u)^(y>>4u))&1u)==0u;
                            pixels[o+0]=255u;pixels[o+1]=mag?0u:255u;pixels[o+2]=255u;pixels[o+3]=255u;
                        }
                    }
                    env_.WriteBytes(data,pixels.data(),pixels.size());
                    if(source)++real_asset_draws_;
                }
            }
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="CGContextClearRect"||name=="CGContextSaveGState"||name=="CGContextRestoreGState"||name=="CGContextScaleCTM"||
           name=="CGContextTranslateCTM"||name=="CGContextSetRGBFillColor"||name=="CGContextSetGrayFillColor"||
           name=="CGContextSetRGBStrokeColor"||name=="CGContextSetLineWidth"||name=="CGContextBeginPath"||
           name=="CGContextMoveToPoint"||name=="CGContextAddArc"||name=="CGContextClosePath"||
           name=="CGContextFillPath"||name=="CGContextFillEllipseInRect"||name=="CGContextStrokeEllipseInRect"){
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="UIApplicationMain"){
            reached_ui_application_main_=true;delegate_name_=ResolveDelegateName(cpu_.Regs()[3]);
            log_<<"IOS: UIApplicationMain reached argc="<<cpu_.Regs()[0]<<" delegate="<<(delegate_name_.empty()?"unknown":delegate_name_)<<" constructors="<<image_.constructor_count<<"\n";
            if(image_.constructor_count!=0u){delegate_launch_deferred_=true;done_=true;cpu_.Regs()[0]=0;log_<<"IOS: delegate launch deferred until Mach-O static constructors are implemented\n";return true;}
            return BeginDelegateLaunch();
        }
        if(name.starts_with("gl")&&HandleHostGraphicsImport(name))return true;

        // OpenAL is still bootstrap-only in PublicTest9. OpenGL ES is forwarded
        // above to the real Win32 OpenGL context whenever the host window exists.
        if(name=="alGetError"||name=="alcGetError"){cpu_.Regs()[0]=0u;return true;}
        if(name=="alGenBuffers"||name=="alGenSources"){WriteGeneratedIds(cpu_.Regs()[0],cpu_.Regs()[1]);cpu_.Regs()[0]=0u;return true;}
        if(name=="alcOpenDevice"||name=="alcCreateContext"){cpu_.Regs()[0]=Allocate(16u);return true;}
        if(name=="alcMakeContextCurrent"){cpu_.Regs()[0]=1u;return true;}
        if(name.starts_with("gl")||name.starts_with("al")||name.starts_with("alc")){
            if(++graphics_stub_logs_<=32u)log_<<"IOS: graphics/audio bootstrap stub "<<imp.name<<"\n";
            cpu_.Regs()[0]=0u;return true;
        }

        // Read-only stdio backed directly by files inside Payload/<app>.app/.
        // cocos2d's CCZ/PVR loaders use the C FILE API even when NSBundle has
        // already resolved the resource path, so returning null from fopen was
        // the exact PublicTest12 MenuScene crash.
        if(name=="fopen"){
            std::string path,mode;
            env_.ReadCString(cpu_.Regs()[0],path,1u<<20);
            env_.ReadCString(cpu_.Regs()[1],mode,64u);
            if(mode.find('w')!=std::string::npos||mode.find('a')!=std::string::npos||mode.find('+')!=std::string::npos){cpu_.Regs()[0]=0u;return true;}
            cpu_.Regs()[0]=OpenVirtualAsset(path,false);
            if(!cpu_.Regs()[0]&&++file_failure_logs_<=16u)log_<<"IOS FILE: fopen miss path="<<path<<" mode="<<mode<<"\\n";
            return true;
        }
        if(name=="fread"){
            const u32 dst=cpu_.Regs()[0],size=cpu_.Regs()[1],count=cpu_.Regs()[2],handle=cpu_.Regs()[3];
            if(!size||!count){cpu_.Regs()[0]=0u;return true;}
            const u64 requested=u64(size)*count;
            if(requested>128u*1024u*1024u){cpu_.Regs()[0]=0u;return true;}
            const u32 bytes=ReadVirtualFile(handle,dst,static_cast<u32>(requested));
            cpu_.Regs()[0]=bytes/size;return true;
        }
        if(name=="fseek"){
            VirtualFile* file=FindVirtualFile(cpu_.Regs()[0]);
            if(!file){cpu_.Regs()[0]=static_cast<u32>(-1);return true;}
            const s64 off=static_cast<s32>(cpu_.Regs()[1]);const u32 whence=cpu_.Regs()[2];
            s64 base=whence==0u?0:(whence==1u?static_cast<s64>(file->pos):(whence==2u?static_cast<s64>(file->bytes.size()):-1));
            const s64 next=base<0?-1:base+off;
            if(next<0||static_cast<u64>(next)>0xffffffffull){cpu_.Regs()[0]=static_cast<u32>(-1);return true;}
            file->pos=static_cast<std::size_t>(next);cpu_.Regs()[0]=0u;return true;
        }
        if(name=="ftell"){
            VirtualFile* file=FindVirtualFile(cpu_.Regs()[0]);
            cpu_.Regs()[0]=file&&file->pos<=0x7fffffffu?static_cast<u32>(file->pos):static_cast<u32>(-1);return true;
        }
        if(name=="rewind"){
            if(VirtualFile* file=FindVirtualFile(cpu_.Regs()[0]))file->pos=0u;
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="feof"){
            VirtualFile* file=FindVirtualFile(cpu_.Regs()[0]);cpu_.Regs()[0]=(!file||file->pos>=file->bytes.size())?1u:0u;return true;
        }
        if(name=="fclose"){
            const u32 handle=cpu_.Regs()[0];virtual_files_.erase(handle);cpu_.Regs()[0]=0u;return true;
        }
        if(name=="gzopen"){
            std::string path,mode;env_.ReadCString(cpu_.Regs()[0],path,1u<<20);env_.ReadCString(cpu_.Regs()[1],mode,64u);
            cpu_.Regs()[0]=OpenVirtualAsset(path,true);return true;
        }
        if(name=="gzread"){
            cpu_.Regs()[0]=ReadVirtualFile(cpu_.Regs()[0],cpu_.Regs()[1],cpu_.Regs()[2]);return true;
        }
        if(name=="gzclose"){
            virtual_files_.erase(cpu_.Regs()[0]);cpu_.Regs()[0]=Z_OK;return true;
        }
        if(name=="gzeof"){
            VirtualFile* file=FindVirtualFile(cpu_.Regs()[0]);cpu_.Regs()[0]=(!file||file->pos>=file->bytes.size())?1u:0u;return true;
        }
        if(name=="uncompress"){
            const u32 dest=cpu_.Regs()[0],dest_len_ptr=cpu_.Regs()[1],source=cpu_.Regs()[2],source_len=cpu_.Regs()[3];
            if(!dest_len_ptr||!env_.IsMapped(dest_len_ptr,4u)||!source||!source_len||!env_.IsMapped(source,source_len)){cpu_.Regs()[0]=Z_STREAM_ERROR;return true;}
            const u32 capacity=env_.MemoryRead32(dest_len_ptr);
            if(!dest||!capacity||capacity>128u*1024u*1024u||!env_.IsMapped(dest,capacity)){cpu_.Regs()[0]=Z_BUF_ERROR;return true;}
            std::vector<u8> in(source_len),out(capacity);env_.ReadBytes(source,in.data(),in.size());
            uLongf out_len=capacity;const int rc=::uncompress(reinterpret_cast<Bytef*>(out.data()),&out_len,reinterpret_cast<const Bytef*>(in.data()),source_len);
            if(rc==Z_OK&&out_len)env_.WriteBytes(dest,out.data(),static_cast<std::size_t>(out_len));
            env_.MemoryWrite32(dest_len_ptr,static_cast<u32>(out_len));cpu_.Regs()[0]=static_cast<u32>(rc);
            if(++zlib_logs_<=16u)log_<<"IOS ZLIB: uncompress src="<<source_len<<" dst="<<out_len<<" rc="<<rc<<"\\n";
            return true;
        }

        if(name=="gettimeofday"){
            const u32 tv=cpu_.Regs()[0];
            if(tv&&env_.IsMapped(tv,8u)){env_.MemoryWrite32(tv,static_cast<u32>(virtual_time_usec_/1000000u));env_.MemoryWrite32(tv+4u,static_cast<u32>(virtual_time_usec_%1000000u));}
            cpu_.Regs()[0]=0u;return true;
        }
        if(name=="time"){
            const u32 sec=static_cast<u32>(virtual_time_usec_/1000000u);
            if(cpu_.Regs()[0]&&env_.IsMapped(cpu_.Regs()[0],4u))env_.MemoryWrite32(cpu_.Regs()[0],sec);
            cpu_.Regs()[0]=sec;return true;
        }
        if(name=="srand"||name=="srandom"){rng_state_=cpu_.Regs()[0]?cpu_.Regs()[0]:1u;cpu_.Regs()[0]=0u;return true;}
        if(name=="rand"||name=="random"){rng_state_=rng_state_*1103515245u+12345u;cpu_.Regs()[0]=(rng_state_>>1u)&0x7fffffffu;return true;}
        if(name=="usleep"){virtual_time_usec_+=cpu_.Regs()[0];cpu_.Regs()[0]=0u;return true;}
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

    enum class HostCallStage { None, Delegate, AcquireDirector, QueryRunningScene, Touch, Frame };

    MachImage image_; Logger& log_; const std::vector<u8>* ipa_=nullptr; std::string app_root_; ProbeEnvironment env_; Dynarmic::ExclusiveMonitor monitor_; Dynarmic::A32::Jit cpu_;
    std::vector<ZipEntry> asset_entries_; std::vector<std::string> asset_relative_; std::unordered_map<std::string,std::size_t> asset_lookup_; std::unordered_map<std::string,DecodedPng> decoded_assets_;
    std::vector<Import> imports_; std::unordered_map<std::string,std::size_t> import_by_name_; std::unordered_map<u32,FakeObject> fake_objects_; std::unordered_map<std::string,u32> fake_named_; std::unordered_map<std::string,u32> data_symbols_; std::unordered_map<std::string,u32> associated_fake_; std::unordered_map<u32,std::vector<u32>> fake_collections_; std::unordered_map<u32,std::unordered_map<std::string,u32>> fake_dictionaries_; std::unordered_map<u32,std::vector<u32>> fake_dictionary_keys_; std::unordered_map<u32,double> fake_numbers_; std::unordered_map<std::string,u32> plist_roots_; std::unordered_map<u32,VirtualFile> virtual_files_; std::vector<GuestClass> classes_;
    u32 heap_cursor_=kHeapBase+0x1000u,object_cursor_=kObjectBase+0x1000u,initial_sp_=0;
    u32 exit_code_=0,delegate_instance_=0,application_instance_=0,delegate_return_value_=0,gl_object_counter_=1u;
    u32 director_instance_=0,running_scene_=0,observed_scene_=0,frame_count_=0,rng_state_=1u;
    u32 eagl_view_instance_=0,window_instance_=0,main_thread_instance_=0,fast_enum_mutation_addr_=0;
    u32 touch_object_=0,touch_set_=0,touch_event_=0,touch_phase_=0;
    float touch_x_=0.0f,touch_y_=0.0f,previous_touch_x_=0.0f,previous_touch_y_=0.0f;
    u32 bound_texture_=0,bound_array_buffer_=0,bound_element_array_buffer_=0;
    u64 virtual_time_usec_=1350000000000000ull;
    u64 frame_present_start_=0,placeholder_texture_uploads_=0,real_asset_draws_=0,asset_resolve_logs_=0,asset_decode_logs_=0,asset_failure_logs_=0,file_open_logs_=0,file_failure_logs_=0,zlib_logs_=0,plist_load_logs_=0,plist_failure_logs_=0,texture_upload_logs_=0;
    u64 unknown_import_count_=0,unimplemented_objc_count_=0,guest_dispatch_logs_=0,testflight_bypass_count_=0,stret_stub_logs_=0,graphics_stub_logs_=0,guest_category_method_count_=0,scene_trace_count_=0,touch_log_count_=0,touch_dispatch_count_=0;
    bool done_=false,reached_ui_application_main_=false,delegate_launch_started_=false,delegate_launch_active_=false,delegate_launch_returned_=false,delegate_launch_deferred_=false,frame_pump_active_=false,frame_probe_completed_=false,touch_dispatch_active_=false;
    bool host_window_attempted_=false,host_window_closed_=false;
    HostCallStage host_call_stage_=HostCallStage::None;
    HostOpenGLWindow host_window_;
    std::set<u32> fake_cgimages_;
    u32 last_cgimage_=0;
    std::unordered_map<u32,std::array<u32,6>> bitmap_contexts_;
    std::string delegate_name_;
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
        const auto slash=exe.member.find_last_of('/');
        const std::string app_root=slash==std::string::npos?std::string{}:exe.member.substr(0,slash+1u);
        IosBootstrap runtime(std::move(image),log,ipa,app_root);
        if(!runtime.Prepare())return 3;
        return runtime.Run()?0:4;
    }catch(const std::exception& e){log<<"RESULT: IOS_BOOTSTRAP_FATAL error="<<e.what()<<"\n";return 5;}
}

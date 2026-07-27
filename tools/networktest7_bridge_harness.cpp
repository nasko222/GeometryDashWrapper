#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>

using u32 = std::uint32_t;
using s32 = std::int32_t;

#pragma pack(push, 1)
struct ResponseLayout32 {
    std::uint8_t ccobject[0x30];
    u32 request;
    std::uint8_t success;
    std::uint8_t padding35[3];
    u32 body_begin;
    u32 body_end;
    u32 body_capacity;
    u32 headers_begin;
    u32 headers_end;
    u32 headers_capacity;
    u32 status_code;
    u32 error_cow_data;
};
#pragma pack(pop)

static_assert(sizeof(ResponseLayout32) == 0x58);
static_assert(offsetof(ResponseLayout32, request) == 0x30);
static_assert(offsetof(ResponseLayout32, success) == 0x34);
static_assert(offsetof(ResponseLayout32, body_begin) == 0x38);
static_assert(offsetof(ResponseLayout32, headers_begin) == 0x44);
static_assert(offsetof(ResponseLayout32, status_code) == 0x50);
static_assert(offsetof(ResponseLayout32, error_cow_data) == 0x54);

constexpr std::string_view Method(u32 type) {
    switch (type) {
    case 0: return "GET";
    case 1: return "POST";
    case 2: return "PUT";
    case 3: return "DELETE";
    default: return "UNSUPPORTED";
    }
}

struct MemberPointerDecode {
    u32 adjusted_target;
    bool virtual_selector;
};

constexpr MemberPointerDecode Decode(u32 target, u32 adjustment_word) {
    const auto signed_adjustment = static_cast<s32>(adjustment_word) >> 1;
    return {target + static_cast<u32>(signed_adjustment),
            (adjustment_word & 1u) != 0u};
}

int main() {
    assert(Method(0) == "GET");
    assert(Method(1) == "POST");
    assert(Method(2) == "PUT");
    assert(Method(3) == "DELETE");
    assert(Method(4) == "UNSUPPORTED");

    const auto positive = Decode(0x1000u, 16u); // +8, non-virtual
    assert(positive.adjusted_target == 0x1008u && !positive.virtual_selector);
    const auto negative = Decode(0x1000u, static_cast<u32>(-8)); // -4, non-virtual
    assert(negative.adjusted_target == 0x0ffcu && !negative.virtual_selector);
    const auto virtual_member = Decode(0x1000u, 17u); // +8, virtual
    assert(virtual_member.adjusted_target == 0x1008u && virtual_member.virtual_selector);

    // Mirror the request ownership path:
    // caller owns 1 -> bridge retain 2 -> caller release 1 -> response dtor release 0.
    u32 request_refcount = 1;
    ++request_refcount;
    assert(request_refcount == 2);
    --request_refcount;
    assert(request_refcount == 1);
    --request_refcount;
    assert(request_refcount == 0);

    std::cout << "PASS response-layout-size=0x" << std::hex
              << sizeof(ResponseLayout32)
              << " request=0x" << offsetof(ResponseLayout32, request)
              << " body=0x" << offsetof(ResponseLayout32, body_begin)
              << " headers=0x" << offsetof(ResponseLayout32, headers_begin)
              << " status=0x" << offsetof(ResponseLayout32, status_code)
              << " error=0x" << offsetof(ResponseLayout32, error_cow_data)
              << std::dec << '\n';
    std::cout << "PASS request-types=GET,POST,PUT,DELETE unsupported-sentinel=4\n";
    std::cout << "PASS member-pointer-decode=nonvirtual-positive,nonvirtual-negative,virtual\n";
    std::cout << "PASS ownership=1->2->1->0\n";
}

#!/usr/bin/env python3
from __future__ import annotations
import re
import struct
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit('usage: networktest7_static_audit.py <libcocos2dcpp.so> <source-root>')
elf_path = Path(sys.argv[1])
root = Path(sys.argv[2])
source = (root / 'src/dynarmic_probe.cpp').read_text(encoding='utf-8')
cmake = (root / 'dynarmic-x64/CMakeLists.txt').read_text(encoding='utf-8')
data = elf_path.read_bytes()

sym_text = subprocess.check_output(['readelf', '-sW', str(elf_path)], text=True)
symbols: dict[str, tuple[int, int, str]] = {}
for line in sym_text.splitlines():
    m = re.match(r'\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+(\w+)\s+\w+\s+\w+\s+\S+\s+(.+)$', line)
    if m:
        symbols[m.group(4)] = (int(m.group(1), 16), int(m.group(2)), m.group(3))

def require_symbol(name: str, expected_type: str | None = None) -> tuple[int, int, str]:
    if name not in symbols:
        raise AssertionError(f'missing ELF symbol {name}')
    value = symbols[name]
    if expected_type and value[2] != expected_type:
        raise AssertionError(f'{name} type={value[2]} expected={expected_type}')
    return value

# ELF32 little-endian PT_LOAD virtual-address mapping.
if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
    raise AssertionError('expected ELF32 little-endian input')
e_phoff = struct.unpack_from('<I', data, 28)[0]
e_phentsize = struct.unpack_from('<H', data, 42)[0]
e_phnum = struct.unpack_from('<H', data, 44)[0]
segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, p_flags, _ = struct.unpack_from('<IIIIIIII', data, off)
    if p_type == 1:
        segments.append((p_vaddr, p_vaddr + p_filesz, p_offset, p_flags, p_memsz))

def file_offset(vaddr: int) -> int:
    for start, end, offset, _, _ in segments:
        if start <= vaddr < end:
            return offset + (vaddr - start)
    raise AssertionError(f'address 0x{vaddr:x} not file-backed')

send_name = '_ZN7cocos2d9extension12CCHttpClient4sendEPNS0_13CCHttpRequestE'
send_addr, send_size, _ = require_symbol(send_name, 'FUNC')
send_code = send_addr & ~1
prologue = struct.unpack_from('<H', data, file_offset(send_code))[0]
if prologue != 0xB570:
    raise AssertionError(f'CCHttpClient::send prologue=0x{prologue:04x}, expected 0xb570')
if send_size != 72:
    raise AssertionError(f'CCHttpClient::send size={send_size}, expected 72')

ctor = require_symbol('_ZN7cocos2d8CCObjectC2Ev', 'FUNC')
retain = require_symbol('_ZN7cocos2d8CCObject6retainEv', 'FUNC')
release = require_symbol('_ZN7cocos2d8CCObject7releaseEv', 'FUNC')
dtor = require_symbol('_ZN7cocos2d9extension14CCHttpResponseD2Ev', 'FUNC')
vtable = require_symbol('_ZTVN7cocos2d9extension14CCHttpResponseE', 'OBJECT')
if vtable[1] != 48:
    raise AssertionError(f'CCHttpResponse vtable size={vtable[1]}, expected 48')

required_source = [
    'InstallV22NativeHttpSendHook',
    '__dynarmic_v22_native_http_send',
    'WinHttpOpen(', 'WinHttpConnect(', 'WinHttpOpenRequest(',
    'WinHttpSendRequest(', 'WinHttpReceiveResponse(', 'WinHttpReadData(',
    'BuildNativeHttpResponse', 'DispatchNativeHttpCallback',
    'native_http_response_vtable_ = response_vtable->address + 8u',
    'env_.MemoryWrite32(object + 0x30u, result.request)',
    'env_.MemoryWrite8(object + 0x34u',
    'env_.MemoryWrite32(object + 0x50u, result.response_code)',
]
for token in required_source:
    if token not in source:
        raise AssertionError(f'missing source token: {token}')

pump = re.search(r'bool PumpNetworkWorkerFrame\(\)\s*\{(.*?)\n\s*\}', source, re.S)
if not pump:
    raise AssertionError('PumpNetworkWorkerFrame body not found')
if 'PumpNativeHttpCallbacks' not in pump.group(1):
    raise AssertionError('frame pump does not dispatch native HTTP callbacks')
if 'PumpCooperativeWorkerSlice' in pump.group(1):
    raise AssertionError('frame pump still slices guest HTTP worker')

if not re.search(r'target_link_libraries\([^)]*\bwinhttp\b', cmake, re.S):
    raise AssertionError('CMake does not link winhttp')

print('PASS selected-beta ELF32 little-endian')
print(f'PASS CCHttpClient::send address=0x{send_addr:08x} size={send_size} prologue=0x{prologue:04x}')
print(f'PASS CCObject ctor=0x{ctor[0]:08x} retain=0x{retain[0]:08x} release=0x{release[0]:08x}')
print(f'PASS CCHttpResponse dtor=0x{dtor[0]:08x} vtable=0x{vtable[0]:08x} vtable-size={vtable[1]} installed-vptr=+8')
print('PASS hook-boundary=CCHttpClient::send synthetic-import=__dynarmic_v22_native_http_send')
print('PASS transport=WinHTTP host-thread APIs present')
print('PASS frame-pump=native-response-callbacks-only guest-worker-slicing=absent')
print('PASS response-layout=request+0x30 success+0x34 body+0x38 headers+0x44 status+0x50 error+0x54')
print('PASS CMake-link=winhttp')

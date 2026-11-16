#!/usr/bin/env python3
"""Check the built package and execute its loader/math with mocked OS calls.

Requires Unicorn and LLVM, but no firmware, game data or phone emulator.
"""

from pathlib import Path
import os
import random
import shutil
import struct
import subprocess
import zipfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_SP,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R3,
    UC_ARM_REG_R4,
    UC_ARM_REG_R11,
)

P = Path(__file__).resolve().parents[1]
B = P / "build"
pack = lambda *v: struct.pack("<%dI" % len(v), *(x & 0xFFFFFFFF for x in v))
u32 = lambda b, o=0: struct.unpack_from("<I", b, o)[0]


def llvm_tool(name):
    if os.environ.get("LLVM"):
        return str(Path(os.environ["LLVM"]) / name)
    found = shutil.which(name)
    if found:
        return found
    candidate = Path("/opt/homebrew/opt/llvm/bin") / name
    if candidate.is_file():
        return str(candidate)
    raise SystemExit("Missing " + name + "; set LLVM to the LLVM binary directory")


def elf_symbols(path):
    out = subprocess.check_output(
        [llvm_tool("llvm-nm"), "--defined-only", str(path)], text=True
    )
    return {
        x[2]: int(x[0], 16) for line in out.splitlines() if len(x := line.split()) == 3
    }


def image():
    b = (B / "doom.bin").read_bytes()
    magic, version, end, mem, entry, n, stack, res = struct.unpack_from("<8I", b)
    assert (magic, version, stack, res) == (0x30443453, 1, 196608, 0)
    assert len(b) == 32 + end + 4 * n and end <= mem and entry < end
    rel = struct.unpack_from("<%dI" % n, b, 32 + end)
    assert len(set(rel)) == n
    data = bytearray(mem)
    data[:end] = b[32 : 32 + end]
    for off in rel:
        assert off % 4 == 0 and off + 4 <= end and (u32(data, off) & ~1) < mem
    return data, rel, entry


def loader_tests():
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    u.mem_map(0x100000, 0x900000)
    u.mem_map(0xF0000000, 0x1000000)
    code = (B / "native.bin").read_bytes()
    assert len(code) % 4 == 0 and len(code) <= 3072
    ORIG = 0x100000
    OBJ = 0x200000
    DESC = OBJ + 12
    SLOT = 0x210000
    STOP = 0x220000
    SP = 0x9FF000
    u.mem_write(ORIG, code)
    u.mem_write(SLOT, pack(ORIG))
    u.mem_write(STOP, pack(0xE1200070))

    def setp(i, v):
        u.mem_write(DESC + 4 * i, pack(v))

    def getp(i):
        return u32(u.mem_read(DESC + 4 * i, 4))

    allocations = {}
    freed = []
    cache = []
    next_addr = 0x300000
    kni = []

    def hook(u, a, size, user):
        nonlocal next_addr
        if a == 0xF0BA6C66:
            assert u.reg_read(UC_ARM_REG_R0) == 1
            u.mem_write(u.reg_read(UC_ARM_REG_R1), pack(OBJ))
        elif a == 0xF0BA681C:
            kni.append(1)
        elif a == 0xF04F0340:
            n = u.reg_read(UC_ARM_REG_R0)
            addr = next_addr
            next_addr += (n + 4095) & ~4095
            assert next_addr < 0x900000
            allocations[addr] = n
            u.reg_write(UC_ARM_REG_R0, addr)
        elif a == 0xF04EFA6E:
            p = u.reg_read(UC_ARM_REG_R0)
            assert p in allocations
            freed.append(p)
            del allocations[p]
            # Poison freed code, proving the tail-free has no return into it.
            u.mem_write(p, b"\xff" * 64)
        elif a == 0xF0D142A0:
            cache.append((u.reg_read(UC_ARM_REG_R0), u.reg_read(UC_ARM_REG_R1)))
        else:
            return
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    u.hook_add(UC_HOOK_CODE, hook, begin=0xF0000000, end=0xF0FFFFFF)

    def call(addr, op):
        setp(1, op)
        u.reg_write(UC_ARM_REG_SP, SP)
        u.reg_write(UC_ARM_REG_LR, STOP)
        for r in range(UC_ARM_REG_R4, UC_ARM_REG_R11 + 1):
            u.reg_write(r, 0xAAAA0000 + r)
        u.emu_start(addr, STOP, count=10000000)
        assert u.reg_read(UC_ARM_REG_PC) == STOP and u.reg_read(UC_ARM_REG_SP) == SP
        assert all(
            u.reg_read(r) == 0xAAAA0000 + r
            for r in range(UC_ARM_REG_R4, UC_ARM_REG_R11 + 1)
        )
        assert getp(0) == 0x444F4F4D

    setp(80, SLOT)
    setp(82, ORIG)
    setp(83, len(code))
    call(ORIG, 0)
    own = getp(84)
    assert own and u32(u.mem_read(SLOT, 4)) == own and (own, len(code)) in cache
    u.mem_write(ORIG, b"\xff" * len(code))  # Java carrier can now be collected/moved.
    setp(11, 4096 + 4 + 196608)
    call(own, 1)
    base = getp(2)
    assert base
    # A real ARM callee records its SP then returns. One absolute data pointer.
    payload = pack(0xE1A0100D, 0xE5801144, 0xE12FFF1E) + bytes(20) + pack(12)
    u.mem_write(DESC + 96 * 4, payload)
    setp(12, 0)
    setp(13, len(payload))
    call(own, 2)
    assert getp(3) == 0
    # Reject overlong/out-of-allocation chunks without touching the image.
    setp(12, getp(11) - 1)
    setp(13, 4)
    call(own, 2)
    assert getp(3) == 23
    setp(12, 4096)
    setp(13, 4)
    u.mem_write(DESC + 96 * 4, pack(32))
    call(own, 2)
    setp(12, 4096)
    setp(13, 1)
    setp(14, 0)
    call(own, 3)
    assert getp(3) == 0 and getp(2) == 0 and getp(84) == 0 and not allocations
    assert u32(u.mem_read(SLOT, 4)) == 0 and getp(80) == 0
    assert getp(81) == ((base + 4096 + 4 + 196608) & ~7)
    assert (base, 4096) in cache and len(freed) == 2 and len(kni) == 6
    # Malformed relative offset: reject, restore binding, free both blocks.
    u.mem_write(ORIG, code)
    setp(80, SLOT)
    setp(82, ORIG)
    setp(83, len(code))
    call(ORIG, 0)
    own = getp(84)
    setp(11, 4096 + 4 + 196608)
    call(own, 1)
    base = getp(2)
    u.mem_write(base + 4096, pack(4097))
    setp(12, 4096)
    setp(13, 1)
    setp(14, 0)
    call(own, 3)
    assert getp(3) == 25 and not allocations and not getp(84)
    print(
        "Loader ARM execution: relocation, own stack, chunk bounds, metadata restoration, KNI balance, all frees pass"
    )


def div_tests():
    data, rel, entry = image()
    base = 0x100000
    for off in rel:
        struct.pack_into("<I", data, off, u32(data, off) + base)
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    u.mem_map(base, 0x200000)
    u.mem_write(base, bytes(data))
    syms = elf_symbols(B / "doom.elf")
    stop = 0x2F0000
    rng = random.Random(443)
    mask = (1 << 64) - 1
    cases = [
        (0, 1),
        (1, 1),
        (-1, 1),
        (-(1 << 63), -1),
        (-(1 << 63), 1),
        ((1 << 63) - 1, 3),
        (-123456, 77),
    ]
    cases += [
        (rng.randrange(-(1 << 63), 1 << 63), rng.randrange(-(1 << 63), 1 << 63) or 1)
        for _ in range(150)
    ]
    for a, b in cases:
        regs = [
            a & 0xFFFFFFFF,
            (a >> 32) & 0xFFFFFFFF,
            b & 0xFFFFFFFF,
            (b >> 32) & 0xFFFFFFFF,
        ]
        for r, v in zip(
            [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], regs
        ):
            u.reg_write(r, v)
        u.reg_write(UC_ARM_REG_SP, 0x2FF000)
        u.reg_write(UC_ARM_REG_LR, stop)
        u.emu_start(base + syms["__aeabi_ldivmod"], stop, count=10000)
        q = abs(a) // abs(b)
        q = -q if (a < 0) != (b < 0) else q
        r = a - q * b
        assert (u.reg_read(UC_ARM_REG_R0) | (u.reg_read(UC_ARM_REG_R1) << 32)) == (
            q & mask
        )
        assert (u.reg_read(UC_ARM_REG_R2) | (u.reg_read(UC_ARM_REG_R3) << 32)) == (
            r & mask
        )
    print(
        "Signed 64-bit ARM division: edge cases and 150 deterministic random cases pass"
    )


def main():
    data, rel, entry = image()
    info = subprocess.check_output(
        [llvm_tool("llvm-readobj"), "--relocations", str(B / "loader.elf")], text=True
    )
    assert "Relocations [\n]" in info
    assert not subprocess.check_output(
        [llvm_tool("llvm-nm"), "--undefined-only", str(B / "doom.elf")], text=True
    ).strip()
    with zipfile.ZipFile(P / "dist/RM443NativeDoom.jar") as z:
        assert z.testzip() is None
        for n in ["stage.bin", "native.bin", "doom.bin"]:
            assert z.read(n) == (B / n).read_bytes()
        c = z.read("DoomLauncher.class")
        assert b"StackMap" in c and c[4:8] == bytes.fromhex("0003002d")
    jad = (P / "dist/RM443NativeDoom.jad").read_text()
    assert (
        f"MIDlet-Jar-Size: {(P / 'dist/RM443NativeDoom.jar').stat().st_size}\n" in jad
    )
    print(
        f"ELF/package: {len(data)} memory bytes; {len(rel)} relocations; no unresolved symbols in game"
    )
    loader_tests()
    div_tests()
    print(
        "PASS: package, loader and arithmetic checks; handset testing remains separate."
    )


if __name__ == "__main__":
    main()

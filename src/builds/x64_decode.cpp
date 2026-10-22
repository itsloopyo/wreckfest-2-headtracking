// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/x64_decode.h"

#include <cstring>

#include "hde/hde64.h"

namespace wf2_ht::builds {

namespace {

constexpr std::size_t kMaxInsn = 15;

// Fills the memory operand fields from a ModRM byte at p[0]. Returns the number
// of ModRM, SIB and displacement bytes consumed, or 0 if they run past avail.
std::size_t DecodeModrm(const std::uint8_t* p, std::size_t avail, bool rex_r, bool rex_x,
                        bool rex_b, Insn& out) {
    if (avail < 1) return 0;
    const std::uint8_t modrm = p[0];
    out.has_modrm = true;
    out.mod = static_cast<std::uint8_t>(modrm >> 6);
    out.reg = static_cast<std::uint8_t>(((modrm >> 3) & 7) | (rex_r ? 8 : 0));
    const std::uint8_t rm = modrm & 7;
    out.rm = static_cast<std::uint8_t>(rm | (rex_b ? 8 : 0));
    std::size_t n = 1;
    if (out.mod == 3) return n;

    std::size_t disp_size = out.mod == 1 ? 1 : out.mod == 2 ? 4 : 0;
    if (rm == 4) {
        if (avail < 2) return 0;
        const std::uint8_t sib = p[1];
        n = 2;
        const std::uint8_t sib_base = sib & 7;
        const std::uint8_t sib_index = static_cast<std::uint8_t>(((sib >> 3) & 7) | (rex_x ? 8 : 0));
        out.index = sib_index == 4 ? Insn::kNone : sib_index;
        if (sib_base == 5 && out.mod == 0) {
            out.base = Insn::kNone;
            disp_size = 4;
        } else {
            out.base = sib_base | (rex_b ? 8 : 0);
        }
    } else if (rm == 5 && out.mod == 0) {
        out.base = Insn::kRip;
        disp_size = 4;
    } else {
        out.base = out.rm;
    }
    if (avail < n + disp_size) return 0;
    if (disp_size == 1) out.disp = static_cast<std::int8_t>(p[n]);
    if (disp_size == 4) {
        std::int32_t d;
        std::memcpy(&d, p + n, 4);
        out.disp = d;
    }
    return n + disp_size;
}

// hde64 predates AVX and lengths C4/C5 as the legacy LES/LDS, which do not
// exist in 64-bit mode. The engine's code is full of VEX moves, so they are
// lengthed here before hde64 sees them.
bool DecodeVex(const std::uint8_t* p, std::size_t avail, std::uint32_t rva, Insn& out) {
    std::size_t i;
    bool rex_r, rex_x = false, rex_b = false;
    unsigned map;
    if (p[0] == 0xC5) {
        if (avail < 3) return false;
        rex_r = !(p[1] & 0x80);
        map = 1;
        i = 2;
    } else {
        if (avail < 4) return false;
        rex_r = !(p[1] & 0x80);
        rex_x = !(p[1] & 0x40);
        rex_b = !(p[1] & 0x20);
        map = p[1] & 0x1F;
        out.rex_w = (p[2] & 0x80) != 0;
        i = 3;
    }
    if (map < 1 || map > 3) return false;

    out.vex = true;
    out.opcode = 0x0F;
    out.opcode2 = p[i++];

    // vzeroupper / vzeroall carry no ModRM.
    if (!(map == 1 && out.opcode2 == 0x77)) {
        const std::size_t n = DecodeModrm(p + i, avail - i, rex_r, rex_x, rex_b, out);
        if (n == 0) return false;
        i += n;
    }

    const std::uint8_t op = out.opcode2;
    const bool imm8 = map == 3
        || (map == 1 && ((op >= 0x70 && op <= 0x73) || op == 0xC2 || op == 0xC4 || op == 0xC5 || op == 0xC6));
    if (imm8) {
        if (avail < i + 1) return false;
        out.has_imm = true;
        out.imm = static_cast<std::int8_t>(p[i]);
        ++i;
    }
    if (i > kMaxInsn) return false;
    out.rva = rva;
    out.len = static_cast<std::uint8_t>(i);
    return true;
}

// hde64 has no tables for the 0F 38 and 0F 3A maps (SSSE3 / SSE4: pshufb,
// pmovzx, ptest, pcmpestri, crc32), and lengths F3 0F B8 (popcnt) as the
// operand-less JMPE. Those are lengthed here: legacy prefixes, an optional REX,
// the opcode, ModRM, and an imm8 for the 0F 3A map.
bool DecodeLegacyMaps(const std::uint8_t* p, std::size_t avail, std::uint32_t rva, Insn& out, bool& handled) {
    handled = false;
    std::size_t i = 0;
    bool rep = false;
    while (i < avail && i < 4) {
        const std::uint8_t b = p[i];
        if (b == 0x66 || b == 0xF2 || b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36
            || b == 0x64 || b == 0x65 || b == 0x67 || b == 0xF0) { ++i; continue; }
        if (b == 0xF3) { rep = true; ++i; continue; }
        break;
    }
    std::uint8_t rex = 0;
    if (i < avail && (p[i] & 0xF0) == 0x40) rex = p[i++];
    if (i + 2 > avail || p[i] != 0x0F) return false;
    const std::uint8_t op = p[i + 1];
    bool imm8 = false;
    if (op == 0x38 || op == 0x3A) {
        if (i + 3 > avail) return false;
        imm8 = op == 0x3A;
        out.opcode2 = p[i + 2];
        i += 3;
    } else if (op == 0xB8 && rep) {
        out.opcode2 = op;
        i += 2;
    } else {
        return false;
    }
    handled = true;
    out.opcode = 0x0F;
    out.rex_w = (rex & 8) != 0;
    const std::size_t n = DecodeModrm(p + i, avail - i, rex & 4, rex & 2, rex & 1, out);
    if (n == 0) return false;
    i += n;
    if (imm8) {
        if (avail < i + 1) return false;
        out.has_imm = true;
        out.imm = static_cast<std::int8_t>(p[i]);
        ++i;
    }
    if (i > kMaxInsn) return false;
    out.rva = rva;
    out.len = static_cast<std::uint8_t>(i);
    return true;
}

}  // namespace

bool DecodeInsn(const std::uint8_t* code, std::size_t avail, std::uint32_t rva, Insn& out) {
    out = Insn{};
    if (avail == 0) return false;
    if (code[0] == 0x62) return false;  // EVEX
    if (code[0] == 0xC4 || code[0] == 0xC5) return DecodeVex(code, avail, rva, out);
    bool handled = false;
    if (DecodeLegacyMaps(code, avail, rva, out, handled)) return true;
    if (handled) return false;
    out = Insn{};

    // hde64 reads up to fifteen bytes whatever the instruction's length, so near
    // the end of a range it decodes from a zero-padded copy.
    std::uint8_t buf[kMaxInsn + 1] = {};
    const std::uint8_t* src = code;
    if (avail < sizeof(buf)) {
        std::memcpy(buf, code, avail);
        src = buf;
    }

    hde64s hs;
    const unsigned len = hde64_disasm(src, &hs);
    // F_ERROR_OPCODE alone marks opcodes hde64's tables call invalid in 64-bit
    // mode (lahf, sahf, ud2, the 0F 01 system group) while still lengthing them
    // correctly. Anything else is a length it cannot vouch for.
    if (hs.flags & (F_ERROR_LENGTH | F_ERROR_LOCK | F_ERROR_OPERAND)) return false;
    if (len == 0 || len > avail) return false;

    out.rva = rva;
    out.len = static_cast<std::uint8_t>(len);
    out.opcode = hs.opcode;
    out.opcode2 = hs.opcode == 0x0F ? hs.opcode2 : 0;
    out.rex_w = hs.rex_w != 0;

    if (hs.flags & F_MODRM) {
        out.has_modrm = true;
        out.mod = hs.modrm_mod;
        out.reg = static_cast<std::uint8_t>(hs.modrm_reg | (hs.rex_r ? 8 : 0));
        out.rm = static_cast<std::uint8_t>(hs.modrm_rm | (hs.rex_b ? 8 : 0));
        if (out.mod != 3) {
            if (hs.modrm_rm == 4) {
                const std::uint8_t idx = static_cast<std::uint8_t>(hs.sib_index | (hs.rex_x ? 8 : 0));
                out.index = idx == 4 ? Insn::kNone : idx;
                out.base = (hs.sib_base == 5 && out.mod == 0)
                    ? Insn::kNone
                    : (hs.sib_base | (hs.rex_b ? 8 : 0));
            } else if (hs.modrm_rm == 5 && out.mod == 0) {
                out.base = Insn::kRip;
            } else {
                out.base = out.rm;
            }
        }
    }
    if (hs.flags & F_DISP8) out.disp = static_cast<std::int8_t>(hs.disp.disp8);
    if (hs.flags & F_DISP32) out.disp = static_cast<std::int32_t>(hs.disp.disp32);

    std::int64_t imm = 0;
    bool has_imm = true;
    if (hs.flags & F_IMM64) imm = static_cast<std::int64_t>(hs.imm.imm64);
    else if (hs.flags & F_IMM32) imm = static_cast<std::int32_t>(hs.imm.imm32);
    else if (hs.flags & F_IMM16) imm = static_cast<std::int16_t>(hs.imm.imm16);
    else if (hs.flags & F_IMM8) imm = static_cast<std::int8_t>(hs.imm.imm8);
    else has_imm = false;

    if (hs.flags & F_RELATIVE) {
        out.relative = true;
        out.target = static_cast<std::uint32_t>(static_cast<std::int64_t>(rva) + len + imm);
    } else {
        out.has_imm = has_imm;
        out.imm = imm;
    }
    return true;
}

}  // namespace wf2_ht::builds

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The decoder's own paths, each on a hand-encoded instruction: the VEX, 0F 38,
// 0F 3A and popcnt lengths hde64 cannot give, the operand fields runtime
// discovery matches on, and the cases where the bytes run out. A wrong length
// derails the decode of the rest of a function, so every case checks it.

#include "builds/x64_decode.h"

#include "test_support.h"

#include <cstdio>
#include <initializer_list>
#include <vector>

using namespace wf2_ht::builds;
using wf_test::Check;

namespace {

bool Decode(std::initializer_list<std::uint8_t> bytes, Insn& out, std::uint32_t rva = 0x1000) {
    const std::vector<std::uint8_t> b(bytes);
    return DecodeInsn(b.data(), b.size(), rva, out);
}

void VexTest() {
    std::printf("VEX\n");
    Insn in;
    if (Check(Decode({ 0xC5, 0xF8, 0x28, 0x74, 0x24, 0x40 }, in), "C5 vmovaps xmm6, [rsp+0x40] decodes")) {
        Check(in.len == 6 && in.vex && in.opcode == 0x0F && in.opcode2 == 0x28, "length 6, VEX opcode 0F 28");
        Check(in.IsMem(4) && in.disp == 0x40, "memory operand [rsp+0x40]");
    }
    if (Check(Decode({ 0xC4, 0xC1, 0x78, 0x28, 0x06 }, in), "C4 vmovaps xmm0, [r14] decodes")) {
        Check(in.len == 5 && in.IsMem(14) && in.disp == 0, "length 5, inverted VEX.B gives base r14");
    }
    if (Check(Decode({ 0xC4, 0xE3, 0x79, 0x0F, 0xC1, 0x04 }, in), "C4 map 3 vpalignr decodes")) {
        Check(in.len == 6 && in.has_imm && in.imm == 4, "length 6 with its imm8");
    }
    if (Check(Decode({ 0xC5, 0xF8, 0x77 }, in), "vzeroupper decodes")) {
        Check(in.len == 3 && !in.has_modrm, "length 3, no ModRM");
    }
    Check(!Decode({ 0x62, 0xF1, 0x7C, 0x48, 0x28, 0xC1 }, in), "EVEX is refused");
}

void LegacyMapsTest() {
    std::printf("0F 38, 0F 3A and popcnt\n");
    Insn in;
    if (Check(Decode({ 0x66, 0x0F, 0x38, 0x00, 0xC1 }, in), "pshufb xmm0, xmm1 decodes")) {
        Check(in.len == 5 && in.opcode2 == 0x00 && in.opsize16, "length 5, with the 66 prefix recorded");
    }
    if (Check(Decode({ 0x66, 0x0F, 0x3A, 0x0F, 0xC1, 0x08 }, in), "palignr xmm0, xmm1, 8 decodes")) {
        Check(in.len == 6 && in.has_imm && in.imm == 8, "length 6 with its imm8");
    }
    if (Check(Decode({ 0xF3, 0x48, 0x0F, 0xB8, 0xC1 }, in), "popcnt rax, rcx decodes")) {
        Check(in.len == 5 && in.rex_w && in.reg == 0 && in.rm == 1, "length 5, REX.W, rax from rcx");
    }
}

void OperandTest() {
    std::printf("Operands\n");
    Insn in;
    if (Check(Decode({ 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00 }, in, 0x1000), "mov rax, [rip+0x10] decodes")) {
        Check(in.len == 7 && in.IsRipRelative() && in.RipTarget() == 0x1017, "targets the end of the instruction plus 0x10");
    }
    if (Check(Decode({ 0xC7, 0x05, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 }, in, 0x1000),
              "mov dword [rip+0x10], 1 decodes")) {
        Check(in.len == 10 && in.RipTarget() == 0x101A && in.has_imm && in.imm == 1,
              "the RIP target counts the immediate after the displacement");
    }
    if (Check(Decode({ 0x41, 0x8B, 0x45, 0x08 }, in), "mov eax, [r13+8] decodes")) {
        Check(in.len == 4 && in.IsMem(13) && in.disp == 8, "base r13, not RIP");
    }
    if (Check(Decode({ 0x41, 0x8B, 0x04, 0x24 }, in), "mov eax, [r12] decodes")) {
        Check(in.len == 4 && in.IsMem(12), "SIB base r12 with no index");
    }
    if (Check(Decode({ 0x8B, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12 }, in), "mov eax, [0x12345678] decodes")) {
        Check(in.len == 7 && in.base == Insn::kNone && in.index == Insn::kNone && in.disp == 0x12345678,
              "SIB with no base and no index");
    }
    if (Check(Decode({ 0x41, 0xB9, 0x10, 0x00, 0x00, 0x00 }, in), "mov r9d, 0x10 decodes")) {
        Check(in.opcode == 0xB9 && in.rex_b, "records REX.B, telling it from mov ecx");
    }
    if (Check(Decode({ 0xB9, 0x10, 0x00, 0x00, 0x00 }, in), "mov ecx, 0x10 decodes")) {
        Check(in.opcode == 0xB9 && !in.rex_b && in.imm == 0x10, "with no REX.B");
    }
    if (Check(Decode({ 0x66, 0xC7, 0x01, 0x05, 0x00 }, in), "mov word [rcx], 5 decodes")) {
        Check(in.len == 5 && in.opsize16 && in.opcode == 0xC7, "length 5, the 66 prefix recorded");
    }
    if (Check(Decode({ 0xE8, 0x00, 0x01, 0x00, 0x00 }, in, 0x2000), "call rel32 decodes")) {
        Check(in.relative && in.target == 0x2105, "targets the end of the call plus rel32");
    }
}

void TruncatedTest() {
    std::printf("Bytes that run out\n");
    Insn in;
    Check(!Decode({ 0x48, 0x8B, 0x05, 0x10, 0x00 }, in), "a RIP load cut short of its displacement is refused");
    Check(!Decode({ 0xC5, 0xF8 }, in), "a VEX prefix with no opcode is refused");
    Check(!Decode({ 0x66, 0x0F, 0x38 }, in), "an 0F 38 escape with no opcode is refused");
    Check(!Decode({ 0x66, 0x0F, 0x3A, 0x0F, 0xC1 }, in), "an 0F 3A instruction missing its imm8 is refused");
    Check(!Decode({ 0xE8, 0x00, 0x01 }, in), "a call cut short of its rel32 is refused");
    std::vector<std::uint8_t> none;
    Check(!DecodeInsn(none.data(), 0, 0, in), "no bytes at all is refused");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - x64 decoder tests\n");
    std::printf("========================================================\n");
    VexTest();
    LegacyMapsTest();
    OperandTest();
    TruncatedTest();
    return wf_test::Summary("x64 decoder");
}

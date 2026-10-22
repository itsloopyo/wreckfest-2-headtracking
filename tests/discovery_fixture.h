// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// Builds a small synthetic x64 PE image, mapped as the loader would map it,
// carrying the structure runtime discovery looks for: MSVC RTTI for the camera
// controllers and singletons, unwind data, and hand-assembled functions in the
// shapes the resolver recognises. Every instruction is written out below from
// its encoding; none of it is copied from the game. A Layout picks the
// addresses and offsets, so the same structure can be laid out differently to
// prove the resolver follows it, and Faults break one piece of it at a time.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "builds/build_profile.h"

namespace wf_fixture {

enum Reg { RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7, R14 = 14 };

struct Layout {
    std::uint32_t pad = 0;  // bytes of filler between functions, to move every RVA
    bool reverse_order = false;

    std::uint32_t camera_near = 0x78;  // far and fov follow at +4 and +8
    std::uint32_t render_camera = 0x1350;
    std::uint32_t camera_stride = 0x300;
    std::uint32_t transform = 0x10;
    std::uint32_t view_matrix = 0xA0;
    std::uint32_t active_controller = 0x28;
    std::uint32_t controller_base = 0x400;  // controllers at base + i * 0x100

    std::uint32_t gameplay_size = 0x15D8;
    std::uint32_t race_phase = 0x15D0;
    std::uint32_t countdown = 0x1574;
    std::uint32_t phase_countdown = 2;

    std::uint8_t pause_object = 0x70;
    std::uint8_t pause_flag = 0x59;

    std::uint32_t client_state = 0x370;
    std::uint32_t client_other_field = 0x3E8;
    std::uint8_t session_lo = 3;
    std::uint8_t session_span = 3;
    std::uint32_t server_state = 0x108;

    // Statics, as offsets into .data.
    std::uint32_t vm_static = 0x408;
    std::uint32_t gameplay_static = 0x418;
    std::uint32_t pause_static = 0x0C8;
    std::uint32_t client_static = 0x428;
    std::uint32_t server_static = 0x430;
    std::uint32_t race_flag = 0x068;
};

struct Faults {
    bool no_pause_strings = false;
    bool second_tail_jump = false;
    bool second_update_caller = false;
    bool three_cameras = false;
    bool no_photo_mode_rtti = false;
    bool no_range_check = false;
    bool no_gameplay_reader = false;
    bool undersized_gameplay_state = false;
    bool second_gameplay_static = false;
};

class Fixture {
public:
    static constexpr std::uint32_t kText = 0x1000, kTextSize = 0x10000;
    static constexpr std::uint32_t kRdata = 0x11000, kRdataSize = 0x8000;
    static constexpr std::uint32_t kData = 0x19000, kDataSize = 0x4000;
    static constexpr std::uint32_t kPdata = 0x1D000, kPdataSize = 0x2000;
    static constexpr std::uint32_t kImageSize = 0x1F000;

    std::vector<std::uint8_t> image;
    wf2_ht::builds::OffsetTable expected{};

    // `in_place` lays the absolute pointers out against the buffer's own address,
    // as the loader leaves them in a running module; otherwise against the
    // header's ImageBase, as they sit in the file.
    Fixture(const Layout& l, const Faults& f, bool in_place) : L(l), F(f) {
        image.assign(kImageSize, 0);
        pointer_base_ = in_place ? reinterpret_cast<std::uintptr_t>(image.data()) : kImageBase;
        Headers();
        Build();
    }

    static constexpr std::uint64_t kImageBase = 0x140000000ull;
    std::uint64_t PointerBase() const { return pointer_base_; }

private:
    Layout L;
    Faults F;
    std::uint64_t pointer_base_ = 0;
    std::uint32_t text_at_ = kText, rdata_at_ = kRdata, data_name_at_ = 0x2000, pdata_at_ = kPdata;
    std::map<std::string, std::uint32_t> labels_;
    struct Fixup { std::uint32_t at; std::string label; int kind; };  // kind 4 = rel32 at `at`
    std::vector<Fixup> fixups_;
    std::vector<std::uint8_t> code_;
    std::uint32_t fn_start_ = 0;

    // ---- raw writes ---------------------------------------------------
    void W8(std::uint32_t at, std::uint8_t v) { image[at] = v; }
    void W16(std::uint32_t at, std::uint16_t v) { std::memcpy(&image[at], &v, 2); }
    void W32(std::uint32_t at, std::uint32_t v) { std::memcpy(&image[at], &v, 4); }
    void W64(std::uint32_t at, std::uint64_t v) { std::memcpy(&image[at], &v, 8); }

    void Headers() {
        W16(0, 0x5A4D);
        W32(0x3C, 0x80);
        W32(0x80, 0x00004550);
        W16(0x84, 0x8664);
        W16(0x86, 4);                // sections
        W32(0x88, 0x12345678);       // TimeDateStamp no profile carries
        W16(0x94, 0xF0);             // optional header size
        const std::uint32_t opt = 0x98;
        W16(opt, 0x20B);
        W64(opt + 0x18, kImageBase);
        W32(opt + 0x38, kImageSize);
        W32(opt + 0x3C, 0x400);
        W32(opt + 0x6C, 16);         // NumberOfRvaAndSizes
        const std::uint32_t sec = opt + 0xF0;
        struct S { const char* n; std::uint32_t rva, size; } secs[] = {
            { ".text", kText, kTextSize }, { ".rdata", kRdata, kRdataSize },
            { ".data", kData, kDataSize }, { ".pdata", kPdata, kPdataSize } };
        for (int i = 0; i < 4; ++i) {
            const std::uint32_t s = sec + i * 40;
            std::memcpy(&image[s], secs[i].n, std::strlen(secs[i].n));
            W32(s + 8, secs[i].size);
            W32(s + 12, secs[i].rva);
        }
    }

    // ---- code emission --------------------------------------------------
    void B(std::initializer_list<std::uint8_t> bytes) { for (auto b : bytes) code_.push_back(b); }
    void D32(std::uint32_t v) { for (int i = 0; i < 4; ++i) code_.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
    std::uint32_t Here() const { return text_at_ + static_cast<std::uint32_t>(code_.size()); }
    void Rel32To(const std::string& label) { fixups_.push_back({ Here(), label, 4 }); D32(0); }
    void RipTo(std::uint32_t target, std::uint32_t insn_end_extra) {
        // disp32 is relative to the end of the instruction: here + 4 + bytes after the disp
        D32(target - (Here() + 4 + insn_end_extra));
    }
    static std::uint8_t Rex(bool w, int reg, int base) {
        return static_cast<std::uint8_t>(0x40 | (w ? 8 : 0) | (reg >= 8 ? 4 : 0) | (base >= 8 ? 1 : 0));
    }

    void LeaMem(int dst, int base, std::uint32_t disp) { B({ Rex(true, dst, base), 0x8D, static_cast<std::uint8_t>(0x80 | ((dst & 7) << 3) | (base & 7)) }); D32(disp); }
    void MovMemReg(int base, std::uint32_t disp, int src) { B({ Rex(true, src, base), 0x89, static_cast<std::uint8_t>(0x80 | ((src & 7) << 3) | (base & 7)) }); D32(disp); }
    void MovRegRip(int dst, std::uint32_t target) { B({ Rex(true, dst, 0), 0x8B, static_cast<std::uint8_t>(0x05 | ((dst & 7) << 3)) }); RipTo(target, 0); }
    void MovRipRax(std::uint32_t target) { B({ 0x48, 0x89, 0x05 }); RipTo(target, 0); }
    void LeaRip(int dst, std::uint32_t target) { B({ Rex(true, dst, 0), 0x8D, static_cast<std::uint8_t>(0x05 | ((dst & 7) << 3)) }); RipTo(target, 0); }
    void MovDwordImm(int base, std::uint32_t disp, std::uint32_t imm) { B({ 0xC7, static_cast<std::uint8_t>(0x80 | (base & 7)) }); D32(disp); D32(imm); }
    void CmpDwordImm8(int base, std::uint32_t disp, std::uint8_t imm) { B({ 0x83, static_cast<std::uint8_t>(0x80 | (7 << 3) | (base & 7)) }); D32(disp); B({ imm }); }
    void MovRegReg(int dst, int src) { B({ Rex(true, dst, src), 0x8B, static_cast<std::uint8_t>(0xC0 | ((dst & 7) << 3) | (src & 7)) }); }
    void Call(const std::string& l) { B({ 0xE8 }); Rel32To(l); }
    void Jmp(const std::string& l) { B({ 0xE9 }); Rel32To(l); }
    void Push(int r) { B({ static_cast<std::uint8_t>(0x50 + r) }); }
    void Pop(int r) { B({ static_cast<std::uint8_t>(0x58 + r) }); }
    void SubRsp(std::uint8_t n) { B({ 0x48, 0x83, 0xEC, n }); }
    void AddRsp(std::uint8_t n) { B({ 0x48, 0x83, 0xC4, n }); }
    void Ret() { B({ 0xC3 }); }

    // A function with unwind data. `chain_split` > 0 splits it into a primary
    // chunk and a chained one at that byte offset.
    void Begin(const std::string& name) {
        for (std::uint32_t i = 0; i < L.pad; ++i) code_.push_back(0xCC);
        labels_[name] = Here();
        fn_start_ = Here();
    }
    void End(std::uint32_t chain_split = 0) {
        const std::uint32_t end = Here();
        if (chain_split == 0) {
            Pdata(fn_start_, end, Unwind(false, 0, 0, 0));
        } else {
            const std::uint32_t mid = fn_start_ + chain_split;
            const std::uint32_t primary_unwind = Unwind(false, 0, 0, 0);
            Pdata(fn_start_, mid, primary_unwind);
            Pdata(mid, end, Unwind(true, fn_start_, mid, primary_unwind));
        }
    }
    void BeginLeaf(const std::string& name) {
        for (std::uint32_t i = 0; i < L.pad; ++i) code_.push_back(0xCC);
        labels_[name] = Here();
    }

    std::uint32_t Unwind(bool chained, std::uint32_t b, std::uint32_t e, std::uint32_t u) {
        const std::uint32_t at = rdata_at_;
        W8(at, static_cast<std::uint8_t>(chained ? 0x21 : 0x01));
        W8(at + 2, 0);
        rdata_at_ += 4;
        if (chained) {
            W32(rdata_at_, b);
            W32(rdata_at_ + 4, e);
            W32(rdata_at_ + 8, u);
            rdata_at_ += 12;
        }
        return at;
    }
    std::vector<std::array<std::uint32_t, 3>> pdata_;
    void Pdata(std::uint32_t b, std::uint32_t e, std::uint32_t u) { pdata_.push_back({ b, e, u }); }

    // ---- RTTI and data --------------------------------------------------
    std::uint32_t RttiVtable(const std::string& cls) {
        const std::string name = ".?AV" + cls + "@romu@@";
        const std::uint32_t td = kData + data_name_at_;
        std::memcpy(&image[td + 16], name.c_str(), name.size() + 1);
        data_name_at_ += static_cast<std::uint32_t>((16 + name.size() + 1 + 15) & ~15u);
        rdata_at_ = (rdata_at_ + 7) & ~7u;
        const std::uint32_t col = rdata_at_;
        W32(col, 1);
        W32(col + 4, 0);
        W32(col + 12, td);
        W32(col + 20, col);
        rdata_at_ += 24;
        rdata_at_ = (rdata_at_ + 7) & ~7u;
        W64(rdata_at_, pointer_base_ + col);
        const std::uint32_t vtable = rdata_at_ + 8;
        rdata_at_ += 8 + 8 * 4;
        return vtable;
    }
    std::uint32_t RdataString(const char* s) {
        const std::uint32_t at = rdata_at_;
        std::memcpy(&image[at], s, std::strlen(s) + 1);
        rdata_at_ += static_cast<std::uint32_t>((std::strlen(s) + 1 + 7) & ~7u);
        return at;
    }

    void Build();
};

inline void Fixture::Build() {
    using namespace wf2_ht::builds;
    const char* controllers[] = { "GarageCamera", "CarPaintCamera", "CarCamera", "FreeCamera",
                                  "TrackSideCamera", "AnimatedCamera", "PhotoModeCamera" };
    std::uint32_t vt[7] = {};
    for (int i = 0; i < 7; ++i) {
        if (i == 6 && F.no_photo_mode_rtti) continue;
        vt[i] = RttiVtable(controllers[i]);
    }
    const std::uint32_t gs_vt = RttiVtable("GameplayState");
    const std::uint32_t client_vt = RttiVtable("NetClient");
    const std::uint32_t server_vt = RttiVtable("NetServer");
    std::uint32_t enter_str = 0, exit_str = 0;
    if (!F.no_pause_strings) {
        enter_str = RdataString("GameState_PauseEnter");
        exit_str = RdataString("GameState_PauseExit");
    } else {
        enter_str = RdataString("GameState_Unrelated1");
        exit_str = RdataString("GameState_Unrelated2");
    }

    const std::uint32_t vm_static = kData + L.vm_static, gs_static = kData + L.gameplay_static;
    const std::uint32_t pause_static = kData + L.pause_static, client_static = kData + L.client_static;
    const std::uint32_t server_static = kData + L.server_static, race_flag = kData + L.race_flag;

    // The functions, in an order the layout can reverse.
    std::vector<std::function<void()>> emit;

    emit.push_back([&] {
        Begin("camera_ctor");
        MovDwordImm(RCX, L.camera_near, 0x3DCCCCCD);
        MovDwordImm(RCX, L.camera_near + 4, 0x447A0000);
        MovDwordImm(RCX, L.camera_near + 8, 0x3F832BF5);
        Ret();
        End();
        // A decoy: the same field of view seeded without the clip planes.
        Begin("decoy_fov");
        MovDwordImm(RCX, 0x200, 0x3F832BF5);
        Ret();
        End();
    });
    for (int i = 0; i < 7; ++i) {
        emit.push_back([&, i] {
            if (vt[i] == 0) return;
            Begin("ctl_ctor_" + std::to_string(i));
            LeaRip(RAX, vt[i]);
            MovMemReg(RCX, 0, RAX);
            Ret();
            End();
        });
    }
    emit.push_back([&] {
        Begin("vm_ctor");
        Push(RBX);
        SubRsp(0x20);
        MovRegReg(RBX, RCX);
        for (int i = 0; i < 7; ++i) {
            if (vt[i] == 0) continue;
            LeaMem(RCX, RBX, L.controller_base + i * 0x100);
            Call("ctl_ctor_" + std::to_string(i));
        }
        for (int k = 0; k < (F.three_cameras ? 3 : 4); ++k) {
            LeaMem(RCX, RBX, L.render_camera + k * L.camera_stride);
            Call("camera_ctor");
        }
        AddRsp(0x20);
        Pop(RBX);
        Ret();
        End();
    });
    emit.push_back([&] {
        Begin("set_camera_type");
        for (int i = 0; i < 7; ++i) {
            if (vt[i] == 0) continue;
            LeaMem(RAX, RDI, L.controller_base + i * 0x100);
            MovMemReg(RDI, L.active_controller, RAX);
        }
        Ret();
        End();
    });
    emit.push_back([&] {
        Begin("invert");
        Ret();
        End();
        Begin("update_view_matrix");
        Push(RBX);
        SubRsp(0x20);
        MovRegReg(RBX, RCX);
        LeaMem(RDX, RBX, L.transform);
        LeaMem(RCX, RBX, L.view_matrix);
        Call("invert");
        AddRsp(0x20);
        Pop(RBX);
        Ret();
        End();
    });
    auto update_body = [&](const char* name, const char* target) {
        Begin(name);
        Push(RDI);
        SubRsp(0x50);
        MovRegReg(RBP, RCX);
        LeaMem(RCX, RBP, L.render_camera);
        B({ 0x48, 0x8B, 0x5C, 0x24, 0x68 });              // mov rbx, [rsp+0x68]
        B({ 0xC5, 0xF8, 0x28, 0x74, 0x24, 0x40 });        // vmovaps xmm6, [rsp+0x40]
        AddRsp(0x50);
        Pop(RDI);
        Jmp(target);
        End();
    };
    emit.push_back([&] {
        update_body("vm_update", "update_view_matrix");
        if (F.second_tail_jump) update_body("vm_update_twin", "update_view_matrix");
    });
    emit.push_back([&] {
        Begin("frame");
        SubRsp(0x28);
        MovRegRip(RCX, vm_static);
        MovRegReg(RDX, RBX);
        Call("vm_update");
        if (F.second_update_caller) {
            MovRegRip(RCX, vm_static);
            Call("vm_update");
        }
        AddRsp(0x28);
        Ret();
        End();
    });
    auto singleton = [&](const std::string& n, std::uint32_t vtable, std::uint32_t st, std::uint32_t size) {
        Begin(n + "_ctor");
        LeaRip(RAX, vtable);
        MovMemReg(RCX, 0, RAX);
        Ret();
        End();
        Begin(n + "_factory");
        SubRsp(0x28);
        B({ 0xB9 }); D32(size);                            // mov ecx, size
        Call("alloc");
        MovRegReg(RCX, RAX);
        Call(n + "_ctor");
        B({ 0xEB, 0x03 });                                 // jmp over the null path
        B({ 0x49, 0x8B, 0xC6 });                           // mov rax, r14
        MovRipRax(st);
        AddRsp(0x28);
        Ret();
        End();
    };
    emit.push_back([&] {
        Begin("alloc");
        Ret();
        End();
        const std::uint32_t gs_size = F.undersized_gameplay_state ? L.race_phase : L.gameplay_size;
        singleton("gameplay", gs_vt, gs_static, gs_size);
        if (F.second_gameplay_static) {
            Begin("gameplay_factory2");
            SubRsp(0x28);
            B({ 0xB9 }); D32(gs_size);
            Call("alloc");
            MovRegReg(RCX, RAX);
            Call("gameplay_ctor");
            MovRipRax(gs_static + 0x100);
            AddRsp(0x28);
            Ret();
            End();
        }
        singleton("client", client_vt, client_static, 0x400);
        singleton("server", server_vt, server_static, 0x200);
    });
    emit.push_back([&] {
        BeginLeaf("race_started");
        B({ 0x88, 0x0D }); RipTo(race_flag, 0);            // mov [rip+flag], cl
        Ret();
        Begin("countdown_tick");
        Push(RSI);
        SubRsp(0x20);
        MovRegReg(RSI, RCX);
        CmpDwordImm8(RSI, L.countdown, 0);
        B({ 0x0F, 0x8C }); D32(26);                         // jl done
        CmpDwordImm8(RSI, L.race_phase, static_cast<std::uint8_t>(L.phase_countdown));
        const std::uint32_t split = Here() - fn_start_;
        B({ 0x75, 0x11 });                                  // jne done
        B({ 0xB1, 0x01 });                                  // mov cl, 1
        MovDwordImm(RSI, L.race_phase, L.phase_countdown + 1);
        Call("race_started");
        AddRsp(0x20);
        Pop(RSI);
        Ret();
        End(split);
        if (!F.no_gameplay_reader) {
            Begin("read_phase");
            MovRegRip(RAX, gs_static);
            B({ 0x8B, 0x80 }); D32(L.race_phase);           // mov eax, [rax+phase]
            Ret();
            End();
        }
    });
    emit.push_back([&] {
        BeginLeaf("pause_owner");
        MovRegRip(RAX, pause_static);
        Ret();
        BeginLeaf("pause_state");
        B({ 0x48, 0x8B, 0x41, L.pause_object });           // mov rax, [rcx+obj]
        Ret();
        Begin("set_paused");
        Push(RBX);
        Push(RDI);
        SubRsp(0x20);
        B({ 0x40, 0x0F, 0xB6, 0xF9 });                     // movzx edi, cl
        Call("pause_owner");
        MovRegReg(RCX, RAX);
        MovRegReg(RBX, RAX);
        Call("pause_state");
        B({ 0x40, 0x38, 0x78, L.pause_flag });             // cmp [rax+flag], dil
        MovRegReg(RCX, RBX);
        Call("pause_state");
        B({ 0x40, 0x88, 0x78, L.pause_flag });             // mov [rax+flag], dil
        LeaRip(RDX, exit_str);
        LeaRip(RAX, enter_str);
        AddRsp(0x20);
        Pop(RDI);
        Pop(RBX);
        Ret();
        End();
    });
    emit.push_back([&] {
        BeginLeaf("client_state");
        B({ 0x8B, 0x81 }); D32(L.client_state);            // mov eax, [rcx+state]
        Ret();
        BeginLeaf("client_other");
        B({ 0x8B, 0x81 }); D32(L.client_other_field);
        Ret();
        BeginLeaf("server_state");
        B({ 0x8B, 0x81 }); D32(L.server_state);
        Ret();
        Begin("in_session");
        SubRsp(0x28);
        MovRegRip(RCX, client_static);
        B({ 0x48, 0x85, 0xC9 });                           // test rcx, rcx
        B({ 0x75, 0x07 });                                 // jne call
        B({ 0x32, 0xC0 });                                 // xor al, al
        AddRsp(0x28);
        Ret();
        Call("client_state");
        if (!F.no_range_check) {
            B({ 0x83, 0xC0, static_cast<std::uint8_t>(-static_cast<int>(L.session_lo)) });  // add eax, -lo
            B({ 0x83, 0xF8, L.session_span });             // cmp eax, span
        } else {
            B({ 0x85, 0xC0 });                             // test eax, eax
        }
        B({ 0x0F, 0x96, 0xC0 });                           // setbe al
        AddRsp(0x28);
        Ret();
        End();
        Begin("client_other_user");
        SubRsp(0x28);
        MovRegRip(RCX, client_static);
        Call("client_other");
        B({ 0x83, 0xF8, 0x03 });                           // cmp eax, 3
        AddRsp(0x28);
        Ret();
        End();
        Begin("server_active");
        SubRsp(0x28);
        MovRegRip(RCX, server_static);
        B({ 0x48, 0x85, 0xC9 });
        B({ 0x74, 0x05 });
        Call("server_state");
        AddRsp(0x28);
        Ret();
        End();
    });

    if (L.reverse_order) std::reverse(emit.begin(), emit.end());
    for (auto& e : emit) e();

    // Resolve the rel32 fixups and copy the code in.
    std::memcpy(&image[kText], code_.data(), code_.size());
    for (const Fixup& fx : fixups_) {
        const std::uint32_t target = labels_.at(fx.label);
        W32(fx.at, target - (fx.at + 4));
    }
    std::sort(pdata_.begin(), pdata_.end());
    for (const auto& p : pdata_) {
        W32(pdata_at_, p[0]); W32(pdata_at_ + 4, p[1]); W32(pdata_at_ + 8, p[2]);
        pdata_at_ += 12;
    }
    const std::uint32_t opt = 0x98;
    W32(opt + 0x70 + 3 * 8, kPdata);
    W32(opt + 0x70 + 3 * 8 + 4, pdata_at_ - kPdata);

    OffsetTable& t = expected;
    t.view_manager_update_rva = labels_.at("vm_update");
    t.camera_view_matrix_rva = labels_.at("update_view_matrix");
    t.camera_world_transform = L.transform;
    t.camera_world_transform_floats = 16;
    t.view_manager_ptr_rva = vm_static;
    t.view_manager_render_camera = L.render_camera;
    t.view_manager_active_controller = L.active_controller;
    t.garage_camera_vtable_rva = vt[0];
    t.car_paint_camera_vtable_rva = vt[1];
    t.car_camera_vtable_rva = vt[2];
    t.free_camera_vtable_rva = vt[3];
    t.track_side_camera_vtable_rva = vt[4];
    t.animated_camera_vtable_rva = vt[5];
    t.photo_mode_camera_vtable_rva = vt[6];
    t.gameplay_state_ptr_rva = gs_static;
    t.race_phase_offset = L.race_phase;
    t.race_phase_countdown = L.phase_countdown;
    t.race_phase_racing = L.phase_countdown + 1;
    t.countdown_ms_offset = L.countdown;
    t.pause_state_ptr_rva = pause_static;
    t.pause_state_object = L.pause_object;
    t.pause_state_paused = L.pause_flag;
    t.net_client_ptr_rva = client_static;
    t.net_client_state = L.client_state;
    t.net_client_session_lo = L.session_lo;
    t.net_client_session_hi = static_cast<unsigned>(L.session_lo + L.session_span);
    t.net_server_ptr_rva = server_static;
    t.net_server_state = L.server_state;
}

}  // namespace wf_fixture

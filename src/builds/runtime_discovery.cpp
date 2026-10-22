// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/runtime_discovery.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "builds/x64_decode.h"

namespace wf2_ht::builds {

#define WF2_FIELD(f) { #f, &OffsetTable::f }
const OffsetField kOffsetFields[] = {
    WF2_FIELD(view_manager_update_rva),
    WF2_FIELD(camera_view_matrix_rva),
    WF2_FIELD(camera_world_transform),
    WF2_FIELD(camera_world_transform_floats),
    WF2_FIELD(view_manager_ptr_rva),
    WF2_FIELD(view_manager_render_camera),
    WF2_FIELD(view_manager_active_controller),
    WF2_FIELD(garage_camera_vtable_rva),
    WF2_FIELD(car_paint_camera_vtable_rva),
    WF2_FIELD(car_camera_vtable_rva),
    WF2_FIELD(free_camera_vtable_rva),
    WF2_FIELD(track_side_camera_vtable_rva),
    WF2_FIELD(animated_camera_vtable_rva),
    WF2_FIELD(photo_mode_camera_vtable_rva),
    WF2_FIELD(gameplay_state_ptr_rva),
    WF2_FIELD(race_phase_offset),
    WF2_FIELD(race_phase_countdown),
    WF2_FIELD(race_phase_racing),
    WF2_FIELD(countdown_ms_offset),
    WF2_FIELD(pause_state_ptr_rva),
    WF2_FIELD(pause_state_object),
    WF2_FIELD(pause_state_paused),
    WF2_FIELD(net_client_ptr_rva),
    WF2_FIELD(net_client_state),
    WF2_FIELD(net_client_session_lo),
    WF2_FIELD(net_client_session_hi),
    WF2_FIELD(net_server_ptr_rva),
    WF2_FIELD(net_server_state),
};
#undef WF2_FIELD
const std::size_t kOffsetFieldCount = sizeof(kOffsetFields) / sizeof(kOffsetFields[0]);

namespace {

constexpr int kRax = 0, kRcx = 1, kRdx = 2, kRsp = 4;

// The camera constructor's seeds: near 0.1, far 1000.0 and a vertical field of
// view of 1.024779 rad, stored as three consecutive floats.
constexpr std::uint32_t kSeedNear = 0x3DCCCCCD;
constexpr std::uint32_t kSeedFar = 0x447A0000;
constexpr std::uint32_t kSeedFov = 0x3F832BF5;

// The mod composes the head pose into a 4x4 of floats.
constexpr unsigned kTransformFloats = 16;

struct Section {
    std::uint32_t rva = 0;
    std::uint32_t size = 0;
    bool Contains(std::uint64_t rva_, std::uint64_t len = 1) const {
        return rva_ >= rva && rva_ + len <= static_cast<std::uint64_t>(rva) + size;
    }
};

struct Chunk {
    std::uint32_t begin;
    std::uint32_t end;
    std::uint32_t unwind;
    std::uint32_t primary;
};

struct Ref {
    std::uint32_t target;
    std::uint32_t site;  // instruction RVA
    bool operator<(const Ref& o) const { return target < o.target || (target == o.target && site < o.site); }
};

class Fail {
public:
    explicit Fail(std::string& out) : out_(out) {}
    bool operator()(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        out_ = buf;
        return false;
    }
private:
    std::string& out_;
};

class Image {
public:
    explicit Image(const ImageView& v) : v_(v) {}

    bool Parse(Fail& fail) {
        if (v_.base == nullptr || v_.size < 0x400) return fail("image: no headers");
        if (U16(0) != 0x5A4D) return fail("image: no MZ header");
        const std::uint32_t nt = U32(0x3C);
        if (!Has(nt, 0x108) || U32(nt) != 0x00004550) return fail("image: no PE header");
        if (U16(nt + 4) != 0x8664) return fail("image: not an x64 image");
        if (U16(nt + 0x18) != 0x20B) return fail("image: not PE32+");
        const std::uint16_t nsec = U16(nt + 6);
        const std::uint16_t opt_size = U16(nt + 0x14);
        if (U32(nt + 0x18 + 0x38) > v_.size) return fail("image: SizeOfImage 0x%X exceeds the mapped 0x%zX", U32(nt + 0x18 + 0x38), v_.size);
        const std::uint32_t exc_dir = nt + 0x18 + 0x70 + 3 * 8;
        if (!Has(exc_dir, 8)) return fail("image: no exception directory");
        pdata_ = { U32(exc_dir), U32(exc_dir + 4) };

        const std::uint32_t sec0 = nt + 0x18 + opt_size;
        if (!Has(sec0, nsec * 40u)) return fail("image: section table truncated");
        for (std::uint16_t i = 0; i < nsec; ++i) {
            const std::uint32_t s = sec0 + i * 40u;
            char name[9] = {};
            std::memcpy(name, v_.base + s, 8);
            const Section sec{ U32(s + 12), U32(s + 8) };
            if (!Has(sec.rva, sec.size)) return fail("image: section %s lies outside the image", name);
            if (std::strcmp(name, ".text") == 0) text_ = sec;
            else if (std::strcmp(name, ".rdata") == 0) rdata_ = sec;
            else if (std::strcmp(name, ".data") == 0) data_ = sec;
        }
        if (text_.size == 0 || rdata_.size == 0 || data_.size == 0) return fail("image: missing .text, .rdata or .data");
        if (pdata_.size == 0 || pdata_.size % 12 || !Has(pdata_.rva, pdata_.size)) return fail("image: bad exception directory");
        return true;
    }

    bool Has(std::uint64_t rva, std::uint64_t len) const { return rva + len <= v_.size && rva + len >= rva; }
    std::uint16_t U16(std::uint32_t rva) const { std::uint16_t x; std::memcpy(&x, v_.base + rva, 2); return x; }
    std::uint32_t U32(std::uint32_t rva) const { std::uint32_t x; std::memcpy(&x, v_.base + rva, 4); return x; }
    std::uint8_t U8(std::uint32_t rva) const { return v_.base[rva]; }
    const std::uint8_t* At(std::uint32_t rva) const { return v_.base + rva; }
    std::uint64_t PointerBase() const { return v_.pointer_base; }

    const Section& Text() const { return text_; }
    const Section& Rdata() const { return rdata_; }
    const Section& Data() const { return data_; }
    const Section& Pdata() const { return pdata_; }

    // Every offset in [sec) where `needle` occurs, at the given alignment.
    std::vector<std::uint32_t> Find(const Section& sec, const void* needle, std::size_t n, std::uint32_t align = 1) const {
        std::vector<std::uint32_t> out;
        const std::uint8_t* p = v_.base + sec.rva;
        const std::uint8_t* end = p + sec.size;
        const std::uint8_t first = *static_cast<const std::uint8_t*>(needle);
        while (p + n <= end) {
            const void* hit = std::memchr(p, first, static_cast<std::size_t>(end - p) - n + 1);
            if (hit == nullptr) break;
            const std::uint8_t* h = static_cast<const std::uint8_t*>(hit);
            const auto rva = static_cast<std::uint32_t>(h - v_.base);
            if (rva % align == 0 && std::memcmp(h, needle, n) == 0) out.push_back(rva);
            p = h + 1;
        }
        return out;
    }

private:
    ImageView v_;
    Section text_, rdata_, data_, pdata_;
};

using Insns = std::vector<Insn>;

class Analysis {
public:
    Analysis(const Image& img) : img_(img) {}

    bool Build(Fail& fail) {
        const Section& pd = img_.Pdata();
        for (std::uint32_t a = pd.rva; a < pd.rva + pd.size; a += 12) {
            const Chunk c{ img_.U32(a), img_.U32(a + 4), img_.U32(a + 8), 0 };
            if (c.begin == 0) continue;
            if (c.end <= c.begin || !img_.Text().Contains(c.begin, c.end - c.begin)) continue;
            if (!chunks_.empty() && c.begin < chunks_.back().end) return fail("image: exception directory is not sorted");
            chunks_.push_back(c);
        }
        if (chunks_.empty()) return fail("image: no functions in the exception directory");
        for (Chunk& c : chunks_) c.primary = ResolvePrimary(c);

        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            const Chunk& c = chunks_[i];
            std::uint32_t at = c.begin;
            while (at < c.end) {
                Insn in;
                if (!DecodeInsn(img_.At(at), c.end - at, at, in)) break;
                if (in.relative && in.len == 5 && (in.opcode == 0xE8 || in.opcode == 0xE9)) {
                    rel_.push_back({ in.target, in.rva });
                }
                if (in.IsRipRelative()) rip_.push_back({ in.RipTarget(), in.rva });
                at += in.len;
            }
            by_primary_.emplace(c.primary, i);
        }
        std::sort(rel_.begin(), rel_.end());
        std::sort(rip_.begin(), rip_.end());

        const Section& t = img_.Text();
        for (std::uint32_t at = t.rva; at + 5 <= t.rva + t.size; ++at) {
            const std::uint8_t op = img_.U8(at);
            if (op != 0xE8 && op != 0xE9) continue;
            const auto rel = static_cast<std::int32_t>(img_.U32(at + 1));
            raw_rel_.push_back(static_cast<std::uint32_t>(static_cast<std::int64_t>(at) + 5 + rel));
        }
        std::sort(raw_rel_.begin(), raw_rel_.end());
        return true;
    }

    const Chunk* ChunkAt(std::uint32_t rva) const {
        auto it = std::upper_bound(chunks_.begin(), chunks_.end(), rva,
                                   [](std::uint32_t r, const Chunk& c) { return r < c.begin; });
        if (it == chunks_.begin()) return nullptr;
        --it;
        return rva < it->end ? &*it : nullptr;
    }
    std::uint32_t PrimaryOf(std::uint32_t rva) const {
        const Chunk* c = ChunkAt(rva);
        return c ? c->primary : 0;
    }
    bool IsFunctionStart(std::uint32_t rva) const {
        const Chunk* c = ChunkAt(rva);
        return c && c->begin == rva && c->primary == rva;
    }

    // Every rel32 call or jump landing on `target`.
    std::vector<Ref> RelTo(std::uint32_t target) const { return Range(rel_, target); }
    std::vector<Ref> RipTo(std::uint32_t target) const { return Range(rip_, target); }

    // E8 / E9 rel32 byte patterns anywhere in .text that land on `target`. A
    // chunk's decode stops at the first instruction the decoder cannot length
    // (EVEX), or at a jump table the compiler placed inside it, so a call after
    // either is missing from RelTo. Every decoded reference is also one of
    // these, so a larger count here means RelTo cannot vouch for a count.
    std::size_t RawRelCount(std::uint32_t target) const {
        const auto [lo, hi] = std::equal_range(raw_rel_.begin(), raw_rel_.end(), target);
        return static_cast<std::size_t>(hi - lo);
    }

    // All of a function's chunks decoded in address order.
    const Insns& Function(std::uint32_t primary) const {
        auto cached = fn_cache_.find(primary);
        if (cached != fn_cache_.end()) return cached->second;
        Insns& out = fn_cache_[primary];
        std::vector<std::size_t> idx;
        auto [lo, hi] = by_primary_.equal_range(primary);
        for (auto it = lo; it != hi; ++it) idx.push_back(it->second);
        std::sort(idx.begin(), idx.end());
        for (std::size_t i : idx) {
            const Chunk& c = chunks_[i];
            for (std::uint32_t at = c.begin; at < c.end;) {
                Insn in;
                if (!DecodeInsn(img_.At(at), c.end - at, at, in)) break;
                out.push_back(in);
                at += in.len;
            }
        }
        return out;
    }

    // A function with no unwind data - a leaf that touches no stack - decoded
    // up to its first ret or direct jmp, or max_insns.
    Insns Leaf(std::uint32_t rva, std::size_t max_insns = 16) const {
        Insns out;
        const Section& t = img_.Text();
        for (std::uint32_t at = rva; t.Contains(at) && out.size() < max_insns;) {
            Insn in;
            if (!DecodeInsn(img_.At(at), t.rva + t.size - at, at, in)) break;
            out.push_back(in);
            at += in.len;
            if (in.opcode == 0xC3 || in.opcode == 0xE9 || in.opcode == 0xEB) break;
        }
        return out;
    }

    // The function's body if it has unwind data, otherwise its leaf decode.
    Insns Body(std::uint32_t rva) const {
        if (IsFunctionStart(rva)) return Function(rva);
        return Leaf(rva, 64);
    }

    static std::size_t IndexOf(const Insns& fn, std::uint32_t rva) {
        for (std::size_t i = 0; i < fn.size(); ++i) if (fn[i].rva == rva) return i;
        return SIZE_MAX;
    }

private:
    static std::vector<Ref> Range(const std::vector<Ref>& v, std::uint32_t target) {
        auto lo = std::lower_bound(v.begin(), v.end(), Ref{ target, 0 });
        std::vector<Ref> out;
        for (auto it = lo; it != v.end() && it->target == target; ++it) out.push_back(*it);
        return out;
    }

    // Follows UNW_FLAG_CHAININFO back to the entry that owns the function.
    std::uint32_t ResolvePrimary(const Chunk& c) const {
        std::uint32_t begin = c.begin, unwind = c.unwind;
        for (int depth = 0; depth < 32; ++depth) {
            if (!img_.Has(unwind, 4)) return begin;
            const std::uint8_t flags = img_.U8(unwind) >> 3;
            if (!(flags & 4)) return begin;
            const std::uint8_t count = img_.U8(unwind + 2);
            const std::uint32_t chained = unwind + 4 + ((count + 1u) & ~1u) * 2;
            if (!img_.Has(chained, 12)) return begin;
            begin = img_.U32(chained);
            unwind = img_.U32(chained + 8);
        }
        return begin;
    }

    const Image& img_;
    std::vector<Chunk> chunks_;
    std::multimap<std::uint32_t, std::size_t> by_primary_;
    std::vector<Ref> rel_;
    std::vector<Ref> rip_;
    std::vector<std::uint32_t> raw_rel_;
    mutable std::unordered_map<std::uint32_t, Insns> fn_cache_;
};

bool IsCall(const Insn& in) { return in.opcode == 0xE8 && in.relative; }
bool IsLea(const Insn& in, int dst) { return in.opcode == 0x8D && in.rex_w && in.reg == dst && in.HasMemory(); }
bool IsMovRegFromMem(const Insn& in, int dst) { return in.opcode == 0x8B && in.rex_w && in.reg == dst && in.HasMemory(); }
bool IsMovRegReg(const Insn& in, int dst, int src) {
    if (!in.has_modrm || in.mod != 3 || !in.rex_w) return false;
    if (in.opcode == 0x8B) return in.reg == dst && in.rm == src;
    if (in.opcode == 0x89) return in.rm == dst && in.reg == src;
    return false;
}
int RegRegDst(const Insn& in) {
    if (!in.has_modrm || in.mod != 3) return Insn::kNone;
    if (in.opcode == 0x8B) return in.reg;
    if (in.opcode == 0x89) return in.rm;
    return Insn::kNone;
}
bool IsMovDwordImm(const Insn& in) { return in.opcode == 0xC7 && !in.rex_w && !in.opsize16 && in.reg == 0 && in.HasMemory() && in.has_imm; }
bool IsCmpDwordImm8(const Insn& in) { return in.opcode == 0x83 && !in.rex_w && !in.opsize16 && in.reg == 7 && in.HasMemory() && in.has_imm; }
// mov ecx, imm32
bool IsMovEcxImm(const Insn& in) { return in.opcode == 0xB9 && !in.rex_w && !in.rex_b && !in.opsize16 && in.has_imm; }
// Whether `in` leaves `reg` holding something else: a call, or a load, lea,
// xor, immediate or register move into it.
bool Clobbers(const Insn& in, int reg) {
    if (in.opcode == 0xE8) return true;
    if ((in.opcode == 0x8B || in.opcode == 0x8D || in.opcode == 0x33) && in.has_modrm && in.reg == reg) return true;
    if ((in.opcode & 0xF8) == 0xB8 && ((in.opcode & 7) | (in.rex_b ? 8 : 0)) == reg) return true;
    return RegRegDst(in) == reg;
}
bool IsRet(const Insn& in) { return in.opcode == 0xC3; }

// mov rax, [rip + static] ; ret
bool LeafLoadsStatic(const Insns& leaf, std::uint32_t& static_rva) {
    if (leaf.size() != 2 || !IsRet(leaf[1])) return false;
    if (!IsMovRegFromMem(leaf[0], kRax) || !leaf[0].IsRipRelative()) return false;
    static_rva = leaf[0].RipTarget();
    return true;
}
// mov r(e)ax, [rcx + field] ; ret, of the given width
bool LeafReadsField(const Insns& leaf, bool qword, std::uint32_t& field) {
    if (leaf.size() != 2 || !IsRet(leaf[1])) return false;
    const Insn& m = leaf[0];
    if (m.opcode != 0x8B || m.rex_w != qword || m.opsize16 || m.reg != kRax || !m.IsMem(kRcx) || m.disp <= 0) return false;
    field = static_cast<std::uint32_t>(m.disp);
    return true;
}

struct Discovery {
    const Image& img;
    const Analysis& an;
    Fail& fail;
    OffsetTable t{};

    std::uint32_t camera_ctor = 0;
    std::uint32_t vm_ctor = 0;
    int vm_ctor_base = Insn::kNone;
    std::map<std::uint32_t, std::uint32_t> controller_offsets;  // vtable -> offset in the view manager
    std::uint32_t gameplay_state_size = 0;

    // The rel32 references to `target`, refused when the raw bytes of .text
    // hold more of them than were decoded.
    bool CallsTo(std::uint32_t target, const char* what, std::vector<Ref>& out) {
        out = an.RelTo(target);
        const std::size_t raw = an.RawRelCount(target);
        if (raw != out.size()) {
            return fail("%s: %zu rel32 references to 0x%X in .text but %zu decoded", what, raw, target, out.size());
        }
        return true;
    }

    bool Vtable(const char* mangled, unsigned int& out) {
        const std::size_t n = std::strlen(mangled) + 1;
        std::vector<std::uint32_t> names = img.Find(img.Data(), mangled, n);
        for (std::uint32_t r : img.Find(img.Rdata(), mangled, n)) names.push_back(r);
        std::vector<std::uint32_t> vtables;
        for (std::uint32_t name : names) {
            const std::uint32_t td = name - 16;
            for (std::uint32_t hit : img.Find(img.Rdata(), &td, 4, 4)) {
                const std::uint32_t col = hit - 12;
                if (!img.Rdata().Contains(col, 24)) continue;
                if (img.U32(col) != 1 || img.U32(col + 4) != 0 || img.U32(col + 20) != col) continue;
                const std::uint64_t col_ptr = img.PointerBase() + col;
                for (std::uint32_t meta : img.Find(img.Rdata(), &col_ptr, 8, 8)) vtables.push_back(meta + 8);
            }
        }
        if (vtables.size() != 1) return fail("rtti: expected one primary vtable for %s, found %zu", mangled, vtables.size());
        out = vtables[0];
        return true;
    }

    bool Vtables() {
        return Vtable(".?AVGarageCamera@romu@@", t.garage_camera_vtable_rva)
            && Vtable(".?AVCarPaintCamera@romu@@", t.car_paint_camera_vtable_rva)
            && Vtable(".?AVCarCamera@romu@@", t.car_camera_vtable_rva)
            && Vtable(".?AVFreeCamera@romu@@", t.free_camera_vtable_rva)
            && Vtable(".?AVTrackSideCamera@romu@@", t.track_side_camera_vtable_rva)
            && Vtable(".?AVAnimatedCamera@romu@@", t.animated_camera_vtable_rva)
            && Vtable(".?AVPhotoModeCamera@romu@@", t.photo_mode_camera_vtable_rva);
    }

    // The one function that stores the near, far and field-of-view seeds as
    // three consecutive floats through the same register.
    bool CameraCtor() {
        std::set<std::uint32_t> fns;
        for (std::uint32_t hit : img.Find(img.Text(), &kSeedFov, 4)) {
            const std::uint32_t fn = an.PrimaryOf(hit);
            if (fn == 0) continue;
            const Insns& body = an.Function(fn);
            for (const Insn& fov : body) {
                if (!IsMovDwordImm(fov) || static_cast<std::uint32_t>(fov.imm) != kSeedFov || fov.index != Insn::kNone) continue;
                bool near_ok = false, far_ok = false;
                for (const Insn& in : body) {
                    if (!IsMovDwordImm(in) || in.base != fov.base || in.index != Insn::kNone) continue;
                    if (static_cast<std::uint32_t>(in.imm) == kSeedNear && in.disp == fov.disp - 8) near_ok = true;
                    if (static_cast<std::uint32_t>(in.imm) == kSeedFar && in.disp == fov.disp - 4) far_ok = true;
                }
                if (near_ok && far_ok) fns.insert(fn);
            }
        }
        if (fns.size() != 1) return fail("camera constructor: expected one function seeding near/far/fov, found %zu", fns.size());
        camera_ctor = *fns.begin();
        return true;
    }

    // The view manager embeds four cameras back to back; the only function that
    // constructs four cameras at an even stride off one register is its
    // constructor, and the first of them is the one it renders through.
    bool RenderCamera() {
        std::vector<Ref> calls;
        if (!CallsTo(camera_ctor, "camera constructor", calls)) return false;
        std::map<std::pair<std::uint32_t, int>, std::vector<std::uint32_t>> groups;
        for (const Ref& r : calls) {
            const std::uint32_t fn = an.PrimaryOf(r.site);
            if (fn == 0) continue;
            const Insns& body = an.Function(fn);
            const std::size_t i = Analysis::IndexOf(body, r.site);
            if (i == SIZE_MAX || i == 0 || !IsCall(body[i])) continue;
            const Insn& lea = body[i - 1];
            if (!IsLea(lea, kRcx) || lea.index != Insn::kNone || lea.base == kRsp || lea.base == Insn::kRip
                || lea.base == Insn::kNone || lea.disp <= 0) continue;
            groups[{ fn, lea.base }].push_back(static_cast<std::uint32_t>(lea.disp));
        }
        std::vector<std::pair<std::pair<std::uint32_t, int>, std::vector<std::uint32_t>>> found;
        for (auto& [key, offs] : groups) {
            if (offs.size() != 4) continue;
            std::sort(offs.begin(), offs.end());
            const std::uint32_t stride = offs[1] - offs[0];
            if (stride == 0 || offs[2] - offs[1] != stride || offs[3] - offs[2] != stride) continue;
            found.push_back({ key, offs });
        }
        if (found.size() != 1) return fail("view manager: expected one constructor building four evenly spaced cameras, found %zu", found.size());
        vm_ctor = found[0].first.first;
        vm_ctor_base = found[0].first.second;
        t.view_manager_render_camera = found[0].second[0];
        return true;
    }

    // ViewManager::Update ends by restoring its saved registers and tail-jumping
    // to Camera::UpdateViewMatrix with rcx pointing at the render camera.
    static bool IsEpilogue(const Insn& in) {
        if (in.opcode == 0x8B && in.rex_w && in.IsMem(kRsp) && in.reg != kRcx) return true;  // mov reg, [rsp+x]
        if (in.vex && in.opcode2 == 0x28 && in.IsMem(kRsp)) return true;                     // vmovaps xmm, [rsp+x]
        if ((in.opcode == 0x83 || in.opcode == 0x81) && in.rex_w && in.mod == 3 && in.rm == kRsp && in.reg == 0) return true;  // add rsp, n
        if (in.opcode >= 0x58 && in.opcode <= 0x5F && in.opcode != 0x59) return true;        // pop, not rcx
        return false;
    }

    bool HookTargets() {
        const std::uint32_t rc = t.view_manager_render_camera;
        struct Tail { std::uint32_t jmp; std::uint32_t target; };
        std::vector<Tail> tails;
        std::set<std::uint32_t> seen;
        for (std::uint32_t hit : img.Find(img.Text(), &rc, 4)) {
            const std::uint32_t fn = an.PrimaryOf(hit);
            if (fn == 0) continue;
            const Insns& body = an.Function(fn);
            for (std::size_t i = 0; i < body.size(); ++i) {
                const Insn& lea = body[i];
                if (!IsLea(lea, kRcx) || lea.disp != static_cast<std::int64_t>(rc) || lea.index != Insn::kNone) continue;
                if (lea.rva > hit || hit >= lea.rva + lea.len) continue;
                for (std::size_t j = i + 1; j < body.size() && j <= i + 12; ++j) {
                    const Insn& in = body[j];
                    if (in.opcode == 0xE9 && in.relative && in.len == 5) {
                        if (seen.insert(in.rva).second) tails.push_back({ in.rva, in.target });
                        break;
                    }
                    if (!IsEpilogue(in)) break;
                }
            }
        }
        if (tails.size() != 1) return fail("view matrix: expected one tail jump with rcx at the render camera (+0x%X), found %zu", rc, tails.size());

        const std::uint32_t vmx = tails[0].target;
        if (!an.IsFunctionStart(vmx)) return fail("view matrix: tail jump target 0x%X is not a function entry", vmx);
        std::vector<Ref> vmx_refs;
        if (!CallsTo(vmx, "view matrix", vmx_refs)) return false;
        if (vmx_refs.size() != 1) return fail("view matrix: expected the tail jump to be its only caller, found %zu", vmx_refs.size());

        // Camera::UpdateViewMatrix keeps `this` in a callee-saved register and
        // inverts the camera-to-world transform into the view matrix:
        //   lea rdx, [this + transform] ; lea rcx, [this + view] ; call invert
        // `this` is copied out of rcx in the entry chunk before the first call
        // or branch, while rcx still holds it.
        const Insns& vbody = an.Function(vmx);
        const Chunk* entry = an.ChunkAt(vmx);
        std::set<int> selves;
        for (const Insn& in : vbody) {
            if (in.rva < entry->begin) continue;
            if (in.rva >= entry->end || in.relative) break;
            const int dst = RegRegDst(in);
            if (dst != Insn::kNone && IsMovRegReg(in, dst, kRcx)) selves.insert(dst);
        }
        if (selves.size() != 1) return fail("view matrix: expected one copy of the camera pointer at the top of 0x%X, found %zu", vmx, selves.size());
        const int self = *selves.begin();
        std::vector<std::uint32_t> transforms;
        for (std::size_t i = 0; i + 2 < vbody.size(); ++i) {
            const Insn& a = vbody[i];
            const Insn& b = vbody[i + 1];
            if (IsLea(a, kRdx) && a.IsMem(self) && IsLea(b, kRcx) && b.IsMem(self) && IsCall(vbody[i + 2])
                && a.disp >= 0 && b.disp >= a.disp + static_cast<std::int64_t>(kTransformFloats * 4)) {
                transforms.push_back(static_cast<std::uint32_t>(a.disp));
            }
        }
        if (transforms.size() != 1) return fail("view matrix: expected one transform-to-view inversion, found %zu", transforms.size());

        const std::uint32_t update = an.PrimaryOf(tails[0].jmp);
        if (update == 0 || !an.IsFunctionStart(update)) return fail("view manager update: the tail jump is not inside a function");
        std::vector<Ref> callers;
        if (!CallsTo(update, "view manager update", callers)) return false;
        if (callers.size() != 1) return fail("view manager update: expected one caller, found %zu", callers.size());
        const std::uint32_t caller_fn = an.PrimaryOf(callers[0].site);
        if (caller_fn == 0) return fail("view manager update: its caller has no unwind data");
        const Insns& cbody = an.Function(caller_fn);
        const std::size_t ci = Analysis::IndexOf(cbody, callers[0].site);
        if (ci == SIZE_MAX || !IsCall(cbody[ci])) return fail("view manager update: its caller is not a call");

        // mov rcx, [rip + static], then nothing but register moves into other
        // registers before the call.
        std::uint32_t vm_static = 0;
        for (std::size_t k = ci; k-- > 0 && ci - k <= 3;) {
            const Insn& in = cbody[k];
            if (IsMovRegFromMem(in, kRcx) && in.IsRipRelative()) { vm_static = in.RipTarget(); break; }
            const int dst = RegRegDst(in);
            if (dst == Insn::kNone || dst == kRcx) break;
        }
        if (vm_static == 0 || !img.Data().Contains(vm_static, 8)) return fail("view manager: the update's caller does not load it from a static in .data");

        t.camera_view_matrix_rva = vmx;
        t.view_manager_update_rva = update;
        t.camera_world_transform = transforms[0];
        t.camera_world_transform_floats = kTransformFloats;
        t.view_manager_ptr_rva = vm_static;
        return true;
    }

    // The view manager constructor runs each controller's constructor on an
    // inline sub-object; a constructor is recognised by the controller vtable
    // it stores. SetCameraType then points one field at whichever sub-object
    // drives the camera, and that field is the active controller.
    bool ActiveController() {
        const std::set<std::uint32_t> vtables{ t.garage_camera_vtable_rva, t.car_paint_camera_vtable_rva,
            t.car_camera_vtable_rva, t.free_camera_vtable_rva, t.track_side_camera_vtable_rva,
            t.animated_camera_vtable_rva, t.photo_mode_camera_vtable_rva };
        const Insns& ctor = an.Function(vm_ctor);
        for (std::size_t i = 1; i < ctor.size(); ++i) {
            if (!IsCall(ctor[i])) continue;
            const Insn& lea = ctor[i - 1];
            if (!IsLea(lea, kRcx) || !lea.IsMem(vm_ctor_base) || lea.disp <= 0) continue;
            for (const Insn& in : an.Body(ctor[i].target)) {
                if (in.opcode == 0x8D && in.IsRipRelative() && vtables.count(in.RipTarget())) {
                    const auto [it, added] = controller_offsets.emplace(in.RipTarget(), static_cast<std::uint32_t>(lea.disp));
                    if (!added && it->second != static_cast<std::uint32_t>(lea.disp)) {
                        return fail("view manager: controller vtable 0x%X constructed at both +0x%X and +0x%X",
                                    in.RipTarget(), it->second, static_cast<std::uint32_t>(lea.disp));
                    }
                    break;
                }
            }
        }
        if (controller_offsets.size() < 3) return fail("view manager: only %zu controller sub-objects identified in its constructor", controller_offsets.size());

        std::set<std::uint32_t> offsets;
        for (const auto& kv : controller_offsets) offsets.insert(kv.second);
        std::map<std::uint32_t, std::set<std::uint32_t>> field_to_offsets;
        for (std::uint32_t off : offsets) {
            if (off < 0x80) continue;  // disp8 encodings carry no 4-byte anchor
            for (std::uint32_t hit : img.Find(img.Text(), &off, 4)) {
                const std::uint32_t fn = an.PrimaryOf(hit);
                if (fn == 0) continue;
                const Insns& body = an.Function(fn);
                for (std::size_t i = 0; i + 1 < body.size(); ++i) {
                    const Insn& lea = body[i];
                    const Insn& mov = body[i + 1];
                    if (lea.opcode != 0x8D || !lea.rex_w || lea.disp != static_cast<std::int64_t>(off)) continue;
                    if (lea.base == Insn::kNone || lea.base == Insn::kRip || lea.index != Insn::kNone) continue;
                    if (mov.opcode != 0x89 || !mov.rex_w || mov.reg != lea.reg || !mov.IsMem(lea.base) || mov.disp < 0) continue;
                    field_to_offsets[static_cast<std::uint32_t>(mov.disp)].insert(off);
                }
            }
        }
        std::vector<std::uint32_t> fields;
        for (const auto& [field, offs] : field_to_offsets) if (offs.size() >= 3) fields.push_back(field);
        if (fields.size() != 1) return fail("view manager: expected one field pointed at three or more controllers, found %zu", fields.size());
        if (fields[0] >= *offsets.begin()) return fail("view manager: the active controller field +0x%X is inside a controller", fields[0]);
        t.view_manager_active_controller = fields[0];
        return true;
    }

    // The functions that store `vtable` into a new object, and the statics
    // their callers store the result into. `size` gets the allocation each
    // caller makes for it, when it asks for one.
    bool SingletonStatic(const char* what, std::uint32_t vtable, std::uint32_t& out, std::uint32_t* size) {
        std::set<std::uint32_t> statics, sizes;
        std::set<std::uint32_t> ctors;
        for (const Ref& r : an.RipTo(vtable)) {
            const std::uint32_t fn = an.PrimaryOf(r.site);
            if (fn != 0) ctors.insert(fn);
        }
        for (std::uint32_t ctor : ctors) {
            std::vector<Ref> calls;
            if (!CallsTo(ctor, what, calls)) return false;
            for (const Ref& c : calls) {
                const std::uint32_t fn = an.PrimaryOf(c.site);
                if (fn == 0) continue;
                const Insns& body = an.Function(fn);
                const std::size_t i = Analysis::IndexOf(body, c.site);
                if (i == SIZE_MAX || !IsCall(body[i])) continue;
                // The store can sit after a short jump that merges the
                // allocation-failed path back in.
                std::size_t j = i + 1;
                for (int steps = 0; steps < 4 && j < body.size(); ++steps) {
                    const Insn& in = body[j];
                    if (in.opcode == 0x89 && in.rex_w && in.reg == kRax && in.IsRipRelative()) {
                        statics.insert(in.RipTarget());
                        break;
                    }
                    if (in.opcode == 0xEB && in.relative) {
                        j = Analysis::IndexOf(body, in.target);
                        if (j == SIZE_MAX) break;
                        continue;
                    }
                    ++j;
                }
                // The allocation: mov ecx, size directly ahead of the allocator
                // call, a few instructions before the constructor call (the
                // engine zeroes the block with a second call in between).
                if (size != nullptr) {
                    for (std::size_t k = i; k-- > 0 && i - k <= 16;) {
                        if (IsMovEcxImm(body[k]) && k + 1 < i && IsCall(body[k + 1])) {
                            sizes.insert(static_cast<std::uint32_t>(body[k].imm));
                            break;
                        }
                    }
                }
            }
        }
        if (statics.size() != 1) return fail("%s: expected one static holding the singleton, found %zu", what, statics.size());
        if (!img.Data().Contains(*statics.begin(), 8)) return fail("%s: the singleton static is not in .data", what);
        out = *statics.begin();
        if (size != nullptr) {
            if (sizes.size() != 1) return fail("%s: expected one allocation size, found %zu", what, sizes.size());
            *size = *sizes.begin();
        }
        return true;
    }

    bool GameplayState() {
        unsigned int vt = 0;
        if (!Vtable(".?AVGameplayState@romu@@", vt)) return false;
        std::uint32_t st = 0;
        if (!SingletonStatic("GameplayState", vt, st, &gameplay_state_size)) return false;
        t.gameplay_state_ptr_rva = st;
        return true;
    }

    // The countdown tick:
    //   cmp dword [this+countdown], 0 ; jl done
    //   cmp dword [this+phase], N ; ... jne
    //   mov dword [this+phase], N+1 ; call SetRaceStarted
    // where SetRaceStarted is the leaf `mov [rip+flag], cl ; ret`.
    bool RacePhase() {
        std::set<std::uint32_t> setters;
        static const std::uint8_t kSetterHead[] = { 0x88, 0x0D };
        for (std::uint32_t hit : img.Find(img.Text(), kSetterHead, 2)) {
            if (img.Text().Contains(hit, 7) && img.U8(hit + 6) == 0xC3) setters.insert(hit);
        }
        struct Tick { std::uint32_t setter, phase, countdown, from, to; };
        std::vector<Tick> ticks;
        for (std::uint32_t s : setters) {
            for (const Ref& c : an.RelTo(s)) {
                const std::uint32_t fn = an.PrimaryOf(c.site);
                if (fn == 0) continue;
                const Insns& body = an.Function(fn);
                const std::size_t i = Analysis::IndexOf(body, c.site);
                if (i == SIZE_MAX || !IsCall(body[i])) continue;
                std::size_t w = SIZE_MAX;
                for (std::size_t k = i; k-- > 0 && i - k <= 2;) if (IsMovDwordImm(body[k])) { w = k; break; }
                if (w == SIZE_MAX) continue;
                const Insn& write = body[w];
                if (write.index != Insn::kNone || write.base == kRsp || write.base == Insn::kRip || write.disp <= 0) continue;
                std::size_t cmp_phase = SIZE_MAX;
                for (std::size_t k = w; k-- > 0 && w - k <= 8;) {
                    const Insn& in = body[k];
                    if (IsCmpDwordImm8(in) && in.IsMem(write.base) && in.disp == write.disp) { cmp_phase = k; break; }
                }
                if (cmp_phase == SIZE_MAX || cmp_phase < 2) continue;
                const Insn& jl = body[cmp_phase - 1];
                const Insn& cmp_cd = body[cmp_phase - 2];
                const bool is_jl = jl.relative && ((jl.opcode == 0x0F && jl.opcode2 == 0x8C) || jl.opcode == 0x7C);
                if (!is_jl || !IsCmpDwordImm8(cmp_cd) || !cmp_cd.IsMem(write.base) || cmp_cd.imm != 0 || cmp_cd.disp <= 0) continue;
                const std::int64_t from = body[cmp_phase].imm;
                if (from <= 0 || write.imm != from + 1) continue;
                ticks.push_back({ s, static_cast<std::uint32_t>(write.disp), static_cast<std::uint32_t>(cmp_cd.disp),
                                  static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(write.imm) });
            }
        }
        if (ticks.size() != 1) return fail("race phase: expected one countdown tick, found %zu", ticks.size());
        const Tick& tk = ticks[0];
        std::vector<Ref> setter_calls;
        if (!CallsTo(tk.setter, "race phase setter", setter_calls)) return false;
        if (tk.phase + 4 > gameplay_state_size || tk.countdown + 4 > gameplay_state_size) {
            return fail("race phase: +0x%X / +0x%X do not fit a GameplayState of 0x%X bytes", tk.phase, tk.countdown, gameplay_state_size);
        }
        // Somewhere loads the GameplayState singleton and reads the phase
        // through it, which is what places the tick's object as that singleton.
        bool owned = false;
        for (const Ref& r : an.RipTo(t.gameplay_state_ptr_rva)) {
            const std::uint32_t fn = an.PrimaryOf(r.site);
            if (fn == 0 || owned) continue;
            const Insns& body = an.Function(fn);
            const std::size_t i = Analysis::IndexOf(body, r.site);
            if (i == SIZE_MAX || body[i].opcode != 0x8B || !body[i].rex_w) continue;
            const int reg = body[i].reg;
            for (std::size_t k = i + 1; k < body.size() && k <= i + 8; ++k) {
                if (body[k].IsMem(reg) && body[k].disp == static_cast<std::int64_t>(tk.phase)) { owned = true; break; }
                if (Clobbers(body[k], reg)) break;
            }
        }
        if (!owned) return fail("race phase: nothing reads +0x%X through the GameplayState static", tk.phase);
        t.race_phase_offset = tk.phase;
        t.countdown_ms_offset = tk.countdown;
        t.race_phase_countdown = tk.from;
        t.race_phase_racing = tk.to;
        return true;
    }

    // SetPaused, the one function naming both GameState_PauseEnter and
    // GameState_PauseExit, opens with
    //   call get_owner ; mov rcx, rax ; ... ; call get_state
    // and compares and writes the pause byte through what get_state returns.
    bool Pause() {
        auto functions_naming = [&](const char* s) {
            std::set<std::uint32_t> fns;
            for (std::uint32_t str : img.Find(img.Rdata(), s, std::strlen(s) + 1)) {
                for (const Ref& r : an.RipTo(str)) {
                    const std::uint32_t fn = an.PrimaryOf(r.site);
                    if (fn != 0) fns.insert(fn);
                }
            }
            return fns;
        };
        const std::set<std::uint32_t> enter = functions_naming("GameState_PauseEnter");
        const std::set<std::uint32_t> exit = functions_naming("GameState_PauseExit");
        if (enter.size() != 1 || exit != enter) return fail("pause: expected one function naming both pause events, found %zu / %zu", enter.size(), exit.size());
        const Insns& body = an.Function(*enter.begin());

        std::vector<std::size_t> calls;
        for (std::size_t i = 0; i < body.size(); ++i) if (IsCall(body[i])) calls.push_back(i);
        if (calls.size() < 2) return fail("pause: SetPaused makes fewer than two calls");
        const std::size_t c0 = calls[0], c1 = calls[1];
        if (c0 + 1 >= body.size() || !IsMovRegReg(body[c0 + 1], kRcx, kRax)) return fail("pause: the owner getter's result is not handed to the state getter");
        std::uint32_t st = 0, object = 0;
        if (!LeafLoadsStatic(an.Leaf(body[c0].target), st)) return fail("pause: the first call is not a static getter");
        if (!LeafReadsField(an.Leaf(body[c1].target), true, object)) return fail("pause: the second call is not a member getter");
        if (!img.Data().Contains(st, 8)) return fail("pause: the static is not in .data");

        // Only accesses through rax while it still holds what the state getter
        // returned count.
        const std::uint32_t state_getter = body[c1].target;
        std::set<std::uint32_t> cmp_flag, mov_flag;
        bool rax_is_state = false;
        for (std::size_t i = c1; i < body.size(); ++i) {
            const Insn& in = body[i];
            if (IsCall(in)) { rax_is_state = in.target == state_getter; continue; }
            if (rax_is_state && in.IsMem(kRax) && in.disp >= 0) {
                if (in.opcode == 0x38) cmp_flag.insert(static_cast<std::uint32_t>(in.disp));
                if (in.opcode == 0x88) mov_flag.insert(static_cast<std::uint32_t>(in.disp));
            }
            if (Clobbers(in, kRax)) rax_is_state = false;
        }
        if (cmp_flag.size() != 1 || mov_flag != cmp_flag) return fail("pause: expected one byte compared and written, found %zu / %zu", cmp_flag.size(), mov_flag.size());
        t.pause_state_ptr_rva = st;
        t.pause_state_object = object;
        t.pause_state_paused = *cmp_flag.begin();
        return true;
    }

    // Every place that loads the singleton into rcx and calls a getter leaf
    // `mov eax, [rcx+state] ; ret`.
    struct Getter { std::uint32_t field; std::size_t index; const Insns* body; };
    std::vector<Getter> StateGetters(std::uint32_t st) {
        std::vector<Getter> out;
        for (const Ref& r : an.RipTo(st)) {
            const std::uint32_t fn = an.PrimaryOf(r.site);
            if (fn == 0) continue;
            const Insns& body = an.Function(fn);
            const std::size_t i = Analysis::IndexOf(body, r.site);
            if (i == SIZE_MAX || !IsMovRegFromMem(body[i], kRcx)) continue;
            for (std::size_t k = i + 1; k < body.size() && k <= i + 8; ++k) {
                if (!IsCall(body[k])) continue;
                std::uint32_t field = 0;
                if (LeafReadsField(an.Leaf(body[k].target), false, field)) out.push_back({ field, k, &body });
                break;
            }
        }
        return out;
    }

    bool Network() {
        unsigned int client_vt = 0, server_vt = 0;
        if (!Vtable(".?AVNetClient@romu@@", client_vt) || !Vtable(".?AVNetServer@romu@@", server_vt)) return false;
        std::uint32_t client = 0, server = 0;
        if (!SingletonStatic("NetClient", client_vt, client, nullptr)) return false;
        if (!SingletonStatic("NetServer", server_vt, server, nullptr)) return false;

        // The client has more than one getter leaf; the state is the one whose
        // result goes straight into the engine's session range check:
        //   add eax, -lo ; cmp eax, hi - lo   ->   (state - lo) <= hi - lo
        struct Range { std::uint32_t field, lo, hi; bool operator<(const Range& o) const {
            return field < o.field || (field == o.field && (lo < o.lo || (lo == o.lo && hi < o.hi))); } };
        std::set<Range> ranges;
        for (const Getter& g : StateGetters(client)) {
            const Insns& b = *g.body;
            if (g.index + 2 >= b.size()) continue;
            const Insn& add = b[g.index + 1];
            const Insn& cmp = b[g.index + 2];
            if (add.opcode == 0x83 && add.mod == 3 && add.rm == kRax && add.reg == 0 && add.imm < 0
                && cmp.opcode == 0x83 && cmp.mod == 3 && cmp.rm == kRax && cmp.reg == 7 && cmp.imm >= 0) {
                const auto lo = static_cast<std::uint32_t>(-add.imm);
                ranges.insert({ g.field, lo, lo + static_cast<std::uint32_t>(cmp.imm) });
            }
        }
        if (ranges.size() != 1) return fail("NetClient: expected one session-state range check, found %zu", ranges.size());

        std::set<std::uint32_t> server_fields;
        for (const Getter& g : StateGetters(server)) server_fields.insert(g.field);
        if (server_fields.size() != 1) return fail("NetServer: expected one state field across its getters, found %zu", server_fields.size());

        t.net_client_ptr_rva = client;
        t.net_client_state = ranges.begin()->field;
        t.net_client_session_lo = ranges.begin()->lo;
        t.net_client_session_hi = ranges.begin()->hi;
        t.net_server_ptr_rva = server;
        t.net_server_state = *server_fields.begin();
        return true;
    }
};

}  // namespace

bool DiscoverOffsets(const ImageView& view, OffsetTable& out, std::string& failure) {
    Fail fail(failure);
    Image img(view);
    if (!img.Parse(fail)) return false;
    Analysis an(img);
    if (!an.Build(fail)) return false;

    Discovery d{ img, an, fail };
    if (!d.Vtables() || !d.CameraCtor() || !d.RenderCamera() || !d.HookTargets() || !d.ActiveController()
        || !d.GameplayState() || !d.RacePhase()) {
        return false;
    }
    std::string pause_failure;
    if (!d.Pause()) pause_failure = failure;
    if (!d.Network()) return false;
    if (!IsProfileComplete(BuildProfile{ "discovered", {}, d.t })) return fail("discovery: a required value resolved to zero");
    out = d.t;
    failure = pause_failure;
    return true;
}

}  // namespace wf2_ht::builds

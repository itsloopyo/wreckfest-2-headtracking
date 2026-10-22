// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "builds/build_profile.h"

namespace wf2_ht::builds {

// A PE image laid out the way the loader maps it: headers at offset 0 and each
// section at its RVA. `pointer_base` is the address the image's absolute
// pointers are relative to - the module's load address in the running game,
// the header's ImageBase for an image mapped from the file on disk.
struct ImageView {
    const std::uint8_t* base = nullptr;
    std::size_t size = 0;
    std::uint64_t pointer_base = 0;
};

// Resolves every value in OffsetTable from the structure of the image: class
// names in the RTTI, a few seed constants and strings, and the call and data
// relationships between the functions that use them. Each step has to find
// exactly one candidate and pass the checks that tie it to the others. On any
// failure `out` is left untouched and `failure` names the check that failed.
//
// The pause chain is optional, as it is in a measured profile: when it alone
// cannot be resolved the call still succeeds, with the three pause fields zero
// and `failure` naming the pause check that failed. On full success `failure`
// is empty.
//
// Scans the whole of .text once. Call it once, at startup, off the loader lock.
bool DiscoverOffsets(const ImageView& image, OffsetTable& out, std::string& failure);

struct OffsetField {
    const char* name;
    unsigned int OffsetTable::*member;
};
extern const OffsetField kOffsetFields[];
extern const std::size_t kOffsetFieldCount;

}  // namespace wf2_ht::builds

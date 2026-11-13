// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/program_header.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/shader_cache.h"

namespace {
Vulkan::FixedPipelineState MakeState() {
    Vulkan::FixedPipelineState state;
    std::memset(&state, 0, sizeof(state));
    state.dynamic_vertex_input.Assign(1);
    // Float attributes 0-3, an unsigned one at 6.
    state.attribute_types = 0b01'01'01'01 | (u64{3} << 12);
    state.color_formats = {0xd5, 0xd5, 0, 0, 0, 0, 0, 0};
    for (auto& attachment : state.attachments) {
        attachment.mask_r.Assign(1);
        attachment.mask_a.Assign(1);
        attachment.factor_source_rgb.Assign(4);
        attachment.factor_dest_rgb.Assign(5);
        attachment.equation_rgb.Assign(1);
    }
    state.attachments[0].enable.Assign(1);
    state.depth_bounds_min = 0x3f000000;
    state.depth_bounds_max = 0x3f800000;
    state.line_stipple_factor = 2;
    state.line_stipple_pattern = 0xf0f0;
    return state;
}
} // Anonymous namespace

TEST_CASE("Canonical native keys keep the attribute types the vertex program reads",
          "[canonical_native_key]") {
    auto state = MakeState();
    state.CanonicalizeNative(0b0000'0101); // attributes 0 and 2
    CHECK(state.attribute_types == 0b01'00'01);

    auto every = MakeState();
    every.CanonicalizeNative(~0U);
    CHECK(every.attribute_types == MakeState().attribute_types);

    auto mesh = MakeState();
    mesh.CanonicalizeNative(0);
    CHECK(mesh.attribute_types == 0);

    // Without dynamic vertex input the field holds the divisors, which stay.
    auto divisors = MakeState();
    divisors.dynamic_vertex_input.Assign(0);
    divisors.CanonicalizeNative(0);
    CHECK(divisors.enabled_divisors == MakeState().enabled_divisors);
}

TEST_CASE("Canonical native keys clear blending the pipeline does not use",
          "[canonical_native_key]") {
    auto state = MakeState();
    const auto blended = state.attachments[0].raw;
    state.CanonicalizeNative(~0U);
    // Blending on: unchanged.
    CHECK(state.attachments[0].raw == blended);
    // Blending off: only the colour write mask is left.
    CHECK(state.attachments[1].raw == 0b1001);
    // Past the last colour target: cleared.
    for (size_t index = 2; index < state.attachments.size(); ++index) {
        CHECK(state.attachments[index].raw == 0);
    }

    // With dynamic blending the attachments are not part of the key and stay.
    auto dynamic = MakeState();
    dynamic.extended_dynamic_state_3_blend.Assign(1);
    dynamic.CanonicalizeNative(~0U);
    CHECK(dynamic.attachments[5].raw == MakeState().attachments[5].raw);
}

TEST_CASE("Canonical native keys clear dynamic depth bounds and an unused stipple",
          "[canonical_native_key]") {
    auto state = MakeState();
    state.CanonicalizeNative(~0U);
    CHECK(state.depth_bounds_min == 0);
    CHECK(state.depth_bounds_max == 0);
    CHECK(state.line_stipple_factor == 0);
    CHECK(state.line_stipple_pattern == 0);

    auto stippled = MakeState();
    stippled.dynamic_state.line_stipple_enable.Assign(1);
    stippled.CanonicalizeNative(~0U);
    CHECK(stippled.line_stipple_factor == 2);
    CHECK(stippled.line_stipple_pattern == 0xf0f0);
}

TEST_CASE("Draws that differ only in cleared state share a canonical key",
          "[canonical_native_key]") {
    auto a = MakeState();
    auto b = MakeState();
    b.attribute_types |= u64{2} << 40;            // an attribute the program does not read
    b.attachments[1].factor_dest_a.Assign(7);     // blending off on target 1
    b.attachments[4].enable.Assign(1);            // no target 4
    b.depth_bounds_max = 0;
    std::fprintf(stderr, "DIAG BEFORE a: %08x %08x %08x %08x %08x\n", a.attachments[0].raw,
                 a.attachments[1].raw, a.attachments[2].raw, a.attachments[3].raw,
                 a.attachments[4].raw);
    std::fprintf(stderr, "DIAG BEFORE b: %08x %08x %08x %08x %08x\n", b.attachments[0].raw,
                 b.attachments[1].raw, b.attachments[2].raw, b.attachments[3].raw,
                 b.attachments[4].raw);
    CHECK(a.Hash() != b.Hash());
    a.CanonicalizeNative(0b1111);
    b.CanonicalizeNative(0b1111);
    std::fprintf(stderr, "DIAG AFTER  a: %08x %08x %08x %08x %08x\n", a.attachments[0].raw,
                 a.attachments[1].raw, a.attachments[2].raw, a.attachments[3].raw,
                 a.attachments[4].raw);
    std::fprintf(stderr, "DIAG AFTER  b: %08x %08x %08x %08x %08x\n", b.attachments[0].raw,
                 b.attachments[1].raw, b.attachments[2].raw, b.attachments[3].raw,
                 b.attachments[4].raw);
    if (!(a == b)) {
        // CI-only diagnosis (Linux GCC build): report the first differing byte.
        const auto* pa = reinterpret_cast<const unsigned char*>(&a);
        const auto* pb = reinterpret_cast<const unsigned char*>(&b);
        for (size_t i = 0; i < sizeof(a); ++i) {
            if (pa[i] != pb[i]) {
                UNSCOPED_INFO("byte " << i << " differs: a=" << int(pa[i]) << " b=" << int(pb[i]));
            }
        }
        UNSCOPED_INFO("color_formats a: " << int(a.color_formats[0]) << ", " << int(a.color_formats[1])
                      << " b: " << int(b.color_formats[0]) << ", " << int(b.color_formats[1]));
        UNSCOPED_INFO("ext_3_blend a: " << (a.extended_dynamic_state_3_blend ? 1 : 0)
                      << " b: " << (b.extended_dynamic_state_3_blend ? 1 : 0));
        UNSCOPED_INFO("dyn_vertex_input a: " << (a.dynamic_vertex_input ? 1 : 0)
                      << " b: " << (b.dynamic_vertex_input ? 1 : 0));
        UNSCOPED_INFO("attachment0 a: raw=" << a.attachments[0].raw << " b: raw="
                      << b.attachments[0].raw);
        UNSCOPED_INFO("attachment1 a: raw=" << a.attachments[1].raw << " b: raw="
                      << b.attachments[1].raw);
        UNSCOPED_INFO("attachment2 a: raw=" << a.attachments[2].raw << " b: raw="
                      << b.attachments[2].raw);
        UNSCOPED_INFO("attachment3 a: raw=" << a.attachments[3].raw << " b: raw="
                      << b.attachments[3].raw);
        UNSCOPED_INFO("attachment4 a: raw=" << a.attachments[4].raw << " b: raw="
                      << b.attachments[4].raw);
        UNSCOPED_INFO("used a: " << (a.color_formats[0] ? 1 : 0) << " p 1x" << (a.color_formats[1] ? 1 : 0));
        UNSCOPED_INFO("sizeof(FixedPipelineState)=" << sizeof(Vulkan::FixedPipelineState)
                      << " offsetof(attributes)=" << offsetof(Vulkan::FixedPipelineState, attributes)
                      << " offsetof(attachments)=" << offsetof(Vulkan::FixedPipelineState, attachments));
        FAIL("states differ after canonicalization");
    }
    CHECK(a == b);
    CHECK(a.Hash() == b.Hash());
}

TEST_CASE("The generic read mask follows the header's input map", "[canonical_native_key]") {
    Shader::ProgramHeader sph{};
    sph.vtg.imap_generic_vector[0] = 0x0f;  // attribute 0, all components
    sph.vtg.imap_generic_vector[0] |= 0x10; // attribute 1, x only
    sph.vtg.imap_generic_vector[15] = 0x80; // attribute 31, w only
    CHECK(VideoCommon::GenericReadMask(sph) == ((1U << 0) | (1U << 1) | (1U << 31)));
    CHECK(VideoCommon::GenericReadMask(Shader::ProgramHeader{}) == 0);
}

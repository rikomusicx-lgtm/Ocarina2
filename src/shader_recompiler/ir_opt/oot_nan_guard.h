// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <optional>

#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Optimization {

// TEMPORARY opt-in NaN/Inf guard for Ocarina of Time's reprojection prepass. The velocity pass
// (R16G16_FLOAT 368x202) was measured with 3,195 NaN texels and max 14,272 on the Kokiri
// load-in flash frame, and its companions were similarly poisoned; the resolve and glow chains
// fan that into the full-screen white-outs this artifact reports. Every fragment-color store of
// the guarded programs is clamped: NaN becomes 0, +-Inf and huge values become +-1024 (real
// motion never comes near that).
inline std::optional<size_t> ApplyOotNaNGuard(IR::Program& program, u64 title_id,
                                              u64 shader_hash, bool enabled) {
    if (!enabled || title_id != 0x0400486024DEC000ULL) {
        return std::nullopt;
    }
    // Velocity (R16G16_FLOAT 368x202 at draws 539-549 in the Kokiri load-in capture), its
    // companion scalar targets (R16_FLOAT/R32_FLOAT 736x405 and 368x202), and the vertex program
    // most of them share. Matched by native hash; a hash with no fragment-color store is a no-op.
    constexpr std::array<u64, 9> guarded{
        0x885ac67a285878dbULL, 0x23c42939fea640adULL, 0xdf0bc0a65c87f552ULL,
        0x1378da1e358bf300ULL, 0x92b7f9d9f1726b50ULL, 0x981016cee718eef6ULL,
        0x93ce19b8882d2d6bULL, 0xeae2677b666c08a9ULL,
    };
    if (std::find(guarded.begin(), guarded.end(), shader_hash) == guarded.end()) {
        return std::nullopt;
    }
    size_t edits = 0;
    for (IR::Block* block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::SetFragColor) {
                continue;
            }
            const IR::Value stored = inst.Arg(2);
            if (stored.IsImmediate()) {
                continue;
            }
            IR::IREmitter ir{*block, block->Instructions().iterator_to(inst)};
            const auto zero = ir.Imm32(0.0f);
            const auto bound = ir.Imm32(1024.0f);
            const auto masked =
                ir.Select(ir.FPIsNan(IR::F32{stored}), IR::F32{zero}, IR::F32{stored});
            const auto clamped = ir.FPClamp(IR::F32{masked}, IR::F32{ir.Imm32(-1024.0f)},
                                            IR::F32{bound});
            inst.SetArg(2, IR::Value{clamped});
            ++edits;
        }
    }
    return edits;
}

} // namespace Shader::Optimization

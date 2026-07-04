#pragma once

// cognition/tensor_leg.h
//
// A shared tensor-times-matrix primitive: absorb a d x d matrix into ONE mode of
// a flat, row-major dense tensor. Factored out of tree_tn.cpp (the exact tree
// contractor) so the general-graph tensor network (graph_tn) can reuse the same
// well-tested leg-folding kernel -- both the tree contraction and the Vidal
// simple-update environment absorb/divide-back are the same operation on one leg.
//
// THIS HEADER IS A LEAF: it depends only on qstate's Complex and the standard
// library. It carries no notion of what a leg MEANS (physical, parent, child,
// bond); it is pure multilinear algebra on a mode of a dense tensor.

#include <cognition/qstate/qstate.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    // Tensor-times-matrix along one mode: converts leg `mode` of W (a dim d
    // index) from a KET into a BRA by contracting with the d x d matrix `m`
    // (row-major [ket*d + bra]):
    //   W'[.., bra, ..] = sum_ket W[.., ket, ..] * m[ket*d + bra].
    // W keeps its shape; `dims` is the full mode-dimension list of W. Cost is
    // O(size(W) * d) -- a single extra bond factor, never a joint blow-up.
    inline void absorb_leg(
        std::vector<wz::engine::cognition::qstate::Complex>& W,
        const std::vector<uint32_t>& dims,
        std::size_t mode,
        const std::vector<wz::engine::cognition::qstate::Complex>& m)
    {
        using wz::engine::cognition::qstate::Complex;

        const std::size_t d = dims[mode];
        // Strides: inner = product of dims after `mode`, outer = before.
        std::size_t inner = 1;
        for (std::size_t i = mode + 1; i < dims.size(); ++i) {
            inner *= dims[i];
        }
        std::size_t outer = 1;
        for (std::size_t i = 0; i < mode; ++i) {
            outer *= dims[i];
        }
        const std::size_t block = d * inner;  // one full [mode,inner...] slab
        std::vector<Complex> out(W.size(), Complex{ 0, 0 });
        for (std::size_t o = 0; o < outer; ++o) {
            const std::size_t base = o * block;
            for (std::size_t ket = 0; ket < d; ++ket) {
                const std::size_t ket_base = base + ket * inner;
                for (std::size_t bra = 0; bra < d; ++bra) {
                    const Complex mm = m[ket * d + bra];
                    if (mm == Complex{ 0, 0 }) {
                        continue;
                    }
                    const std::size_t bra_base = base + bra * inner;
                    for (std::size_t j = 0; j < inner; ++j) {
                        out[bra_base + j] += W[ket_base + j] * mm;
                    }
                }
            }
        }
        W.swap(out);
    }
}

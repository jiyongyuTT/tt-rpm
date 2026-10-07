// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Tenstorrent USA, Inc.

#pragma once

#include <cstdint>

#include "Common/PipelinePacket.hpp"

namespace core {

// Unified squash predicate used by all pipeline stages.
// Returns true if instruction should be squashed based on flush request.
//
// The decision is purely depth-based and is the same for both flush sources
// (BranchPredictor and Execute):
// - Always squash instructions at deeper speculation levels than the branch
// - Squash instructions at the same depth that are younger than the branch
inline bool shouldSquash(uint64_t inst_tag, uint8_t inst_depth, uint64_t branch_tag, uint8_t branch_depth, FlushSource source) {
    (void)source;  // Use depth-based logic for both sources

    // Squash deeper speculation
    if (inst_depth > branch_depth) {
        return true;
    }

    // Same depth: squash younger instructions
    if (inst_depth == branch_depth && inst_tag > branch_tag) {
        return true;
    }

    return false;
}

// Convenience overload taking a FlushRequest
inline bool shouldSquash(uint64_t inst_tag, uint8_t inst_depth, const FlushRequest& req) {
    return shouldSquash(inst_tag, inst_depth, req.branch_tag, req.branch_depth, req.source);
}

// Convenience overload taking a PipelinePacket
inline bool shouldSquash(const PipelinePacket& pkt, const FlushRequest& req) { return shouldSquash(pkt.tag, pkt.wrong_path_depth, req); }

}  // namespace core

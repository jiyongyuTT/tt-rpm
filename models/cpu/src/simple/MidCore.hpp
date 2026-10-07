// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Tenstorrent USA, Inc.

#pragma once

#include "sparta/ports/DataPort.hpp"
#include "sparta/simulation/Unit.hpp"

#include "Logging.hpp"
#include "Types.hpp"

class MidCore : public sparta::Unit {
   public:
    sparta::DataInPort<Instruction> inst_in{&unit_port_set_, "inst_in"};

    sparta::DataOutPort<Operation> op_out{&unit_port_set_, "op_out"};

    MidCore(sparta::TreeNode* node)
        : sparta::Unit(node) {
        inst_in.registerConsumerHandler(CREATE_SPARTA_HANDLER_WITH_DATA(MidCore, handleInst, Instruction));
    }

   private:
    void handleInst(const Instruction& inst) {
        ILOG("[midcore] cycle " << getClock()->currentCycle() << " received instruction pc=0x" << std::hex << inst.pc << std::dec
                                << (inst.is_mem ? (inst.is_load ? " load" : " store") : " alu"));
        Operation op;
        op.pc = inst.pc;
        op.is_mem = inst.is_mem;
        op.is_load = inst.is_load;
        op.is_store = inst.is_store;
        op.mem_addr = inst.mem_addr;
        op.driver_tag = inst.driver_tag;
        ILOG("[midcore] cycle " << getClock()->currentCycle() << " sending operation pc=0x" << std::hex << op.pc << std::dec << " is_mem=" << op.is_mem
                                << " addr=0x" << std::hex << op.mem_addr << std::dec);
        op_out.send(op, 1);
    }
};

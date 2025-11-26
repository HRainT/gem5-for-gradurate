#include "cpu/rxuo3/comm.hh"

#include <list>
#include <map>
#include <queue>
#include <vector>
#include "cpu/rxuo3/RMU.hh"
#include "cpu/rxuo3/dyn_inst.hh"


namespace gem5
{

namespace rxuo3
{

/** Resize the dependency graph to have num_entries registers. */

RMU::RMU(CPU *cpu_ptr, const BaseRxuO3CPUParams &params)
    : cpu(cpu_ptr)
{

    numPhysRegs = params.numPhysIntRegs + params.numPhysFloatRegs +                 // 计算物理寄存器总数
                    params.numPhysVecRegs +
                    params.numPhysVecPredRegs +
                    params.numPhysCCRegs;

    //Create an entry for each physical register within the
    //dependency graph.
    dependGraph.resize(numPhysRegs);

    // Resize the register scoreboard.
    regScoreboard.resize(numPhysRegs);

    std::fill(regScoreboard.begin(), regScoreboard.end(), true);

    // sc_resetState();
}

RMU::~RMU()
{
    dependGraph.reset();
#ifdef GEM5_DEBUG
    cprintf("Nodes traversed: %i, removed: %i\n",
            dependGraph.nodesTraversed, dependGraph.nodesRemoved);
#endif
}


RMU::Scoreboard::Scoreboard(const std::string &_my_name,
        unsigned _numPhysicalRegs) :
    _name(_my_name), regScoreBoard(_numPhysicalRegs, true),
    numPhysRegs(_numPhysicalRegs)
{}


/** Checks if the register is ready. */
bool RMU::Scoreboard::getReg(PhysRegIdPtr phys_reg) const
{
    if (phys_reg->isFixedMapping()) {
        // Fixed mapping regs are always ready
        return true;
    }

    assert(phys_reg->flatIndex() < numPhysRegs);

    return regScoreBoard[phys_reg->flatIndex()];
}

/** Sets the register as ready. */
void
RMU::Scoreboard::setReg(PhysRegIdPtr phys_reg)
{
    if (phys_reg->isFixedMapping()) {
        // Fixed mapping regs are always ready, ignore attempts to change
        // that
        return;
    }

    assert(phys_reg->flatIndex() < numPhysRegs);

    DPRINTF(RxuScoreboard, "Setting reg %i (%s) as ready\n",
            phys_reg->index(), phys_reg->className());

    regScoreBoard[phys_reg->flatIndex()] = true;
}

void
RMU::sc_resetState()
{
    for (int i = 0; i < numPhysRegs; ++i) {
        regScoreboard[i] = false;
    }
}

/** Sets the register as not ready. */
void
RMU::Scoreboard::unsetReg(PhysRegIdPtr phys_reg)
{
    if (phys_reg->isFixedMapping()) {
        // Fixed mapping regs are always ready, ignore attempts to
        // change that
        return;
    }

    assert(phys_reg->flatIndex() < numPhysRegs);

    regScoreBoard[phys_reg->flatIndex()] = false;
}

bool
RMU::addToDependents(const DynInstPtr &new_inst)
{
    // Loop through the instruction's source registers, adding
    // them to the dependency list if they are not ready.
    int8_t total_src_regs = new_inst->numSrcRegs();
    bool return_val = false;
    // PhysRegIdPtr srcx_reg[total_src_regs];

    for (int src_reg_idx = 0;
         src_reg_idx < total_src_regs;
         src_reg_idx++)
    {
        //srcx_reg[src_reg_idx] = new_inst->renamedSrcIdx(src_reg_idx);
        // PhysRegIdPtr src_regx = new_inst->renamedSrcIdx(src_reg_idx);
        // Only add it to the dependency graph if it's not ready.
        if (!new_inst->readySrcIdx(src_reg_idx)) {
            PhysRegIdPtr src_reg = new_inst->renamedSrcIdx(src_reg_idx);

            // Check the IQ's scoreboard to make sure the register
            // hasn't become ready while the instruction was in flight
            // between stages.  Only if it really isn't ready should
            // it be added to the dependency graph.
            if (src_reg->isFixedMapping()) {
                continue;
            } else if (!regScoreboard[src_reg->flatIndex()]) {
                DPRINTF(RxuRMU, "Instruction PC %s has src reg %i (%s) that "
                        "is being added to the dependency chain.\n",
                        new_inst->pcState(), src_reg->index(),
                        src_reg->className());
                    new_inst->src_need_wake[src_reg_idx] = true;
                    dependGraph.insert(src_reg->flatIndex(), new_inst);

                //dependGraph.print_depedence_info(src_reg->flatIndex());
                // Change the return value to indicate that something
                // was added to the dependency graph.
                return_val = true;
            } else {
                // DPRINTF(IQ, "Instruction PC %s has src reg %i (%s) that "
                //         "became ready before it reached the IQ.\n",
                //         new_inst->pcState(), src_reg->index(),
                //         src_reg->className());
                // Mark a register ready within the instruction.
                new_inst->markSrcRegReady(src_reg_idx);
                if(new_inst->isMemRef() && !new_inst->isVector()){
                    int srcx = 8;
                    if(src_reg_idx == 0){
                        srcx = 0;
                        new_inst->if_src0_ready = true;
                    }
                    else if(src_reg_idx == 1){
                        srcx = 1;
                    }
                    else if(src_reg_idx == 2){
                        srcx = 2;
                    }
                    else if(src_reg_idx == 3){
                        srcx = 3;
                    }
                    else if(src_reg_idx == 4){
                        srcx = 4;
                    }
                    bool srcready = new_inst->readySrcIdx(src_reg_idx);
                    if(srcx != 8){
                        new_inst->markSrcRegReady(src_reg_idx,srcready,srcx);
                    }
                }
                else if(new_inst->isMemRef()){
                    int srcx = 8;
                    if(src_reg_idx == 0){
                        srcx = 0;
                        new_inst->if_src0_ready = true;
                    }
                    else if(src_reg_idx == 1){
                        srcx = 1;
                    }
                    else if(src_reg_idx == 2){
                        srcx = 2;
                    }
                    else if(src_reg_idx == 3){
                        srcx = 3;
                    }
                    else if(src_reg_idx == 4){
                        srcx = 4;
                    }
                    bool srcready = new_inst->readySrcIdx(src_reg_idx);
                    bool is_index_stride = new_inst->staticInst->isStrideIndex();
                    if(srcx != 8){
                        new_inst->markSrcRegReady(src_reg_idx,srcready,srcx,new_inst->isStore(),is_index_stride);
                    }
                }

            }
        }
    }

    return return_val;
}

void
RMU::addToProducers(const DynInstPtr &new_inst)
{
    // Nothing really needs to be marked when an instruction becomes
    // the producer of a register's value, but for convenience a ptr
    // to the producing instruction will be placed in the head node of
    // the dependency links.
    int8_t total_dest_regs = new_inst->numDestRegs();

    for (int dest_reg_idx = 0;
         dest_reg_idx < total_dest_regs;
         dest_reg_idx++)
    {
        PhysRegIdPtr dest_reg = new_inst->renamedDestIdx(dest_reg_idx);

        // Some registers have fixed mapping, and there is no need to track
        // dependencies as these instructions must be executed at commit.
        if (dest_reg->isFixedMapping()) {
            continue;
        }

        if (!dependGraph.empty(dest_reg->flatIndex())) {
            // dependGraph.dependGraph[dest_reg->flatIndex()].next = NULL;
            dependGraph.dump();
            panic("Dependency graph %i (%s) (flat: %i) not empty!",
                  dest_reg->index(), dest_reg->className(),
                  dest_reg->flatIndex());
        }

        dependGraph.setInst(dest_reg->flatIndex(), new_inst);
        //dependGraph.print_dest_info(dest_reg->flatIndex());
        // Mark the scoreboard to say it's not yet ready.
        regScoreboard[dest_reg->flatIndex()] = false;
    }
}

int
RMU::wakeDependents(const DynInstPtr &completed_inst)
{
    int dependents = 0;

    // The instruction queue here takes care of both floating and int ops
    // if (completed_inst->isFloating()) {
    //     iqIOStats.fpInstQueueWakeupAccesses++;
    // } else if (completed_inst->isVector()) {
    //     iqIOStats.vecInstQueueWakeupAccesses++;
    // } else {
    //     iqIOStats.intInstQueueWakeupAccesses++;
    // }

    //completed_inst->lastWakeDependents = curTick();

    DPRINTF(RxuRMU, "Waking dependents of completed instruction.\n");

    assert(!completed_inst->isSquashed());

    // Tell the memory dependence unit to wake any dependents on this
    // instruction if it is a memory instruction.  Also complete the memory
    // instruction at this point since we know it executed without issues.
    ThreadID tid = completed_inst->threadNumber;
    if (completed_inst->isMemRef() && !completed_inst->needEop()) {
        cpu->dispipe3.wtb.memDepUnit[tid].completeInst(completed_inst);

        DPRINTF(RxuRMU, "Completing mem instruction PC: %s [sn:%llu]\n",
            completed_inst->pcState(), completed_inst->seqNum);

        ++freeEntries;
        completed_inst->memOpDone(true);
        count[tid]--;
    } else if (completed_inst->isReadBarrier() ||
               completed_inst->isWriteBarrier()) {
        // Completes a non mem ref barrier
        cpu->dispipe3.wtb.memDepUnit[tid].completeInst(completed_inst);
    }

    for (int dest_reg_idx = 0;
         dest_reg_idx < completed_inst->numDestRegs();
         dest_reg_idx++)
    {
        PhysRegIdPtr dest_reg =
            completed_inst->renamedDestIdx(dest_reg_idx);

        // Special case of uniq or control registers.  They are not
        // handled by the IQ and thus have no dependency graph entry.
        if (dest_reg->isFixedMapping()) {
            DPRINTF(RxuRMU, "Reg %d [%s] is part of a fix mapping, skipping\n",
                    dest_reg->index(), dest_reg->className());
            continue;
        }

        // Avoid waking up dependents if the register is pinned
        dest_reg->decrNumPinnedWritesToComplete();
        if (dest_reg->isPinned())
            completed_inst->setPinnedRegsWritten();

        if (dest_reg->getNumPinnedWritesToComplete() != 0) {
            DPRINTF(RxuRMU, "Reg %d [%s] is pinned, skipping\n",
                    dest_reg->index(), dest_reg->className());
            continue;
        }

        DPRINTF(RxuRMU, "Waking any dependents on register %i (%s).\n",
                dest_reg->index(),
                dest_reg->className());

        //Go through the dependency chain, marking the registers as
        //ready within the waiting instructions.
        DynInstPtr dep_inst = dependGraph.pop(dest_reg->flatIndex());


        while (dep_inst) {
            DPRINTF(RxuRMU, "Waking up a dependent instruction, [sn:%llu] "
                    "PC %s.\n", dep_inst->seqNum, dep_inst->pcState());

            // Might want to give more information to the instruction
            // so that it knows which of its source registers is
            // ready.  However that would mean that the dependency
            // graph entries would need to hold the src_reg_idx.
            // dep_inst->markSrcRegReady();

            dep_inst->markSrcRegReady();
            if(dep_inst->isMemRef() && !dep_inst->isVector()){
                if(dep_inst->numSrcRegs() != 0){
                    int8_t total_src_regs = dep_inst->numSrcRegs();
                    for (int src_reg_idx = 0;
                        src_reg_idx < total_src_regs;
                        src_reg_idx++){
                    // PhysRegIdPtr src_reg = dep_inst->renamedSrcIdx(src_reg_idx);

                    PhysRegIdPtr src_reg =
                        dep_inst->renamedSrcIdx(src_reg_idx);
                    int srcx = 8;
                    if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 0){
                        srcx = 0;
                        dep_inst->if_src0_ready = true;
                    }
                    else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 1){
                        srcx = 1;
                    }
                    else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 2){
                        srcx = 2;
                    }
                    else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 3){
                        srcx = 3;
                    }
                    else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 4){
                        srcx = 4;
                    }
                    bool srcready = dep_inst->readySrcIdx(src_reg_idx);
                    if(srcx != 8){
                        dep_inst->markSrcRegReady(src_reg_idx,srcready,srcx);
                    }
                    if(src_reg_idx == 1 && srcx == 1){
                        dep_inst->stdDataReady = true;
                        // if(!dep_inst->odd_inst && dep_inst->stdReady == false){
                        //     cpu->dispipe3.wtb.readySTdata1.push(dep_inst);
                        //     dep_inst->stdReady = true;
                        //     DPRINTF(RxuRMU, "src1 ready [sn:%llu],add to readystdata0,produ\n", dep_inst->seqNum);
                        // }
                        if(dep_inst->stdReady == false){
                            dep_inst->stdReady = true;
                            cpu->dispipe3.wtb.readySTdata0.push(dep_inst);
                            DPRINTF(RxuRMU, "src1 ready [sn:%llu],add to readystdata1,produ\n", dep_inst->seqNum);
                        }
                    }

                        if(srcx == 0 && dep_inst->staInReadyList == false){
                            cpu->dispipe3.wtb.addIfReady(dep_inst);
                            dep_inst->ifhasreadyed++;
                        }

                    }
                }
            }
            else if(dep_inst->isMemRef()){
                if(dep_inst->numSrcRegs() != 0){
                    int8_t total_src_regs = dep_inst->numSrcRegs();
                    for (int src_reg_idx = 0;
                        src_reg_idx < total_src_regs;
                        src_reg_idx++){
                    // PhysRegIdPtr src_reg = dep_inst->renamedSrcIdx(src_reg_idx);

                        PhysRegIdPtr src_reg =
                            dep_inst->renamedSrcIdx(src_reg_idx);
                        int srcx = 8;
                        if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 0){
                            srcx = 0;
                            dep_inst->if_src0_ready = true;
                        }
                        else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 1){
                            srcx = 1;
                        }
                        else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 2){
                            srcx = 2;
                        }
                        else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 3){
                            srcx = 3;
                        }
                        else if(src_reg->flatIndex() == dest_reg->flatIndex() && src_reg_idx == 4){
                            srcx = 4;
                        }
                        bool srcready = dep_inst->readySrcIdx(src_reg_idx);
                        bool is_index_stride = dep_inst->staticInst->isStrideIndex();
                        if(srcx !=8){
                            dep_inst->markSrcRegReady(src_reg_idx,srcready,srcx,dep_inst->isStore(),is_index_stride);
                        }
                        if(dep_inst->stdDataReady == true && dep_inst->isStore()){
                            if(dep_inst->stdReady == false){
                                dep_inst->stdReady = true;
                                cpu->dispipe3.wtb.readySTdata0.push(dep_inst);
                                DPRINTF(RxuRMU, "src1 ready [sn:%llu],add to readystdata1,produ\n", dep_inst->seqNum);
                            }
                        }
                        if(dep_inst->isStore() && srcx == 0 && dep_inst->staInReadyList == false){
                            cpu->dispipe3.wtb.addIfReady(dep_inst);
                            dep_inst->ifhasreadyed++;
                        }
                        else if(!dep_inst->isStore() && (dep_inst->readyRegs_vector == dep_inst->numSrcRegs() || dep_inst->readyRegs == dep_inst->numSrcRegs())){
                            cpu->dispipe3.wtb.addIfReady(dep_inst);
                            dep_inst->ifhasreadyed++;
                        }
                    }
                }
            }


            if(!dep_inst->isMemRef()){
                for (auto it = cpu->dispipe3.wtb.inst_without_rPort.begin(); it != cpu->dispipe3.wtb.inst_without_rPort.end(); ) {
                    if (dep_inst->seqNum == (*it)->seqNum) {
                        it = cpu->dispipe3.wtb.inst_without_rPort.erase(it);
                    } else {
                        ++it;
                    }
                }
                cpu->dispipe3.wtb.addIfReady(dep_inst);
            }

            dep_inst = dependGraph.pop(dest_reg->flatIndex());

            ++dependents;
        }

        // Reset the head node now that all of its dependents have
        // been woken up.
        assert(dependGraph.empty(dest_reg->flatIndex()));
        dependGraph.clearInst(dest_reg->flatIndex());

        // Mark the scoreboard as having that register ready.
        regScoreboard[dest_reg->flatIndex()] = true;
    }
    return dependents;
}

} // namespace o3
} // namespace gem5

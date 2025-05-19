// ----------- added by longting.du -------------- //

#include "cpu/rxuo3/rename_unit.hh"

#include <list>
#include <any>

#include "base/logging.hh"
#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/reg_class.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuRename.hh"
#include "params/BaseRxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

RenameUnit::RenameUnit(CPU *_cpu, const BaseRxuO3CPUParams &params)
    :   cpu(_cpu),
        // frm(),
        numThreads(params.numThreads)
        // stats(_cpu)
{
}

std::string
RenameUnit::name() const
{
    return cpu->name() + ".rename";
}

void
RenameUnit::regProbePoints()
{
    ppRename = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Rename");
    ppSquashInRename = new ProbePointArg<SeqNumRegPair>(cpu->getProbeManager(),
                                                        "SquashInRename");
}

void
RenameUnit::setRenameMap(UnifiedRenameMap rm_ptr[])
{
    for (ThreadID tid = 0; tid < numThreads; tid++)
        renameMap[tid] = &rm_ptr[tid];
}

void
RenameUnit::setFreeList(RxuUnifiedFreeList *fl_ptr)
{
    freeList = fl_ptr;
}

void 
RenameUnit::setFrm(RxuFRMRename *frm_ptr)
{
    frm = frm_ptr;
}

void
RenameUnit::setVecCsr(RxuUnifiedVecRename *vec_csr_ptr)
{
    vecCsr = vec_csr_ptr;
}

void
RenameUnit::setScoreboard(Scoreboard *_scoreboard)
{
    scoreboard = _scoreboard;
}

RenameInfo
RenameUnit::rename(RegId arch_reg, unsigned inst_index)
{
    PhysRegIdPtr renamed_reg;
    PhysRegIdPtr prev_reg = map[arch_reg.index()];
    if (arch_reg.is(InvalidRegClass)) {
        assert(prev_reg->is(InvalidRegClass));
        renamed_reg = prev_reg;
    } else if (prev_reg->getNumPinnedWrites() > 0) {
        // Do not rename if the register is pinned
        assert(arch_reg.getNumPinnedWrites() == 0);  // Prevent pinning the
                                                    // same register twice
        DPRINTF(RxuRename, "Renaming pinned reg, numPinnedWrites %d\n",
                prev_reg->getNumPinnedWrites());
        renamed_reg = prev_reg;
        renamed_reg->decrNumPinnedWrites();
    } else {
        bool stall = false;
        renamed_reg = freeList->getReg(arch_reg, arch_reg.classValue(), inst_index, stall);
        if (stall) {
            DPRINTF(RxuRename, "Stall from rename dest reg.\n");
            return RenameInfo(nullptr, nullptr);
        }
        map[arch_reg.index()] = renamed_reg;
        renamed_reg->setNumPinnedWrites(arch_reg.getNumPinnedWrites());
        renamed_reg->setNumPinnedWritesToComplete(
            arch_reg.getNumPinnedWrites() + 1);
    }

    DPRINTF(RxuRename, "Renamed reg %d to physical reg %d (%d) old mapping was"
            " %d (%d)\n",
            arch_reg, renamed_reg->flatIndex(), renamed_reg->flatIndex(),
            prev_reg->flatIndex(), prev_reg->flatIndex());
    return RenameInfo(renamed_reg, prev_reg);
}

PhysRegIdPtr
RenameUnit::lookup(RegId arch_reg)
{
    assert(arch_reg.index() <= map.size());
    return map[arch_reg.index()];
}

void
RenameUnit::setEntry(const RegId arch_reg, PhysRegIdPtr phys_reg)
{
    assert(arch_reg.index() <= map.size());
    map[arch_reg.index()] = phys_reg;
}

void
RenameUnit::doSquash(const InstSeqNum &squashed_seq_num, ThreadID tid)
{
    auto hb_it = historyBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!historyBuffer[tid].empty() &&
           hb_it->instSeqNum > squashed_seq_num) {
        assert(hb_it != historyBuffer[tid].end());

        if (hb_it->isMacroVector) {
            DPRINTF(RxuRename, "[tid:%i] Removing MarcroVector history entry with "
                    "[sn:%llu] .\n",
                    tid, hb_it->instSeqNum);
            historyBuffer[tid].erase(hb_it++);
            continue;            
        }

        DPRINTF(RxuRename, "[tid:%i] Removing history entry with "
                "[sn:%llu] (archReg: %d, newPhysReg: %d, prevPhysReg: %d).\n",
                tid, hb_it->instSeqNum, hb_it->archReg.index(),
                hb_it->newPhysReg->index(), hb_it->prevPhysReg->index());

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (hb_it->newPhysReg != hb_it->prevPhysReg) {
            // Tell the rename map to set the architected register to the
            // previous physical register that it was renamed to.
            renameMap[tid]->setEntry(hb_it->archReg, hb_it->prevPhysReg);

            // Put the renamed physical register back on the free list.
            freeList->addReg(hb_it->newPhysReg);
            // ------- added by longting.du --------
            freeList->setRecCnt(hb_it->archReg);
            // -------------------------------------
        }

        // Notify potential listeners that the register mapping needs to be
        // removed because the instruction it was mapped to got squashed. Note
        // that this is done before hb_it is incremented.
        ppSquashInRename->notify(std::make_pair(hb_it->instSeqNum,
                                                hb_it->newPhysReg));

        historyBuffer[tid].erase(hb_it++);

        // ++stats.undoneMaps;
    }
    
    // ----------------- frm recover ----------------------------------

    auto frm_hb_it = frmHistoryBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!frmHistoryBuffer[tid].empty() &&
           frm_hb_it->instSeqNum > squashed_seq_num) {
        assert(frm_hb_it != frmHistoryBuffer[tid].end());

        DPRINTF(RxuRename, "[tid:%i] Removing FRM history entry with "
                "[sn:%llu] (archReg: FRM, newPhysReg: %d, prevPhysReg: %d).\n",
                tid, frm_hb_it->instSeqNum, 
                frm_hb_it->newPhysReg, frm_hb_it->prevPhysReg);

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (frm_hb_it->newPhysReg != frm_hb_it->prevPhysReg) {
            frm->setRecCnt();
        }

        // Notify potential listeners that the register mapping needs to be
        // removed because the instruction it was mapped to got squashed. Note
        // that this is done before hb_it is incremented.
        // ppSquashInRename->notify(std::make_pair(frm_hb_it->instSeqNum,
        //                                         frm_hb_it->newPhysReg));

        frmHistoryBuffer[tid].erase(frm_hb_it++);

        // ++stats.undoneMaps;
    }

    // ----------------- frm recover --------------------------------
    // ----------------- vxrm recover ----------------------------------

    auto vxrm_hb_it = vxrmHistoryBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!vxrmHistoryBuffer[tid].empty() &&
           vxrm_hb_it->instSeqNum > squashed_seq_num) {
        assert(vxrm_hb_it != vxrmHistoryBuffer[tid].end());

        DPRINTF(RxuRename, "[tid:%i] Removing vxrm history entry with "
                "[sn:%llu] (archReg: vxrm, newPhysReg: %d, prevPhysReg: %d).\n",
                tid, vxrm_hb_it->instSeqNum, 
                vxrm_hb_it->newPhysReg, vxrm_hb_it->prevPhysReg);

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (vxrm_hb_it->newPhysReg != vxrm_hb_it->prevPhysReg) {
            vecCsr->setRecCnt(Vxrm);
        }

        // Notify potential listeners that the register mapping needs to be
        // removed because the instruction it was mapped to got squashed. Note
        // that this is done before hb_it is incremented.

        vxrmHistoryBuffer[tid].erase(vxrm_hb_it++);

    }

    // ----------------- vxrm recover --------------------------------

    // ----------------- vl recover ----------------------------------

    auto vl_hb_it = vlHistoryBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!vlHistoryBuffer[tid].empty() &&
           vl_hb_it->instSeqNum > squashed_seq_num) {
        assert(vl_hb_it != vlHistoryBuffer[tid].end());

        DPRINTF(RxuRename, "[tid:%i] Removing vl history entry with "
                "[sn:%llu] (archReg: vl, newPhysReg: %d, prevPhysReg: %d).\n",
                tid, vl_hb_it->instSeqNum, 
                vl_hb_it->newPhysReg, vl_hb_it->prevPhysReg);

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (vl_hb_it->newPhysReg != vl_hb_it->prevPhysReg) {
            vecCsr->setRecCnt(Vl);
        }

        // Notify potential listeners that the register mapping needs to be
        // removed because the instruction it was mapped to got squashed. Note
        // that this is done before hb_it is incremented.

        vlHistoryBuffer[tid].erase(vl_hb_it++);

    }

    // ----------------- vl recover --------------------------------

    // ----------------- vtype recover ----------------------------------

    auto vtype_hb_it = vtypeHistoryBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!vtypeHistoryBuffer[tid].empty() &&
           vtype_hb_it->instSeqNum > squashed_seq_num) {
        assert(vtype_hb_it != vtypeHistoryBuffer[tid].end());

        DPRINTF(RxuRename, "[tid:%i] Removing vl history entry with "
                "[sn:%llu] (archReg: vl, newPhysReg: %d, prevPhysReg: %d).\n",
                tid, vtype_hb_it->instSeqNum, 
                vtype_hb_it->newPhysReg, vtype_hb_it->prevPhysReg);

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (vtype_hb_it->newPhysReg != vtype_hb_it->prevPhysReg) {
            vecCsr->setRecCnt(Vtype);
        }

        // Notify potential listeners that the register mapping needs to be
        // removed because the instruction it was mapped to got squashed. Note
        // that this is done before hb_it is incremented.

        vtypeHistoryBuffer[tid].erase(vtype_hb_it++);

    }

    // ----------------- vtype recover --------------------------------
}

void
RenameUnit::removeFromHistory(InstSeqNum inst_seq_num, ThreadID tid)
{
    DPRINTF(RxuRename, "[tid:%i] Removing a committed instruction from the "
            "history buffer %u (size=%i), until [sn:%llu].\n",
            tid, tid, historyBuffer[tid].size(), inst_seq_num);

    auto hb_it = historyBuffer[tid].end();

    --hb_it;

    if (historyBuffer[tid].empty()) {
        DPRINTF(RxuRename, "[tid:%i] History buffer is empty.\n", tid);
        return;
    } else if (hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Old sequence number encountered. "
                "Ensure that a syscall happened recently.\n",
                tid,inst_seq_num);
        return;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!historyBuffer[tid].empty() &&
           hb_it != historyBuffer[tid].end() &&
           hb_it->instSeqNum <= inst_seq_num) {

        if (hb_it->isMacroVector) {
            DPRINTF(RxuRename, "MarcroVector skip release.\n");
            historyBuffer[tid].erase(hb_it--);
            continue;
        }

        DPRINTF(RxuRename, "[tid:%i] Freeing up older rename of reg %i (%s), "
                "[sn:%llu].\n",
                tid, hb_it->prevPhysReg->index(),
                hb_it->prevPhysReg->className(),
                hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (hb_it->newPhysReg != hb_it->prevPhysReg) {
            freeList->addReg(hb_it->prevPhysReg);
            freeList->setRelCnt(hb_it->archReg);
        }

        // ++stats.committedMaps;

        historyBuffer[tid].erase(hb_it--);
    }

    bool frm_no_remove = false;    
     // --------------- frm release ------------
    DPRINTF(RxuRename, "[tid:%i] Removing a committed instruction from the "
            "FRM history buffer %u (size=%i), until [sn:%llu].\n",
            tid, tid, frmHistoryBuffer[tid].size(), inst_seq_num);

    auto frm_hb_it = frmHistoryBuffer[tid].end();

    --frm_hb_it;

    if (frmHistoryBuffer[tid].empty()) {
        DPRINTF(RxuRename, "[tid:%i] FRM History buffer is empty.\n", tid);
        frm_no_remove = true;
    } else if (frm_hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Old sequence number encountered. "
                "Ensure that a syscall happened recently.\n",
                tid, inst_seq_num);
        frm_no_remove = true;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!frmHistoryBuffer[tid].empty() &&
           frm_hb_it != frmHistoryBuffer[tid].end() &&
           frm_hb_it->instSeqNum <= inst_seq_num &&
           !frm_no_remove) {

        DPRINTF(RxuRename, "[tid:%i] Freeing up older rename of reg %i (FRM) , "
                "[sn:%llu].\n",
                tid, frm_hb_it->prevPhysReg,
                frm_hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (frm_hb_it->newPhysReg != frm_hb_it->prevPhysReg) {
            frm->setRelCnt();
        }

        frmHistoryBuffer[tid].erase(frm_hb_it--);
    }

    // ---------------- frm release ----------------------------------------
    bool vxrm_no_remove = false;
    // --------------- vxrm release ------------
    DPRINTF(RxuRename, "[tid:%i] Removing a committed instruction from the "
            "vxrm history buffer %u (size=%i), until [sn:%llu].\n",
            tid, tid, vxrmHistoryBuffer[tid].size(), inst_seq_num);

    auto vxrm_hb_it = vxrmHistoryBuffer[tid].end();

    --vxrm_hb_it;

    if (vxrmHistoryBuffer[tid].empty()) {
        DPRINTF(RxuRename, "[tid:%i] vxrm History buffer is empty.\n", tid);
        vxrm_no_remove = true;
    } else if (vxrm_hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Old sequence number encountered. "
                "Ensure that a syscall happened recently.\n",
                tid, inst_seq_num);
        vxrm_no_remove = true;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!vxrmHistoryBuffer[tid].empty() &&
           vxrm_hb_it != vxrmHistoryBuffer[tid].end() &&
           vxrm_hb_it->instSeqNum <= inst_seq_num &&
           !vxrm_no_remove) {

        DPRINTF(RxuRename, "[tid:%i] Freeing up older rename of reg %i (vxrm) , "
                "[sn:%llu].\n",
                tid, vxrm_hb_it->prevPhysReg,
                vxrm_hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (vxrm_hb_it->newPhysReg != vxrm_hb_it->prevPhysReg) {
            vecCsr->setRelCnt(Vxrm);
        }

        vxrmHistoryBuffer[tid].erase(vxrm_hb_it--);
    }

    // ---------------- vxrm release ----------------------------------------

    // --------------- vl release ------------
    DPRINTF(RxuRename, "[tid:%i] Removing a committed instruction from the "
            "vl history buffer %u (size=%i), until [sn:%llu].\n",
            tid, tid, vlHistoryBuffer[tid].size(), inst_seq_num);

    auto vl_hb_it = vlHistoryBuffer[tid].end();

    --vl_hb_it;

    if (vlHistoryBuffer[tid].empty()) {
        DPRINTF(RxuRename, "[tid:%i] vl History buffer is empty.\n", tid);
        return;
    } else if (vl_hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Old sequence number encountered. "
                "Ensure that a syscall happened recently.\n",
                tid, inst_seq_num);
        return;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!vlHistoryBuffer[tid].empty() &&
           vl_hb_it != vlHistoryBuffer[tid].end() &&
           vl_hb_it->instSeqNum <= inst_seq_num) {

        DPRINTF(RxuRename, "[tid:%i] Freeing up older rename of reg %i (vl) , "
                "[sn:%llu].\n",
                tid, vl_hb_it->prevPhysReg,
                vl_hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (vl_hb_it->newPhysReg != vl_hb_it->prevPhysReg) {
            vecCsr->setRelCnt(Vl);
        }

        vlHistoryBuffer[tid].erase(vl_hb_it--);
    }

    // ---------------- vl release ----------------------------------------

    // --------------- vtype release ------------
    DPRINTF(RxuRename, "[tid:%i] Removing a committed instruction from the "
            "vtype history buffer %u (size=%i), until [sn:%llu].\n",
            tid, tid, vtypeHistoryBuffer[tid].size(), inst_seq_num);

    auto vtype_hb_it = vtypeHistoryBuffer[tid].end();

    --vtype_hb_it;

    if (vtypeHistoryBuffer[tid].empty()) {
        DPRINTF(RxuRename, "[tid:%i] vtype History buffer is empty.\n", tid);
        return;
    } else if (vtype_hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Old sequence number encountered. "
                "Ensure that a syscall happened recently.\n",
                tid, inst_seq_num);
        return;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!vtypeHistoryBuffer[tid].empty() &&
           vtype_hb_it != vtypeHistoryBuffer[tid].end() &&
           vtype_hb_it->instSeqNum <= inst_seq_num) {

        DPRINTF(RxuRename, "[tid:%i] Freeing up older rename of reg %i (vtype) , "
                "[sn:%llu].\n",
                tid, vtype_hb_it->prevPhysReg,
                vtype_hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (vtype_hb_it->newPhysReg != vtype_hb_it->prevPhysReg) {
            vecCsr->setRelCnt(Vtype);
        }

        vtypeHistoryBuffer[tid].erase(vtype_hb_it--);
    }

    // ---------------- vl release ----------------------------------------

}


bool
RenameUnit::renameDestRegs(const DynInstPtr &inst, ThreadID tid, unsigned inst_index)
{
    gem5::ThreadContext *tc = inst->tcBase();
    unsigned num_dest_regs = inst->numDestRegs();
    UnifiedRenameMap *map = renameMap[tid];
    auto *isa = tc->getIsaPtr();

    if (inst->isCsrFRM()) {
        // std::string message = "Instruction [sn:" + std::to_string(inst->seqNum) + "] is " + inst->staticInst->disassemble(inst->pc->instAddr());
        // panic(message);
        unsigned prev_rn_frm = frm->curPhysFRM();
        unsigned new_rn_frm = frm->get();
        frm->unsetReady(new_rn_frm);
        frm->addProducer(new_rn_frm);

        DPRINTF(RxuRename,
                "[tid:%i] "
                "Renaming dest: arch reg FRM to physical reg %i. "
                "Old mapping is %i.\n",
                tid, new_rn_frm, prev_rn_frm);

        FRMRenameHistory frm_hb_entry(inst->seqNum, new_rn_frm, prev_rn_frm);
        frmHistoryBuffer[tid].push_front(frm_hb_entry);

        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                            "Adding instruction to frm history buffer (size=%i).\n",
                tid, (*frmHistoryBuffer[tid].begin()).instSeqNum,
                frmHistoryBuffer[tid].size());

        inst->renameFRM(new_rn_frm, prev_rn_frm);
    }

    if (inst->isVecCsrVxrm()) {

        unsigned prev_rn_vxrm = vecCsr->curPhysVecCsr(Vxrm);
        unsigned new_rn_vxrm = vecCsr->get(Vxrm);
        vecCsr->unsetReady(Vxrm, new_rn_vxrm);
        vecCsr->addProducer(Vxrm, new_rn_vxrm);

        DPRINTF(RxuRename,
                "[tid:%i] "
                "Renaming dest: arch reg vxrm to physical reg %i. "
                "Old mapping is %i.\n",
                tid, new_rn_vxrm, prev_rn_vxrm);

        VxrmRenameHistory vxrm_hb_entry(inst->seqNum, new_rn_vxrm, prev_rn_vxrm);
        vxrmHistoryBuffer[tid].push_front(vxrm_hb_entry);

        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                            "Adding instruction to vxrm history buffer (size=%i).\n",
                tid, (*vxrmHistoryBuffer[tid].begin()).instSeqNum,
                vxrmHistoryBuffer[tid].size());

        inst->renameVxrm(new_rn_vxrm, prev_rn_vxrm);
    }

    if (!cpu->vsetBranch) {
        if (inst->isVset()) {

            unsigned prev_rn_vl = vecCsr->curPhysVecCsr(Vl);
            unsigned new_rn_vl = vecCsr->get(Vl);
            vecCsr->unsetReady(Vl, new_rn_vl);
            vecCsr->addProducer(Vl, new_rn_vl);
    
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming dest: arch reg vl to physical reg %i. "
                    "Old mapping is %i.\n",
                    tid, new_rn_vl, prev_rn_vl);
    
            VlRenameHistory vl_hb_entry(inst->seqNum, new_rn_vl, prev_rn_vl);
            vlHistoryBuffer[tid].push_front(vl_hb_entry);
    
            DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                                "Adding instruction to vl history buffer (size=%i).\n",
                    tid, (*vlHistoryBuffer[tid].begin()).instSeqNum,
                    vlHistoryBuffer[tid].size());
    
            inst->renameVl(new_rn_vl, prev_rn_vl);
    
            if (inst->isVsetivli()) {
                writeImmVl(inst, tid);
            }
        }
    
        if (inst->isVset()) {
    
            unsigned prev_rn_vtype = vecCsr->curPhysVecCsr(Vtype);
            unsigned new_rn_vtype = vecCsr->get(Vtype);
            vecCsr->unsetReady(Vtype, new_rn_vtype);
            vecCsr->addProducer(Vtype, new_rn_vtype);
    
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming dest: arch reg vtype to physical reg %i. "
                    "Old mapping is %i.\n",
                    tid, new_rn_vtype, prev_rn_vtype);
    
            VtypeRenameHistory vtype_hb_entry(inst->seqNum, new_rn_vtype, prev_rn_vtype);
            vtypeHistoryBuffer[tid].push_front(vtype_hb_entry);
    
            DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                                "Adding instruction to vtype history buffer (size=%i).\n",
                    tid, (*vtypeHistoryBuffer[tid].begin()).instSeqNum,
                    vtypeHistoryBuffer[tid].size());
    
            inst->renameVtype(new_rn_vtype, prev_rn_vtype);
    
            if (!inst->isVecCsrVtype()) {
                writeImmVtype(inst, tid);
            }
        }
    
        if (inst->isCsrVl()) {
    
            unsigned prev_rn_vl = vecCsr->curPhysVecCsr(Vl);
            unsigned new_rn_vl = vecCsr->get(Vl);
            vecCsr->unsetReady(Vl, new_rn_vl);
            vecCsr->addProducer(Vl, new_rn_vl);
    
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming dest: arch reg vl to physical reg %i. "
                    "Old mapping is %i.\n",
                    tid, new_rn_vl, prev_rn_vl);
    
            VlRenameHistory vl_hb_entry(inst->seqNum, new_rn_vl, prev_rn_vl);
            vlHistoryBuffer[tid].push_front(vl_hb_entry);
    
            DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                                "Adding instruction to vl history buffer (size=%i).\n",
                    tid, (*vlHistoryBuffer[tid].begin()).instSeqNum,
                    vlHistoryBuffer[tid].size());
    
            inst->renameVl(new_rn_vl, prev_rn_vl);
    
        }             
    
        if (inst->isCsrVtype()) {
    
            unsigned prev_rn_vtype = vecCsr->curPhysVecCsr(Vtype);
            unsigned new_rn_vtype = vecCsr->get(Vtype);
            vecCsr->unsetReady(Vtype, new_rn_vtype);
            vecCsr->addProducer(Vtype, new_rn_vtype);
    
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming dest: arch reg vtype to physical reg %i. "
                    "Old mapping is %i.\n",
                    tid, new_rn_vtype, prev_rn_vtype);
    
            VtypeRenameHistory vtype_hb_entry(inst->seqNum, new_rn_vtype, prev_rn_vtype);
            vtypeHistoryBuffer[tid].push_front(vtype_hb_entry);
    
            DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                                "Adding instruction to vtype history buffer (size=%i).\n",
                    tid, (*vtypeHistoryBuffer[tid].begin()).instSeqNum,
                    vtypeHistoryBuffer[tid].size());
    
            inst->renameVtype(new_rn_vtype, prev_rn_vtype);
    
        }  
    }

    if (inst->isVsseg() && inst->isMacroVector()) {
        RegId empty;
        RenameHistory hb_entry(inst->seqNum, empty, 0, 0, true);

        historyBuffer[tid].push_front(hb_entry);
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                            "Adding Vsseg MacroVector instruction to history buffer (size=%i).\n",
                tid, (*historyBuffer[tid].begin()).instSeqNum,
                historyBuffer[tid].size());         
    }
    for (int dest_idx = 0; dest_idx < num_dest_regs; dest_idx++)
    {
        const RegId &dest_reg = inst->destRegIdx(dest_idx);

        UnifiedRenameMap::RenameInfo rename_result;

        RegId flat_dest_regid = dest_reg.flatten(*isa);

        if (inst->isMacroVector() && (inst->isNonSplitVector() &&
            dest_reg.regClass().type() == VecRegClass || !inst->isNonSplitVector())) {

            RenameHistory hb_entry(inst->seqNum, flat_dest_regid, 0, 0, true);

            historyBuffer[tid].push_front(hb_entry);
            DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                                "Adding MacroVector instruction to history buffer (size=%i).\n",
                    tid, (*historyBuffer[tid].begin()).instSeqNum,
                    historyBuffer[tid].size()); 
            continue;
        } else if (inst->isMicroVector() && inst->isNonSplitVector() &&
                  (dest_reg.regClass().type() == IntRegClass ||
                  dest_reg.regClass().type() == FloatRegClass)) {

            continue;

        }

        flat_dest_regid.setNumPinnedWrites(dest_reg.getNumPinnedWrites());

        bool stall = false;
        rename_result = map->rename(flat_dest_regid, inst_index, stall);
        if ((!rename_result.first) || stall) {
            return false;
        }

        inst->flattenedDestIdx(dest_idx, flat_dest_regid);

        scoreboard->unsetReg(rename_result.first);

        DPRINTF(RxuRename,
                "[tid:%i] "
                "Renaming dest: arch reg %i (%s) to physical reg %i (%i).\n",
                tid, dest_reg.index(), dest_reg.className(),
                rename_result.first->index(),
                rename_result.first->flatIndex());

        if (inst->isMicroVector() && !inst->isVset()) {
            // Record the rename information so that a history can be kept.
            RenameHistory hb_entry(inst->seqNum, flat_dest_regid,
                                    rename_result.first,
                                    rename_result.second,
                                    false);

            microAddToHistory(hb_entry, inst, tid);

            DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                                "Adding MicroVector instruction to history buffer (size=%i).\n",
                    tid, inst->seqNum,
                    historyBuffer[tid].size());

            inst->ifrenamedest = true;                
        } else {
                        // Record the rename information so that a history can be kept.
            RenameHistory hb_entry(inst->seqNum, flat_dest_regid,
                                    rename_result.first,
                                    rename_result.second,
                                    false);

            historyBuffer[tid].push_front(hb_entry);

            DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                                "Adding instruction to history buffer (size=%i).\n",
                    tid, (*historyBuffer[tid].begin()).instSeqNum,
                    historyBuffer[tid].size());
            
            inst->ifrenamedest = true;        
        }

        // Tell the instruction to rename the appropriate destination
        // register (dest_idx) to the new physical register
        // (rename_result.first), and record the previous physical
        // register that the same logical register was renamed to
        // (rename_result.second).
        inst->renameDestReg(dest_idx,
                            rename_result.first,
                            rename_result.second);

        // ++stats.renamedOperands;
        // return true;
    }
    // freeList->dump();
    // cprintf("after rename dst: \n");
    // dumpHistory();

    ppRename->notify(inst);

    return true;
}

void
RenameUnit::renameSrcRegs(const DynInstPtr &inst, ThreadID tid)
{
    gem5::ThreadContext *tc = inst->tcBase();
    UnifiedRenameMap *map = renameMap[tid];
    unsigned num_src_regs = inst->numSrcRegs();
    auto *isa = tc->getIsaPtr();

    if (inst->needFRM()) {

        unsigned curFRMIndex = frm->curPhysFRM();
        inst->renameFRM(curFRMIndex);
        
        DPRINTF(RxuRename,
                "[tid:%i] "
                "Renaming src: frm, got phys reg %i.\n",
                tid, curFRMIndex);
        if (frm->isReady(curFRMIndex)) {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (FRM) is ready.\n",
                    tid, curFRMIndex);
            inst->readyFRM(true);
        } else {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (FRM) is not ready.\n",
                    tid, curFRMIndex);
        }
        frm->addConsumer(curFRMIndex, inst);
    }

    if (!cpu->vsetBranch) {
        if (inst->isMacroVector() && !inst->redecoded) {
            unsigned curVlIndex = vecCsr->curPhysVecCsr(Vl);
            inst->renameVl(curVlIndex);
            
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming src: vl, got phys reg %i.\n",
                    tid, curVlIndex);
            if (vecCsr->isReady(Vl,curVlIndex)) {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vl) is ready.\n",
                        tid, curVlIndex);
                inst->readyVl(true);
            } else {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vl) is not ready.\n",
                        tid, curVlIndex);
            }
            vecCsr->addConsumer(Vl,curVlIndex, inst);
    
            unsigned curVtypeIndex = vecCsr->curPhysVecCsr(Vtype);
            inst->renameVtype(curVtypeIndex);
            
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming src: vtype, got phys reg %i.\n",
                    tid, curVtypeIndex);
            if (vecCsr->isReady(Vtype,curVtypeIndex)) {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vtype) is ready.\n",
                        tid, curVtypeIndex);
                inst->readyVtype(true);
            } else {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vtype) is not ready.\n",
                        tid, curVtypeIndex);
            }
            vecCsr->addConsumer(Vtype,curVtypeIndex, inst);
        }
        if (inst->isCsrVl()) {
            unsigned curVlIndex = vecCsr->curPhysVecCsr(Vl);
            inst->renameVl(curVlIndex);
            
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming src: csr vl, got phys reg %i.\n",
                    tid, curVlIndex);
            if (vecCsr->isReady(Vl,curVlIndex)) {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vl) is ready.\n",
                        tid, curVlIndex);
                inst->readyVl(true);
            } else {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vl) is not ready.\n",
                        tid, curVlIndex);
            }
            vecCsr->addConsumer(Vl,curVlIndex, inst);
        }
    
        if (inst->isCsrVtype()) {
            unsigned curVtypeIndex = vecCsr->curPhysVecCsr(Vtype);
            inst->renameVtype(curVtypeIndex);
            
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Renaming src: csr vtype, got phys reg %i.\n",
                    tid, curVtypeIndex);
            if (vecCsr->isReady(Vtype,curVtypeIndex)) {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vtype) is ready.\n",
                        tid, curVtypeIndex);
                inst->readyVtype(true);
            } else {
                DPRINTF(RxuRename,
                        "[tid:%i] "
                        "Register %d (Vtype) is not ready.\n",
                        tid, curVtypeIndex);
            }
            vecCsr->addConsumer(Vtype,curVtypeIndex, inst);
        }
    }

    if (inst->needVxrm() && inst->isMicroVector()) {

        unsigned curVxrmIndex = vecCsr->curPhysVecCsr(Vxrm);
        inst->renameVxrm(curVxrmIndex);
        
        DPRINTF(RxuRename,
                "[tid:%i] "
                "Renaming src: vxrm, got phys reg %i.\n",
                tid, curVxrmIndex);
        if (vecCsr->isReady(Vxrm, curVxrmIndex)) {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (vxrm) is ready.\n",
                    tid, curVxrmIndex);
            inst->readyVxrm(true);
        } else {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (vxrm) is not ready.\n",
                    tid, curVxrmIndex);
        }
        vecCsr->addConsumer(Vxrm, curVxrmIndex, inst);
    }

    /** Special vector inst first to normal rename pipeline, it should just rename scalar reg. */
    if (inst->isSpecialVector() && inst->isMacroVector() && !inst->redecoded) {
        num_src_regs = 1;
    }

    for (int src_idx = 0; src_idx < num_src_regs; src_idx++)
    {

        const RegId &src_reg = inst->srcRegIdx(src_idx);
        
        if (inst->isSpecialVector() && inst->isMacroVector()
             && inst->redecoded && src_reg.regClass().type() != VecRegClass) {
            continue;
        }

        if (src_reg.regClass().type() ==VecRegClass) {
            inst->is_vec_reg[src_idx] = true;
        }

        if ((src_reg.regClass().type() == IntRegClass || src_reg.regClass().type() == FloatRegClass) &&
            inst->isMicroVector() && !inst->isNonSplitVector()) {

            microInheritMacroRename(inst,src_idx);
            continue;

        } else if (inst->isNonSplitVector() && inst->isMicroVector() &&
                  (src_reg.regClass().type() == IntRegClass || src_reg.regClass().type() == FloatRegClass)){

            continue;

        }

        const RegId flat_reg = src_reg.flatten(*isa);
        PhysRegIdPtr renamed_reg;

        renamed_reg = map->lookup(flat_reg);
        
        if (inst->isMacroop()) {
            DPRINTF(RxuRename,
                    "[tid:%i] [sn:%lli] "
                    "MacroVector renaming src: %s arch reg %i, got phys reg %i (%s)\n",
                    tid, inst->seqNum, flat_reg.className(),
                    src_reg.index(), renamed_reg->index(),
                    renamed_reg->className());            
        } else {
            DPRINTF(RxuRename,
                    "[tid:%i] [sn:%lli] "
                    "Renaming src: %s arch reg %i, got phys reg %i (%s)\n",
                    tid, inst->seqNum, flat_reg.className(),
                    src_reg.index(), renamed_reg->index(),
                    renamed_reg->className());            
        }

        inst->renameSrcReg(src_idx, renamed_reg);
        
        if (inst->isMacroVector() && src_reg.regClass().type() == VecRegClass) continue;
        inst->ifrenamesrc = true;

        // See if the register is ready or not.
        if (scoreboard->getReg(renamed_reg))
        {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (flat: %d) (%s) is ready.\n",
                    tid, renamed_reg->index(), renamed_reg->flatIndex(),
                    renamed_reg->className());
            if(src_idx == 0) {
                inst->if_src0_ready = true;
            }

            inst->markSrcRegReady(src_idx);                

            PhysRegIdPtr srcreg = inst->renamedSrcIdx(src_idx);
            if(inst->isMemRef() && !inst->isVector()){
                bool srcready = srcreg->classValue() == InvalidRegClass ? true : inst->readySrcIdx(src_idx);
                inst->markSrcRegReady(src_idx,srcready,src_idx);
                if(src_idx == 1){
                    inst->stdDataReady = true;
                }
            }
            else if(inst->isMemRef()){
                bool srcready = srcreg->classValue() == InvalidRegClass ? true : inst->readySrcIdx(srcreg->index());
                bool is_index_stride = inst->staticInst->isStrideIndex();
                inst->markSrcRegReady(src_idx,srcready,src_idx,inst->isStore(),is_index_stride);
            }
        }
        else
        {   
            if (false && inst->isMicroVector() && num_src_regs >= 2 && inst->numDestRegs()
                 && !inst->ori_inst->isSpecialVectorNotExec() && !inst->isRxuVmInst()) {
                if (inst->staticInst->machInst.vm && src_idx == (num_src_regs -1) && src_reg.index() == inst->destRegIdx(0).index()) {
                    inst->markSrcRegReady(src_idx);                

                    PhysRegIdPtr srcreg = inst->renamedSrcIdx(src_idx);
                    if(inst->isMemRef()){
                        bool srcready = srcreg->classValue() == InvalidRegClass ? true : inst->readySrcIdx(srcreg->index());
                        bool is_index_stride = inst->staticInst->isStrideIndex();
                        inst->markSrcRegReady(src_idx,srcready,src_idx,inst->isStore(),is_index_stride);
                    }
                } else if (!inst->staticInst->machInst.vm && src_idx == (num_src_regs -2) && src_reg.index() == inst->destRegIdx(0).index()) {
                    inst->markSrcRegReady(src_idx);                

                    PhysRegIdPtr srcreg = inst->renamedSrcIdx(src_idx);
                    if(inst->isMemRef()){
                        bool srcready = srcreg->classValue() == InvalidRegClass ? true : inst->readySrcIdx(srcreg->index());
                        bool is_index_stride = inst->staticInst->isStrideIndex();
                        inst->markSrcRegReady(src_idx,srcready,src_idx,inst->isStore(),is_index_stride);
                    }
                }
            }
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (flat: %d) (%s) is not ready.\n",
                    tid, renamed_reg->index(), renamed_reg->flatIndex(),
                    renamed_reg->className());
        }
    }

}

void
RenameUnit::recoverAndRelease()
{
    freeList->recoverAndRelease();
    frm->recover();
    frm->release();
    vecCsr->recover();
    vecCsr->release();
}

std::vector<bool>
RenameUnit::checkScalarRenameStall()
{
    return freeList->checkStall();
}

std::vector<bool>
RenameUnit::checkVecRenameStall()
{
    return freeList->checkVecStall();
}

bool
RenameUnit::checkFRMRenameStall() {
    return frm->stall();
}

std::vector<bool>
RenameUnit::checkVecCsrRenameStall() {
    return vecCsr->stall();
}

void
RenameUnit::dumpHistory()
{
    cprintf("Tick %i:\n", curTick());
    std::list<RenameHistory>::iterator buf_it;
    for (ThreadID tid = 0; tid < numThreads; tid++)
    {
        buf_it = historyBuffer[tid].begin();
        while (buf_it != historyBuffer[tid].end())
        {
            cprintf("Seq num: %i\nArch reg[%s]: %i New phys reg:"
                    " %i[%s] Old phys reg: %i[%s]\n",
                    (*buf_it).instSeqNum,
                    (*buf_it).archReg.className(),
                    (*buf_it).archReg.index(),
                    (*buf_it).newPhysReg->index(),
                    (*buf_it).newPhysReg->className(),
                    (*buf_it).prevPhysReg->index(),
                    (*buf_it).prevPhysReg->className());

            buf_it++;
        }
    }
}

bool 
RenameUnit::checkVlVtypeReady(const DynInstPtr &inst) 
{
    return (vecCsr->isReady(Vl, inst->rn_src_vl) && vecCsr->isReady(Vtype,inst->rn_src_vtype));
}

void
RenameUnit::getVlVtype(const DynInstPtr &inst)
{
    inst->new_vl = vecCsr->getVal(Vl, inst->rn_src_vl);
    uint64_t vtype = vecCsr->getVal(Vtype, inst->rn_src_vtype);
    inst->new_vtype = vtypeTransfer(vtype);
}

void 
RenameUnit::microInheritMacroRename(const DynInstPtr &inst, int src_idx)
{
    if (inst->ori_inst->ifrenamesrc) {
        PhysRegIdPtr renamed_reg = inst->ori_inst->renamedSrcIdx(src_idx);
        if (!renamed_reg) return;
        inst->renameSrcReg(src_idx, renamed_reg);
        inst->ifrenamesrc = true;
        // See if the register is ready or not.
        if (scoreboard->getReg(renamed_reg))
        {
            DPRINTF(RxuRename,
                    "Inherit register %d (flat: %d) (%s) is ready.\n",
                    renamed_reg->index(), renamed_reg->flatIndex(),
                    renamed_reg->className());

            inst->markSrcRegReady(src_idx);

            PhysRegIdPtr srcreg = inst->renamedSrcIdx(src_idx);
            if(inst->isMemRef() && !inst->isVector()){
                DPRINTF(RxuRename,
                        "srcreg index %d.\n",
                        srcreg->index());
                bool srcready = srcreg->classValue() == InvalidRegClass ? true : inst->readySrcIdx(src_idx);
                inst->markSrcRegReady(src_idx,srcready,src_idx);
                if(src_idx == 1){
                    inst->stdDataReady = true;
                }
            }
            else if(inst->isMemRef()){
                bool srcready = srcreg->classValue() == InvalidRegClass ? true : inst->readySrcIdx(src_idx);
                bool is_index_stride = inst->staticInst->isStrideIndex();
                inst->markSrcRegReady(src_idx,srcready,src_idx,inst->isStore(),is_index_stride);
            }
        }
        else
        {
            DPRINTF(RxuRename,
                    "Inherit register %d (flat: %d) (%s) is not ready.\n",
                    renamed_reg->index(), renamed_reg->flatIndex(),
                    renamed_reg->className());
        }
    }
}

RiscvISA::VTYPE
RenameUnit::vtypeTransfer(uint64_t vtype)
{
    RiscvISA::VTYPE newVtype = 0;
    newVtype.vlmul =  cpu->extractBits(vtype, 0, 3);
    newVtype.vsew = cpu->extractBits(vtype, 3, 3);
    newVtype.vta = cpu->extractBits(vtype, 6, 1);
    newVtype.vma = cpu->extractBits(vtype, 7, 1);
    newVtype.vill = cpu->extractBits(vtype, 63, 1);
    return newVtype;
}

void 
RenameUnit::writeImmVl(const DynInstPtr &inst, ThreadID tid)
{
    RegVal zimm_vsetivli = inst->staticInst->machInst.zimm_vsetivli;
    RegVal uimm_vsetivli = inst->staticInst->machInst.uimm_vsetivli;

    RegVal newVlVal = 0;
    RegVal oldVlVal = cpu->vecCsr.getVal(Vl, inst->prev_vl);
    RegVal reqVl =  uimm_vsetivli;
    RegVal rs1bits = -1;

    RegVal vsew =  8 << (cpu->extractBits(zimm_vsetivli, 3, 3));

    int vlmul = cpu->extractBits(zimm_vsetivli, 0, 3);

    if (vlmul >= 5) vlmul = -(8-vlmul);

    RegVal zero = cpu->extractBits(zimm_vsetivli, 10, 1);

    float vflmul = vlmul >= 0 ? 1 << vlmul : 1.0 / (1 << -vlmul);

    RegVal newVill = !(vflmul >= 0.125 && vflmul <= 8) || vsew > std::min(vflmul, 1.0f) * 64 ||
                        zero != 0;
    RegVal vlmax = newVill ? 0 : (128/vsew) * vflmul;

    if (vlmax == 0) {
        newVlVal = 0;
    } else if (rs1bits != 0) {
        newVlVal = reqVl > vlmax ? vlmax : reqVl;
    } else if (!inst->destRegIdx(0).isZeroReg()) {
        newVlVal = vlmax; 
    } else {
        newVlVal = oldVlVal > vlmax ? vlmax : oldVlVal;
    }
    cpu->vecCsr.setVal(Vl, inst->rn_vl, newVlVal);
    DPRINTF(RxuRename, "Writing MiscReg vl (%d): %#x.\n", inst->rn_vl, newVlVal);

    cpu->vecCsr.setReady(Vl, inst->rn_vl);
    DPRINTF(RxuRename, "waking up vl; (%d): ready.\n", inst->rn_vl);
    cpu->vecCsr.wakeDependences(Vl, inst->rn_vl);              
}

void 
RenameUnit::writeImmVtype(const DynInstPtr &inst, ThreadID tid)
{
    std::string inst_name = inst->staticInst->getName();

    RegVal zimm_vsetivli = inst->staticInst->machInst.zimm_vsetivli;
    RegVal uimm_vsetivli = inst->staticInst->machInst.uimm_vsetivli;
    RegVal zimm_vsetvli = inst->staticInst->machInst.zimm_vsetvli;

    RegVal newVtypeVal = 0;
    RegVal oldVtypeVal = cpu->vecCsr.getVal(Vtype, inst->rn_vtype);
    RegVal reqVtype =  inst_name == "vsetvli" ? zimm_vsetvli : zimm_vsetivli;
    RegVal rs1bits = -1;

    RegVal vsew =  8 << (cpu->extractBits(reqVtype, 3, 3));

    int vlmul = cpu->extractBits(reqVtype, 0, 3);

    if (vlmul >= 5) vlmul = -(8-vlmul);

    RegVal zero = inst_name == "vsetvli" ? cpu->extractBits(zimm_vsetvli, 8, 3) :
    cpu->extractBits(zimm_vsetivli, 8, 2);

    float vflmul = vlmul >= 0 ? 1 << vlmul : 1.0 / (1 << -vlmul);

    RegVal newVill = !(vflmul >= 0.125 && vflmul <= 8) || vsew > std::min(vflmul, 1.0f) * 64 ||
                        zero != 0;
    RegVal vlmax = newVill ? 0 : (128/vsew) * vflmul;

    if (newVill) {
        newVtypeVal = 1ULL << 63;
    }else if (oldVtypeVal == reqVtype) {
        newVtypeVal = oldVtypeVal;
    } else {
        newVtypeVal = reqVtype; 
    }

    cpu->vecCsr.setVal(Vtype, inst->rn_vtype, newVtypeVal);
    DPRINTF(RxuRename, "Writing MiscReg vtype (%d): %#x.\n", inst->rn_vtype, newVtypeVal);
    
    cpu->vecCsr.setReady(Vtype, inst->rn_vtype);
    DPRINTF(RxuRename, "waking up vtype (%d): ready.\n", inst->rn_vtype);
    cpu->vecCsr.wakeDependences(Vtype, inst->rn_vtype);              
}

void 
RenameUnit::microAddToHistory(RenameHistory hb_entry, const DynInstPtr &inst, ThreadID tid)
{   
    InstSeqNum target = inst->oriSeqNum;
    auto it = std::find_if(historyBuffer[tid].begin(), historyBuffer[tid].end(), [target](const RenameHistory& item) {
        return item.instSeqNum == target;
    });
    if (it == historyBuffer[tid].end()) return;

    if (inst->isLastMicroop()) {
        *it = hb_entry;
    } else {
        historyBuffer[tid].insert(it, hb_entry);        
    }
    historyBuffer[tid].sort([](const RenameHistory& a, const RenameHistory& b) {
        return a.instSeqNum > b.instSeqNum;
    });
}

} // namespace rxuo3

} // namespace gem5

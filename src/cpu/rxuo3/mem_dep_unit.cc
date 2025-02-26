#include "cpu/rxuo3/mem_dep_unit.hh"

#include <map>
#include <memory>
#include <vector>

#include "base/compiler.hh"
#include "base/debug.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/wtb.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuMemDepUnit.hh"
#include "debug/Rxucun.hh"
#include "params/BaseRxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

#ifdef GEM5_DEBUG
int MemDepUnit::MemDepEntry::memdep_count = 0;
int MemDepUnit::MemDepEntry::memdep_insert = 0;
int MemDepUnit::MemDepEntry::memdep_erase = 0;
#endif

MemDepUnit::MemDepUnit() : wtbPtr(NULL), stats(nullptr) {}

MemDepUnit::MemDepUnit(const BaseRxuO3CPUParams &params)
    : _name(params.name + ".memdepunit"),
      depPred(params.store_set_clear_period, params.SSITSize,
              params.LFSTSize),
      wtbPtr(NULL),
      stats(nullptr)
{
    DPRINTF(RxuMemDepUnit, "Creating MemDepUnit object.\n");
}

MemDepUnit::~MemDepUnit()
{
    for (ThreadID tid = 0; tid < MaxThreads; tid++) {

        ListIt inst_list_it = instList[tid].begin();

        MemDepHashIt hash_it;

        while (!instList[tid].empty()) {
            hash_it = memDepHash.find((*inst_list_it)->seqNum);

            assert(hash_it != memDepHash.end());

            memDepHash.erase(hash_it);

            instList[tid].erase(inst_list_it++);
        }
    }

#ifdef GEM5_DEBUG
    assert(MemDepEntry::memdep_count == 0);
#endif
}

void
MemDepUnit::init(const BaseRxuO3CPUParams &params, ThreadID tid, CPU *cpu)
{
    DPRINTF(RxuMemDepUnit, "Creating MemDepUnit %i object.\n",tid);

    _name = csprintf("%s.memDep%d", params.name, tid);
    id = tid;

    depPred.init(params.store_set_clear_period, params.SSITSize,
            params.LFSTSize);

    std::string stats_group_name = csprintf("MemDepUnit__%i", tid);
    cpu->addStatGroup(stats_group_name.c_str(), &stats);
}

MemDepUnit::MemDepUnitStats::MemDepUnitStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(insertedLoads, statistics::units::Count::get(),
               "Number of loads inserted to the mem dependence unit."),
      ADD_STAT(insertedStores, statistics::units::Count::get(),
               "Number of stores inserted to the mem dependence unit."),
      ADD_STAT(conflictingLoads, statistics::units::Count::get(),
               "Number of conflicting loads."),
      ADD_STAT(conflictingStores, statistics::units::Count::get(),
               "Number of conflicting stores.")
{
}

bool
MemDepUnit::isDrained() const
{
    bool drained = instsToReplay.empty()
                 && memDepHash.empty()
                 && instsToReplay.empty();
    for (int i = 0; i < MaxThreads; ++i)
        drained = drained && instList[i].empty();

    return drained;
}

void
MemDepUnit::drainSanityCheck() const
{
    assert(instsToReplay.empty());
    assert(memDepHash.empty());
    for (int i = 0; i < MaxThreads; ++i)
        assert(instList[i].empty());
    assert(instsToReplay.empty());
    assert(memDepHash.empty());
}

void
MemDepUnit::takeOverFrom()
{
    // Be sure to reset all state.
    loadBarrierSNs.clear();
    storeBarrierSNs.clear();
    depPred.clear();
}

void
MemDepUnit::setWTB(WTB *wtb_ptr)
{
    wtbPtr = wtb_ptr;
}

void
MemDepUnit::insertBarrierSN(const DynInstPtr &barr_inst)
{
    InstSeqNum barr_sn = barr_inst->seqNum;

    if (barr_inst->isReadBarrier() || barr_inst->isHtmCmd())
        loadBarrierSNs.insert(barr_sn);
    if (barr_inst->isWriteBarrier() || barr_inst->isHtmCmd())
        storeBarrierSNs.insert(barr_sn);

    if (debug::RxuMemDepUnit) {
        const char *barrier_type = nullptr;
        if (barr_inst->isReadBarrier() && barr_inst->isWriteBarrier())
            barrier_type = "memory";
        else if (barr_inst->isReadBarrier())
            barrier_type = "read";
        else if (barr_inst->isWriteBarrier())
            barrier_type = "write";

        if (barrier_type) {
            DPRINTF(RxuMemDepUnit, "Inserted a %s barrier %s SN:%lli\n",
                    barrier_type, barr_inst->pcState(), barr_sn);
        }

        if (loadBarrierSNs.size() || storeBarrierSNs.size()) {
            DPRINTF(RxuMemDepUnit, "Outstanding load barriers = %d; "
                                "store barriers = %d\n",
                    loadBarrierSNs.size(), storeBarrierSNs.size());
        }
    }
}

void
MemDepUnit::memhashadd(const DynInstPtr &inst){
    if(inst->memhash == false){
        ThreadID tid = inst->threadNumber;
        MemDepEntryPtr inst_entry = std::make_shared<MemDepEntry>(inst);
        // Add the MemDepEntry to the hash.
        memDepHash.insert(
            std::pair<InstSeqNum, MemDepEntryPtr>(inst->seqNum, inst_entry));
    #ifdef GEM5_DEBUG
        MemDepEntry::memdep_insert++;
    #endif
        instList[tid].push_back(inst);

        inst_entry->listIt = --(instList[tid].end());
    }
    inst->memhash = true;
}

void
MemDepUnit::insert_test(const DynInstPtr &inst)
{
    MemDepEntryPtr inst_entry = findInHash(inst);
    DPRINTF(RxuMemDepUnit, "memhash_inst seqnum:%lli ",inst_entry->inst->seqNum);
}

void
MemDepUnit::insert(const DynInstPtr &inst)
{
    // ThreadID tid = inst->threadNumber;

    // MemDepHashIt hashfind_it = memDepHash.find(inst->seqNum);
    MemDepEntryPtr inst_entry = findInHash(inst);
    // if (hashfind_it == memDepHash.end()){
        // MemDepEntryPtr inst_entry = std::make_shared<MemDepEntry>(inst);

        //Add the MemDepEntry to the hash.
        // memDepHash.insert(
        //     std::pair<InstSeqNum, MemDepEntryPtr>(inst->seqNum, inst_entry));
        // #ifdef GEM5_DEBUG
        //     MemDepEntry::memdep_insert++;
        // #endif

        // instList[tid].push_back(inst);

        // inst_entry->listIt = --(instList[tid].end());

        // Check any barriers and the dependence predictor for any
        // producing memrefs/stores.
        std::vector<InstSeqNum>  producing_stores;
        if ((inst->isLoad() || inst->isAtomic()) && hasLoadBarrier()) {
            DPRINTF(RxuMemDepUnit, "%d load barriers in flight\n",
            loadBarrierSNs.size());
            // producing_stores.insert(std::end(producing_stores),
            // std::begin(loadBarrierSNs),
            // std::end(loadBarrierSNs));
            for (std::unordered_set<InstSeqNum>::iterator it = loadBarrierSNs.begin();
                it != loadBarrierSNs.end(); ++it) {
                if ((*it) < inst->seqNum) {
                    producing_stores.insert(std::end(producing_stores), (*it));
                }
            }
        } else if ((inst->isStore() || inst->isAtomic()) && hasStoreBarrier()) {
            DPRINTF(RxuMemDepUnit, "%d store barriers in flight\n",
            storeBarrierSNs.size());
            // producing_stores.insert(std::end(producing_stores),
            // std::begin(storeBarrierSNs),
            // std::end(storeBarrierSNs));
            for (std::unordered_set<InstSeqNum>::iterator it = storeBarrierSNs.begin();
                it != storeBarrierSNs.end(); ++it) {
                if ((*it) < inst->seqNum) {
                    producing_stores.insert(std::end(producing_stores), (*it));
                }
            }
        } else if(!inst->isStore()){
            int dep = depPred.checkInst(inst->pcState().instAddr());
            if (dep != 0){
                for (auto itvalid = depPred.validLFST[dep].begin();itvalid != depPred.validLFST[dep].end();
                ++itvalid){
                    if((*itvalid).first < inst->seqNum && (*itvalid).second){
                        producing_stores.push_back((*itvalid).first);
                    }
                }
            }
        }

        std::vector<MemDepEntryPtr> store_entries;

        // If there is a producing store, try to find the entry.
        for (auto producing_store : producing_stores) {
            DPRINTF(RxuMemDepUnit, "Searching for producer [sn:%lli]\n",
                            producing_store);
            MemDepHashIt hash_it = memDepHash.find(producing_store);

            if (hash_it != memDepHash.end()) {
                store_entries.push_back((*hash_it).second);
                DPRINTF(RxuMemDepUnit, "Producer found\n");
            }
        }

        // If no store entry, then instruction can issue as soon as the registers
        // are ready.
        if (store_entries.empty()) {
            DPRINTF(RxuMemDepUnit, "No dependency for inst PC "
                    "%s [sn:%lli]: \"%s\", ibuffer_id: %d.\n", inst->pcState(), inst->seqNum, inst->staticInst->disassemble(inst->pcState().instAddr()), inst->ibuffer_id);

            assert(inst_entry->memDeps == 0);

            if (inst->readyToIssue() && inst->had_src_data) {
                if(inst->arrivewtb){
                    if (inst->arrive_wtb == -1) {
                        inst->arrive_wtb = curTick();
                    }
                    inst_entry->regsReady = true;
                    inst->staReady = true;
                    moveToReady(inst_entry);
                }
            }

        } else {
            // Otherwise make the instruction dependent on the store/barrier.
            DPRINTF(RxuMemDepUnit, "Adding to dependency list\n");
            for ([[maybe_unused]] auto producing_store : producing_stores)
                DPRINTF(RxuMemDepUnit, "\tinst PC %s is dependent on [sn:%lli].\n",
                    inst->pcState(), producing_store);

            if (inst->readyToIssue()) {
                inst_entry->regsReady = true;
            }

            // Clear the bit saying this instruction can issue.
            inst->clearCanIssue();

            // Add this instruction to the list of dependents.
            for (auto store_entry : store_entries)
                store_entry->dependInsts.push_back(inst_entry);

            inst_entry->memDeps = store_entries.size();

            if (inst->isLoad()) {
                ++stats.conflictingLoads;
            } else {
                ++stats.conflictingStores;
            }
        }

        // for load-acquire store-release that could also be a barrier
        insertBarrierSN(inst);

        if (inst->isStore() || inst->isAtomic()) {
            DPRINTF(RxuMemDepUnit, "Inserting store/atomic PC %s [sn:%lli].\n",
                    inst->pcState(), inst->seqNum);
                stq.emplace(inst->seqNum,inst);
            depPred.insertStore(inst->pcState().instAddr(), inst->seqNum,
                    inst->threadNumber);

            ++stats.insertedStores;
        } else if (inst->isLoad()) {
            ++stats.insertedLoads;
        } else {
            panic("Unknown type! (most likely a barrier).");
        }


}

void
MemDepUnit::validlfst(const DynInstPtr &inst){
    if (inst->isStore() || inst->isAtomic()) {
        DPRINTF(RxuMemDepUnit, "Inserting store/atomic PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);

        depPred.insertStore(inst->pcState().instAddr(), inst->seqNum,
                inst->threadNumber);
    }
}

void
MemDepUnit::insertNonSpec(const DynInstPtr &inst)
{
    insertBarrier(inst);

    // Might want to turn this part into an inline function or something.
    // It's shared between both insert functions.
    if (inst->isStore() || inst->isAtomic()) {
        DPRINTF(RxuMemDepUnit, "Inserting store/atomic PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);

        depPred.insertStore(inst->pcState().instAddr(), inst->seqNum,
                inst->threadNumber);

        ++stats.insertedStores;
    } else if (inst->isLoad()) {
        ++stats.insertedLoads;
    } else {
        panic("Unknown type! (most likely a barrier).");
    }
}

void
MemDepUnit::insertBarrier(const DynInstPtr &barr_inst)
{
    // __attribute_maybe_unused__ ThreadID tid = barr_inst->threadNumber;

    // MemDepEntryPtr inst_entry = std::make_shared<MemDepEntry>(barr_inst);
    // if(barr_inst->memhash == false){
    //      // Add the MemDepEntry to the hash.
    //     memDepHash.insert(
    //         std::pair<InstSeqNum, MemDepEntryPtr>(barr_inst->seqNum, inst_entry));
    // #ifdef GEM5_DEBUG
    //     MemDepEntry::memdep_insert++;
    // #endif

    //     // Add the instruction to the instruction list.
    //     instList[tid].push_back(barr_inst);

    //     inst_entry->listIt = --(instList[tid].end());
    // }
    // barr_inst->memhash = true;
    insertBarrierSN(barr_inst);
}

void
MemDepUnit::regsReady(const DynInstPtr &inst)
{
    DPRINTF(RxuMemDepUnit, "Marking registers as ready for "
            "instruction PC %s [sn:%lli].\n",
            inst->pcState(), inst->seqNum);
    if(inst->isSquashed()){
        return;;
    }

    MemDepEntryPtr inst_entry = findInHash(inst);

    inst_entry->regsReady = true;

    if (inst_entry->memDeps == 0) {
        DPRINTF(RxuMemDepUnit, "Instruction has its memory "
                "dependencies resolved, adding it to the ready list.\n");

        moveToReady(inst_entry);
        // if(inst_entry->inst->isStore() && inst_entry->inst->ifstdataready){
        //     if(wtbPtr->readySTdata0.size() <= wtbPtr->readySTdata1.size()){
        //         wtbPtr->readySTdata0.push(inst_entry->inst);
        //     }
        //     else {
        //         wtbPtr->readySTdata1.push(inst_entry->inst);
        //     }
        // }
    } else {
        DPRINTF(RxuMemDepUnit, "Instruction still waiting on "
                "memory dependency.\n");
    }
}

void
MemDepUnit::nonSpecInstReady(const DynInstPtr &inst)
{
    DPRINTF(RxuMemDepUnit, "Marking non speculative "
            "instruction PC %s as ready [sn:%lli].\n",
            inst->pcState(), inst->seqNum);

    MemDepEntryPtr inst_entry = findInHash(inst);

    moveToReady(inst_entry);
}

void
MemDepUnit::reschedule(const DynInstPtr &inst)
{
    instsToReplay.push_back(inst);
}

void
MemDepUnit::replay()
{
    DynInstPtr temp_inst;

    // For now this replay function replays all waiting memory ops.
    while (!instsToReplay.empty()) {

        temp_inst = instsToReplay.front();

        MemDepEntryPtr inst_entry = findInHash(temp_inst);

        DPRINTF(RxuMemDepUnit, "Replaying mem instruction PC %s [sn:%lli].\n",
                temp_inst->pcState(), temp_inst->seqNum);

        moveToReady(inst_entry);
        //if need to deel with std??

        instsToReplay.pop_front();
    }
}

void
MemDepUnit::completed(const DynInstPtr &inst)
{
    DPRINTF(RxuMemDepUnit, "Completed mem instruction PC %s [sn:%lli].\n",
            inst->pcState(), inst->seqNum);

    if(inst->strictlyOrdered() && inst->lbu_exfault){
        DPRINTF(RxuMemDepUnit, "because of lbu fault can not Completed mem instruction PC %s [sn:%lli].\n",
            inst->pcState(), inst->seqNum);
            return;
    }
    ThreadID tid = inst->threadNumber;

    // Remove the instruction from the hash and the list.
    MemDepHashIt hash_it = memDepHash.find(inst->seqNum);

    assert(hash_it != memDepHash.end());

    instList[tid].erase((*hash_it).second->listIt);

    (*hash_it).second = NULL;

    memDepHash.erase(hash_it);
#ifdef GEM5_DEBUG
    MemDepEntry::memdep_erase++;
#endif
}

void
MemDepUnit::completeInst(const DynInstPtr &inst)
{
    wakeDependents(inst);
    completed(inst);
    InstSeqNum barr_sn = inst->seqNum;

    if (inst->isWriteBarrier() || inst->isHtmCmd()) {
        assert(hasStoreBarrier());
        storeBarrierSNs.erase(barr_sn);
    }
    if (inst->isReadBarrier() || inst->isHtmCmd()) {
        assert(hasLoadBarrier());
        loadBarrierSNs.erase(barr_sn);
    }
    if (debug::RxuMemDepUnit) {
        const char *barrier_type = nullptr;
        if (inst->isWriteBarrier() && inst->isReadBarrier())
            barrier_type = "Memory";
        else if (inst->isWriteBarrier())
            barrier_type = "Write";
        else if (inst->isReadBarrier())
            barrier_type = "Read";

        if (barrier_type) {
            DPRINTF(RxuMemDepUnit, "%s barrier completed: %s SN:%lli\n",
                                barrier_type, inst->pcState(), inst->seqNum);
        }
    }
}

void
MemDepUnit::wakeDependents(const DynInstPtr &inst)
{
    // Only stores, atomics and barriers have dependents.
    if (!inst->isStore() && !inst->isAtomic() && !inst->isReadBarrier() &&
        !inst->isWriteBarrier() && !inst->isHtmCmd()) {
        return;
    }

    MemDepEntryPtr inst_entry = findInHash(inst);

    for (int i = 0; i < inst_entry->dependInsts.size(); ++i ) {
        MemDepEntryPtr woken_inst = inst_entry->dependInsts[i];

        if (!woken_inst->inst) {
            // Potentially removed mem dep entries could be on this list
            continue;
        }

        DPRINTF(RxuMemDepUnit, "Waking up a dependent inst, "
                "[sn:%lli].\n",
                woken_inst->inst->seqNum);

        assert(woken_inst->memDeps > 0);
        woken_inst->memDeps -= 1;

        if ((woken_inst->memDeps == 0) &&
            woken_inst->regsReady &&
            !woken_inst->squashed) {
            moveToReady(woken_inst);
        }

    }

    inst_entry->dependInsts.clear();
}

MemDepUnit::MemDepEntry::MemDepEntry(const DynInstPtr &new_inst) :
    inst(new_inst)
{
#ifdef GEM5_DEBUG
    ++memdep_count;

    DPRINTF(RxuMemDepUnit,
            "Memory dependency entry created. memdep_count=%i %s\n",
            memdep_count, inst->pcState());
#endif
}

MemDepUnit::MemDepEntry::~MemDepEntry()
{
    for (int i = 0; i < dependInsts.size(); ++i) {
        dependInsts[i] = NULL;
    }
#ifdef GEM5_DEBUG
    --memdep_count;

    DPRINTF(RxuMemDepUnit,
            "Memory dependency entry deleted. memdep_count=%i %s\n",
            memdep_count, inst->pcState());
#endif
}

void
MemDepUnit::squash(const InstSeqNum &squashed_num, ThreadID tid)
{
    if (!instsToReplay.empty()) {
        ListIt replay_it = instsToReplay.begin();
        while (replay_it != instsToReplay.end()) {
            if ((*replay_it)->threadNumber == tid &&
                (*replay_it)->seqNum > squashed_num) {
                instsToReplay.erase(replay_it++);
            } else {
                ++replay_it;
            }
        }
    }

    for (auto itstq = stq.begin();itstq != stq.end(); ++itstq){
        if((*itstq).first > squashed_num){
            stq.erase((*itstq).first);
        }
    }
    for (auto itldq = ldq.begin();itldq != ldq.end(); ++itldq){
        if((*itldq).first > squashed_num){
            ldq.erase((*itldq).first);
        }
    }

    for (auto itldstdep = ldstdep.begin();itldstdep != ldstdep.end(); ++itldstdep){

        for (auto itldst_deplist = (*itldstdep).second.begin();itldst_deplist != (*itldstdep).second.end();){
            if(*itldst_deplist > squashed_num){
                itldst_deplist = (*itldstdep).second.erase(itldst_deplist);
            }
            else {
                ++itldst_deplist;
            }
        }
        if((*itldstdep).first > squashed_num){
            ldstdep.erase((*itldstdep).first);
        }
    }

    ListIt squash_it = instList[tid].end();
    --squash_it;

    MemDepHashIt hash_it;

    while (!instList[tid].empty() &&
           (*squash_it)->seqNum > squashed_num) {


        DPRINTF(RxuMemDepUnit, "Squashing inst [sn:%lli],ibuffer_id:%i\n",
                (*squash_it)->seqNum,(*squash_it)->ibuffer_id);

        if (!(*squash_it)->isIssued() ||
            ((*squash_it)->isMemRef() &&
            !(*squash_it)->memOpDone())) {

            DPRINTF(RxuMemDepUnit, "[tid:%i] Instruction [sn:%llu] PC %s squashed.\n",
                    tid, (*squash_it)->seqNum, (*squash_it)->pcState());

            bool is_acq_rel = (*squash_it)->isFullMemBarrier() &&
                        ((*squash_it)->isLoad() ||
                        ((*squash_it)->isStore() &&
                        !(*squash_it)->isStoreConditional()));

            // Remove the instruction from the dependency list.
            if (is_acq_rel ||
                (!(*squash_it)->isNonSpeculative() &&
                !(*squash_it)->isStoreConditional() &&
                !(*squash_it)->isAtomic() &&
                !(*squash_it)->isReadBarrier() &&
                !(*squash_it)->isWriteBarrier())) {

                for (int src_reg_idx = 0;
                    src_reg_idx < (*squash_it)->numSrcRegs();
                    src_reg_idx++)
                {
                    PhysRegIdPtr src_reg =
                        (*squash_it)->renamedSrcIdx(src_reg_idx);

                    if (!(*squash_it)->readySrcIdx(src_reg_idx) &&
                        !src_reg->isFixedMapping()) {
                        wtbPtr->dispipe3Stage->rmu->dependGraph.remove(src_reg->flatIndex(),
                            (*squash_it));
                    }
                }

                // Might want to also clear out the head of the dependency graph.

                // @todo: Remove this hack where several statuses are set so the
                // inst will flow through the rest of the pipeline.
                (*squash_it)->setIssued();
                (*squash_it)->setCanCommit();
            }
            }


            for (int dest_reg_idx = 0;
                dest_reg_idx < (*squash_it)->numDestRegs();
                dest_reg_idx++)
            {
                PhysRegIdPtr dest_reg =
                    (*squash_it)->renamedDestIdx(dest_reg_idx);
                if (dest_reg->isFixedMapping()){
                    continue;
                }
                while(!wtbPtr->dispipe3Stage->rmu->dependGraph.empty(dest_reg->flatIndex())) {
                        wtbPtr->dispipe3Stage->rmu->dependGraph.remove(dest_reg->flatIndex(),wtbPtr->dispipe3Stage->rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next->inst);
                       // dispipe3Stage->rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next = NULL;
                }
                assert(wtbPtr->dispipe3Stage->rmu->dependGraph.empty(dest_reg->flatIndex()));
                wtbPtr->dispipe3Stage->rmu->dependGraph.clearInst(dest_reg->flatIndex());
            }

        loadBarrierSNs.erase((*squash_it)->seqNum);

        storeBarrierSNs.erase((*squash_it)->seqNum);

        hash_it = memDepHash.find((*squash_it)->seqNum);

        assert(hash_it != memDepHash.end());

        (*hash_it).second->squashed = true;

        (*hash_it).second = NULL;

        memDepHash.erase(hash_it);
#ifdef GEM5_DEBUG
        MemDepEntry::memdep_erase++;
#endif

        instList[tid].erase(squash_it--);
    }

    // Tell the dependency predictor to squash as well.
    depPred.squash(squashed_num, tid);
}

void
MemDepUnit::violation(const DynInstPtr &store_inst,
        const DynInstPtr &violating_load)
{
    DPRINTF(RxuMemDepUnit, "Passing violating PCs to store sets,"
            " load: %#x, store: %#x\n", violating_load->pcState().instAddr(),
            store_inst->pcState().instAddr());
    // Tell the memory dependence unit of the violation.
    depPred.violation(store_inst->pcState().instAddr(),
            violating_load->pcState().instAddr());
}

void
MemDepUnit::issue(const DynInstPtr &inst)
{
    DPRINTF(RxuMemDepUnit, "Issuing instruction PC %#x [sn:%lli].\n",
            inst->pcState().instAddr(), inst->seqNum);

        depPred.issued(inst->pcState().instAddr(), inst->seqNum, inst->isStore());

}

MemDepUnit::MemDepEntryPtr &
MemDepUnit::findInHash(const DynInstConstPtr &inst)
{
    MemDepHashIt hash_it = memDepHash.find(inst->seqNum);

    assert(hash_it != memDepHash.end());

    return (*hash_it).second;
}

void
MemDepUnit::moveToReady(MemDepEntryPtr &woken_inst_entry)
{
    DPRINTF(RxuMemDepUnit, "Adding instruction [sn:%lli] "
            "to the ready list.\n", woken_inst_entry->inst->seqNum);

    assert(!woken_inst_entry->squashed);

    wtbPtr->addReadyMemInst(woken_inst_entry->inst);

}


void
MemDepUnit::dumpLists()
{
    for (ThreadID tid = 0; tid < MaxThreads; tid++) {
        cprintf("Instruction list %i size: %i\n",
                tid, instList[tid].size());

        ListIt inst_list_it = instList[tid].begin();
        int num = 0;

        while (inst_list_it != instList[tid].end()) {
            cprintf("Instruction:%i\nPC: %s\n[sn:%llu]\n[tid:%i]\nIssued:%i\n"
                    "Squashed:%i\n\n",
                    num, (*inst_list_it)->pcState(),
                    (*inst_list_it)->seqNum,
                    (*inst_list_it)->threadNumber,
                    (*inst_list_it)->isIssued(),
                    (*inst_list_it)->isSquashed());
            inst_list_it++;
            ++num;
        }
    }

    cprintf("Memory dependence hash size: %i\n", memDepHash.size());

#ifdef GEM5_DEBUG
    cprintf("Memory dependence entries: %i\n", MemDepEntry::memdep_count);
#endif
}

} // namespace rxuo3
} // namespace gem5

#include "cpu/rxuo3/rob.hh"

#include <list>

#include "base/logging.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuFetch.hh"
#include "debug/RxuROB.hh"
#include "debug/RxuDebug.hh"
#include "debug/RxuVectorRob.hh"
#include "params/BaseRxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

ROB::ROB(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : robPolicy(params.smtROBPolicy),
      cpu(_cpu),
      numEntries(params.numROBEntries),
      numVecEntries(params.numVecRobEntries),
      squashWidth(params.squashWidth),
      numInstsInROB(0),
      numThreads(params.numThreads),
      stats(_cpu)
{
    //Figure out rob policy
    if (robPolicy == RxuSMTQueuePolicy::Dynamic) {
        //Set Max Entries to Total ROB Capacity
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = numEntries;
            maxVecEntries[tid] = numVecEntries;
        }

    } else if (robPolicy == RxuSMTQueuePolicy::Partitioned) {
        DPRINTF(RxuFetch, "ROB sharing policy set to Partitioned\n");

        //@todo:make work if part_amt doesnt divide evenly.
        int part_amt = numEntries / numThreads;
        int part_amt_vec = numVecEntries / numThreads;

        //Divide ROB up evenly
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = part_amt;
            maxVecEntries[tid] = part_amt_vec;
        }

    } else if (robPolicy == RxuSMTQueuePolicy::Threshold) {
        DPRINTF(RxuFetch, "ROB sharing policy set to Threshold\n");

        int threshold =  params.smtROBThreshold;


        //Divide up by threshold amount
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = threshold;
            maxVecEntries[tid] = numVecEntries;
        }
    }

    for (ThreadID tid = numThreads; tid < MaxThreads; tid++) {
        maxEntries[tid] = 0;
        maxVecEntries[tid] = 0;        
    }

    resetState();
}

void
ROB::resetState()
{
    for (ThreadID tid = 0; tid  < MaxThreads; tid++) {
        threadEntries[tid] = 0;
        threadVecEntries[tid] = 0;
        squashIt[tid] = instList[tid].end();
        squashedSeqNum[tid] = 0;
        doneSquashing[tid] = true;
    }
    numInstsInROB = 0;

    // Initialize the "universal" ROB head & tail point to invalid
    // pointers
    head = instList[0].end();
    tail = instList[0].end();
}

std::string
ROB::name() const
{
    return cpu->name() + ".rob";
}

void
ROB::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    DPRINTF(RxuROB, "Setting active threads list pointer.\n");
    activeThreads = at_ptr;
}

void
ROB::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid  < numThreads; tid++)
        assert(instList[tid].empty());
    assert(isEmpty());
}

void
ROB::takeOverFrom()
{
    resetState();
}

void
ROB::resetEntries()
{
    if (robPolicy != RxuSMTQueuePolicy::Dynamic || numThreads > 1) {
        auto active_threads = activeThreads->size();

        std::list<ThreadID>::iterator threads = activeThreads->begin();
        std::list<ThreadID>::iterator end = activeThreads->end();

        while (threads != end) {
            ThreadID tid = *threads++;

            if (robPolicy == RxuSMTQueuePolicy::Partitioned) {
                maxEntries[tid] = numEntries / active_threads;
                maxVecEntries[tid] = numVecEntries / active_threads;
            } else if (robPolicy == RxuSMTQueuePolicy::Threshold &&
                       active_threads == 1) {
                maxEntries[tid] = numEntries;
                maxVecEntries[tid] = numVecEntries;
            }
        }
    }
}

int
ROB::entryAmount(ThreadID num_threads)
{
    if (robPolicy == RxuSMTQueuePolicy::Partitioned) {
        return numEntries / num_threads;
    } else {
        return 0;
    }
}

int
ROB::countInsts()
{
    int total = 0;

    for (ThreadID tid = 0; tid < numThreads; tid++)
        total += countInsts(tid);

    return total;
}

size_t
ROB::countInsts(ThreadID tid)
{
    return instList[tid].size();
}

void
ROB::insertInst(const DynInstPtr &inst)
{
    assert(inst);

    stats.writes++;

    DPRINTF(RxuROB, "Adding inst PC %s to the ROB.\n", inst->pcState());

    if (inst->isMacroVector()) {
        maxEntries[0]++;
        threadVecEntries[0]++;
        DPRINTF(RxuVectorRob, "Adding inst [sn:%lli] to the Vector ROB, VectorEntries:%i\n", inst->seqNum,threadVecEntries[0]);
    }

    assert(numInstsInROB != maxEntries[0]);

    ThreadID tid = inst->threadNumber;

    instList[tid].push_back(inst);

    //Set Up head iterator if this is the 1st instruction in the ROB
    if (numInstsInROB == 0) {
        head = instList[tid].begin();
        assert((*head) == inst);
    }

    //Must Decrement for iterator to actually be valid  since __.end()
    //actually points to 1 after the last inst
    tail = instList[tid].end();
    tail--;

    inst->setInROB();

    ++numInstsInROB;
    ++threadEntries[tid];

    assert((*tail) == inst);

    DPRINTF(RxuROB, "[tid:%i] Now has %d instructions, %d vector instructions.\n", tid,
            threadEntries[tid], threadVecEntries[0]);
    

}

void
ROB::retireHead(ThreadID tid)
{
    stats.writes++;

    assert(numInstsInROB > 0);

    // Get the head ROB instruction by copying it and remove it from the list
    InstIt head_it = instList[tid].begin();

    DynInstPtr head_inst = std::move(*head_it);
    instList[tid].erase(head_it);

    assert(head_inst->readyToCommit());

    DPRINTF(RxuROB, "[tid:%i] Retiring head instruction, "
            "instruction PC %s, [sn:%llu]\n", tid, head_inst->pcState(),
            head_inst->seqNum);
    
    head_inst->inst_Real_Ready = true;

    --numInstsInROB;
    --threadEntries[tid];

    if (head_inst->isMacroVector()) {
        maxEntries[tid]--;
        threadVecEntries[tid]--;
        DPRINTF(RxuVectorRob, "Delectting mac inst [sn:%lli] to the Vector ROB, VecEntries:%i.\n", head_inst->seqNum,threadVecEntries[tid]);
        head_inst->macroVecHasDecrease = true;
    }

    if (head_inst->isLastMicroop() && head_inst->isMicroVector() && !head_inst->macroVecHasDecrease) {
        --threadVecEntries[tid];
        --maxEntries[tid];
        DPRINTF(RxuVectorRob, "Delectting mic inst [sn:%lli] to the Vector ROB, VecEntries:%i.\n", head_inst->seqNum,threadVecEntries[tid]);
    } else if (!head_inst->isLastMicroop() && head_inst->isMicroVector()) {
        --maxEntries[tid];        
    }

    head_inst->clearInROB();
    head_inst->setCommitted();

    //Update "Global" Head of ROB
    updateHead();

    // @todo: A special case is needed if the instruction being
    // retired is the only instruction in the ROB; otherwise the tail
    // iterator will become invalidated.
    cpu->removeFrontInst(head_inst);
}

bool
ROB::isHeadReady(ThreadID tid)
{
    stats.reads++;
    if (threadEntries[tid] != 0) {
        return instList[tid].front()->readyToCommit();
    }

    return false;
}

bool
ROB::canCommit()
{
    //@todo: set ActiveThreads through ROB or CPU
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (isHeadReady(tid)) {
            return true;
        }
    }

    return false;
}

unsigned
ROB::numFreeEntries()
{
    return numEntries - numInstsInROB;
}

unsigned
ROB::numFreeEntries(ThreadID tid)
{
    return maxEntries[tid] - threadEntries[tid];
}

unsigned 
ROB::numFreeVecEntries(ThreadID tid)
{
    return maxVecEntries[tid] - threadVecEntries[tid];
}

void
ROB::doSquash(ThreadID tid)
{

    stats.writes++;
    DPRINTF(RxuROB, "[tid:%i] Squashing instructions until [sn:%llu].\n",
            tid, squashedSeqNum[tid]);

    assert(squashIt[tid] != instList[tid].end());

    if ((*squashIt[tid])->seqNum < squashedSeqNum[tid]) {
        DPRINTF(RxuROB, "[tid:%i] Done squashing instructions.\n",
                tid);

        squashIt[tid] = instList[tid].end();

        doneSquashing[tid] = true;
        return;
    }

    bool robTailUpdate = false;

    unsigned int numInstsToSquash = squashWidth;

    // If the CPU is exiting, squash all of the instructions
    // it is told to, even if that exceeds the squashWidth.
    // Set the number to the number of entries (the max).
    if (cpu->isThreadExiting(tid))
    {
        numInstsToSquash = numEntries;
    }

    for (int numSquashed = 0;
         numSquashed < numInstsToSquash &&
         squashIt[tid] != instList[tid].end() &&
         (*squashIt[tid])->seqNum > squashedSeqNum[tid];
         ++numSquashed)
    {
        DPRINTF(RxuROB, "[tid:%i] Squashing instruction PC %s, seq num %i.\n",
                (*squashIt[tid])->threadNumber,
                (*squashIt[tid])->pcState(),
                (*squashIt[tid])->seqNum);
        bool is_acq_rel = (*squashIt[tid])->isFullMemBarrier() &&
                        ((*squashIt[tid])->isLoad() ||
                        ((*squashIt[tid])->isStore() &&
                        !(*squashIt[tid])->isStoreConditional()));

            // Remove the instruction from the dependency list.
            if (is_acq_rel ||
                (!(*squashIt[tid])->isNonSpeculative() &&
                !(*squashIt[tid])->isStoreConditional() &&
                !(*squashIt[tid])->isAtomic() &&
                !(*squashIt[tid])->isReadBarrier() &&
                !(*squashIt[tid])->isWriteBarrier() &&
                !(*squashIt[tid])->isMacroVector())) {

            for (int src_reg_idx = 0;
                src_reg_idx < (*squashIt[tid])->numSrcRegs();
                src_reg_idx++){
                    if ((*squashIt[tid])->isMicroVector() && !(*squashIt[tid])->ifrenamesrc){
                        continue;
                    }
                    PhysRegIdPtr src_reg =
                        (*squashIt[tid])->renamedSrcIdx(src_reg_idx);

                    if (!(*squashIt[tid])->readySrcIdx(src_reg_idx) &&
                        !src_reg->isFixedMapping()) {
                        cpu->dispipe3.rmu->dependGraph.remove(src_reg->flatIndex(),
                            (*squashIt[tid]));
                    }
                }
            }

            if (!(*squashIt[tid])->isMacroVector()) {
                for (int dest_reg_idx = 0;
                    dest_reg_idx < (*squashIt[tid])->numDestRegs();
                    dest_reg_idx++)
                {   
                    if ((*squashIt[tid])->isMicroVector() && !(*squashIt[tid])->ifrenamedest) {
                        continue;
                    }
                    PhysRegIdPtr dest_reg =
                        (*squashIt[tid])->renamedDestIdx(dest_reg_idx);
                    if (dest_reg->isFixedMapping()){
                        continue;
                    }
                    while(!cpu->dispipe3.rmu->dependGraph.empty(dest_reg->flatIndex())) {
                            cpu->dispipe3.rmu->dependGraph.remove(dest_reg->flatIndex(),cpu->dispipe3.rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next->inst);
                        // dispipe3Stage->rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next = NULL;
                        }
                    assert(cpu->dispipe3.rmu->dependGraph.empty(dest_reg->flatIndex()));
                    cpu->dispipe3.rmu->dependGraph.clearInst(dest_reg->flatIndex());
                }
            }

        // Mark the instruction as squashed, and ready to commit so that
        // it can drain out of the pipeline.
        (*squashIt[tid])->setSquashed();

        (*squashIt[tid])->setCanCommit();


        if (squashIt[tid] == instList[tid].begin()) {
            DPRINTF(RxuROB, "Reached head of instruction list while "
                    "squashing.\n");

            squashIt[tid] = instList[tid].end();

            doneSquashing[tid] = true;

            return;
        }

        InstIt tail_thread = instList[tid].end();
        tail_thread--;

        if ((*squashIt[tid]) == (*tail_thread))
            robTailUpdate = true;

        squashIt[tid]--;
    }


    // Check if ROB is done squashing.
    if ((*squashIt[tid])->seqNum <= squashedSeqNum[tid]) {
        DPRINTF(RxuROB, "[tid:%i] Done squashing instructions.\n",
                tid);

        squashIt[tid] = instList[tid].end();

        doneSquashing[tid] = true;
    }

    if (robTailUpdate) {
        updateTail();
    }
}


void
ROB::updateHead()
{
    InstSeqNum lowest_num = 0;
    bool first_valid = true;

    // @todo: set ActiveThreads through ROB or CPU
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (instList[tid].empty())
            continue;

        if (first_valid) {
            head = instList[tid].begin();
            lowest_num = (*head)->seqNum;
            first_valid = false;
            continue;
        }

        InstIt head_thread = instList[tid].begin();

        DynInstPtr head_inst = (*head_thread);

        assert(head_inst != 0);

        if (head_inst->seqNum < lowest_num) {
            head = head_thread;
            lowest_num = head_inst->seqNum;
        }
    }

    if (first_valid) {
        head = instList[0].end();
    }

}

void
ROB::updateTail()
{
    tail = instList[0].end();
    bool first_valid = true;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (instList[tid].empty()) {
            continue;
        }

        // If this is the first valid then assign w/out
        // comparison
        if (first_valid) {
            tail = instList[tid].end();
            tail--;
            first_valid = false;
            continue;
        }

        // Assign new tail if this thread's tail is younger
        // than our current "tail high"
        InstIt tail_thread = instList[tid].end();
        tail_thread--;

        if ((*tail_thread)->seqNum > (*tail)->seqNum) {
            tail = tail_thread;
        }
    }
}


void
ROB::squash(InstSeqNum squash_num, ThreadID tid)
{
    if (isEmpty(tid)) {
        DPRINTF(RxuROB, "Does not need to squash due to being empty "
                "[sn:%llu]\n",
                squash_num);

        return;
    }

    DPRINTF(RxuROB, "Starting to squash within the ROB.\n");

    robStatus[tid] = ROBSquashing;

    doneSquashing[tid] = false;

    squashedSeqNum[tid] = squash_num;

    if (!instList[tid].empty()) {
        InstIt tail_thread = instList[tid].end();
        tail_thread--;

        squashIt[tid] = tail_thread;

        doSquash(tid);
    }
}

const DynInstPtr&
ROB::readHeadInst(ThreadID tid)
{
    if (threadEntries[tid] != 0) {
        InstIt head_thread = instList[tid].begin();

        assert((*head_thread)->isInROB());

        return *head_thread;
    } else {
        return dummyInst;
    }
}

DynInstPtr
ROB::readTailInst(ThreadID tid)
{
    InstIt tail_thread = instList[tid].end();
    tail_thread--;

    return *tail_thread;
}

ROB::ROBStats::ROBStats(statistics::Group *parent)
  : statistics::Group(parent, "rob"),
    ADD_STAT(reads, statistics::units::Count::get(),
        "The number of ROB reads"),
    ADD_STAT(writes, statistics::units::Count::get(),
        "The number of ROB writes")
{
}

DynInstPtr
ROB::findInst(ThreadID tid, InstSeqNum squash_inst)
{
    for (InstIt it = instList[tid].begin(); it != instList[tid].end(); it++) {
        if ((*it)->seqNum == squash_inst) {
            return *it;
        }
    }
    return NULL;
}

void 
ROB::insertUopInst(std::list<DynInstPtr> &uopList) 
{    
    DynInstPtr inst = uopList.front();
    int size = uopList.size() - 1;
    ThreadID tid = inst->threadNumber;

    auto it = std::find(instList[tid].begin(), instList[tid].end(), inst->ori_inst);

    if (it != instList[tid].end() && !uopList.empty()) {
        for (std::list<DynInstPtr>::iterator micro_it = uopList.begin(); micro_it != uopList.end(); ++micro_it) {
            (*micro_it)->setInROB();
        }
        
        bool head = false;
        if (it == instList[tid].begin()) head = true; 
        DynInstPtr macro_inst = std::move(*it); 
        it = instList[tid].erase(it);
        if (inst->ori_inst->isMacroVectorMemRef()) {
            inst->ori_inst->macroMemRefIsErased = true;
        }
        instList[tid].insert(it, uopList.begin(), uopList.end());

        DPRINTF(RxuROB,
                "[sn:%llu] Microinsts : 0x%.8x, %s add to rob.\n",
                inst->oriSeqNum,
                inst->pcState().instAddr(),
                inst->staticInst->disassemble(inst->pcState().instAddr()));

        numInstsInROB += size;
        maxEntries[tid] += size;
        threadEntries[tid] += size;

        cpu->commit.changedROBNumEntries[tid] = true;

        instList[tid].sort([] (const DynInstPtr& a, const DynInstPtr& b) {
            return a->seqNum < b->seqNum;
        });
        
        tail = instList[tid].end();
        tail--;

        if (head) updateHead();

        DPRINTF(RxuROB, "insert uop [tid:%i] Now has %d vector instructions.\n", tid,
                threadVecEntries[tid]);  
           
    }  

}   

} // namespace rxuo3
} // namespace gem5

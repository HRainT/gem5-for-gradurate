
#ifndef __CPU_RxuO3_RMU_HH__
#define __CPU_RxuO3_RMU_HH__

#include "cpu/rxuo3/comm.hh"
#include <list>
#include <map>
#include <queue>
#include <vector>
#include "base/compiler.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/reg_class.hh"
#include <string>
//#include "cpu/rxuo3/probe/rxu_simple_trace.hh"
#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuRMU.hh"
#include "debug/RxuScoreboard.hh"
#include "cpu/rxuo3/mem_dep_unit.hh"
// #include "cpu/rxuo3/wtb.hh"

namespace gem5
{

namespace rxuo3
{

class CPU;
// class Dispipe3;
// class WTB;

 template <class DynInstPtr>
    class DependencyEntry
    {
    public:
        DependencyEntry()
            : inst(NULL), next(NULL)
        { }

        DynInstPtr inst;
        //Might want to include data about what arch. register the
        //dependence is waiting on.
        DependencyEntry<DynInstPtr> *next;
    };

 template <class DynInstPtr>
    class DependencyGraph
    {
    public:
        typedef DependencyEntry<DynInstPtr> DepEntry;

        /** Default construction.  Must call resize() prior to use. */
        DependencyGraph()
            : numEntries(0), memAllocCounter(0), nodesTraversed(0), nodesRemoved(0)
        { }

        ~DependencyGraph();

        /** Resize the dependency graph to have num_entries registers. */
        void resize(int num_entries);

        /** Clears all of the linked lists. */
        void reset();

        /** Inserts an instruction to be dependent on the given index. */
        void insert(RegIndex idx, const DynInstPtr &new_inst);

        /** Sets the producing instruction of a given register. */
        void setInst(RegIndex idx, const DynInstPtr &new_inst)
        { dependGraph[idx].inst = new_inst; }

        /** Clears the producing instruction. */
        void clearInst(RegIndex idx);

        void remove_base(const DynInstPtr &new_inst);

        void clear_base(const DynInstPtr &new_inst);

        void clrrem_base(const DynInstPtr &new_inst);

        /** Removes an instruction from a single linked list. */
        void remove(RegIndex idx, const DynInstPtr &inst_to_remove);

        /** Removes and returns the newest dependent of a specific register. */
        DynInstPtr pop(RegIndex idx);

        /** Checks if the entire dependency graph is empty. */
        bool empty() const;

        /** Checks if there are any dependents on a specific register. */
        bool empty(RegIndex idx) const ;

        void print_depedence_info(RegIndex idx);

        void print_dest_info(RegIndex idx);

        /** Debugging function to dump out the dependency graph.*/
        void dump();

        /** Array of linked lists.  Each linked list is a list of all the
         *  instructions that depend upon a given register.  The actual
         *  register's index is used to index into the graph; ie all
         *  instructions in flight that are dependent upon r34 will be
         *  in the linked list of dependGraph[34].
         */
        std::vector<DepEntry> dependGraph;

    private:

        /** Number of linked lists; identical to the number of registers. */
        int numEntries;

        // Debug variable, remove when done testing.
        unsigned memAllocCounter;

    public:
        // Debug variable, remove when done testing.
        uint64_t nodesTraversed;
        // Debug variable, remove when done testing.
        uint64_t nodesRemoved;
    };


class RMU
{
public:

    /** Constructs an RMU. */
    RMU(CPU *cpu_ptr, const BaseRxuO3CPUParams &params);

    /** Destructs the RMU. */
    ~RMU();
/*--------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------*/
    class Scoreboard
    {
    private:
        /** The object name, for DPRINTF.  We have to declare this
         *  explicitly because Scoreboard is not a SimObject. */
        const std::string _name;

        /** Scoreboard of physical integer registers, saying whether or not they
         *  are ready. */
        std::vector<bool> regScoreBoard;

        /** The number of actual physical registers */
        GEM5_CLASS_VAR_USED unsigned numPhysRegs;

    public:
        /** Constructs a scoreboard.
         *  @param _numPhysicalRegs Number of physical registers.
         *  @param _numMiscRegs Number of miscellaneous registers.
         */
        Scoreboard(const std::string &_my_name, unsigned _numPhysicalRegs);

        /** Destructor. */
        ~Scoreboard() {}

        /** Returns the name of the scoreboard. */
        std::string name() const { return _name; }

        /** Checks if the register is ready. */
        bool
        getReg(PhysRegIdPtr phys_reg) const;

        /** Sets the register as ready. */
        void
        setReg(PhysRegIdPtr phys_reg);

        /** Sets the register as not ready. */
        void
        unsetReg(PhysRegIdPtr phys_reg);


    };

     /** Resets all instruction queue state. */
        void sc_resetState();

    //bool instIsReady(const DynInstPtr &inst);
    bool addToDependents(const DynInstPtr &new_inst);

    void addToProducers(const DynInstPtr &new_inst);

    /** Wakes all dependents of a completed instruction. */
    int wakeDependents(const DynInstPtr &completed_inst);

    //void addIfReady(const DynInstPtr &inst);

    // /** Sets pointer to IEW stage. Used only for initialization. */
    // void setIEWStage(IEW *iew_stage) { iew_ptr = iew_stage; }

    std::vector<bool> regScoreboard;

    DependencyGraph<DynInstPtr> dependGraph;

    /** The number of physical registers in the CPU. */
    unsigned numPhysRegs;

    // MemDepUnit memDepUnit[MaxThreads];

    unsigned freeEntries;

    unsigned count[MaxThreads];

private:

    // // IEW *iew_ptr;

    // Dispipe3 *dispipe3Stage;

    // WTB *wtb;

    /** CPU pointer. */
    CPU *cpu;

};

template <class DynInstPtr>
DependencyGraph<DynInstPtr>::~DependencyGraph()
{
}

template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::resize(int num_entries)
{
    numEntries = num_entries;
    dependGraph.resize(numEntries);
}

/** Clears all of the linked lists. */
template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::reset()
{
    // Clear the dependency graph
    DepEntry *curr;
    DepEntry *prev;

    for (int i = 0; i < numEntries; ++i) {
        curr = dependGraph[i].next;

        while (curr) {
            memAllocCounter--;

            prev = curr;
            curr = prev->next;
            prev->inst = NULL;

            delete prev;
        }

        if (dependGraph[i].inst) {
            dependGraph[i].inst = NULL;
        }

        dependGraph[i].next = NULL;
    }
}

/** Inserts an instruction to be dependent on the given index. */
template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::insert(RegIndex idx, const DynInstPtr &new_inst)
{
    //Add this new, dependent instruction at the head of the dependency
    //chain.

    // First create the entry that will be added to the head of the
    // dependency chain.
    DepEntry *new_entry = new DepEntry;
    new_entry->next = dependGraph[idx].next;
    new_entry->inst = new_inst;

    // Then actually add it to the chain.
    dependGraph[idx].next = new_entry;

    ++memAllocCounter;
}

template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::print_depedence_info(RegIndex idx){
    DepEntry *curr = dependGraph[idx].next;
    DepEntry *prev = &dependGraph[idx];
    int count = 0;
    while(curr != NULL)
    {
        count++;
        DPRINTF(RxuRMU, "Depedence Chain %i has inst_seqNum[sn:%llu],NO. %i in the chain. \n",
            idx,curr->inst->seqNum,count );
        prev = curr;
        curr = curr->next;
    }

}


template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::print_dest_info(RegIndex idx){
    DepEntry *curr = dependGraph[idx].next;
    while(dependGraph[idx].inst != NULL)
    {
        DPRINTF(RxuRMU, "Dest Chain %i has inst_seqNum[sn:%llu] \n",
            idx,curr->inst->seqNum);
    }

}


//
/** Clears the producing instruction. */
template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::clearInst(RegIndex idx)
{
    while(dependGraph[idx].inst = NULL)
    { return;
    }
    // DPRINTF(RxuRMU, "curr [sn:%llu] \n",dependGraph[idx].inst->seqNum);
    dependGraph[idx].inst = NULL; 
}

/** Removes an instruction from a single linked list. */
template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::remove(RegIndex idx,
     const DynInstPtr &inst_to_remove)
{

    DepEntry *prev = &dependGraph[idx];
    DepEntry *curr = dependGraph[idx].next;
    
    // Make sure curr isn't NULL.  Because this instruction is being
    // removed from a dependency list, it must have been placed there at
    // an earlier time.  The dependency chain should not be empty,
    // unless the instruction dependent upon it is already ready.
    if (curr == NULL) {
        return;
    }
    DPRINTF(RxuRMU, "curr [sn:%llu] \n",curr->inst->seqNum);
    nodesRemoved++;

    // Find the instruction to remove within the dependency linked list.
    while (curr && curr->inst != inst_to_remove) {
        prev = curr;
        curr = curr->next;
        nodesTraversed++;

     //   assert(curr != NULL);
    }
    if(curr == NULL){
        return;
    }
    // Now remove this instruction from the list.
    prev->next = curr->next;

    --memAllocCounter;
    DPRINTF(RxuRMU, "curr remove [sn:%llu] \n",curr->inst->seqNum);
    // Could push this off to the destructor of DependencyEntry
    curr->inst = NULL;

    delete curr;
}

template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::remove_base(const DynInstPtr &new_inst)
{
    int8_t total_src_regs = new_inst->numSrcRegs();

    for (int src_reg_idx = 0;
         src_reg_idx < total_src_regs;
         src_reg_idx++)
    {
        // Only add it to the dependency graph if it's not ready.
            PhysRegIdPtr src_reg = new_inst->renamedSrcIdx(src_reg_idx);

            // Check the IQ's scoreboard to make sure the register
            // hasn't become ready while the instruction was in flight
            // between stages.  Only if it really isn't ready should
            // it be added to the dependency graph.
            if (src_reg->isFixedMapping()) {
                continue;
        }
        remove(src_reg->flatIndex(), new_inst);
    }
}

template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::clear_base(const DynInstPtr &new_inst)
{
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
        clearInst(dest_reg->flatIndex());
    }
}

template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::clrrem_base(const DynInstPtr &new_inst)
{
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
        remove(dest_reg->flatIndex(), new_inst);
        clearInst(dest_reg->flatIndex());
    }
}


/** Removes and returns the newest dependent of a specific register. */
template <class DynInstPtr>
DynInstPtr
DependencyGraph<DynInstPtr>::pop(RegIndex idx)
{
    DepEntry *node;
    node = dependGraph[idx].next;
    DynInstPtr inst = NULL;
    if (node) {
        inst = node->inst;
        dependGraph[idx].next = node->next;
        node->inst = NULL;
        memAllocCounter--;
        delete node;
    }
    return inst;
}

/** Checks if the entire dependency graph is empty. */
template <class DynInstPtr>
bool
DependencyGraph<DynInstPtr>::empty() const
{
    for (int i = 0; i < numEntries; ++i) {
        if (!empty(i))
            return false;
    }
    return true;
}

/** Checks if there are any dependents on a specific register. */
template <class DynInstPtr>
bool
DependencyGraph<DynInstPtr>::empty(RegIndex idx) const
{ return !dependGraph[idx].next; }

/** Debugging function to dump out the dependency graph.*/
template <class DynInstPtr>
void
DependencyGraph<DynInstPtr>::dump()
{
    DepEntry *curr;

    for (int i = 0; i < numEntries; ++i)
    {
        curr = &dependGraph[i];
        cprintf("tick:%i",curTick());
        if (curr->inst) {
            cprintf("dependGraph[%i]: producer: %s [sn:%lli] consumer: ",
                    i, curr->inst->pcState(), curr->inst->seqNum);
        } else {
            cprintf("dependGraph[%i]: No producer. consumer: ", i);
        }

        while (curr->next != NULL) {
            curr = curr->next;

            cprintf("%s [sn:%lli] ",
                    curr->inst->pcState(), curr->inst->seqNum);
        }

        cprintf("\n");
    }
    cprintf("memAllocCounter: %i\n", memAllocCounter);
}

} // namespace o3
} // namespace gem5


#endif

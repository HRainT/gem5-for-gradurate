#ifndef __CPU_RxuO3_THREAD_CONTEXT_HH__
#define __CPU_RxuO3_THREAD_CONTEXT_HH__

#include "cpu/rxuo3/cpu.hh"
#include "cpu/thread_context.hh"

namespace gem5
{

namespace rxuo3
{

/**
 * Derived ThreadContext class for use with the RxuO3CPU.  It
 * provides the interface for any external objects to access a
 * single thread's state and some general CPU state.  Any time
 * external objects try to update state through this interface,
 * the CPU will create an event to squash all in-flight
 * instructions in order to ensure state is maintained correctly.
 * It must be defined specifically for the RxuO3CPU because
 * not all architectural state is located within the ThreadState
 * (such as the commit PC, and registers), and specific actions
 * must be taken when using this interface (such as squashing all
 * in-flight instructions when doing a write to this interface).
 */
class ThreadContext : public gem5::ThreadContext
{
  public:
   /** Pointer to the CPU. */
    CPU *cpu;

    bool
    schedule(PCEvent *e) override
    {
        return thread->pcEventQueue.schedule(e);
    }
    bool
    remove(PCEvent *e) override
    {
        return thread->pcEventQueue.remove(e);
    }

    void
    scheduleInstCountEvent(Event *event, Tick count) override
    {
        thread->comInstEventQueue.schedule(event, count);
    }
    void
    descheduleInstCountEvent(Event *event) override
    {
        thread->comInstEventQueue.deschedule(event);
    }
    Tick
    getCurrentInstCount() override
    {
        return thread->comInstEventQueue.getCurTick();
    }

    /** Pointer to the thread state that this TC corrseponds to. */
    ThreadState *thread;

    /** Returns a pointer to the MMU. */
    BaseMMU *getMMUPtr() override { return cpu->mmu; }

    CheckerCPU *getCheckerCpuPtr() override { return NULL; }

    BaseISA *
    getIsaPtr() const override
    {
        return cpu->isa[thread->threadId()];
    }

    InstDecoder *
    getDecoderPtr() override
    {
        return cpu->fetch.decoder[thread->threadId()];
    }

    /** Returns a pointer to this CPU. */
    BaseCPU *getCpuPtr() override { return cpu; }

    /** Reads this CPU's ID. */
    int cpuId() const override { return cpu->cpuId(); }

    /** Reads this CPU's Socket ID. */
    uint32_t socketId() const override { return cpu->socketId(); }

    ContextID contextId() const override { return thread->contextId(); }

    void setContextId(ContextID id) override { thread->setContextId(id); }

    /** Returns this thread's ID number. */
    int threadId() const override { return thread->threadId(); }
    void setThreadId(int id) override { return thread->setThreadId(id); }

    /** Returns a pointer to the system. */
    System *getSystemPtr() override { return cpu->system; }

    /** Returns a pointer to this thread's process. */
    Process *getProcessPtr() override { return thread->getProcessPtr(); }

    void setProcessPtr(Process *p) override { thread->setProcessPtr(p); }

    /** Returns this thread's status. */
    Status status() const override { return thread->status(); }

    /** Sets this thread's status. */
    void
    setStatus(Status new_status) override
    {
        thread->setStatus(new_status);
    }

    /** Set the status to Active. */
    void activate() override;

    /** Set the status to Suspended. */
    void suspend() override;

    /** Set the status to Halted. */
    void halt() override;

    /** Takes over execution of a thread from another CPU. */
    void takeOverFrom(gem5::ThreadContext *old_context) override;

    /** Reads the last tick that this thread was activated on. */
    Tick readLastActivate() override;
    /** Reads the last tick that this thread was suspended on. */
    Tick readLastSuspend() override;

    /** Copies the architectural registers from another TC into this TC. */
    void copyArchRegs(gem5::ThreadContext *tc) override;

    /** Resets all architectural registers to 0. */
    void clearArchRegs() override;

    /** Reads this thread's PC state. */
    const PCStateBase &
    pcState() const override
    {
        return cpu->pcState(thread->threadId());
    }

    /** Sets this thread's PC state. */
    void pcState(const PCStateBase &val) override;

    void pcStateNoRecord(const PCStateBase &val) override;

    /** Reads a miscellaneous register. */
    RegVal
    readMiscRegNoEffect(RegIndex misc_reg) const override
    {
        return cpu->readMiscRegNoEffect(misc_reg, thread->threadId());
    }

    /** Reads a misc. register, including any side-effects the
     * read might have as defined by the architecture. */
    RegVal
    readMiscReg(RegIndex misc_reg) override
    {
        return cpu->readMiscReg(misc_reg, thread->threadId());
    }

    /** Sets a misc. register. */
    void setMiscRegNoEffect(RegIndex misc_reg, RegVal val) override;

    /** Sets a misc. register, including any side-effects the
     * write might have as defined by the architecture. */
    void setMiscReg(RegIndex misc_reg, RegVal val) override;

    /** Returns the number of consecutive store conditional failures. */
    // @todo: Figure out where these store cond failures should go.
    unsigned
    readStCondFailures() const override
    {
        return thread->storeCondFailures;
    }

    /** Sets the number of consecutive store conditional failures. */
    void
    setStCondFailures(unsigned sc_failures) override
    {
        thread->storeCondFailures = sc_failures;
    }

    /** check if the cpu is currently in state update mode and squash if not.
     * This function will return true if a trap is pending or if a fault or
     * similar is currently writing to the thread context and doesn't want
     * reset all the state (see noSquashFromTC).
     */
    void
    conditionalSquash()
    {
        if (!thread->trapPending && !thread->noSquashFromTC)
            cpu->squashFromTC(thread->threadId());
    }

    RegVal getReg(const RegId &reg) const override;
    void getReg(const RegId &reg, void *val) const override;
    void *getWritableReg(const RegId &reg) override;

    void setReg(const RegId &reg, RegVal val) override;
    void setReg(const RegId &reg, const void *val) override;

    // hardware transactional memory
    void htmAbortTransaction(uint64_t htm_uid,
                             HtmFailureFaultCause cause) override;
    BaseHTMCheckpointPtr& getHtmCheckpointPtr() override;
    void setHtmCheckpointPtr(BaseHTMCheckpointPtr new_cpt) override;
};

} // namespace rxuo3
} // namespace gem5

#endif

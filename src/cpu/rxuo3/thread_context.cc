#include "cpu/rxuo3/thread_context.hh"

#include "debug/RxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

void
ThreadContext::takeOverFrom(gem5::ThreadContext *old_context)
{
    gem5::takeOverFrom(*this, *old_context);

    getIsaPtr()->takeOverFrom(this, old_context);

    InstDecoder *newDecoder = getDecoderPtr();
    InstDecoder *oldDecoder = old_context->getDecoderPtr();
    newDecoder->takeOverFrom(oldDecoder);

    thread->noSquashFromTC = false;
    thread->trapPending = false;
}

void
ThreadContext::activate()
{
    DPRINTF(RxuO3CPU, "Calling activate on Thread Context %d\n",
            threadId());

    if (thread->status() == gem5::ThreadContext::Active)
        return;

    thread->lastActivate = curTick();
    thread->setStatus(gem5::ThreadContext::Active);

    // status() == Suspended
    cpu->activateContext(thread->threadId());
}

void
ThreadContext::suspend()
{
    DPRINTF(RxuO3CPU, "Calling suspend on Thread Context %d\n",
            threadId());

    if (thread->status() == gem5::ThreadContext::Suspended)
        return;

    if (cpu->isDraining()) {
        DPRINTF(RxuO3CPU, "Ignoring suspend on TC due to pending drain\n");
        return;
    }

    thread->lastActivate = curTick();
    thread->lastSuspend = curTick();

    thread->setStatus(gem5::ThreadContext::Suspended);
    cpu->suspendContext(thread->threadId());
}

void
ThreadContext::halt()
{
    DPRINTF(RxuO3CPU, "Calling halt on Thread Context %d\n", threadId());

    if (thread->status() == gem5::ThreadContext::Halting ||
        thread->status() == gem5::ThreadContext::Halted)
        return;

    // the thread is not going to halt/terminate immediately in this cycle.
    // The thread will be removed after an exit trap is processed
    // (e.g., after trapLatency cycles). Until then, the thread's status
    // will be Halting.
    thread->setStatus(gem5::ThreadContext::Halting);

    // add this thread to the exiting list to mark that it is trying to exit.
    cpu->addThreadToExitingList(thread->threadId());
}

Tick
ThreadContext::readLastActivate()
{
    return thread->lastActivate;
}

Tick
ThreadContext::readLastSuspend()
{
    return thread->lastSuspend;
}

void
ThreadContext::copyArchRegs(gem5::ThreadContext *tc)
{
    // Prevent squashing
    thread->noSquashFromTC = true;
    getIsaPtr()->copyRegsFrom(tc);
    thread->noSquashFromTC = false;
}

void
ThreadContext::clearArchRegs()
{
    cpu->isa[thread->threadId()]->clear();
}

RegVal
ThreadContext::getReg(const RegId &reg) const
{
    return cpu->getArchReg(reg, thread->threadId());
}

void *
ThreadContext::getWritableReg(const RegId &reg)
{
    return cpu->getWritableArchReg(reg, thread->threadId());
}

void
ThreadContext::getReg(const RegId &reg, void *val) const
{
    cpu->getArchReg(reg, val, thread->threadId());
}

void
ThreadContext::setReg(const RegId &reg, RegVal val)
{
    cpu->setArchReg(reg, val, thread->threadId());
    conditionalSquash();
}

void
ThreadContext::setReg(const RegId &reg, const void *val)
{
    cpu->setArchReg(reg, val, thread->threadId());
    conditionalSquash();
}

void
ThreadContext::pcState(const PCStateBase &val)
{
    cpu->pcState(val, thread->threadId());

    conditionalSquash();
}

void
ThreadContext::pcStateNoRecord(const PCStateBase &val)
{
    cpu->pcState(val, thread->threadId());

    conditionalSquash();
}

void
ThreadContext::setMiscRegNoEffect(RegIndex misc_reg, RegVal val)
{
    cpu->setMiscRegNoEffect(misc_reg, val, thread->threadId());

    conditionalSquash();
}

void
ThreadContext::setMiscReg(RegIndex misc_reg, RegVal val)
{
    cpu->setMiscReg(misc_reg, val, thread->threadId());

    conditionalSquash();
}

// hardware transactional memory
void
ThreadContext::htmAbortTransaction(uint64_t htmUid,
        HtmFailureFaultCause cause)
{
    cpu->htmSendAbortSignal(thread->threadId(), htmUid, cause);

    conditionalSquash();
}

BaseHTMCheckpointPtr&
ThreadContext::getHtmCheckpointPtr()
{
    return thread->htmCheckpoint;
}

void
ThreadContext::setHtmCheckpointPtr(BaseHTMCheckpointPtr new_cpt)
{
    thread->htmCheckpoint = std::move(new_cpt);
}

} // namespace rxuo3
} // namespace gem5

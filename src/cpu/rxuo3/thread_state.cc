#include "cpu/rxuo3/thread_state.hh"

#include "cpu/rxuo3/cpu.hh"

namespace gem5
{

namespace rxuo3
{

ThreadState::ThreadState(CPU *_cpu, int _thread_num, Process *_process) :
    gem5::ThreadState(_cpu, _thread_num, _process),
    comInstEventQueue("instruction-based event queue")
{}

void
ThreadState::serialize(CheckpointOut &cp) const
{
    gem5::ThreadState::serialize(cp);
    // Use the ThreadContext serialization helper to serialize the
    // TC.
    gem5::serialize(*tc, cp);
}

void
ThreadState::unserialize(CheckpointIn &cp)
{
    // Prevent squashing - we don't have any instructions in
    // flight that we need to squash since we just instantiated a
    // clean system.
    noSquashFromTC = true;
    gem5::ThreadState::unserialize(cp);
    // Use the ThreadContext serialization helper to unserialize
    // the TC.
    gem5::unserialize(*tc, cp);
    noSquashFromTC = false;
}

} // namespace rxuo3
} // namespace gem5

#ifndef __CPU_RxuO3_THREAD_STATE_HH__
#define __CPU_RxuO3_THREAD_STATE_HH__

#include <memory>

#include "cpu/thread_context.hh"
#include "cpu/thread_state.hh"

namespace gem5
{

class Process;

namespace rxuo3
{

class CPU;

/**
 * Class that has various thread state, such as the status, the
 * current instruction being processed, whether or not the thread has
 * a trap pending or is being externally updated, the ThreadContext
 * pointer, etc.  It also handles anything related to a specific
 * thread's process, such as syscalls and checking valid addresses.
 */
class ThreadState : public gem5::ThreadState
{
  public:
    PCEventQueue pcEventQueue;
    /**
     * An instruction-based event queue. Used for scheduling events based on
     * number of instructions committed.
     */
    EventQueue comInstEventQueue;

    /* This variable controls if writes to a thread context should cause a all
     * dynamic/speculative state to be thrown away. Nominally this is the
     * desired behavior because the external thread context write has updated
     * some state that could be used by an inflight instruction, however there
     * are some cases like in a fault/trap handler where this behavior would
     * lead to successive restarts and forward progress couldn't be made. This
     * variable controls if the squashing will occur.
     */
    bool noSquashFromTC = false;

    /** Whether or not the thread is currently waiting on a trap, and
     * thus able to be externally updated without squashing.
     */
    bool trapPending = false;

    /** Pointer to the hardware transactional memory checkpoint. */
    std::unique_ptr<BaseHTMCheckpoint> htmCheckpoint;

    ThreadState(CPU *_cpu, int _thread_num, Process *_process);

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

    /** Pointer to the ThreadContext of this thread. */
    gem5::ThreadContext *tc = nullptr;

    /** Returns a pointer to the TC of this thread. */
    gem5::ThreadContext *getTC() { return tc; }
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_THREAD_STATE_HH__

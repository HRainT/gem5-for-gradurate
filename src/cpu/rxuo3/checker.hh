#ifndef __CPU_RxuO3_CHECKER_HH__
#define __CPU_RxuO3_CHECKER_HH__

#include "cpu/checker/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"

namespace gem5
{

namespace rxuo3
{

/**
 * Specific non-templated derived class used for SimObject configuration.
 */
class Checker : public gem5::Checker<DynInstPtr>
{
  public:
    Checker(const Params &p) : gem5::Checker<DynInstPtr>(p)
    {
        // The checker should check all instructions executed by the main
        // cpu and therefore any parameters for early exit don't make much
        // sense.
        fatal_if(p.max_insts_any_thread || p.max_insts_all_threads ||
                 p.progress_interval, "Invalid checker parameters");
    }
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_CHECKER_HH__

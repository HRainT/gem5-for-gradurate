#include "cpu/rxuo3/probe/rxu_simple_trace.hh"

#include "base/trace.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "debug/RxuSimpleTrace.hh"

namespace gem5
{

namespace rxuo3
{

void
RxuSimpleTrace::traceCommit(const DynInstConstPtr& dynInst)
{
    DPRINTFR(RxuSimpleTrace, "[%s]: Commit 0x%08x %s.\n", name(),
             dynInst->pcState().instAddr(),
             dynInst->staticInst->disassemble(dynInst->pcState().instAddr()));
}

void
RxuSimpleTrace::traceFetch(const DynInstConstPtr& dynInst)
{
    DPRINTFR(RxuSimpleTrace, "[%s]: Fetch 0x%08x %s.\n", name(),
             dynInst->pcState().instAddr(),
             dynInst->staticInst->disassemble(dynInst->pcState().instAddr()));
}

void
RxuSimpleTrace::regProbeListeners()
{
    typedef ProbeListenerArg<RxuSimpleTrace,
            DynInstConstPtr> DynInstListener;
    listeners.push_back(new DynInstListener(this, "Commit",
                &RxuSimpleTrace::traceCommit));
    listeners.push_back(new DynInstListener(this, "Fetch",
                &RxuSimpleTrace::traceFetch));
}

} // namespace rxuo3
} // namespace gem5

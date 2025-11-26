/**
 * @file This file initializes a simple trace unit which listens to
 * a probe point in the fetch and commit stage of the O3 pipeline
 * and simply outputs those events as a dissassembled instruction stream
 * to the trace output.
 */
#ifndef __CPU_RxuO3_PROBE_SIMPLE_TRACE_HH__
#define __CPU_RxuO3_PROBE_SIMPLE_TRACE_HH__

#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "params/RxuSimpleTrace.hh"
#include "sim/probe/probe.hh"

namespace gem5
{

namespace rxuo3
{

class RxuSimpleTrace : public ProbeListenerObject
{

  public:
    RxuSimpleTrace(const RxuSimpleTraceParams &params) :
        ProbeListenerObject(params)
    {
    }

    /** Register the probe listeners. */
    void regProbeListeners() override;

    std::string
    name() const override
    {
        return ProbeListenerObject::name() + ".trace";
    }

  private:
    void traceFetch(const DynInstConstPtr& dynInst);
    void traceCommit(const DynInstConstPtr& dynInst);

};

} // namespace rxuo3
} // namespace gem5

#endif//__CPU_RxuO3_PROBE_SIMPLE_TRACE_HH__

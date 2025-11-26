#ifndef __CPU_RxuO3_DYN_INST_PTR_HH__
#define __CPU_RxuO3_DYN_INST_PTR_HH__

#include "base/refcnt.hh"

namespace gem5
{

namespace rxuo3
{

class DynInst;

using DynInstPtr = RefCountingPtr<DynInst>;
using DynInstConstPtr = RefCountingPtr<const DynInst>;

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_DYN_INST_PTR_HH__

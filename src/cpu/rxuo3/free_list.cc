#include "cpu/rxuo3/free_list.hh"

#include "base/trace.hh"
#include "debug/RxuFreeList.hh"

namespace gem5
{

namespace rxuo3
{

UnifiedFreeList::UnifiedFreeList(const std::string &_my_name,
                                 PhysRegFile *_regFile)
    : _name(_my_name), regFile(_regFile)
{
    DPRINTF(RxuFreeList, "Creating new free list object.\n");

    // Have the register file initialize the free list since it knows
    // about its internal organization
    regFile->initFreeList(this);
}

} // namespace rxuo3
} // namespace gem5

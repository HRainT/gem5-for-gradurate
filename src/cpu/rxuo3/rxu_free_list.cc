// ----------- added by longting.du --------------//

#include "cpu/rxuo3/rxu_free_list.hh"

#include "base/trace.hh"
#include "debug/FreeList.hh"

namespace gem5
{

namespace rxuo3

{

RxuUnifiedFreeList::RxuUnifiedFreeList(const std::string &_my_name,
                                 PhysRegFile *_regFile)
    : _name(_my_name), regFile(_regFile)
{
    DPRINTF(FreeList, "Creating new free list object.\n");

    freeLists[IntRegClass]._type = IntRegClass;
    freeLists[FloatRegClass]._type = FloatRegClass;
    freeLists[VecRegClass]._type = VecRegClass;
    freeLists[VecElemClass]._type = VecElemClass;
    freeLists[VecPredRegClass]._type = VecPredRegClass;
    freeLists[MatRegClass]._type = MatRegClass;
    freeLists[CCRegClass]._type = CCRegClass;

    freeLists[IntRegClass].setNumRegs(_regFile->numPhysicalIntRegs);
    freeLists[IntRegClass].resetVector();
    freeLists[FloatRegClass].setNumRegs(_regFile->numPhysicalFloatRegs);
    freeLists[FloatRegClass].resetVector();
    freeLists[VecRegClass].setNumRegs(256);
    freeLists[VecRegClass].resetVector();
    freeLists[VecPredRegClass].setNumRegs(32);
    freeLists[MatRegClass].setNumRegs(2);
    freeLists[VecElemClass].setNumRegs(_regFile->numPhysicalVecElemRegs);
    freeLists[CCRegClass].setNumRegs(_regFile->numPhysicalCCRegs);
    
    // Have the register file initialize the free list since it knows
    // about its internal organization
    regFile->initFreeList(this);

}

} // namespace o3
} // namespace gem5

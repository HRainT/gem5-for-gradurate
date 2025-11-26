#include "cpu/rxuo3/regfile.hh"

#include "cpu/rxuo3/rxu_free_list.hh"
#include "cpu/rxuo3/free_list.hh"

namespace gem5
{

namespace rxuo3
{

PhysRegFile::PhysRegFile(unsigned _numPhysicalIntRegs,
                         unsigned _numPhysicalFloatRegs,
                         unsigned _numPhysicalVecRegs,
                         unsigned _numPhysicalVecPredRegs,
                         unsigned _numPhysicalMatRegs,
                         unsigned _numPhysicalCCRegs,
                         const BaseISA::RegClasses &reg_classes)
    : intRegFile(*reg_classes.at(IntRegClass), _numPhysicalIntRegs),
      floatRegFile(*reg_classes.at(FloatRegClass), _numPhysicalFloatRegs),
      vectorRegFile(*reg_classes.at(VecRegClass), _numPhysicalVecRegs),
      vectorElemRegFile(*reg_classes.at(VecElemClass), _numPhysicalVecRegs * (
                  reg_classes.at(VecElemClass)->numRegs() /
                  reg_classes.at(VecRegClass)->numRegs())),
      vecPredRegFile(*reg_classes.at(VecPredRegClass),
              _numPhysicalVecPredRegs),
      matRegFile(*reg_classes.at(MatRegClass), _numPhysicalMatRegs),
      ccRegFile(*reg_classes.at(CCRegClass), _numPhysicalCCRegs),
      numPhysicalIntRegs(_numPhysicalIntRegs),
      numPhysicalFloatRegs(_numPhysicalFloatRegs),
      numPhysicalVecRegs(_numPhysicalVecRegs),
      numPhysicalVecElemRegs(_numPhysicalVecRegs * (
                  reg_classes.at(VecElemClass)->numRegs() /
                  reg_classes.at(VecRegClass)->numRegs())),
      numPhysicalVecPredRegs(_numPhysicalVecPredRegs),
      numPhysicalMatRegs(_numPhysicalMatRegs),
      numPhysicalCCRegs(_numPhysicalCCRegs),
      totalNumRegs(_numPhysicalIntRegs
                   + _numPhysicalFloatRegs
                   + _numPhysicalVecRegs
                   + numPhysicalVecElemRegs
                   + _numPhysicalVecPredRegs
                   + _numPhysicalMatRegs
                   + _numPhysicalCCRegs)
{
    RegIndex phys_reg;
    RegIndex flat_reg_idx = 0;

    // The initial batch of registers are the integer ones
    for (phys_reg = 0; phys_reg < numPhysicalIntRegs; phys_reg++) {
        intRegIds.emplace_back(*reg_classes.at(IntRegClass),
                phys_reg, flat_reg_idx++);
    }

    // The next batch of the registers are the floating-point physical
    // registers; put them onto the floating-point free list.
    for (phys_reg = 0; phys_reg < numPhysicalFloatRegs; phys_reg++) {
        floatRegIds.emplace_back(*reg_classes.at(FloatRegClass),
                phys_reg, flat_reg_idx++);
    }

    // The next batch of the registers are the vector physical
    // registers; put them onto the vector free list.
    for (phys_reg = 0; phys_reg < numPhysicalVecRegs; phys_reg++) {
        vecRegIds.emplace_back(*reg_classes.at(VecRegClass), phys_reg,
                flat_reg_idx++);
    }
    // The next batch of the registers are the vector element physical
    // registers; put them onto the vector free list.
    for (phys_reg = 0; phys_reg < numPhysicalVecElemRegs; phys_reg++) {
        vecElemIds.emplace_back(*reg_classes.at(VecElemClass), phys_reg,
                flat_reg_idx++);
    }

    // The next batch of the registers are the predicate physical
    // registers; put them onto the predicate free list.
    for (phys_reg = 0; phys_reg < numPhysicalVecPredRegs; phys_reg++) {
        vecPredRegIds.emplace_back(*reg_classes.at(VecPredRegClass), phys_reg,
                flat_reg_idx++);
    }

    // The next batch of the registers are the matrix physical
    // registers; put them onto the matrix free list.
    for (phys_reg = 0; phys_reg < numPhysicalMatRegs; phys_reg++) {
        matRegIds.emplace_back(*reg_classes.at(MatRegClass), phys_reg,
                flat_reg_idx++);
    }

    // The rest of the registers are the condition-code physical
    // registers; put them onto the condition-code free list.
    for (phys_reg = 0; phys_reg < numPhysicalCCRegs; phys_reg++) {
        ccRegIds.emplace_back(*reg_classes.at(CCRegClass), phys_reg,
                flat_reg_idx++);
    }

    // Misc regs have a fixed mapping but still need PhysRegIds.
    for (phys_reg = 0; phys_reg < reg_classes.at(MiscRegClass)->numRegs();
            phys_reg++) {
        miscRegIds.emplace_back(*reg_classes.at(MiscRegClass), phys_reg, 0);
    }
}


void
PhysRegFile::initFreeList(RxuUnifiedFreeList *freeList)
{
    // Initialize the free lists.
    int reg_idx = 0;

    // The initial batch of registers are the integer ones
    for (reg_idx = 0; reg_idx < numPhysicalIntRegs; reg_idx++) {
        assert(intRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(intRegIds.begin(), intRegIds.end());

    // The next batch of the registers are the floating-point physical
    // registers; put them onto the floating-point free list.
    for (reg_idx = 0; reg_idx < numPhysicalFloatRegs; reg_idx++) {
        assert(floatRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(floatRegIds.begin(), floatRegIds.end());

    /* The next batch of the registers are the vector physical
     * registers; put them onto the vector free list. */
    for (reg_idx = 0; reg_idx < numPhysicalVecRegs; reg_idx++) {
        assert(vecRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(vecRegIds.begin(), vecRegIds.end());
    for (reg_idx = 0; reg_idx < numPhysicalVecElemRegs; reg_idx++) {
        assert(vecElemIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(vecElemIds.begin(), vecElemIds.end());

    // The next batch of the registers are the predicate physical
    // registers; put them onto the predicate free list.
    for (reg_idx = 0; reg_idx < numPhysicalVecPredRegs; reg_idx++) {
        assert(vecPredRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(vecPredRegIds.begin(), vecPredRegIds.end());

    /* The next batch of the registers are the matrix physical
     * registers; put them onto the matrix free list. */
    for (reg_idx = 0; reg_idx < numPhysicalMatRegs; reg_idx++) {
        assert(matRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(matRegIds.begin(), matRegIds.end());

    // The rest of the registers are the condition-code physical
    // registers; put them onto the condition-code free list.
    for (reg_idx = 0; reg_idx < numPhysicalCCRegs; reg_idx++) {
        assert(ccRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(ccRegIds.begin(), ccRegIds.end());
}

void
PhysRegFile::initFreeList(UnifiedFreeList *freeList)
{
    // Initialize the free lists.
    int reg_idx = 0;

    // The initial batch of registers are the integer ones
    for (reg_idx = 0; reg_idx < numPhysicalIntRegs; reg_idx++) {
        assert(intRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(intRegIds.begin(), intRegIds.end());

    // The next batch of the registers are the floating-point physical
    // registers; put them onto the floating-point free list.
    for (reg_idx = 0; reg_idx < numPhysicalFloatRegs; reg_idx++) {
        assert(floatRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(floatRegIds.begin(), floatRegIds.end());

    /* The next batch of the registers are the vector physical
     * registers; put them onto the vector free list. */
    for (reg_idx = 0; reg_idx < numPhysicalVecRegs; reg_idx++) {
        assert(vecRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(vecRegIds.begin(), vecRegIds.end());
    for (reg_idx = 0; reg_idx < numPhysicalVecElemRegs; reg_idx++) {
        assert(vecElemIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(vecElemIds.begin(), vecElemIds.end());

    // The next batch of the registers are the predicate physical
    // registers; put them onto the predicate free list.
    for (reg_idx = 0; reg_idx < numPhysicalVecPredRegs; reg_idx++) {
        assert(vecPredRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(vecPredRegIds.begin(), vecPredRegIds.end());

    /* The next batch of the registers are the matrix physical
     * registers; put them onto the matrix free list. */
    for (reg_idx = 0; reg_idx < numPhysicalMatRegs; reg_idx++) {
        assert(matRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(matRegIds.begin(), matRegIds.end());

    // The rest of the registers are the condition-code physical
    // registers; put them onto the condition-code free list.
    for (reg_idx = 0; reg_idx < numPhysicalCCRegs; reg_idx++) {
        assert(ccRegIds[reg_idx].index() == reg_idx);
    }
    freeList->addRegs(ccRegIds.begin(), ccRegIds.end());
}

} // namespace rxuo3
} // namespace gem5

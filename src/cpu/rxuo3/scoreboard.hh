#ifndef __CPU_RxuO3_SCOREBOARD_HH__
#define __CPU_RxuO3_SCOREBOARD_HH__

#include <cassert>
#include <vector>

#include "base/compiler.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/reg_class.hh"
#include "debug/RxuScoreboard.hh"

namespace gem5
{

namespace rxuo3
{

/**
 * Implements a simple scoreboard to track which registers are
 * ready. This class operates on the unified physical register space,
 * because the different classes of registers do not need to be distinguished.
 * Registers being part of a fixed mapping are always considered ready.
 */
class Scoreboard
{
  private:
    /** The object name, for DPRINTF.  We have to declare this
     *  explicitly because Scoreboard is not a SimObject. */
    const std::string _name;

    /** Scoreboard of physical integer registers, saying whether or not they
     *  are ready. */
    std::vector<bool> regScoreBoard;

    /** The number of actual physical registers */
    GEM5_CLASS_VAR_USED unsigned numPhysRegs;

  public:
    /** Constructs a scoreboard.
     *  @param _numPhysicalRegs Number of physical registers.
     *  @param _numMiscRegs Number of miscellaneous registers.
     */
    Scoreboard(const std::string &_my_name, unsigned _numPhysicalRegs);

    /** Destructor. */
    ~Scoreboard() {}

    /** Returns the name of the scoreboard. */
    std::string name() const { return _name; };

    /** Checks if the register is ready. */
    bool
    getReg(PhysRegIdPtr phys_reg) const
    {
        if (phys_reg->isFixedMapping()) {
            // Fixed mapping regs are always ready
            return true;
        }

        assert(phys_reg->flatIndex() < numPhysRegs);

        return regScoreBoard[phys_reg->flatIndex()];
    }

    /** Sets the register as ready. */
    void
    setReg(PhysRegIdPtr phys_reg)
    {
        if (phys_reg->isFixedMapping()) {
            // Fixed mapping regs are always ready, ignore attempts to change
            // that
            return;
        }

        assert(phys_reg->flatIndex() < numPhysRegs);

        DPRINTF(RxuScoreboard, "Setting reg %i (%s) as ready\n",
                phys_reg->index(), phys_reg->className());

        regScoreBoard[phys_reg->flatIndex()] = true;
    }

    /** Sets the register as not ready. */
    void
    unsetReg(PhysRegIdPtr phys_reg)
    {
        if (phys_reg->isFixedMapping()) {
            // Fixed mapping regs are always ready, ignore attempts to
            // change that
            return;
        }

        assert(phys_reg->flatIndex() < numPhysRegs);

        regScoreBoard[phys_reg->flatIndex()] = false;
    }

};

} // namespace rxuo3
} // namespace gem5

#endif

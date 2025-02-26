#include "cpu/rxuo3/scoreboard.hh"

namespace gem5
{

namespace rxuo3
{

Scoreboard::Scoreboard(const std::string &_my_name,
        unsigned _numPhysicalRegs) :
    _name(_my_name), regScoreBoard(_numPhysicalRegs, true),
    numPhysRegs(_numPhysicalRegs)
{}

} // namespace rxuo3
} // namespace gem5

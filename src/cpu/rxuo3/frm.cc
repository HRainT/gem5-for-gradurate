#include "cpu/rxuo3/frm.hh"

#include "cpu/rxuo3/dyn_inst.hh"
#include "debug/RxuRename.hh"

namespace gem5 
{

namespace rxuo3
{

RxuFRMRename::RxuFRMRename() 
{
    relCnt = 0;
    recCnt = 0;
    // ready = std::vector<bool>(16, false);
    // value = std::vector<uint64_t>(16, 0);
    memset(ready, false, sizeof(ready));
    memset(value, 0, sizeof(value));
    memset(producer, false, sizeof(producer));

    ringBuffer.push_back(0);
    ready[0] = true;

}

unsigned
RxuFRMRename::get()
{
    unsigned rn_index = frm.pop();
    ringBuffer.push_back(rn_index);
    ready[rn_index] = false;
    addProducer(rn_index);
    return rn_index;
}

bool
RxuFRMRename::stall()
{   
    if (frm.stall()) {
        DPRINTF(RxuRename, "rename should stall due to lack of frm physical regs.\n");
        return true;
    }
    if (recCnt > 0) {
        DPRINTF(RxuRename, "rename should stall due to frm recover is doing.\n");
        return true;
    }
    return false;
}

void
RxuFRMRename::recover() 
{
    if (recCnt > 0) {
        if (ringBuffer.empty()) return;
        unsigned rec = ringBuffer.back();
        ringBuffer.pop_back();
        frm.push(rec);

        DPRINTF(RxuRename,"recover [frm] physical reg %i.\n", rec);

        recCnt--;
    }
}

void 
RxuFRMRename::release()
{
    if (relCnt > 0) {
        if (ringBuffer.empty()) return;
        unsigned rel = ringBuffer.front();
        ringBuffer.pop_front();
        frm.push(rel);

        DPRINTF(RxuRename,"release [frm] physical reg %i.\n", rel);

        relCnt--;
    }
}

void
RxuFRMRename::setRecCnt()
{
    recCnt++;
}

void
RxuFRMRename::setRelCnt() 
{
    relCnt++;
}

void
RxuFRMRename::setReady(unsigned frm_i)
{
    ready[frm_i] = true;
}

void
RxuFRMRename::unsetReady(unsigned frm_i)
{
    ready[frm_i] = false;
}

bool
RxuFRMRename::isReady(unsigned frm_i) 
{
    return ready[frm_i];
}

unsigned
RxuFRMRename::curPhysFRM() 
{
    return ringBuffer.size() > 0 ? ringBuffer.back() : 0;
}

uint64_t
RxuFRMRename::getVal(unsigned frm_i) 
{
    return value[frm_i];
}

void 
RxuFRMRename::setVal(unsigned frm_i, uint64_t val)
{
    value[frm_i] = val;
}

void
RxuFRMRename::wakeDependences(unsigned phys_reg) 
{
    assert(producer[phys_reg]);
    Consumers::iterator iter = dependGraph[phys_reg].begin();
    for (; iter != dependGraph[phys_reg].end(); ++iter) {
        (*iter)->frm_ready = true;
        if ((*iter)->isInWTB())
            wtb->addIfReady(*iter);
    }
    removeDependence(phys_reg);
}

void 
RxuFRMRename::removeDependence(unsigned phys_reg)
{
    dependGraph[phys_reg].clear();
    producer[phys_reg] = false;
}

void
RxuFRMRename::addConsumer(unsigned phys_reg, const DynInstPtr &inst)
{
    if (!producer[phys_reg])   return;
    dependGraph[phys_reg].push_back(inst);
}

void 
RxuFRMRename::addProducer(unsigned phys_reg)
{
    dependGraph[phys_reg].clear();
    producer[phys_reg] = true;
}

} // rxuo3

} // gem5
#include "cpu/rxuo3/veccsr.hh"

#include "cpu/rxuo3/dyn_inst.hh"
#include "debug/RxuRename.hh"

namespace gem5 
{

namespace rxuo3
{

RxuSimpleVecRename::RxuSimpleVecRename() 
{
    relCnt = 0;
    recCnt = 0;
    memset(ready, false, sizeof(ready));
    memset(value, 0, sizeof(value));
    memset(producer, false, sizeof(producer));

    ringBuffer.push_back(0);
    ready[0] = true;

}

void 
RxuSimpleVecRename::reset(RegVal miscregval)
{
    value[0] = miscregval;
}

unsigned
RxuSimpleVecRename::get()
{
    unsigned rn_index = renameVec.pop();
    ringBuffer.push_back(rn_index);
    ready[rn_index] = false;
    addProducer(rn_index);
    return rn_index;
}

std::vector<bool>
RxuSimpleVecRename::stall()
{   
    std::vector<bool> stall(2, false);
    if (renameVec.stall()) {
        DPRINTF(RxuRename, "rename should stall due to lack of %s physical regs.\n", transferString(_type));
        stall[0] = true;
    }
    if (recCnt > 0) {
        DPRINTF(RxuRename, "rename should stall due to %s recover is doing.\n", transferString(_type));
        stall[1] = true;
    }
    return stall;
}

void
RxuSimpleVecRename::recover() 
{   
    unsigned recNum = 0;
    while (recCnt > 0 && recNum < 8) {
        if (ringBuffer.empty()) return;
        unsigned rec = ringBuffer.back();
        ringBuffer.pop_back();
        renameVec.push(rec);

        DPRINTF(RxuRename,"recover [%s] physical reg %i.\n", transferString(_type), rec);

        recCnt--;
        recNum++;
    }
}

void 
RxuSimpleVecRename::release()
{   
    unsigned relNum = 0;
    while (relCnt > 0 && relNum < 8) {
        if (ringBuffer.empty()) return;
        unsigned rel = ringBuffer.front();
        ringBuffer.pop_front();
        renameVec.push(rel);

        DPRINTF(RxuRename,"release [%s] physical reg %i.\n", transferString(_type), rel);

        relCnt--;
        relNum++;
    }
}

void
RxuSimpleVecRename::setRecCnt()
{
    recCnt++;
}

void
RxuSimpleVecRename::setRelCnt() 
{
    relCnt++;
}

void
RxuSimpleVecRename::setReady(unsigned vec_csr_i)
{
    ready[vec_csr_i] = true;
}

void
RxuSimpleVecRename::unsetReady(unsigned vec_csr_i)
{
    ready[vec_csr_i] = false;
}

bool
RxuSimpleVecRename::isReady(unsigned vec_csr_i) 
{
    return ready[vec_csr_i];
}

unsigned
RxuSimpleVecRename::curPhysVecCsr() 
{
    return ringBuffer.size() > 0 ? ringBuffer.back() : 0;
}

uint64_t
RxuSimpleVecRename::getVal(unsigned vec_csr_i) 
{
    return value[vec_csr_i];
}

void 
RxuSimpleVecRename::setVal(unsigned vec_csr_i, uint64_t val)
{
    value[vec_csr_i] = val;
}

void
RxuSimpleVecRename::wakeDependences(unsigned phys_reg) 
{
    assert(producer[phys_reg]);
    Consumers::iterator iter = dependGraph[phys_reg].begin();
    for (; iter != dependGraph[phys_reg].end(); ++iter) {
        if (_type == Vl) {
            (*iter)->vl_ready = true;                
        } else if (_type == Vtype) {
            (*iter)->vtype_ready = true;
        } else {
            (*iter)->vxrm_ready = true;
        } 
        if ((*iter)->isInWTB())
            wtb->addIfReady(*iter);
    }
    removeDependence(phys_reg);
}

void 
RxuSimpleVecRename::removeDependence(unsigned phys_reg)
{
    dependGraph[phys_reg].clear();
    producer[phys_reg] = false;
}

void
RxuSimpleVecRename::addConsumer(unsigned phys_reg, const DynInstPtr &inst)
{
    if (!producer[phys_reg])   return;
    dependGraph[phys_reg].push_back(inst);
}

void 
RxuSimpleVecRename::addProducer(unsigned phys_reg)
{
    dependGraph[phys_reg].clear();
    producer[phys_reg] = true;
}

void 
RxuSimpleVecRename::setWTB(WTB *wtb_ptr) { wtb = wtb_ptr; }

std::string 
RxuSimpleVecRename::transferString(VecCsrType _type) {
    switch (_type) {
        case Vxrm: return "Vxrm";
        case Vl: return "Vl";
        case Vtype: return "Vtype";
        case VecCsrClass: return "VecCsrClass";
        default: return "Unknown Type";
    }
}

RxuUnifiedVecRename::RxuUnifiedVecRename() 
{
    vecCsrRename[Vxrm]._type = Vxrm;
    vecCsrRename[Vl]._type = Vl;
    vecCsrRename[Vtype]._type = Vtype;
}

void 
RxuUnifiedVecRename::reset(VecCsrType type, RegVal miscregval)
{   
    vecCsrRename[type].reset(miscregval);
}

unsigned
RxuUnifiedVecRename::get(VecCsrType type)
{
    return vecCsrRename[type].get();
}

std::vector<bool>
RxuUnifiedVecRename::stall()
{   
    std::vector<bool> vxrmStall = vecCsrRename[Vxrm].stall();
    std::vector<bool> vlStall = vecCsrRename[Vl].stall();
    std::vector<bool> vtypeStall = vecCsrRename[Vtype].stall();
    for (int i = 0; i < 2; ++i) {
        vxrmStall[i] = vxrmStall[i] | vlStall[i] | vtypeStall[i];
    }

    return vxrmStall;
}

void
RxuUnifiedVecRename::recover() 
{
    vecCsrRename[Vxrm].recover();
    vecCsrRename[Vl].recover();
    vecCsrRename[Vtype].recover();

}

void 
RxuUnifiedVecRename::release()
{
    vecCsrRename[Vxrm].release();
    vecCsrRename[Vl].release();
    vecCsrRename[Vtype].release();
}

void
RxuUnifiedVecRename::setRecCnt(VecCsrType type)
{
    vecCsrRename[type].setRecCnt();
}

void
RxuUnifiedVecRename::setRelCnt(VecCsrType type) 
{
    vecCsrRename[type].setRelCnt();
}

void
RxuUnifiedVecRename::setReady(VecCsrType type, unsigned vec_csr_i)
{
    vecCsrRename[type].setReady(vec_csr_i);
}

void
RxuUnifiedVecRename::unsetReady(VecCsrType type, unsigned vec_csr_i)
{
    vecCsrRename[type].unsetReady(vec_csr_i);
}

bool
RxuUnifiedVecRename::isReady(VecCsrType type, unsigned vec_csr_i) 
{
    return vecCsrRename[type].isReady(vec_csr_i);
}

unsigned
RxuUnifiedVecRename::curPhysVecCsr(VecCsrType type) 
{
    return vecCsrRename[type].curPhysVecCsr();
}

uint64_t
RxuUnifiedVecRename::getVal(VecCsrType type, unsigned vec_csr_i) 
{
    return vecCsrRename[type].getVal(vec_csr_i);
}

void 
RxuUnifiedVecRename::setVal(VecCsrType type, unsigned vec_csr_i, uint64_t val)
{
    vecCsrRename[type].setVal(vec_csr_i, val);
}

void
RxuUnifiedVecRename::wakeDependences(VecCsrType type, unsigned phys_reg) 
{
    vecCsrRename[type].wakeDependences(phys_reg);
}

void 
RxuUnifiedVecRename::removeDependence(VecCsrType type, unsigned phys_reg)
{
    vecCsrRename[type].removeDependence(phys_reg);        
}

void
RxuUnifiedVecRename::addConsumer(VecCsrType type, unsigned phys_reg, const DynInstPtr &inst)
{
    vecCsrRename[type].addConsumer(phys_reg, inst);
}

void 
RxuUnifiedVecRename::addProducer(VecCsrType type, unsigned phys_reg)
{
    vecCsrRename[type].addProducer(phys_reg);
}

} // rxuo3

} // gem5
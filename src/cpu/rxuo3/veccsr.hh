#ifndef __RXU_VECCSR_RENAME_HH__
#define __RXU_VECCSR_RENAME_HH__

#include <list>

#include "cpu/rxuo3/wtb.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "base/logging.hh"
// #include "base/trace.hh"
#include "debug/RxuRename.hh"

namespace gem5 
{

namespace rxuo3 
{

enum VecCsrType
{
    Vxrm,
    Vl,
    Vtype,
    VecCsrClass

};


class VecRenameVector
{

public:
    VecRenameVector() {
        memset(vld, true, sizeof(vld));
        cnt = 63;
        get_index = 1;
        vld[0] = false;
    }

    void push(unsigned index) {
        if (vld[index]) return;
        cnt++;
        vld[index] = true;
    }

    unsigned pop() {
        // assert(cnt > 0);
        uint32_t res = 0;
        for (; get_index < VEC_LEN; get_index = (get_index + 1) % VEC_LEN) {
            if (vld[get_index]) {
                cnt--;
                vld[get_index] = false;
                res = get_index;
                get_index = (get_index + 1) % VEC_LEN;
                break;
            }
        }
        return res;
    }

    bool stall() { return cnt < 4; }

private:
    const unsigned VEC_LEN = 64;
    unsigned cnt;
    bool vld[64];
    uint32_t get_index;

};

class RxuSimpleVecRename 
{

public:

    WTB *wtb;
    VecRenameVector renameVec;
    unsigned relCnt;
    unsigned recCnt;
    std::list<unsigned> ringBuffer;
    bool        ready[64];
    uint64_t    value[64];

    typedef std::list<DynInstPtr> Consumers;
    Consumers dependGraph[64];
    bool producer[64];

public:

    RxuSimpleVecRename();

    void 
    reset(RegVal miscregval);

    unsigned 
    get();

    std::vector<bool> 
    stall();

    void 
    recover();

    void 
    release();

    void 
    setRecCnt();

    void 
    setRelCnt();

    void 
    setReady(unsigned vec_csr_i);

    void 
    unsetReady(unsigned vec_csr_i);

    bool 
    isReady(unsigned vec_csr_i);

    unsigned 
    curPhysVecCsr();

    uint64_t 
    getVal(unsigned vec_csr_i);

    void 
    setVal(unsigned vec_csr_i, uint64_t val);

    void 
    setWTB(WTB *wtb_ptr);

    void
    wakeDependences(unsigned phys_reg);

    void 
    removeDependence(unsigned phys_reg);

    void
    addConsumer(unsigned phys_reg, const DynInstPtr &inst);

    void 
    addProducer(unsigned phys_reg);

    VecCsrType _type;

    std::string 
    transferString(VecCsrType _type);

};

class RxuUnifiedVecRename
{
  private:

    std::array<RxuSimpleVecRename,VecCsrClass> vecCsrRename;

  public:

    RxuUnifiedVecRename();

    void 
    reset(VecCsrType type, RegVal miscregval);

    unsigned 
    get(VecCsrType type);

    std::vector<bool>
    stall();

    void 
    recover();

    void 
    release();

    void 
    setRecCnt(VecCsrType type);

    void 
    setRelCnt(VecCsrType type);

    void 
    setReady(VecCsrType type, unsigned vec_csr_i);

    void 
    unsetReady(VecCsrType type, unsigned vec_csr_i);

    bool 
    isReady(VecCsrType type, unsigned vec_csr_i);

    unsigned 
    curPhysVecCsr(VecCsrType type);

    uint64_t 
    getVal(VecCsrType type, unsigned vec_csr_i);

    void 
    setVal(VecCsrType type, unsigned vec_csr_i, uint64_t val);

    void 
    setWTB(VecCsrType type, WTB *wtb_ptr) { vecCsrRename[type].setWTB(wtb_ptr); }

    void
    wakeDependences(VecCsrType type, unsigned phys_reg);

    void 
    removeDependence(VecCsrType type, unsigned phys_reg);

    void
    addConsumer(VecCsrType type,unsigned phys_reg, const DynInstPtr &inst);

    void 
    addProducer(VecCsrType type, unsigned phys_reg);

};

} // rxuo3

} // gem5

#endif // __RXU_VECCSR_RENAME_HH__
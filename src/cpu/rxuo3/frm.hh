#ifndef __RXU_FRM_RENAME_HH__
#define __RXU_FRM_RENAME_HH__

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

class FRMVector
{

public:
    FRMVector() {
        memset(vld, true, sizeof(vld));
        cnt = 15;
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
                if (get_index == 0)   get_index = 1;
                break;
            }
        }
        return res;
    }

    bool empty() { return cnt == 0; }

    bool stall() { return cnt < 8; }

private:
    const unsigned VEC_LEN = 16;
    unsigned cnt;
    bool vld[16];
    uint32_t get_index;

};

class RxuFRMRename 
{

private:
    // CPU *cpu;
    WTB *wtb;
    FRMVector frm;
    unsigned relCnt;
    unsigned recCnt;
    std::list<unsigned> ringBuffer;
    // std::vector<bool> ready;
    // std::vector<uint64_t> value;
    bool        ready[16];
    uint64_t    value[16];

    typedef std::list<DynInstPtr> Consumers;
    Consumers dependGraph[16];
    bool producer[16];

public:
    RxuFRMRename();

    unsigned 
    get();

    bool 
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
    setReady(unsigned frm_i);

    void 
    unsetReady(unsigned frm_i);

    bool 
    isReady(unsigned frm_i);

    unsigned 
    curPhysFRM();

    uint64_t 
    getVal(unsigned frm_i);

    void 
    setVal(unsigned frm_i, uint64_t val);

    void 
    setWTB(WTB *wtb_ptr) { wtb = wtb_ptr; }

    void
    wakeDependences(unsigned phys_reg);

    void 
    removeDependence(unsigned phys_reg);

    void
    addConsumer(unsigned phys_reg, const DynInstPtr &inst);

    void 
    addProducer(unsigned phys_reg);

};



} // rxuo3

} // gem5

#endif // __RXU_FRM_RENAME_HH
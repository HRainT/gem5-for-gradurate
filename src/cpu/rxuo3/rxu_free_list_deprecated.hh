// ----------- added by longting.du --------------//

#ifndef __CPU_RXUO3_RXU_FREE_LIST_HH__
#define __CPU_RXUO3_RXU_FREE_LIST_HH__

#include <algorithm>
#include <array>
#include <iostream>
#include <queue>
#include <vector>
#include <unordered_map>

#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/regfile.hh"
#include "debug/RxuRename.hh"

namespace gem5
{

namespace rxuo3
{

class UnifiedRenameMap;

enum RenameStallStatus {
    NoStall,
    NotEnough,
    Recovering,
    AnyVectorFull,
    NumStallStatus
};

class RenameVec
{
    public:
        RenameVec() {
            memset(vld, false, sizeof(vld));
            isInteger = true;
        };

        bool empty() {
            return cnt == 0;
        }

        uint32_t get() {
            for (uint32_t i = 0; i < VecLen; ++i) {
                // if (isInteger && getIndex == 0) continue;
                if (vld[i]) {
                    vld[i] = false;
                    cnt--;
                    return i;
                }
            }
            cprintf("Tick %i: [warning] rename should stall, but not stall!\n", curTick());
            DPRINTF(RxuRename, "[warning] rename should stall, but not stall!\n");
            return 0;
        }

        bool isValid(int i) {
            return vld[i];
        }

        void put(uint32_t i) {
            if (vld[i]) return;
            vld[i] = true;
            cnt++;
        }

    public:
        uint32_t cnt = 30;
        int VecLen = 32;
        bool vld[64] = {false};
        bool isInteger = true;
};



class RxuSimpleFreeList
{
  public:
    int numRegs = 0;
    int used = 0;
    int cpu_get_index = 0;
    int rn_get_index = 0;

    RegClassType _type;
    RenameVec renameVec[16];
    int rel_cnt_t[32] = {0};
    int rec_cnt_t[32] = {0};
    std::vector<int> rollpoling;
    uint8_t _rollpoling;
    uint8_t whenrotating = 0;
    uint8_t rotatingOffset = 0;
    bool useRotating = false;

    std::vector<PhysRegIdPtr> freeRegs;
    std::vector<bool> isFree;
    std::vector<int> phyReg2Arch;

    std::deque<uint32_t> mapping_table[32];
    std::queue<uint16_t> fifo_out[32];
    uint32_t vecPut[16] = {0};
    uint32_t robin_state[16] = {0};
    uint32_t robin[16][32] = {0};

  public:
    RxuSimpleFreeList() {
        phyReg2Arch = std::vector<int>(512, -1);
        rollpoling = std::vector<int>(8, 0);
        memset(vecPut, 0, sizeof(vecPut));
        memset(robin_state, 0, sizeof(robin_state));
        memset(robin, 0, sizeof(robin));
    }

    std::string getRegClass() {
        if (_type == IntRegClass) {
            return "Integer";
        } else {
            return "Floating";
        }
    }

    /** Add a physical register to the free list */
    void addReg(PhysRegIdPtr reg) {
        if (numRegs == 0)   return;
        if (freeRegs.size() < numRegs) {
            // DPRINTF(FreeList, "addReg index: %i; flat_index: %i.\n", reg->index(), reg->flatIndex());
            freeRegs.push_back(reg);
            isFree.push_back(true);
            return;
        }
        RegIndex index = reg->index();
        // freeRegs[index] = reg;
        isFree[index] = true;
    }

    void setRelCnt(int _relCnt[]) {
        memcpy(rel_cnt_t, _relCnt, sizeof(rel_cnt_t));
    }

    void setRecCnt(int _recCnt[]) {
        memcpy(rec_cnt_t, _recCnt, sizeof(rec_cnt_t));
    }

    void setRelCnt(RegId arch_reg) {
        RegIndex archRegIndex = arch_reg.index();
        rel_cnt_t[int(archRegIndex)]++;
    }

    void setRecCnt(RegId arch_reg) {
        RegIndex archRegIndex = arch_reg.index();
        rec_cnt_t[int(archRegIndex)]++;
    }

    void rollbackAndRelease() {
        release_recovery();
    }

    std::vector<bool> checkStasll() {
        int numVec = 16;
        if (_type == FloatRegClass) numVec = 8;
        std::vector<bool> stall(3, false);
        int i = (_type == IntRegClass ? 1 : 0);
        for (; i < 32; i++) {
            stall[0] = stall[0] | (mapping_table[i].size() > (((numRegs / numVec) * 2) - 8));
            stall[1] = stall[1] | (rec_cnt_t[i] > 0);
            if (stall[0]) {
                DPRINTF(RxuRename, "[%s], rename should stall due to mapping_table[%i] useRatio > 56.\n"
                        , getRegClass(), i);
            }
            if (stall[1]) {
                DPRINTF(RxuRename, "[%s], rename should stall due to recover is doing at %i.\n"
                        , getRegClass(), i);
            }
            if (stall[0] | stall[1]) {
                break;
            }
        }
        for (i = 0; i < numVec; i++) {
            stall[2] = stall[2] | renameVec[i].empty();
            if (stall[2]) {
                DPRINTF(RxuRename, "[%s], rename should stall due to vector[%i] is empty.\n"
                        , getRegClass(), i);
                break;
            }
        }
        return stall;
    }

    /** Add physical registers to the free list */
    template<class InputIt>
    void
    addRegs(InputIt first, InputIt last) {
        std::for_each(first, last, [this](typename InputIt::value_type& reg) {
            freeRegs.push_back(&reg);
        });
    }

    PhysRegIdPtr getReg()
    {
        assert(!freeRegs.empty());
        // if (cpu_get_index > 31) {
        //     cpu_get_index = 0;
        // }
        PhysRegIdPtr free_reg = freeRegs[cpu_get_index++];
        return free_reg;
    }

    /** Get the next available register from the free list */
    PhysRegIdPtr getReg(RegId arch_reg, unsigned inst_index, bool& stall)
    {
        assert(inst_index < 8);
        PhysRegIdPtr free_reg = nullptr;
        if (_type != IntRegClass && _type != FloatRegClass) {
            free_reg = freeRegs[rn_get_index++];
            return free_reg;
        }
        RegIndex regIdx = arch_reg.index();
        int rn_index = -1;
        int numVec = 16;
        if (_type == FloatRegClass) numVec = 8;
        // int i = _type == IntRegClass ? 1 : 0;
        // for ( ; i < 32; i++) {
        //     stall |= (mapping_table[i].size() > 56) | (rec_cnt_t[i] > 0);
        //     // stall |= (rec_cnt_t[i] > 0);
        //     if (stall) {
        //         DPRINTF(RxuRename, "[%s], rename should stall due to ring buffer[%i] size > 56 or recover is doing at %i.\n",
        //                 getRegClass(), i, i);
        //         break;
        //     }
        // }
        // if (!stall) {
        //     for (i = 0; i < numVec; i++) {
        //         stall |= renameVec[i].empty();
        //         if (stall) {
        //             DPRINTF(RxuRename, "[%s], rename should stall due to vector[%i] is empty.\n",
        //                     getRegClass(), i);
        //             break;
        //         }
        //     }
        // }

        // rename
        if (!stall) {
            int shift = (_type == IntRegClass) ? 4 : 3;
            if (_type == IntRegClass) {
                if (!useRotating) {
                    rn_index = (renameVec[inst_index + rollpoling[inst_index] * 8].get() << shift)
                            + (rollpoling[inst_index] << 3) + inst_index;
                    rollpoling[inst_index] ^= 1; // to use another vector
                } else {
                    unsigned vec_index_chose = inst_index + (_rollpoling << 3) + rotatingOffset;
                    vec_index_chose %= numVec;
                    rn_index = (renameVec[vec_index_chose].get() << shift)
                            + vec_index_chose;
                    whenrotating++;
                    if (whenrotating == 8) {
                        _rollpoling ^= 1; // to use another vector
                    } else if (whenrotating == 16) {
                        _rollpoling ^= 1;
                        whenrotating = 0;
                        rotatingOffset++;
                        if (rotatingOffset == 8) {
                            rotatingOffset = 0;
                        }
                    }
                }
            } else if (_type == FloatRegClass) {
                if (!useRotating) 
                    rn_index = (renameVec[inst_index].get() << shift) + inst_index;
                else {
                    unsigned vec_index_chose = inst_index + rotatingOffset;
                    vec_index_chose %= numVec;
                    rn_index = (renameVec[vec_index_chose].get() << shift)
                            + vec_index_chose;
                    whenrotating++;
                    if (whenrotating == 8) {
                        whenrotating = 0;
                        rotatingOffset++;
                        if (rotatingOffset == 8) {
                            rotatingOffset = 0;
                        }
                    }
                }
            }
            mapping_table[int(regIdx)].push_back(rn_index);
            // cprintf("Tick: %i, [%s] [arch: %i] ringbuffer-front: %i, ringbuffer-back: %i, has %i entries.\n", 
            //         curTick(), getRegClass(), regIdx, mapping_table[regIdx].front(), mapping_table[regIdx].back(), mapping_table[regIdx].size());
            used++;
        }

        if (rn_index < 0 || rn_index >= numRegs) {
            return nullptr;
        }

        free_reg = freeRegs[rn_index];
        phyReg2Arch[rn_index] = int(regIdx);
        isFree[rn_index] = false;
        return free_reg;
    }

    void release_recovery()
    {
        int numVec = 16;
        if (_type == FloatRegClass) numVec = 8;

        // release  and recovery
        int i = (_type == IntRegClass ? 1 : 0);
        for ( ; i < 32; i++) {
            if (fifo_out[i].size() != numVec) {
                if (rec_cnt_t[i] > 0) {
                    int rec_width = 8;
                    while (rec_cnt_t[i] > 0 && rec_width--) {
                        if (mapping_table[i].empty()) {
                            continue;
                        }
                        uint32_t recover = mapping_table[i].back();
                        fifo_out[i].push(recover);
                        mapping_table[i].pop_back();
                        DPRINTF(RxuRename,
                                "recover [%s] physical reg %i.\n",
                                getRegClass(), recover);
                        rec_cnt_t[i]--;
                    }
                }
                else if (rel_cnt_t[i] > 0) {
                    int rel_width = 8;
                    while (rel_cnt_t[i] > 0 && rel_width--) {
                        if (mapping_table[i].empty()) {
                            continue;
                        }
                        uint32_t released = mapping_table[i].front();
                        fifo_out[i].push(released);
                        mapping_table[i].pop_front();
                        DPRINTF(RxuRename,
                                "release [%s] physical reg %i.\n",
                                getRegClass(), released);
                        rel_cnt_t[i]--;
                    }
                }
            }
        }

        int j_start = (_type == IntRegClass ? 1 : 0);
        for (i = 0; i < numVec; i++) {
            for (int j = j_start; j < 32; j++) {
                robin[i][j] =
                    !fifo_out[j].empty() && ((fifo_out[j].front() & (numVec - 1)) == i);
            }
        }

        for (i = 0; i < numVec; i++) {
            for (int j = j_start; j < 32; j++) {
                if (robin[i][j]) {
                    robin_state[i] = (robin_state[i] + 1) % 32;
                    break;
                }
            }
        }

        // robin state update
        for (i = 0; i < numVec; i++) {
            for (int k = 0, j = robin_state[i]; k < 32; j = (j + 1) % 32, k++) {
                if (robin[i][j]) {
                    // cprintf("Tick %i , %d vec.%d  release %d \n", curTick(), i , j , fifo_out[j].front());
                    vecPut[i] = (_type == IntRegClass ? (fifo_out[j].front() >> 4) : (fifo_out[j].front() >> 3)) + 1;
                    fifo_out[j].pop();
                    break;
                }
            }
        }

        for (i = 0; i < numVec; i++) {
            if (vecPut[i] > 0) {
                renameVec[i].put(vecPut[i] - 1);
                vecPut[i] = 0;
                used--;
            }
        }
    }

    void dump()
    {
        if(_type != IntRegClass && _type != FloatRegClass) {
            return;
        }
        for (int i = 0; i < phyReg2Arch.size(); ++i) {
            if (phyReg2Arch[i] < 0) {
                // DPRINTF(FreeList, "phy reg %i not be used. \n", i);
            } else {
                DPRINTF(RxuRename, "phy reg %i <= arch reg %i. \n", i, phyReg2Arch[i]);
            }
        }
    }

    void dumpVector()
    {
        int numVec = 16;
        if (_type == FloatRegClass) numVec = 8;
        cprintf("Tick %i, dump vector:\n", curTick());
        cprintf("[%s] dump: \n", getRegClass());
        for (int i = 0; i < 32; ++i) {
            for (int j = 0; j < numVec; ++j) {
                if (renameVec[j].isValid(i)) {
                    // cprintf("Tick %i, vector i(%i) and j(%i) [phy:%i] is valid\n", curTick(), j, i, j + i * 16);
                } else {
                    cprintf("Tick %i, vector i(%i) and j(%i) [phy:%i] is busy\n", curTick(), j, i, j + i * numVec);
                }
            }
        }
    }

    /** Return the number of free registers on the list. */
    unsigned numFreeRegs() {
        return numRegs - used;
    }

    /** True iff there are free registers on the list. */
    bool hasFreeRegs() {
        return (numRegs > used);
    }

    void setNumRegs(int num) {  numRegs = num;  }

    int getNumRegs() {  return numRegs;  }

    void resetVector() {
        if (_type == IntRegClass) {
            int num_vec = numRegs / 16;
            for (int i = 0; i < 16; ++i) {
                for (int j = 2; j < num_vec; ++j) {
                    renameVec[i].vld[j] = true;
                }
                renameVec[i].cnt = num_vec - 2;
                renameVec[i].isInteger = true;
                renameVec[i].VecLen = num_vec;
            }
        }
        if (_type == FloatRegClass) {
            int num_vec = numRegs / 8;
            for (int i = 0; i < 8; ++i) {
                for (int j = 4; j < num_vec; ++j) {
                    renameVec[i].vld[j] = true;
                }
                renameVec[i].cnt = num_vec - 4;
                renameVec[i].isInteger = false;
                renameVec[i].VecLen = num_vec;
            }
        }
        for (int i = 0; i < 32; i++) {
            mapping_table[i].push_back(i);
        }
    }

    void setRotating() { useRotating = true; }

};


class RxuUnifiedFreeList
{
  private:

    /** The object name, for DPRINTF.  We have to declare this
     *  explicitly because Scoreboard is not a SimObject. */
    const std::string _name;

    std::array<RxuSimpleFreeList, CCRegClass + 1> freeLists;

    /**
     * The register file object is used only to distinguish integer
     * from floating-point physical register indices.
     */
    PhysRegFile *regFile;

    /*
     * We give UnifiedRenameMap internal access so it can get at the
     * internal per-class free lists and associate those with its
     * per-class rename maps. See UnifiedRenameMap::init().
     */
    friend class UnifiedRenameMap;

  public:
    /** Constructs a free list.
     *  @param _numPhysicalIntRegs Number of physical integer registers.
     *  @param reservedIntRegs Number of integer registers already
     *                         used by initial mappings.
     *  @param _numPhysicalFloatRegs Number of physical fp registers.
     *  @param reservedFloatRegs Number of fp registers already
     *                           used by initial mappings.
     */
    RxuUnifiedFreeList(const std::string &_my_name, PhysRegFile *_regFile);

    /** Gives the name of the freelist. */
    std::string name() const { return _name; };

    /** Gets a free register of type type. */
    PhysRegIdPtr getReg(
        RegId arch_reg,
        RegClassType type,
        unsigned index,
        bool & stall)
    {
        return freeLists[type].getReg(arch_reg, index, stall);
    }

    PhysRegIdPtr getReg(RegClassType type)
    {
        return freeLists[type].getReg();
    }

    /** Adds a register back to the free list. */
    template<class InputIt>
    void
    addRegs(InputIt first, InputIt last)
    {
        std::for_each(first, last, [this](auto &reg) { addReg(&reg); });
    }

    /** Adds a register back to the free list. */
    void
    addReg(PhysRegIdPtr freed_reg)
    {
        freeLists[freed_reg->classValue()].addReg(freed_reg);
    }

    /** Checks if there are any free registers of type type. */
    bool
    hasFreeRegs(RegClassType type)
    {
        return freeLists[type].hasFreeRegs();
    }

    /** Returns the number of free registers of type type. */
    unsigned
    numFreeRegs(RegClassType type)
    {
        return freeLists[type].numFreeRegs();
    }

    void setRelCnt(RegId arch_reg)
    {
        freeLists[arch_reg.classValue()].setRelCnt(arch_reg);
    }

    void setRecCnt(RegId arch_reg)
    {
        freeLists[arch_reg.classValue()].setRecCnt(arch_reg);
    }

    void setRotating() {
        freeLists[IntRegClass].setRotating();
        freeLists[FloatRegClass].setRotating();
    }

    void dump()
    {
        // freeLists[IntRegClass].dump();
        freeLists[IntRegClass].dumpVector();
        freeLists[FloatRegClass].dumpVector();
    }

    void recoverAndRelease() {
        freeLists[IntRegClass].release_recovery();
        freeLists[FloatRegClass].release_recovery();
    }

    std::vector<bool> checkStall() {
        std::vector<bool> intRnStall = freeLists[IntRegClass].checkStasll();
        std::vector<bool> ret = freeLists[FloatRegClass].checkStasll();
        for (int i = 0; i < 3; ++i)
            ret[i] = ret[i] | intRnStall[i];
        return ret;
    }

};

}

}

#endif

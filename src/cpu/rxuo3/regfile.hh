#ifndef __CPU_RxuO3_REGFILE_HH__
#define __CPU_RxuO3_REGFILE_HH__

#include <cstring>
#include <vector>

#include "arch/generic/isa.hh"
#include "base/trace.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/regfile.hh"
#include "debug/RxuEW.hh"
#include "debug/Rxuewload.hh"
#include "debug/Spike.hh"

namespace gem5
{

namespace rxuo3
{

class RxuUnifiedFreeList;
class UnifiedFreeList;

/**
 * Simple physical register file class.
 */
class PhysRegFile
{
  private:

    using PhysIds = std::vector<PhysRegId>;
  public:
    using IdRange = std::pair<PhysIds::iterator,
                              PhysIds::iterator>;
  private:
    /** Integer register file. */
    RegFile intRegFile;
    std::vector<PhysRegId> intRegIds;

    /** Floating point register file. */
    RegFile floatRegFile;
    std::vector<PhysRegId> floatRegIds;

    /** Vector register file. */
    RegFile vectorRegFile;
    std::vector<PhysRegId> vecRegIds;

    /** Vector element register file. */
    RegFile vectorElemRegFile;
    std::vector<PhysRegId> vecElemIds;

    /** Predicate register file. */
    RegFile vecPredRegFile;
    std::vector<PhysRegId> vecPredRegIds;

    /** Matrix register file. */
    RegFile matRegFile;
    std::vector<PhysRegId> matRegIds;

    /** Condition-code register file. */
    RegFile ccRegFile;
    std::vector<PhysRegId> ccRegIds;

    /** Misc Reg Ids */
    std::vector<PhysRegId> miscRegIds;

  public:

    /**
     * Number of physical general purpose registers
     */
    unsigned numPhysicalIntRegs;

    /**
     * Number of physical floating point registers
     */
    unsigned numPhysicalFloatRegs;

    /**
     * Number of physical vector registers
     */
    unsigned numPhysicalVecRegs;

    /**
     * Number of physical vector element registers
     */
    unsigned numPhysicalVecElemRegs;

    /**
     * Number of physical predicate registers
     */
    unsigned numPhysicalVecPredRegs;

    /**
     * Number of physical matrix registers
     */
    unsigned numPhysicalMatRegs;

    /**
     * Number of physical CC registers
     */
    unsigned numPhysicalCCRegs;

    /** Total number of physical registers. */
    unsigned totalNumRegs;

  public:
    /**
     * Constructs a physical register file with the specified amount of
     * integer and floating point registers.
     */
    PhysRegFile(unsigned _numPhysicalIntRegs,
                unsigned _numPhysicalFloatRegs,
                unsigned _numPhysicalVecRegs,
                unsigned _numPhysicalVecPredRegs,
                unsigned _numPhysicalMatRegs,
                unsigned _numPhysicalCCRegs,
                const BaseISA::RegClasses &classes);

    /**
     * Destructor to free resources
     */
    ~PhysRegFile() {}

    /** Initialize the free list */
    void initFreeList(RxuUnifiedFreeList *freeList);
    void initFreeList(UnifiedFreeList *freeList);

    /** @return the total number of physical registers. */
    unsigned totalNumPhysRegs() const { return totalNumRegs; }

    /** Gets a misc register PhysRegIdPtr. */
    PhysRegIdPtr getMiscRegId(RegIndex reg_idx) {
        return &miscRegIds[reg_idx];
    }

    __uint128_t
    getVectorReg(PhysRegIdPtr phys_reg)
    {
        return vectorRegFile.reg<__uint128_t>(phys_reg->index());
    }

    RegVal
    getReg(PhysRegIdPtr phys_reg) const
    {
        const RegClassType type = phys_reg->classValue();
        const RegIndex idx = phys_reg->index();

        RegVal val;
        switch (type) {
          case IntRegClass:
            val = intRegFile.reg(idx);
            DPRINTF(RxuEW, "RegFile: Access to int register %i, has data %#x\n",
                    idx, val);
            DPRINTF(Rxuewload, "RegFile: Access to int register %i, has data %#x\n",
                    idx, val);
            DPRINTF(Spike, "RegFile: Access to int register %i, has data %#x\n",
                    idx, val);
            return val;
          case FloatRegClass:
            val = floatRegFile.reg(idx);
            DPRINTF(RxuEW, "RegFile: Access to float register %i has data %#x\n",
                    idx, val);
            DPRINTF(Spike, "RegFile: Access to float register %i has data %#x\n",
                    idx, val);
            return val;
          case VecElemClass:
            val = vectorElemRegFile.reg(idx);
            DPRINTF(RxuEW, "RegFile: Access to vector element register %i "
                    "has data %#x\n", idx, val);
            DPRINTF(Spike, "RegFile: Access to vector element register %i "
                    "has data %#x\n", idx, val);
            return val;
          case VecRegClass:
            val = vectorRegFile.reg<__uint128_t>(idx);
            DPRINTF(RxuEW, "RegFile: Access to vector register %i "
                    "has data %#x\n", idx, val);
            DPRINTF(Spike, "RegFile: Access to vector register %i "
                    "has data %#x\n", idx, val);
            return val;
          case CCRegClass:
            val = ccRegFile.reg(idx);
            DPRINTF(RxuEW, "RegFile: Access to cc register %i has data %#x\n",
                    idx, val);
            DPRINTF(Spike, "RegFile: Access to cc register %i has data %#x\n",
                    idx, val);
            return val;
          default:
            panic("Unsupported register class type %d.", type);
        }
    }

    std::string reverseByteOrder(const std::string& input) const {
      // 去除方括号
      std::string hexStr = input.substr(1, input.length() - 2);

      // 移除下划线
      hexStr.erase(std::remove(hexStr.begin(), hexStr.end(), '_'), hexStr.end());

      // 检查字符串长度是否为偶数
      if (hexStr.length() % 2 != 0) {
          throw std::invalid_argument("Invalid input string length");
      }

      // 将字符串每两个字符分割为字节
      std::vector<std::string> bytes;
      for (size_t i = 0; i < hexStr.length(); i += 2) {
          bytes.push_back(hexStr.substr(i, 2));
      }

      // 反转字节序
      std::reverse(bytes.begin(), bytes.end());

      // 重新格式化输出字符串
      std::ostringstream oss;
      oss << "[";
      for (size_t i = 0; i < bytes.size(); ++i) {
          // 每 4 个字节添加一个下划线（除了第一个字节）
          if (i != 0 && i % 4 == 0) {
              oss << "_";
          }
          oss << bytes[i];
      }
      oss << "]";

      return oss.str();
    }

    void
    getReg(PhysRegIdPtr phys_reg, void *val) const
    {
        const RegClassType type = phys_reg->classValue();
        const RegIndex idx = phys_reg->index();

        switch (type) {
          case IntRegClass:
            *(RegVal *)val = getReg(phys_reg);
            break;
          case FloatRegClass:
            *(RegVal *)val = getReg(phys_reg);
            break;
          case VecRegClass:
            vectorRegFile.get(idx, val);
            DPRINTF(RxuEW, "RegFile: Access to vector register %i, has "
                    "data %s\n", idx, reverseByteOrder(vectorRegFile.regClass.valString(val)));
            DPRINTF(Spike, "RegFile: Access to vector register %i, has "
                    "data %s\n", idx, reverseByteOrder(vectorRegFile.regClass.valString(val)));
            break;
          case VecElemClass:
            *(RegVal *)val = getReg(phys_reg);
            break;
          case VecPredRegClass:
            vecPredRegFile.get(idx, val);
            DPRINTF(RxuEW, "RegFile: Access to predicate register %i, has "
                    "data %s\n", idx, reverseByteOrder(vecPredRegFile.regClass.valString(val)));
            DPRINTF(Spike, "RegFile: Access to predicate register %i, has "
                    "data %s\n", idx, reverseByteOrder(vecPredRegFile.regClass.valString(val)));
            break;
          case MatRegClass:
            matRegFile.get(idx, val);
            DPRINTF(RxuEW, "RegFile: Access to matrix register %i, has "
                    "data %s\n", idx, matRegFile.regClass.valString(val));
            DPRINTF(Spike, "RegFile: Access to matrix register %i, has "
                    "data %s\n", idx, matRegFile.regClass.valString(val));
            break;
          case CCRegClass:
            *(RegVal *)val = getReg(phys_reg);
            break;
          default:
            panic("Unrecognized register class type %d.", type);
        }
    }

    void *
    getWritableReg(PhysRegIdPtr phys_reg)
    {
        const RegClassType type = phys_reg->classValue();
        const RegIndex idx = phys_reg->index();

        switch (type) {
          case VecRegClass:
            return vectorRegFile.ptr(idx);
          case VecPredRegClass:
            return vecPredRegFile.ptr(idx);
          case MatRegClass:
            return matRegFile.ptr(idx);
          default:
            panic("Unrecognized register class type %d.", type);
        }
    }

    void
    setReg(PhysRegIdPtr phys_reg, RegVal val)
    {
        const RegClassType type = phys_reg->classValue();
        const RegIndex idx = phys_reg->index();

        switch (type) {
          case InvalidRegClass:
            break;
          case IntRegClass:
            intRegFile.reg(idx) = val;
            DPRINTF(RxuEW, "RegFile: Setting int register %i to %#x\n",
                    idx, val);
            DPRINTF(Spike, "RegFile: Setting int register %i to %#x\n",
                    idx, val);
            break;
          case FloatRegClass:
            floatRegFile.reg(idx) = val;
            DPRINTF(RxuEW, "RegFile: Setting float register %i to %#x\n",
                    idx, val);
            DPRINTF(Spike, "RegFile: Setting float register %i to %#x\n",
                    idx, val);
            break;
          case VecElemClass:
            vectorElemRegFile.reg(idx) = val;
            DPRINTF(RxuEW, "RegFile: Setting vector element register %i to "
                    "%#x\n", idx, val);
            DPRINTF(Spike, "RegFile: Setting vector element register %i to "
                    "%#x\n", idx, val);
            break;
          case CCRegClass:
            ccRegFile.reg(idx) = val;
            DPRINTF(RxuEW, "RegFile: Setting cc register %i to %#x\n",
                    idx, val);
            DPRINTF(Spike, "RegFile: Setting cc register %i to %#x\n",
                    idx, val);
            break;
          default:
            panic("Unsupported register class type %d.", type);
        }
    }

    void
    setReg(PhysRegIdPtr phys_reg, const void *val)
    {
        const RegClassType type = phys_reg->classValue();
        const RegIndex idx = phys_reg->index();

        switch (type) {
          case IntRegClass:
            setReg(phys_reg, *(RegVal *)val);
            break;
          case FloatRegClass:
            setReg(phys_reg, *(RegVal *)val);
            break;
          case VecRegClass:
            DPRINTF(RxuEW, "RegFile: Setting vector register %i to %s\n",
                    idx, vectorRegFile.regClass.valString(val));
            DPRINTF(Spike, "RegFile: Setting vector register %i to %s\n",
                    idx, vectorRegFile.regClass.valString(val));
            vectorRegFile.set(idx, val);
            break;
          case VecElemClass:
            setReg(phys_reg, *(RegVal *)val);
            break;
          case VecPredRegClass:
            DPRINTF(RxuEW, "RegFile: Setting predicate register %i to %s\n",
                    idx, vecPredRegFile.regClass.valString(val));
            DPRINTF(Spike, "RegFile: Setting predicate register %i to %s\n",
                    idx, vecPredRegFile.regClass.valString(val));
            vecPredRegFile.set(idx, val);
            break;
          case MatRegClass:
            DPRINTF(RxuEW, "RegFile: Setting matrix register %i to %s\n",
                    idx, matRegFile.regClass.valString(val));
            DPRINTF(Spike, "RegFile: Setting matrix register %i to %s\n",
                    idx, matRegFile.regClass.valString(val));
            matRegFile.set(idx, val);
            break;
          case CCRegClass:
            setReg(phys_reg, *(RegVal *)val);
            break;
          default:
            panic("Unrecognized register class type %d.", type);
        }
    }
};

} // namespace rxuo3
} // namespace gem5

#endif //__CPU_RxuO3_REGFILE_HH__

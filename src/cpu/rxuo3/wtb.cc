// -- add by hongfei.liu ---------------------------------------------
#include "cpu/rxuo3/wtb.hh"

#include <limits>

#include "base/logging.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/rxu_fu_pool.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuWTB.hh"
#include "debug/Rxucun.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/core.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

namespace gem5
{

namespace rxuo3
{
WTB::intDivCompletion::intDivCompletion(WTB *wtb_ptr)
    : Event(Stat_Event_Pri, AutoDelete),
      wtbPtr(wtb_ptr)
{
}

void
WTB::intDivCompletion::process()
{
    wtbPtr->processIntDivCompletion();
}

WTB::fpDivCompletion::fpDivCompletion(WTB *wtb_ptr)
    : Event(Stat_Event_Pri, AutoDelete),
      wtbPtr(wtb_ptr)
{
}

void
WTB::fpDivCompletion::process()
{
    wtbPtr->processFpDivCompletion();
}

WTB::earlyWakeUp::earlyWakeUp(WTB *wtb_ptr, DynInstPtr &_inst)
    : Event(Stat_Event_Pri, AutoDelete),
      wtbPtr(wtb_ptr), inst(_inst)
{
}

void
WTB::earlyWakeUp::process()
{
    wtbPtr->processEarlyWakeUp(inst);
    inst = NULL;
}

WTB::LdstWakeToIssue::LdstWakeToIssue(WTB *wtb_ptr, DynInstPtr &_inst)
    : Event(Serialize_Pri, AutoDelete),
      wtbPtr(wtb_ptr), inst(_inst)
{
}


void
WTB::LdstWakeToIssue::process()
{
    wtbPtr->processLdstWakeToIssue(inst);
    inst = NULL;
}

WTB::readySrc_readReg::readySrc_readReg(WTB *wtb_ptr, DynInstPtr &_inst)
    : Event(Serialize_RR, AutoDelete),
      wtbPtr(wtb_ptr), inst(_inst)
{
}

void
WTB::readySrc_readReg::process()
{
    wtbPtr->processreadySrc_readReg(inst);
    inst = NULL;
}

WTB::WTB(CPU *cpu_ptr, Dispipe3 *p3_ptr,
        const BaseRxuO3CPUParams &params)
    : cpu(cpu_ptr),
      dispipe3Stage(p3_ptr),
      numWTBEntries(params.numWTBEntries),
      numVectorEntries(params.numVectorEntries),
      numIntSpecialEntries(params.numIntSpecialEntries),
      LdStEntries(params.LdStEntries),
      intNormalOddRegReadNums(params.intNormalOddRegReadNums),
      intSpecialOddRegReadNums(params.intSpecialOddRegReadNums),
      intNormalEvenRegReadNums(params.intNormalEvenRegReadNums),
      intSpecialEvenRegReadNums(params.intSpecialEvenRegReadNums),
      intNormalOddRegWriteNums(params.intNormalOddRegWriteNums),
      intSpecialOddRegWriteNums(params.intSpecialOddRegWriteNums),
      intNormalEvenRegWriteNums(params.intNormalEvenRegWriteNums),
      intSpecialEvenRegWriteNums(params.intSpecialEvenRegWriteNums),
      ldstRegReadNums(params.ldstRegReadNums),
      fstRegReadNums(params.fstRegReadNums),
      fpNormalRegReadNums(params.fpNormalRegReadNums),
      fpSpecialRegReadNums(params.fpSpecialRegReadNums),
      fpRegWriteNums(params.fpRegWriteNums),
      fldRegWriteNums(params.fldRegWriteNums),
      vectorRegWriteNums(params.vectorRegWriteNums),
      numThreads(params.numThreads),
      initFdivNums(params.system->fdivNums()),
      initFdivDelay(params.system->fdivDelay()),
      stats(cpu)
{
    //Initialize Mem Dependence Units
    for (ThreadID tid = 0; tid < MaxThreads; tid++) {
        memDepUnit[tid].init(params, tid, cpu_ptr);
        memDepUnit[tid].setWTB(this);
    }

    wbNums.clear();
    for (int i = 0; i < 30; i++) {
        WbNums wbNumsRecord;

        wbNumsRecord.intNormalOddWbNums = 2;
        wbNumsRecord.intSpecialOddWbNums = 1;
        wbNumsRecord.intNormalEvenWbNums = 2;
        wbNumsRecord.intSpecialEvenWbNums = 1;
        wbNumsRecord.fpWbNums = 3;
        wbNumsRecord.fldWbNums = 1;
        wbNumsRecord.vecttorWbNums = 4;

        wbNums.push_back(wbNumsRecord);
    }

    pipelineUseNums = {2, initFdivNums, 2};

    resetState();
}

WTB::~WTB()
{
}

std::string
WTB::name() const
{
    return cpu->name() + ".wtb";
}

WTB::WTBStats::WTBStats(CPU *cpu)
    : statistics::Group(cpu),
    ADD_STAT(instsIssued, statistics::units::Count::get(),
             "Number of instructions issued"),
    ADD_STAT(intInstsIssued, statistics::units::Count::get(),
             "Number of integer instructions issued"),
    ADD_STAT(intNorInstsIssued, statistics::units::Count::get(),
             "Number of integer normal instructions issued"),
    ADD_STAT(intSpeInstsIssued, statistics::units::Count::get(),
             "Number of integer special instructions issued"),
    ADD_STAT(fpInstsIssued, statistics::units::Count::get(),
             "Number of float instructions issued"),
    ADD_STAT(fpNorInstsIssued, statistics::units::Count::get(),
             "Number of float normal instructions issued"),
    ADD_STAT(fpSpeInstsIssued, statistics::units::Count::get(),
             "Number of float special instructions issued"),
    ADD_STAT(branchInstsIssued, statistics::units::Count::get(),
             "Number of branch instructions issued"),
    ADD_STAT(memInstsIssued, statistics::units::Count::get(),
             "Number of memory instructions issued"),
    ADD_STAT(loadInstsIssued, statistics::units::Count::get(),
             "Number of load instructions issued"),
    ADD_STAT(storeInstsIssued, statistics::units::Count::get(),
             "Number of store instructions issued"),
    ADD_STAT(squashedInsts, statistics::units::Count::get(),
             "Number of squashed instructions in WTB"),
    ADD_STAT(statFuBusy, statistics::units::Count::get(),
             "attempts to use FU when none available"),
    ADD_STAT(fuBusyCycles, statistics::units::Count::get(),
             "Number of cycles instructions fail to issue due to the FU busy"),
    ADD_STAT(intRegReadBusy, statistics::units::Count::get(),
             "Number of times instructions fail to issue due to the int regReadPort busy"),
    // ADD_STAT(intRegReadFree, statistics::units::Count::get(),
    //          "Number of times 2 instructions selected success to issue"),
    ADD_STAT(intRegWriteBusy, statistics::units::Count::get(),
             "Number of times instructions fail to issue due to the int regWritePort busy"),
    ADD_STAT(fpRegReadBusy, statistics::units::Count::get(),
             "Number of times instructions fail to issue due to the float regReadPort busy"),
    // ADD_STAT(fpRegReadFree, statistics::units::Count::get(),
    //          "Number of times instructions selected success to issue"),
    ADD_STAT(fpRegWriteBusy, statistics::units::Count::get(),
             "Number of times instructions fail to issue due to the float regWritePort busy"),
    ADD_STAT(vectorRegWriteBusy, statistics::units::Count::get(),
             "Number of times instructions fail to issue due to the vector regWritePort busy"),
    ADD_STAT(ibuffer0Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 0 per cycle"),
    ADD_STAT(ibuffer1Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 1 per cycle"),
    ADD_STAT(ibuffer4Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 4 per cycle"),
    ADD_STAT(ibuffer5Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 5 per cycle"),

    ADD_STAT(ibuffer2Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 2 per cycle"),
    ADD_STAT(ibuffer3Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 3 per cycle"),
    ADD_STAT(ibuffer20Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 20 per cycle"),
    ADD_STAT(ibuffer21Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 21 per cycle"),
    ADD_STAT(ibuffer22Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 22 per cycle"),
    ADD_STAT(ibuffer23Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 23 per cycle"),

    ADD_STAT(ibuffer6Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 6 per cycle"),
    ADD_STAT(ibuffer7Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 7 per cycle"),
    ADD_STAT(ibuffer14Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 14 per cycle"),
    ADD_STAT(ibuffer15Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 15 per cycle"),
    ADD_STAT(ibuffer16Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 16 per cycle"),
    ADD_STAT(ibuffer17Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 17 per cycle"),
    ADD_STAT(ibuffer18Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 18 per cycle"),
    ADD_STAT(ibuffer19Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 19 per cycle"),

    ADD_STAT(ibuffer8Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 8 per cycle"),
    ADD_STAT(ibuffer9Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 9 per cycle"),
    ADD_STAT(ibuffer10Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 10 per cycle"),
    ADD_STAT(ibuffer13Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 13 per cycle"),

    ADD_STAT(ibuffer11Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 11 per cycle"),
    ADD_STAT(ibuffer12Utilize, statistics::units::Count::get(),
             "Number of total instructions ibuffer 12 per cycle"),

    ADD_STAT(ibuffer0_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 0 (int normal)",
             ibuffer0Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer1_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 1 (int normal)",
             ibuffer1Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer4_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 4 (int normal)",
             ibuffer4Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer5_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 5 (int normal)",
             ibuffer5Utilize / (cpu->baseStats.numCycles * 12)),

    ADD_STAT(ibuffer2_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 2 (int special)",
             ibuffer2Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer3_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 3 (int special)",
             ibuffer3Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer20_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 20 (int special)",
             ibuffer20Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer21_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 21 (int special)",
             ibuffer21Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer22_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 22 (int special)",
             ibuffer22Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer23_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 23 (int special)",
             ibuffer23Utilize / (cpu->baseStats.numCycles * 12)),

    ADD_STAT(ibuffer6_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 6 (load store)",
             ibuffer6Utilize / (cpu->baseStats.numCycles * 32)),
    ADD_STAT(ibuffer7_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 7 (load store)",
             ibuffer7Utilize / (cpu->baseStats.numCycles * 32)),
    ADD_STAT(ibuffer14_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 14 (load store)",
             ibuffer14Utilize / (cpu->baseStats.numCycles * 32)),
    ADD_STAT(ibuffer15_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 15 (load store)",
             ibuffer15Utilize / (cpu->baseStats.numCycles * 32)),
    ADD_STAT(ibuffer16_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 16 (load store)",
             ibuffer16Utilize / (cpu->baseStats.numCycles * 32)),
    ADD_STAT(ibuffer17_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 17 (load store)",
             ibuffer17Utilize / (cpu->baseStats.numCycles * 32)),
    ADD_STAT(ibuffer18_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 18 (load store)",
             ibuffer18Utilize / (cpu->baseStats.numCycles * 32)),
    ADD_STAT(ibuffer19_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 19 (load store)",
             ibuffer19Utilize / (cpu->baseStats.numCycles * 32)),

    ADD_STAT(ibuffer8_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 8 (fp normal)",
             ibuffer8Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer9_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 9 (fp normal)",
             ibuffer9Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer10_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 10 (fp normal)",
             ibuffer10Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer13_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 13 (fp normal)",
             ibuffer13Utilize / (cpu->baseStats.numCycles * 12)),

    ADD_STAT(ibuffer11_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 11 (fp special)",
             ibuffer11Utilize / (cpu->baseStats.numCycles * 12)),
    ADD_STAT(ibuffer12_UtilizationRate, statistics::units::Ratio::get(),
             "Utilization rate of ibuffer 12 (fp special)",
             ibuffer12Utilize / (cpu->baseStats.numCycles * 12)),

    ADD_STAT(ibuffer2Out,
                "Distribution of number of instructions issued by ibuffer2 per cycle"),
    ADD_STAT(ibuffer3Out,
                "Distribution of number of instructions issued by ibuffer3 per cycle"),
    ADD_STAT(intnormal_lockRpork, statistics::units::Count::get(),
                "intnormal inst lock Rport in wtb"),
    ADD_STAT(intspecial_lockRpork, statistics::units::Count::get(),
                "intspecial inst lock Rport in wtb"),
    ADD_STAT(fnormal_lockRpork, statistics::units::Count::get(),
                "fnormal inst lock Rport in wtb"),
    ADD_STAT(fspecial_lockRpork, statistics::units::Count::get(),
                "fspecial inst lock Rport in wtb"),
    ADD_STAT(vector_lockRpork, statistics::units::Count::get(),
                "vector inst lock Rport in wtb"),
    ADD_STAT(circle_inwtb, "Distribution of cycle latency between the "
                "addtowtb and issue"),
    ADD_STAT(ldst_lockRpork, statistics::units::Count::get(),
                "ldst inst lock Rport in wtb"),
    ADD_STAT(circleover3, statistics::units::Count::get(),
                "inst locladata over 3 circle"),
    ADD_STAT(circleover3_noRport, statistics::units::Count::get(),
                "inst locladata over 3 circle noRort")
{
    instsIssued
        .prereq(instsIssued);

    intInstsIssued
        .prereq(intInstsIssued);

    intNorInstsIssued
        .prereq(intNorInstsIssued);

    intSpeInstsIssued
        .prereq(intSpeInstsIssued);

    fpInstsIssued
        .prereq(fpInstsIssued);

    fpNorInstsIssued
        .prereq(fpNorInstsIssued);

    fpSpeInstsIssued
        .prereq(fpSpeInstsIssued);

    branchInstsIssued
        .prereq(branchInstsIssued);

    memInstsIssued
        .prereq(memInstsIssued);

    loadInstsIssued
        .prereq(loadInstsIssued);

    storeInstsIssued
        .prereq(storeInstsIssued);

    squashedInsts
        .prereq(squashedInsts);

    statFuBusy
        .init(3)
        .flags(statistics::pdf | statistics::dist)
        ;
    statFuBusy.subname(0, enums::OpClassStrings[3]);
    statFuBusy.subname(1, enums::OpClassStrings[9]);
    statFuBusy.subname(2, enums::OpClassStrings[11]);

    fuBusyCycles
        .prereq(fuBusyCycles);

    intRegReadBusy
        .prereq(intRegReadBusy);
    intRegReadFree
        .prereq(intRegReadFree);
    intRegWriteBusy
        .prereq(intRegWriteBusy);
    fpRegReadBusy
        .prereq(fpRegReadBusy);
    fpRegReadFree
        .prereq(fpRegReadFree);
    fpRegWriteBusy
        .prereq(fpRegWriteBusy);
    vectorRegWriteBusy
        .prereq(vectorRegWriteBusy);
    ibuffer0Utilize
        .prereq(ibuffer0Utilize);
    ibuffer1Utilize
        .prereq(ibuffer1Utilize);
    ibuffer2Utilize
        .prereq(ibuffer2Utilize);
    ibuffer3Utilize
        .prereq(ibuffer3Utilize);
    ibuffer4Utilize
        .prereq(ibuffer4Utilize);
    ibuffer5Utilize
        .prereq(ibuffer5Utilize);
    ibuffer6Utilize
        .prereq(ibuffer6Utilize);
    ibuffer7Utilize
        .prereq(ibuffer7Utilize);
    ibuffer8Utilize
        .prereq(ibuffer8Utilize);
    ibuffer9Utilize
        .prereq(ibuffer9Utilize);
    ibuffer10Utilize
        .prereq(ibuffer10Utilize);
    ibuffer11Utilize
        .prereq(ibuffer11Utilize);
    ibuffer12Utilize
        .prereq(ibuffer12Utilize);
    ibuffer13Utilize
        .prereq(ibuffer13Utilize);
    ibuffer14Utilize
        .prereq(ibuffer14Utilize);
    ibuffer15Utilize
        .prereq(ibuffer15Utilize);
    ibuffer16Utilize
        .prereq(ibuffer16Utilize);
    ibuffer17Utilize
        .prereq(ibuffer17Utilize);
    ibuffer18Utilize
        .prereq(ibuffer18Utilize);
    ibuffer19Utilize
        .prereq(ibuffer19Utilize);
    ibuffer20Utilize
        .prereq(ibuffer20Utilize);
    ibuffer21Utilize
        .prereq(ibuffer21Utilize);
    ibuffer22Utilize
        .prereq(ibuffer22Utilize);
    ibuffer23Utilize
        .prereq(ibuffer23Utilize);

    ibuffer0_UtilizationRate
        .precision(6);
    ibuffer1_UtilizationRate
        .precision(6);
    ibuffer2_UtilizationRate
        .precision(6);
    ibuffer3_UtilizationRate
        .precision(6);
    ibuffer4_UtilizationRate
        .precision(6);
    ibuffer5_UtilizationRate
        .precision(6);
    ibuffer6_UtilizationRate
        .precision(6);
    ibuffer7_UtilizationRate
        .precision(6);
    ibuffer8_UtilizationRate
        .precision(6);
    ibuffer9_UtilizationRate
        .precision(6);
    ibuffer10_UtilizationRate
        .precision(6);
    ibuffer11_UtilizationRate
        .precision(6);
    ibuffer12_UtilizationRate
        .precision(6);
    ibuffer13_UtilizationRate
        .precision(6);
    ibuffer14_UtilizationRate
        .precision(6);
    ibuffer15_UtilizationRate
        .precision(6);
    ibuffer16_UtilizationRate
        .precision(6);
    ibuffer17_UtilizationRate
        .precision(6);
    ibuffer18_UtilizationRate
        .precision(6);
    ibuffer19_UtilizationRate
        .precision(6);
    ibuffer20_UtilizationRate
        .precision(6);
    ibuffer21_UtilizationRate
        .precision(6);
    ibuffer22_UtilizationRate
        .precision(6);
    ibuffer23_UtilizationRate
        .precision(6);


    ibuffer2Out
        .init(0, 3, 1)
        .flags(statistics::nozero);

    ibuffer3Out
        .init(0, 3, 1)
        .flags(statistics::nozero);

    intnormal_lockRpork
        .prereq(intnormal_lockRpork);

    intspecial_lockRpork
        .prereq(intspecial_lockRpork);

    fnormal_lockRpork
        .prereq(fnormal_lockRpork);

    fspecial_lockRpork
        .prereq(fspecial_lockRpork);

    vector_lockRpork
        .prereq(vector_lockRpork);

    circle_inwtb
        .init(0, 20, 1)
        .flags(statistics::nozero);

    ldst_lockRpork
        .prereq(ldst_lockRpork);

    circleover3
        .prereq(circleover3);

    circleover3_noRport
        .prereq(circleover3_noRport);
}

void
WTB::resetState()
{
    for (ThreadID tid = 0; tid < MaxThreads; ++tid) {
        squashedSeqNum[tid] = 0;
    }

    for (int i = 0; i < 24; i++) {
        for (int j = 0; j < 2; j++) {
            while (!readyInsts[i][j].empty())
                readyInsts[i][j].pop();
        }
    }

    while (!readyVecInst0.empty()) {
        readyVecInst0.pop();
    }

    while (!readyVecInst1.empty()) {
        readyVecInst1.pop();
    }

    while (!readySTdata0.empty()) {
        readySTdata0.pop();
    }

    while (!readySTdata1.empty()) {
        readySTdata1.pop();
    }

    while (!readyLDSTodd0.empty()) {
        readyLDSTodd0.pop();
    }

    while (!readyVecInst0.empty()) {
        readyVecInst0.pop();
    }

    while (!readyVecInst1.empty()) {
        readyVecInst1.pop();
    }

    while (!sc_queue.empty()) {
        sc_queue.pop();
    }

    // while (!readyLDSTodd1.empty()) {
    //     readyLDSTodd1.pop();
    // }

    while (!readyLDSTeven0.empty()) {
        readyLDSTeven0.pop();
    }

    // while (!readyLDSTeven1.empty()) {
    //     readyLDSTeven1.pop();
    // }

    while (!readyStore.empty()) {
        readyStore.pop();
    }

    while (!readyLD_Dst_odd.empty()) {
        readyLD_Dst_odd.pop();
    }

    while (!readyLD_Dst_even.empty()) {
        readyLD_Dst_even.pop();
    }

    nonSpecInsts.clear();
    deferredMemInsts.clear();
    blockedMemInsts.clear();
    retryMemInsts.clear();
}

void
WTB::setActiveThreads(list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

bool
WTB::isDrained() const
{
    bool drained = true;
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        drained = drained && memDepUnit[tid].isDrained();

    return drained;
}

void
WTB::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        memDepUnit[tid].drainSanityCheck();
}

void
WTB::takeOverFrom()
{
    resetState();
}

// unsigned
// WTB::numFreeEntries(ThreadID tid, int index)
// {
//     return index != 6 && index != 7 && index != 14 && index != 15 && index != 16 && index != 17 && index != 18 && index != 19?
//         numWTBEntries - ibuffer[tid][index].size() : LdStEntries - ibuffer[tid][index].size();
// }

bool
WTB::isFull(ThreadID tid, int index)
{
    if (index == 2 || index == 3) {
        return ibuffer[tid][index].size() >= numIntSpecialEntries;
    } else if (index == 20 || index == 21) {
        return ibuffer[tid][index].size() >= numWTBEntries;
    } else if (index != 6 && index != 7 && index != 14 && index != 15 && index != 16 && index != 17 && index != 18 && index != 19) {
        return ibuffer[tid][index].size() >= numWTBEntries;
    } else {
        if(index == 14){
            return ibuffer[tid][14].size() >= LdStEntries;
        }
        else if(ibuffer[tid][6].size() >= ibuffer[tid][7].size()){
            return ibuffer[tid][7].size() >= LdStEntries;
        }
        else {
            return ibuffer[tid][6].size() >= LdStEntries;
        }
    }
}

bool
WTB::isFull(ThreadID tid, int index, bool isStore, bool ismemRef)
{
    if (index == 2 || index == 3) {
        return ibuffer[tid][index].size() >= numIntSpecialEntries;
    } else if (index == 20 || index == 21) {
        return ibuffer[tid][index].size() >= numWTBEntries;
    } else if (index != 6 && index != 7 && index != 14 && index != 15 && index != 16 && index != 17 && index != 18 && index != 19) {
        if(ismemRef){
            if(isStore){
                return ibuffer[tid][14].size() >= LdStEntries;
            }
            else if(ibuffer[tid][6].size() >= ibuffer[tid][7].size()){
                return ibuffer[tid][7].size() >= LdStEntries;
            }
            else {
                return ibuffer[tid][6].size() >= LdStEntries;
            }
        } else{
            return ibuffer[tid][index].size() >= numWTBEntries;
        }
    } else {
        if(isStore){
            return ibuffer[tid][14].size() >= LdStEntries;
        }
        else if(ibuffer[tid][6].size() >= ibuffer[tid][7].size()){
            return ibuffer[tid][7].size() >= LdStEntries;
        }
        else {
            return ibuffer[tid][6].size() >= LdStEntries;
        }
    }
}

bool
WTB::isFull(ThreadID tid)
{
    for (int i = 0; i < 24; i++) {
        if (i == 2 || i == 3) {
            if (ibuffer[tid][i].size() >= numIntSpecialEntries) {
                return true;
            }
        } else if (i == 20 || i == 21) {
            if (ibuffer[tid][i].size() >= numWTBEntries) {
                return true;
            }
        }
        else if (i == 22 || i == 23) {
            if (ibuffer[tid][i].size() >= numVectorEntries) {
                return true;
            }
        }
        else if (i != 6 && i != 7 && i != 14 && i != 15 && i != 16 && i != 17 && i != 18 && i != 19) {
            if (ibuffer[tid][i].size() >= numWTBEntries) {
                return true;
            }
        } else {
            if (ibuffer[tid][6].size() >= LdStEntries) {
                return true;
            }
            if (ibuffer[tid][7].size() >= LdStEntries) {
                return true;
            }
            if (ibuffer[tid][14].size() >= LdStEntries) {
                return true;
            }
        }

    }

    return false;
}

bool
WTB::hasReadyInsts()
{

    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 2; ++j) {
            if (!readyInsts[i][j].empty()) {
                return true;
            }
        }
    }

    if(!readyVecInst0.empty() || !readyVecInst1.empty()){
        return true;
    }

    // if (!readyLDSTodd0.empty() || !readyLDSTodd1.empty()
    // || !readyLDSTeven0.empty() || !readyLDSTeven1.empty()
    // || !readySTdata0.empty() || !readySTdata1.empty()) {
    //     return true;
    // }
    if (!readyLDSTodd0.empty() || !readyLDSTeven0.empty()
    || !readySTdata0.empty() || !readySTdata1.empty() ||!sc_queue.empty() ||!readyLD_Dst_even.empty()
    || !readyLD_Dst_odd.empty() || readyStore.empty()) {
        return true;
    }

    return false;
}

void
WTB::insert_vector_read_network36(bool from_disq, DynInstPtr inst0, DynInstPtr inst1, DynInstPtr inst2, DynInstPtr inst3){
    if(from_disq){
        if(inst0){
            if(inst0->isMemRef()){
                for (int num_src = 0; num_src < inst0->numSrcRegs(); num_src++)
                {
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst0->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst0->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==1 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[17].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==2 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[21].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src == 3 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[25].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "instruction [sn:%llu] should not have five or more regs.\n",inst0->seqNum);
                    }
                } 
            } else {
                for (int num_src = 0; num_src < inst0->numSrcRegs(); num_src++)
                {
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[1].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==1 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[5].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==2 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[9].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==3 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[13].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst0->seqNum);
                    }
                }
            }
        }
        else if(inst1){
            if(inst1->isMemRef()){
                for (int num_src = 0; num_src < inst1->numSrcRegs(); num_src++)
                {   
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst1->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst1->seqNum);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==1 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[19].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==2 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[23].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==3 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[27].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst1->seqNum);
                    }
                } 
            } else {
                for (int num_src = 0; num_src < inst1->numSrcRegs(); num_src++)
                {
                    if(inst1->is_vec_reg[num_src] && num_src==0 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[3].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==1 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[7].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==2 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[11].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==3 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[15].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst1->seqNum);
                    }
                }
            }
        }
        else if(inst2){
            if(inst2->isMemRef()){
                for (int num_src = 0; num_src < inst2->numSrcRegs(); num_src++)
                {   
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst2->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst2->seqNum);
                    }
                    if(inst2->is_vec_reg[num_src] && num_src==1 && inst2->readySrcIdx(num_src)){
                        vec_read_network_36_18[29].push_back(inst2);
                    }
                    if(inst2->is_vec_reg[num_src] && num_src==2 && inst2->readySrcIdx(num_src)){
                        vec_read_network_36_18[33].push_back(inst2);
                    } 
                    if(inst2->is_vec_reg[num_src] && num_src==3 && inst2->readySrcIdx(num_src)){
                        vec_read_network_36_18[35].push_back(inst2);
                    }
                    if(inst2->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst2->seqNum);
                    }
                } 
            } else {
                 DPRINTF(RxuWTB, "instruction [sn:%llu] should not from disq,because of oover 2.\n",inst2->seqNum);
            }
        }
        else if(inst3){
            if(inst3->isMemRef()){
                for (int num_src = 0; num_src < inst3->numSrcRegs(); num_src++)
                {   
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst3->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst3->seqNum);
                    }
                    if(inst3->is_vec_reg[num_src] && num_src==1 && inst3->readySrcIdx(num_src)){
                        vec_read_network_36_18[31].push_back(inst3);
                    }
                    if(inst3->is_vec_reg[num_src] && num_src==2 && inst3->readySrcIdx(num_src)){
                        vec_read_network_36_18[32].push_back(inst3);
                    }
                    if(inst3->is_vec_reg[num_src] && num_src==3 && inst3->readySrcIdx(num_src)){
                        vec_read_network_36_18[34].push_back(inst3);
                    }
                    if(inst3->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst3->seqNum);
                    }
                } 
            } else {
                 DPRINTF(RxuWTB, "instruction [sn:%llu] should not from disq,because of oover 2.\n",inst3->seqNum);
            }
        }
    } else {
        if(inst0){
            if(inst0->isMemRef()){
                for (int num_src = 0; num_src < inst0->numSrcRegs(); num_src++)
                {   
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst0->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst0->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==1 && inst0->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 had read index reg ,do not need read agine  .\n",inst0->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==2 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[16].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==3 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[20].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src >=4){
                        DPRINTF(RxuWTB, "instruction [sn:%llu] should not have four or more regs.\n",inst0->seqNum);
                    }
                } 
            } else {
                for (int num_src = 0; num_src < inst0->numSrcRegs(); num_src++)
                {
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[0].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==1 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[4].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==2 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[8].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==3 && inst0->readySrcIdx(num_src)){
                        vec_read_network_36_18[12].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst0->seqNum);
                    }
                }
            }
        }
        else if(inst1){
            if(inst1->isMemRef()){
                for (int num_src = 0; num_src < inst1->numSrcRegs(); num_src++)
                {
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst1->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst1->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==1 && inst1->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 had read index reg ,do not need read agine  .\n",inst1->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==2 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[18].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==3 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[22].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src >=4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst1->seqNum);
                    }
                } 
            } else {
                for (int num_src = 0; num_src < inst1->numSrcRegs(); num_src++)
                {
                    if(inst1->is_vec_reg[num_src] && num_src==0 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[2].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==1 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[6].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==2 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[10].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src==3 && inst1->readySrcIdx(num_src)){
                        vec_read_network_36_18[14].push_back(inst1);
                    }
                    if(inst1->is_vec_reg[num_src] && num_src >= 4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst1->seqNum);
                    }
                }
            }
        }
        else if(inst2){
            if(inst2->isMemRef()){
                for (int num_src = 0; num_src < inst2->numSrcRegs(); num_src++)
                {
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst2->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst2->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==1 && inst2->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 had read index reg ,do not need read agine  .\n",inst2->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==2 && inst2->readySrcIdx(num_src)){
                        vec_read_network_36_18[24].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==3 && inst2->readySrcIdx(num_src)){
                        vec_read_network_36_18[26].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src >=4 && inst2->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst2->seqNum);
                    }
                } 
            } else {
                 DPRINTF(RxuWTB, "instruction [sn:%llu] should not from disq,because of oover 2.\n",inst2->seqNum);
            }
        }
        else if(inst3){
            if(inst3->isMemRef()){
                for (int num_src = 0; num_src < inst3->numSrcRegs(); num_src++)
                {
                    if(inst0->is_vec_reg[num_src] && num_src==0 && inst3->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 is int reg ,do not need read  vector reg.\n",inst3->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==1 && inst3->readySrcIdx(num_src)){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] src0 had read index reg ,do not need read agine  .\n",inst3->seqNum);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==2 && inst3->readySrcIdx(num_src)){
                        vec_read_network_36_18[28].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src==3 && inst3->readySrcIdx(num_src)){
                        vec_read_network_36_18[30].push_back(inst0);
                    }
                    if(inst0->is_vec_reg[num_src] && num_src >=4){
                        DPRINTF(RxuWTB, "vector instruction [sn:%llu] should not have five or more regs.\n",inst3->seqNum);
                    }
                } 
            } else {
                DPRINTF(RxuWTB, "instruction [sn:%llu] should not from disq,because of oover 2.\n",inst3->seqNum);
            }
        }
    }
}

void
WTB::select_network36to18(){
    for (int i = 0; i < 36; i+2)
    {
        select_network2to1_36(i,i+1);
    }  
}


void 
WTB::select_network2to1_36(int first, int second){
    DynInstPtr inst0 = nullptr;
    DynInstPtr inst1 = nullptr;
    DynInstPtr inst_select = nullptr;
    if(!vec_read_network_36_18[first].empty()){
        inst0 =  vec_read_network_36_18[first].front();
    }

    if(!vec_read_network_36_18[second].empty()){
        inst1 =  vec_read_network_36_18[second].front();
    }
    
    if(inst0 && inst1){
        if(inst0->seqNum < inst1->seqNum){
            inst_select = inst0;
        }
        else {
            inst_select = inst1;
        }
    }
    else if(inst0){
        inst_select = inst0;
    }
    else if(inst1){
        inst_select = inst1;
    }

    if(inst_select){
        if(first == 0){
            vec_read_network_18_9[0].push_back(inst_select);
        }
        else if(first == 2){
            vec_read_network_18_9[1].push_back(inst_select);
        }
        else if(first == 4){
            vec_read_network_18_9[2].push_back(inst_select);
        }
        else if(first == 6){
            vec_read_network_18_9[3].push_back(inst_select);
        }
        else if(first == 8){
            vec_read_network_18_9[4].push_back(inst_select);
        }
        else if(first == 10){
            vec_read_network_18_9[5].push_back(inst_select);
        }
        else if(first == 12){
            vec_read_network_18_9[6].push_back(inst_select);
        }
        else if(first == 14){
            vec_read_network_18_9[7].push_back(inst_select);
        }
        else if(first == 16){
            vec_read_network_18_9[8].push_back(inst_select);
        }
        else if(first == 18){
            vec_read_network_18_9[9].push_back(inst_select);
        }
        else if(first == 20){
            vec_read_network_18_9[10].push_back(inst_select);
        }
        else if(first == 22){
            vec_read_network_18_9[11].push_back(inst_select);
        }
        else if(first == 24){
            vec_read_network_18_9[12].push_back(inst_select);
        }
        else if(first == 26){
            vec_read_network_18_9[13].push_back(inst_select);
        }
        else if(first == 28){
            vec_read_network_18_9[14].push_back(inst_select);
        }
        else if(first == 30){
            vec_read_network_18_9[15].push_back(inst_select);
        }
        else if(first == 32){
            vec_read_network_18_9[16].push_back(inst_select);
        }
        else if(first == 34){
            vec_read_network_18_9[17].push_back(inst_select);
        }
    }
    
};

void
WTB::select_network18to9(){
    for (int i = 0; i < 36; i+2)
    {
        select_network2to1_18(i,i+1);
    }  
}

void 
WTB::select_network2to1_18(int first, int second){
    DynInstPtr inst0 = nullptr;
    DynInstPtr inst1 = nullptr;
    DynInstPtr inst_select = nullptr;
    if(!vec_read_network_18_9[first].empty()){
        inst0 =  vec_read_network_18_9[first].front();
    }

    if(!vec_read_network_18_9[second].empty()){
        inst1 =  vec_read_network_18_9[second].front();
    }
    
    if(inst0 && inst1){
        if(inst0->seqNum < inst1->seqNum){
            inst_select = inst0;
        }
        else {
            inst_select = inst1;
        }
    }
    else if(inst0){
        inst_select = inst0;
    }
    else if(inst1){
        inst_select = inst1;
    }

    if(inst_select){
        if(first == 0){
            vec_read_network_9[0].push_back(inst_select);
        }
        else if(first == 2){
            vec_read_network_9[1].push_back(inst_select);
        }
        else if(first == 4){
            vec_read_network_9[2].push_back(inst_select);
        }
        else if(first == 6){
            vec_read_network_9[3].push_back(inst_select);
        }
        else if(first == 8){
            vec_read_network_9[4].push_back(inst_select);
        }
        else if(first == 10){
            vec_read_network_9[5].push_back(inst_select);
        }
        else if(first == 12){
            vec_read_network_9[6].push_back(inst_select);
        }
        else if(first == 14){
            vec_read_network_9[7].push_back(inst_select);
        }
        else if(first == 16){
            vec_read_network_9[8].push_back(inst_select);
        }
    }

    if(cnt_vector_network_round == 0){
        cnt_vector_network_round = 1;
    } else if(cnt_vector_network_round == 1){
        cnt_vector_network_round = 2;
    } else if(cnt_vector_network_round == 2){
        cnt_vector_network_round = 3;
    } else if(cnt_vector_network_round == 3){
        cnt_vector_network_round = 4;
    } else if(cnt_vector_network_round == 4){
        cnt_vector_network_round = 5;
    } else if(cnt_vector_network_round == 5){
        cnt_vector_network_round = 6;
    } else if(cnt_vector_network_round == 6){
        cnt_vector_network_round = 7;
    } else if(cnt_vector_network_round == 7){
        cnt_vector_network_round = 0;
    }
    
};

void
WTB::read_vectorReg(){
    DynInstPtr inst = nullptr;
    for (int i = 0; i < 8; i++)
    {
        if(!vec_read_network_9[i].empty()){
            inst = vec_read_network_9[i].front();
            for (int num_src = 0; num_src < inst->numSrcs(); num_src++)
            {
                if(inst->readySrcIdx(num_src) && !inst->is_vecReg_read[num_src]){
                    inst->is_vecReg_read[num_src] = true;
                    continue;
                }
            }
            vec_read_network_9[i].erase(vec_read_network_9[i].begin());
        }
    }
    
}

void
WTB::insert(DynInstPtr &new_inst)
{
    // Make sure the instruction is valid
    assert(new_inst);

    new_inst->intoWTBTick = curTick();

    new_inst->arrivewtb = true;

    DPRINTF(RxuWTB, "Adding instruction [sn:%llu] PC %s to the WTB, ibuffer_id = %i.\n",
            new_inst->seqNum, new_inst->pcState(), new_inst->ibuffer_id);
    new_inst->setInWTB();
    // Look through its source registers (physical regs), and mark any
    // dependencies.
    dispipe3Stage->rmu->addToDependents(new_inst);

    for (int i = 0; i < new_inst->numSrcRegs(); i++) {
        if (new_inst->readySrcIdx(i)) {
            new_inst->srcsLocalData[i] = false;
            new_inst->unlocaldata = true;
            new_inst->had_src_data = false;
        }
    }

    if(new_inst->had_src_data){
        if (new_inst->arrive_wtb == -1) {
            new_inst->arrive_wtb = curTick();
        }
    }
    if(need_readR_again){
        need_readR_again = false;
        for (int i = 0; i < 24; i++) {
            int size = ibuffer[0][i].size();
            for (auto it = ibuffer[0][i].begin(); it != ibuffer[0][i].end(); ++it) {
                bool wheOld = (*it)->numSrcRegs() > i ? (*it)->renamedSrcIdx(i)->index() & 1 : false;
                if((*it)->unlocaldata == false && (cpu->ticksToCycles(curTick()-(*it)->arrive_wtb)) > 3 && (*it)->arrive_wtb != -1){
                    (*it)->unlocaldata = true;
                    stats.circleover3++;
                    if((*it)->ibuffer_id == 0 || (*it)->ibuffer_id == 1 || (*it)->ibuffer_id == 4 || (*it)->ibuffer_id == 5){
                        for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                            if ((*it)->src_need_wake[i]) {
                                if(wheOld && intNormalOddRegReadFreeNums > 0){
                                    intNormalOddRegReadFreeNums--;
                                }
                                else if(!wheOld && intNormalEvenRegReadFreeNums > 0){
                                    intNormalEvenRegReadFreeNums--;
                                }
                                else {
                                    DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                    ++stats.intnormal_lockRpork;
                                    stats.circleover3_noRport++;
                                    (*it)->unlocaldata = false;
                                }
                            }
                        }
                    }

                    if((*it)->ibuffer_id == 2 || (*it)->ibuffer_id == 3 || (*it)->ibuffer_id == 20 || (*it)->ibuffer_id == 21){
                        for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                            if ((*it)->src_need_wake[i]) {
                                if(wheOld && intSpecialOddRegReadFreeNums > 0){
                                    intSpecialOddRegReadFreeNums--;
                                }
                                else if(!wheOld && intSpecialEvenRegReadFreeNums > 0){
                                    intSpecialEvenRegReadFreeNums--;
                                }
                                else {
                                    DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                    ++stats.intspecial_lockRpork;
                                    stats.circleover3_noRport++;
                                    (*it)->unlocaldata = false;
                                }
                            }
                        }
                    }

                    if((*it)->ibuffer_id == 8 || (*it)->ibuffer_id == 9 || (*it)->ibuffer_id == 10 || (*it)->ibuffer_id == 13){
                        for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                            if ((*it)->src_need_wake[i]) {
                                if(wheOld && fpNormalRegReadFreeNums > 0){
                                    fpNormalRegReadFreeNums--;
                                }
                                else if(!wheOld && fpNormalRegReadFreeNums > 0){
                                    fpNormalRegReadFreeNums--;
                                }
                                else {
                                    DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                    ++stats.fnormal_lockRpork;
                                    stats.circleover3_noRport++;
                                    (*it)->unlocaldata = false;
                                }
                            }
                        }
                    }

                    if((*it)->ibuffer_id == 22 || (*it)->ibuffer_id == 23){
                        for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                            if ((*it)->src_need_wake[i]) {
                                if(vectorRegReadFreeNums > 0){
                                    vectorRegReadFreeNums--;
                                }
                                else {
                                    DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                    ++stats.fnormal_lockRpork;
                                    stats.circleover3_noRport++;
                                    (*it)->unlocaldata = false;
                                }
                            }
                        }
                    }

                    if((*it)->ibuffer_id == 12 || (*it)->ibuffer_id == 11){
                        for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                            if ((*it)->src_need_wake[i]) {
                                if(wheOld && fpSpecialRegReadFreeNums > 0){
                                    fpSpecialRegReadFreeNums--;  
                                }
                                else if(!(*it) && fpSpecialRegReadFreeNums > 0){
                                    fpSpecialRegReadFreeNums--;
                                }
                                else {
                                    DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                    ++stats.fspecial_lockRpork;
                                    stats.circleover3_noRport++;
                                    (*it)->unlocaldata = false;
                                }
                            }
                        }
                    }

                    if((*it)->ibuffer_id == 7 || (*it)->ibuffer_id == 6 || (*it)->ibuffer_id == 14){
                        if((*it)->isFloating()){
                            for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                                if ((*it)->src_need_wake[i]) {
                                    if(ldstRegReadFreeNums+fstRegReadFreeNums > 0){
                                        if(fstRegReadFreeNums >0){
                                            fstRegReadFreeNums--;
                                        }
                                        else {
                                            ldstRegReadFreeNums--;
                                        }
                                    }
                                    else {
                                        DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                        ++stats.ldst_lockRpork;
                                        stats.circleover3_noRport++;
                                        (*it)->unlocaldata = false;
                                    }
                                }
                            }
                        }
                        else if((*it)->isVector()){
                            for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                                if ((*it)->src_need_wake[i] ) {
                                    if(vectorRegReadFreeNums > 0){
                                       vectorRegReadFreeNums--;
                                    }
                                    else {
                                        DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                        ++stats.ldst_lockRpork;
                                        stats.circleover3_noRport++;
                                        (*it)->unlocaldata = false;
                                    }
                                }
                            }
                        }
                        else {
                            for (int i = 0; i < (*it)->numSrcRegs(); i++) {
                                if ((*it)->src_need_wake[i]) {
                                    if(ldstRegReadFreeNums > 0){
                                        ldstRegReadFreeNums--;
                                    }
                                    else {
                                        DPRINTF(RxuWTB, "inst[sn:%llu] lock Rport the circle arrive 3.\n",(*it)->seqNum);
                                        ++stats.ldst_lockRpork;
                                        stats.circleover3_noRport++;
                                        (*it)->unlocaldata = false;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    

    bool wheOE_dst = new_inst->numDestRegs() > 0 ? new_inst->renamedDestIdx(0)->index() & 1 : false;
    bool wheOE_src = new_inst->numSrcRegs() > 0 ? new_inst->renamedSrcIdx(0)->index() & 1 : false;

    if (!new_inst->isMemRef()){

    }
    else if(new_inst->isStore()){
        new_inst->ibuffer_id = 14;
        ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    }
    else if(ibuffer[new_inst->threadNumber][6].size() >= ibuffer[new_inst->threadNumber][7].size()){
        new_inst->ibuffer_id = 7;
        ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    }
    else {
        new_inst->ibuffer_id = 6;
        ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    }

    if (new_inst->ibuffer_id == 2 || new_inst->ibuffer_id == 3) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numIntSpecialEntries);
    }
    else if (new_inst->ibuffer_id == 20 || new_inst->ibuffer_id == 21) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numWTBEntries);
    }
    else if (new_inst->ibuffer_id == 22 || new_inst->ibuffer_id == 23) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numVectorEntries);
    }
    else if (new_inst->ibuffer_id != 6 && new_inst->ibuffer_id != 7 && new_inst->ibuffer_id != 14 && new_inst->ibuffer_id != 15
    && new_inst->ibuffer_id != 16 && new_inst->ibuffer_id != 17 && new_inst->ibuffer_id != 18 && new_inst->ibuffer_id != 19) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numWTBEntries);
    }
    else {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= LdStEntries);
    }
    //const RegId &src_reg_oe = new_inst->srcRegIdx(0);
    // bool wheOE = new_inst->numSrcRegs() > 0 ? new_inst->renamedSrcIdx(0)->index() & 1 : false;
    if (new_inst->ibuffer_id != 6 && new_inst->ibuffer_id != 7 && new_inst->ibuffer_id != 14 && new_inst->ibuffer_id != 15
    && new_inst->ibuffer_id != 16 && new_inst->ibuffer_id != 17 && new_inst->ibuffer_id != 18 && new_inst->ibuffer_id != 19){
        ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    }
    // // 32 x 2
    // else if(new_inst->isStore()){
    //     new_inst->ibuffer_id = 14;
    //     ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    // }
    // else if(wheOE_dst){
    //     new_inst->ibuffer_id = 7;
    //     ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    // }
    // else {
    //     new_inst->ibuffer_id = 6;
    //     ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    // }
    // // 8 x 8
    // else {
    //     ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);
    // }

    if(new_inst->ibuffer_id == 0 || new_inst->ibuffer_id == 1 || new_inst->ibuffer_id == 4 || new_inst->ibuffer_id == 5){
        for (int i = 0; i < new_inst->numSrcRegs(); i++) {
            if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                if(wheOE_src && intNormalOddRegReadFreeNums > 0){
                    intNormalOddRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else if(!wheOE_src && intNormalEvenRegReadFreeNums > 0){
                    intNormalEvenRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else {
                    new_inst->had_src_data = false;
                    inst_without_rPort.push_back(new_inst);
                    ++stats.intnormal_lockRpork;
                    return;
                }
            }
        }
    }

    if(new_inst->ibuffer_id == 2 || new_inst->ibuffer_id == 3 || new_inst->ibuffer_id == 20 || new_inst->ibuffer_id == 21){
        for (int i = 0; i < new_inst->numSrcRegs(); i++) {
            if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                if(wheOE_src && intSpecialOddRegReadFreeNums > 0){
                    intSpecialOddRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else if(!wheOE_src && intSpecialEvenRegReadFreeNums > 0){
                    intSpecialEvenRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else {
                    new_inst->had_src_data = false;
                    inst_without_rPort.push_back(new_inst);
                    ++stats.intspecial_lockRpork;
                    return;
                }
            }
        }
    }

    if(new_inst->ibuffer_id == 22 || new_inst->ibuffer_id == 23){
        for (int i = 0; i < new_inst->numSrcRegs(); i++) {
            if (new_inst->readySrcIdx(i)) {
                if(vectorRegReadFreeNums > 0){
                    vectorRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else {
                    new_inst->had_src_data = false;
                    inst_without_rPort.push_back(new_inst);
                    ++stats.vector_lockRpork;
                    return;
                }
            }      
        }
    }

    if(new_inst->ibuffer_id == 8 || new_inst->ibuffer_id == 9 || new_inst->ibuffer_id == 10 || new_inst->ibuffer_id == 13){
        for (int i = 0; i < new_inst->numSrcRegs(); i++) {
            if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                if(wheOE_src && fpNormalRegReadFreeNums > 0){
                    fpNormalRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else if(!wheOE_src && fpNormalRegReadFreeNums > 0){
                    fpNormalRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else {
                    new_inst->had_src_data = false;
                    inst_without_rPort.push_back(new_inst);
                    ++stats.fnormal_lockRpork;
                    return;
                }
            }
        }
    }

    if(new_inst->ibuffer_id == 12 || new_inst->ibuffer_id == 11){
        for (int i = 0; i < new_inst->numSrcRegs(); i++) {
            if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                if(wheOE_src && fpSpecialRegReadFreeNums > 0){
                    fpSpecialRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else if(!wheOE_src && fpSpecialRegReadFreeNums > 0){
                    fpSpecialRegReadFreeNums--;
                    new_inst->had_read_RR[i] = true;
                    new_inst->had_src_data = true;
                }
                else {
                    new_inst->had_src_data = false;
                    inst_without_rPort.push_back(new_inst);
                    ++stats.fspecial_lockRpork;
                    return;
                }
            }
        }
    }

    if(new_inst->ibuffer_id == 7 || new_inst->ibuffer_id == 6 || new_inst->ibuffer_id == 14){
        if(new_inst->isFloating()){
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    if(ldstRegReadFreeNums+fstRegReadFreeNums > 0){
                        if(fstRegReadFreeNums >0){
                            fstRegReadFreeNums--;
                        }
                        else {
                            ldstRegReadFreeNums--;
                        }
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                        inst_without_rPort.push_back(new_inst);
                        ++stats.ldst_lockRpork;
                        return;
                    }
                }
            }
        }
        else if(new_inst->isVector()){
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    if(vectorRegReadFreeNums > 0){
                        vectorRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                        inst_without_rPort.push_back(new_inst);
                        ++stats.vector_lockRpork;
                        return;
                    }
                }      
            }
        }
        else {
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    if(ldstRegReadFreeNums > 0){
                        ldstRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                        inst_without_rPort.push_back(new_inst);
                        ++stats.ldst_lockRpork;
                        return;
                    }
                }
            }
        }
    }

    if(new_inst->isStore() && new_inst->unlocaldata){
        readySrc_readReg *readySrcreadReg = new readySrc_readReg(this, new_inst);
        cpu->schedule(readySrcreadReg, cpu->clockEdge(Cycles(1)));
    }
    else if(new_inst->unlocaldata && !new_inst->isStore()){
        readySrc_readReg *readySrcreadReg = new readySrc_readReg(this, new_inst);
        cpu->schedule(readySrcreadReg, cpu->clockEdge(Cycles(1)));
    }
    else{
        // if(new_inst->ifstdataready && new_inst->stdReady == false){
        //     // const RegId &src_reg = new_inst->srcRegIdx(0);
        //     // bool wheOE = src_reg & 1 ? true : false;
        //     //bool wheOE = new_inst->renamedSrcIdx(0)->index() & 1;
        //     bool wheOE = (readySTdata0.size() <=  readySTdata1.size()) ? true : false;
        //     // if(!wheOE){
        //         readySTdata0.push(new_inst);
        //         new_inst->stdReady = true;
        //         DPRINTF(RxuRename,
        //         "[tid:%i] "
        //         "SRC1 Register [sn:%llu] add to readystdata0\n", new_inst->threadNumber, new_inst->seqNum);
        //     //}
        //     // else {
        //     //     readySTdata1.push(new_inst);
        //     //     new_inst->stdReady = true;
        //     //     DPRINTF(RxuRename,
        //     //     "[tid:%i] "
        //     //     "SRC1 Register [sn:%llu] add to readystdata1\n", new_inst->threadNumber, new_inst->seqNum);
        //     // }
        // }

        if (new_inst->isMemRef()) {
            memDepUnit[new_inst->threadNumber].insert(new_inst);
        } else {
            addIfReady(new_inst);
        }
    }

}

void
WTB::insertNonSpec(const DynInstPtr &new_inst)
{
    assert(new_inst);

    nonSpecInsts[new_inst->seqNum] = new_inst;

    DPRINTF(RxuWTB, "Adding non-speculative instruction [sn:%llu] PC %s "
            "to the WTB, ibuffer_id = %i.\n",
            new_inst->seqNum, new_inst->pcState(), new_inst->ibuffer_id);

    if (new_inst->ibuffer_id == 2 || new_inst->ibuffer_id == 3) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numIntSpecialEntries);
    }
    else if (new_inst->ibuffer_id == 20 || new_inst->ibuffer_id == 21) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numWTBEntries);
    }
    else if (new_inst->ibuffer_id == 22 || new_inst->ibuffer_id == 23) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numVectorEntries);
    }
    else if (new_inst->ibuffer_id != 6 && new_inst->ibuffer_id != 7 && new_inst->ibuffer_id != 14 && new_inst->ibuffer_id != 15
    && new_inst->ibuffer_id != 16 && new_inst->ibuffer_id != 17 && new_inst->ibuffer_id != 18 && new_inst->ibuffer_id != 19) {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= numWTBEntries);
    }
    else {
        assert(ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].size() <= LdStEntries);
    }

    ibuffer[new_inst->threadNumber][new_inst->ibuffer_id].push_back(new_inst);

    new_inst->setInWTB();

    // if(new_inst->isStoreConditional() && !new_inst->isSquashed()){
    //     dispipe3Stage->rmu->addToDependents(new_inst);
    // }

    // If it's a memory instruction, add it to the memory dependency
    // unit.
    if (new_inst->isMemRef()) {
        memDepUnit[new_inst->threadNumber].insertNonSpec(new_inst);
    }
}

void
WTB::insertBarrier(const DynInstPtr &barr_inst)
{
    memDepUnit[barr_inst->threadNumber].insertBarrier(barr_inst);

    insertNonSpec(barr_inst);
}

void
WTB::handle_inst_noRport()
{
    DynInstPtr new_inst = nullptr;
    while (!inst_without_rPort.empty()){
        new_inst = inst_without_rPort.front();
        DPRINTF(RxuWTB, "inst seqnum:[sn:%llu]\n",new_inst->seqNum);

        bool wheOE_src = new_inst->numSrcRegs() > 0 ? new_inst->renamedSrcIdx(0)->index() & 1 : false;

        if(new_inst->ibuffer_id == 0 || new_inst->ibuffer_id == 1 || new_inst->ibuffer_id == 4 || new_inst->ibuffer_id == 5){
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                    if(wheOE_src && intNormalOddRegReadFreeNums > 0){
                        intNormalOddRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else if(!wheOE_src && intNormalEvenRegReadFreeNums > 0){
                        intNormalEvenRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                    }
                }
            }
            if(new_inst->had_src_data){
                inst_without_rPort.pop_front();
            }
            else{
                inst_without_rPort.pop_front();
                TSbuffer.push_back(new_inst);
            }
        }

        if(new_inst->ibuffer_id == 2 || new_inst->ibuffer_id == 3 || new_inst->ibuffer_id == 20 || new_inst->ibuffer_id == 21){
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                    if(wheOE_src && intSpecialOddRegReadFreeNums > 0){
                        intSpecialOddRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else if(!wheOE_src && intSpecialEvenRegReadFreeNums > 0){
                        intSpecialEvenRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                    }
                }
            }
            if(new_inst->had_src_data){
                inst_without_rPort.pop_front();
            }
            else{
                inst_without_rPort.pop_front();
                TSbuffer.push_back(new_inst);
            }
        }

        if(new_inst->ibuffer_id == 22 || new_inst->ibuffer_id == 23){
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    if(vectorRegReadFreeNums > 0){
                        vectorRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                    }
                }      
            }
            if(new_inst->had_src_data){
                inst_without_rPort.pop_front();
            }
            else{
                inst_without_rPort.pop_front();
                TSbuffer.push_back(new_inst);
            }
        }

        if(new_inst->ibuffer_id == 8 || new_inst->ibuffer_id == 9 || new_inst->ibuffer_id == 10 || new_inst->ibuffer_id == 13){
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                    if(wheOE_src && fpNormalRegReadFreeNums > 0){
                        fpNormalRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else if(!wheOE_src && fpNormalRegReadFreeNums > 0){
                        fpNormalRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                    }
                }
            }
            if(new_inst->had_src_data){
                inst_without_rPort.pop_front();
            }
            else{
                inst_without_rPort.pop_front();
                TSbuffer.push_back(new_inst);
            }
        }

        if(new_inst->ibuffer_id == 12 || new_inst->ibuffer_id == 11){
            for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i]) {
                    wheOE_src = new_inst->numSrcRegs() > i ? new_inst->renamedSrcIdx(i)->index() & 1 : false;
                    if(wheOE_src && fpSpecialRegReadFreeNums > 0){
                        fpSpecialRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else if(!wheOE_src && fpSpecialRegReadFreeNums > 0){
                        fpSpecialRegReadFreeNums--;
                        new_inst->had_read_RR[i] = true;
                        new_inst->had_src_data = true;
                    }
                    else {
                        new_inst->had_src_data = false;
                    }
                }
            }
            if(new_inst->had_src_data){
                inst_without_rPort.pop_front();
            }
            else{
                inst_without_rPort.pop_front();
                TSbuffer.push_back(new_inst);
            }
        }

        if(new_inst->ibuffer_id == 6 || new_inst->ibuffer_id == 7 || new_inst->ibuffer_id == 14){
            if(new_inst->isFloating()){
                for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                    if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i] ) {
                        if(ldstRegReadFreeNums+fstRegReadFreeNums > 0){
                            if(fstRegReadFreeNums >0){
                                fstRegReadFreeNums--;
                            }
                            else {
                                ldstRegReadFreeNums--;
                            }
                            new_inst->had_read_RR[i] = true;
                            new_inst->had_src_data = true;
                        }
                        else {
                            new_inst->had_src_data = false;
                        }
                    }
                }
            }
            else if(new_inst->isVector()){
                for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                    if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i] ) {
                        if(vectorRegReadFreeNums > 0){
                            vectorRegReadFreeNums--;
                            new_inst->had_read_RR[i] = true;
                            new_inst->had_src_data = true;
                        }
                        else {
                            new_inst->had_src_data = false;
                        }
                    }
                }
            } else {
                for (int i = 0; i < new_inst->numSrcRegs(); i++) {
                    if (new_inst->readySrcIdx(i) && !new_inst->had_read_RR[i] ) {
                        if(ldstRegReadFreeNums > 0){
                            ldstRegReadFreeNums--;
                            new_inst->had_read_RR[i] = true;
                            new_inst->had_src_data = true;
                        }
                        else {
                            new_inst->had_src_data = false;
                        }
                    }
                }
            }

            if(new_inst->had_src_data){
                inst_without_rPort.pop_front();
            }
            else{
                inst_without_rPort.pop_front();
                TSbuffer.push_back(new_inst);
            }
        }

        if(new_inst->had_src_data){
            readySrc_readReg *readySrcreadReg = new readySrc_readReg(this, new_inst);
            cpu->schedule(readySrcreadReg, cpu->clockEdge(Cycles(1)));
        }
    }
    for (auto it = TSbuffer.begin(); it != TSbuffer.end(); ++it) {
        inst_without_rPort.push_back(*it);
    }
    TSbuffer.clear();
}

void
WTB::scheduleReadyInsts()
{
    DPRINTF(RxuWTB, "Attempting to issue ready instructions from "
            "the WTB.\n");

    DynInstPtr mem_inst;
    while ((mem_inst = getDeferredMemInstToExecute())) {
        mem_inst->tlb_wait++;
        addReadyMemInst(mem_inst);
    }

    // See if any cache blocked instructions are able to be executed
    while ((mem_inst = getBlockedMemInstToExecute())) {
        mem_inst->cache_wait++;
        addReadyMemInst(mem_inst);
    }

    while ((mem_inst = getSCtoExecute())) {
        addReadyMemInst(mem_inst);
    }

    auto it_ld = ld_needwait.begin();
    while (it_ld != ld_needwait.end()) {

        if ((*it_ld)->seqNum < memDepUnit[0].stq.begin()->first) {
            ibufferToEW.push((*it_ld));
            it_ld = ld_needwait.erase(it_ld);
            DPRINTF(RxuWTB, "inst seqnum:[sn:%i] to insttoew \n",(*it_ld)->seqNum);
        } else {
            ++it_ld;
        }
    }

    // while(!ibufferToEW.empty()) {
    //     ibufferToEW.pop();
    // }

    // if (curTick() >= 1926500) {
    //     for (int i = 0; i < ibuffer[0][6].size(); i++) {
    //         DynInstPtr inst = ibuffer[0][6].front();
    //         ibuffer[0][6].pop_front();
    //         DPRINTF(RxuWTB, "ibuffer[6]: idx: %i   [sn:%llu]\n", i, inst->seqNum);
    //         ibuffer[0][6].push_back(inst);
    //     }
    // }

    // if (curTick() >= 1926500) {
    //     for (int i = 0; i < ibuffer[0][7].size(); i++) {
    //         DynInstPtr inst = ibuffer[0][7].front();
    //         ibuffer[0][7].pop_front();
    //         DPRINTF(RxuWTB, "ibuffer[7]: idx: %i   [sn:%llu]\n", i, inst->seqNum);
    //         ibuffer[0][7].push_back(inst);
    //     }
    // }

    bool hasReadyInsts0 = false;
    bool hasReadyInsts1 = false;
    bool hasReadyInsts3 = false;
    bool hasReadyInsts2 = false;
    bool hasReadyInsts4 = false;
    bool hasReadyInsts5 = false;

    bool issueInst0 = false;
    bool issueInst1 = false;
    bool issueInst3 = false;
    bool issueInst2 = false;
    bool issueInst4 = false;
    bool issueInst5 = false;

    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13
    for (int i = 0; i < 24; i++) {
        for (int j = 0; j < 2; j++) {
            while (!readyInsts[i][j].empty() && readyInsts[i][j].top()->isSquashed()) {
                readyInsts[i][j].pop();
            }
            DPRINTF(RxuWTB, "ibuffer[%i][%i] has %i ready instructions.\n", i, j, readyInsts[i][j].size());
        }
        switch (i)
        {
        case 0:
            if (!readyInsts[i][0].empty() || !readyInsts[i][1].empty()) {
                hasReadyInsts0 = true;
            }
            break;
        case 1:
            if (!readyInsts[i][0].empty() || !readyInsts[i][1].empty()) {
                hasReadyInsts1 = true;
            }
            break;
        case 3:
            if (!readyInsts[i][0].empty() || !readyInsts[i][1].empty()) {
                hasReadyInsts3 = true;
            }
            break;
        case 2:
            if (!readyInsts[i][0].empty() || !readyInsts[i][1].empty()) {
                hasReadyInsts2 = true;
            }
            break;
        case 4:
            if (!readyInsts[i][0].empty() || !readyInsts[i][1].empty()) {
                hasReadyInsts4 = true;
            }
            break;
        case 5:
            if (!readyInsts[i][0].empty() || !readyInsts[i][1].empty()) {
                hasReadyInsts5 = true;
            }
            break;
        default:
            break;
        }
        DPRINTF(RxuWTB, "ibuffer[%i] has %i instructions.\n", i, ibuffer[0][i].size());
            }

    while (!readyVecInst0.empty() && readyVecInst0.top()->isSquashed()) {
        readyVecInst0.pop();
    }

    while (!readyVecInst1.empty() && readyVecInst1.top()->isSquashed()) {
        readyVecInst1.pop();
    }

    while (!readySTdata0.empty() && readySTdata0.top()->isSquashed()) {
        readySTdata0.pop();
    }

    while (!readySTdata1.empty() && readySTdata1.top()->isSquashed()) {
        readySTdata1.pop();
    }

    while (!readyLDSTodd0.empty() && readyLDSTodd0.top()->isSquashed()) {
        readyLDSTodd0.pop();
    }

    while (!sc_queue.empty() && sc_queue.top()->isSquashed()) {
        sc_queue.pop();
    }

    while (!readyLD_Dst_even.empty() && readyLD_Dst_even.top()->isSquashed()) {
        readyLD_Dst_even.pop();
    }

    while (!readyLD_Dst_odd.empty() && readyLD_Dst_odd.top()->isSquashed()) {
        readyLD_Dst_odd.pop();
    }

    while (!readyStore.empty() && readyStore.top()->isSquashed()) {
        readyStore.pop();
    }

    // while (!readyLDSTodd1.empty() && readyLDSTodd1.top()->isSquashed()) {
    //     readyLDSTodd1.pop();
    // }

    while (!readyLDSTeven0.empty() && readyLDSTeven0.top()->isSquashed()) {
        readyLDSTeven0.pop();
    }

    // while (!readyLDSTeven1.empty() && readyLDSTeven1.top()->isSquashed()) {
    //     readyLDSTeven1.pop();
    // }

    // DPRINTF(RxuWTB, "debug1.\n");
    // intNormal A
    selectTwo(0, 4);
    arbiterInt();

    // intNormal B
    selectTwo(1, 5);
    arbiterInt();

    selectVec();
    arbiterVec();
    
    // Branch and Special
    arbiterIntSpecialAndBranch();

    // // intSpecial
    // selectTwo(2, 3);
    // arbiterInt();

    // DPRINTF(RxuWTB, "debug2.\n");
    // fpNormal A
    selectOne(8, 13);

    // fpNormal B
    selectOne(9, 10);

    // fpSpecial
    arbiterFloat();

    // ldWait
    while (!ldWait.empty()) {
        auto ldqit = ldWait.front();
        // if (ldstRegReadFreeNums >= std::get<1>(ldqit)->numSrcRegs()) {
        //     ldstRegReadFreeNums -= std::get<1>(ldqit)->numSrcRegs();
            std::get<1>(ldqit)->ststdstat = 1;
            ibufferToEW.push(std::get<1>(ldqit));
            memDepUnit[0].ldq.erase(std::get<0>(ldqit));
            ldWait.pop_front();
        // } else {
        //     break;
        // }
    }

    // DPRINTF(RxuWTB, "debug3.\n");
    // load/store
    //selectLdSt();
    selectLdSt_dst();

    // DPRINTF(RxuWTB, "debug4.\n");
    DynInstPtr issuing_inst;
    int processed_insts = 0;
    int toEwIdx = 0;

    while (!ibufferToEW.empty()) {

        issuing_inst = ibufferToEW.top();
        ibufferToEW.pop();

        // csrFence
        if (issuing_inst->isSerializeBefore()) {
            if (cpu->isOldestInstInPipe(issuing_inst)) {
                DPRINTF(RxuWTB, "Sending the oldest csr Read instructions to execute.\n");
                DPRINTF(RxuWTB, "Instruction [sn:%i] is: %s\n", issuing_inst->seqNum,
                        issuing_inst->staticInst->disassemble(issuing_inst->pc->instAddr()));
            } else {
                DPRINTF(RxuWTB, "Waiting older instructions to commit.\n");
                DPRINTF(RxuWTB, "Csr read instruction [sn:%i] is: %s\n", issuing_inst->seqNum,
                        issuing_inst->staticInst->disassemble(issuing_inst->pc->instAddr()));

                ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                    if (issuing_inst->isControl()) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else {
                        readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                    }
                } else {
                    if (issuing_inst->odd_inst) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else {
                        readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                    }
                }
                continue;
            }
        }
        // DPRINTF(RxuWTB, "debug5.\n");

        if (issuing_inst->opClass() == 3 && pipelineUseNums.intDivNums <= 0) {               // indiv pipeline
            stats.statFuBusy[0]++;
            stats.fuBusyCycles++;
            DPRINTF(RxuWTB, "instruction PC %s can't issue due to lack of int div FU, "
                "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                issuing_inst->seqNum, issuing_inst->ibuffer_id);

            ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

            if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
            } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                if (issuing_inst->isControl()) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else {
                    readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                }
            } else {
                if (issuing_inst->odd_inst) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else {
                    readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                }
            }
            continue;
        } else if (issuing_inst->opClass() == 9 && pipelineUseNums.fpDivNums <= 0) {            // fpdiv pipeline
            stats.statFuBusy[1]++;
            stats.fuBusyCycles++;
            DPRINTF(RxuWTB, "instruction PC %s can't issue due to lack of float div FU, "
                "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                issuing_inst->seqNum, issuing_inst->ibuffer_id);

            ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

            if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
            } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                if (issuing_inst->isControl()) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else {
                    readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                }
            } else {
                if (issuing_inst->odd_inst) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else {
                    readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                }
            }
            continue;
        } else if (issuing_inst->opClass() == 11 && pipelineUseNums.fpDivNums <= 0) {         // fpsqrt pipeline
            stats.statFuBusy[1]++;
            stats.fuBusyCycles++;
            DPRINTF(RxuWTB, "instruction PC %s can't issue due to lack of float sqrt FU, "
                "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                issuing_inst->seqNum, issuing_inst->ibuffer_id);

            ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

            if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
            } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                if (issuing_inst->isControl()) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else {
                    readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                }
            } else {
                if (issuing_inst->odd_inst) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else {
                    readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                }
            }
            continue;
        }
        // DPRINTF(RxuWTB, "debug6.\n");
        // Check the write port of fpSpecial
        if ((issuing_inst->ibuffer_id == 11 || issuing_inst->ibuffer_id == 12) &&
            (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())) {
            if (issuing_inst->destRegIdx(0).classValue() == IntRegClass) {                // fp2int
                issuing_inst->iid = issuing_inst->seqNum;
                if (issuing_inst->odd_inst) {
                    if (wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intSpecialOddWbNums >= 1) {
                        if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                            wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intSpecialOddWbNums--;
                    } else {
                    ++stats.intRegWriteBusy;
                    DPRINTF(RxuWTB, "fp2int PC %s can't issue due to lack of intSpecialOdd RegWritePort, "
                        "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                        issuing_inst->seqNum, issuing_inst->ibuffer_id);

                    ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                    if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                        if (issuing_inst->isControl()) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    } else {
                        if (issuing_inst->odd_inst) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    }
                    continue;
                    }
                } else {
                    if (wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intSpecialEvenWbNums >= 1) {
                        if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                            wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intSpecialEvenWbNums--;
                    } else {
                    ++stats.intRegWriteBusy;
                    DPRINTF(RxuWTB, "fp2int PC %s can't issue due to lack of intSpecialEven RegWritePort, "
                        "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                        issuing_inst->seqNum, issuing_inst->ibuffer_id);

                    ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                    if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                        if (issuing_inst->isControl()) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    } else {
                        if (issuing_inst->odd_inst) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    }
                    continue;
                    }
                }
            } else {
                unsigned delay;
                if (issuing_inst->opClass() == 9 || issuing_inst->opClass() == 11) {
                    delay = initFdivDelay + 1;
                    DPRINTF(RxuWTB, "fDiv/fSqrt PC %s encountered, "
                        "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                        issuing_inst->seqNum, issuing_inst->ibuffer_id);
                } else {
                    delay = cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1;
                }
                if (wbNums.at(delay).fpWbNums >= 1) {
                    if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                        wbNums.at(delay).fpWbNums--;
                } else {
                    ++stats.fpRegWriteBusy;
                    DPRINTF(RxuWTB, "fpSpecial PC %s can't issue due to lack of float RegWritePort, "
                        "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                        issuing_inst->seqNum, issuing_inst->ibuffer_id);

                    ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                    if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                        if (issuing_inst->isControl()) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    } else {
                        if (issuing_inst->odd_inst) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    }
                    continue;
                }
            }
        // DPRINTF(RxuWTB, "debug7.\n");
        // Check the write port of intSpecial
        } else if ((issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) &&
            (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())) {
            if (issuing_inst->destRegIdx(0).classValue() == FloatRegClass) {                // int2fp
                issuing_inst->iid = issuing_inst->seqNum;
                if (wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).fldWbNums >= 1) {
                    if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                        wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).fldWbNums--;
                } else {
                ++stats.fpRegWriteBusy;
                DPRINTF(RxuWTB, "int2fp PC %s can't issue due to lack of float RegWritePort, "
                    "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                    issuing_inst->seqNum, issuing_inst->ibuffer_id);

                ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                    if (issuing_inst->isControl()) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else {
                        readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                    }
                } else {
                    if (issuing_inst->odd_inst) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else {
                        readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                    }
                }
                continue;
                }
            } else {
                unsigned delay;
                if (issuing_inst->opClass() == 3) {
                    delay = initFdivDelay + 1;
                    DPRINTF(RxuWTB, "intDiv PC %s encountered, "
                        "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                        issuing_inst->seqNum, issuing_inst->ibuffer_id);
                } else {
                    delay = cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1;
                }
                if (issuing_inst->odd_inst) {
                    if (wbNums.at(delay).intSpecialOddWbNums >= 1) {
                        if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                            wbNums.at(delay).intSpecialOddWbNums--;
                    } else {
                        ++stats.intRegWriteBusy;
                        DPRINTF(RxuWTB, "intSpecial PC %s can't issue due to lack of int odd RegWritePort, "
                            "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                            issuing_inst->seqNum, issuing_inst->ibuffer_id);

                        ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                        if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                            if (issuing_inst->isControl()) {
                                readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                            } else {
                                readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                            }
                        } else {
                            if (issuing_inst->odd_inst) {
                                readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                            } else {
                                readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                            }
                        }
                        continue;
                    }
                } else {
                    if (wbNums.at(delay).intSpecialEvenWbNums >= 1) {
                        if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                            wbNums.at(delay).intSpecialEvenWbNums--;
                    } else {
                        ++stats.intRegWriteBusy;
                        DPRINTF(RxuWTB, "intSpecial PC %s can't issue due to lack of int even RegWritePort, "
                            "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                            issuing_inst->seqNum, issuing_inst->ibuffer_id);

                        ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                        if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                            if (issuing_inst->isControl()) {
                                readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                            } else {
                                readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                            }
                        } else {
                            if (issuing_inst->odd_inst) {
                                readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                            } else {
                                readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                            }
                        }
                        continue;
                    }
                }
            }
        // DPRINTF(RxuWTB, "debug8.\n");
        // Check the write port of intNormal
        } else if ((issuing_inst->ibuffer_id == 0 || issuing_inst->ibuffer_id == 4
                || issuing_inst->ibuffer_id == 1 || issuing_inst->ibuffer_id == 5) &&
                (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())) {
            if (issuing_inst->odd_inst) {
                if (wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intNormalOddWbNums >= 1) {
                    if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                        wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intNormalOddWbNums--;
                } else {
                    ++stats.intRegWriteBusy;
                    DPRINTF(RxuWTB, "intNormal PC %s can't issue due to lack of int odd RegWritePort, "
                        "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                        issuing_inst->seqNum, issuing_inst->ibuffer_id);

                    ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                    if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                        if (issuing_inst->isControl()) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    } else {
                        if (issuing_inst->odd_inst) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    }
                    continue;
                }
            } else {
                if (wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intNormalEvenWbNums >= 1) {
                    if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                        wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).intNormalEvenWbNums--;
                } else {
                    ++stats.intRegWriteBusy;
                    DPRINTF(RxuWTB, "intSpecial PC %s can't issue due to lack of int even RegWritePort, "
                        "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                        issuing_inst->seqNum, issuing_inst->ibuffer_id);

                    ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                    if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                        if (issuing_inst->isControl()) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    } else {
                        if (issuing_inst->odd_inst) {
                            readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                        } else {
                            readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                        }
                    }
                    continue;
                }
            }
        // DPRINTF(RxuWTB, "debug9.\n");
        // Check the write port of fpNormal
        } else if ((issuing_inst->ibuffer_id == 8 || issuing_inst->ibuffer_id == 13
                || issuing_inst->ibuffer_id == 9 || issuing_inst->ibuffer_id == 10) &&
                (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())) {
            if (wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).fpWbNums >= 1) {
                if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                    wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).fpWbNums--;
            } else {
                ++stats.fpRegWriteBusy;
                DPRINTF(RxuWTB, "fpNormal PC %s can't issue due to lack of float RegWritePort, "
                    "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                    issuing_inst->seqNum, issuing_inst->ibuffer_id);

                ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                if (issuing_inst->ibuffer_id == 20 || issuing_inst->ibuffer_id == 21) {
                    readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                } else if (issuing_inst->ibuffer_id == 2 || issuing_inst->ibuffer_id == 3) {
                    if (issuing_inst->isControl()) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else {
                        readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                    }
                } else {
                    if (issuing_inst->odd_inst) {
                        readyInsts[issuing_inst->ibuffer_id][0].push(issuing_inst);
                    } else {
                        readyInsts[issuing_inst->ibuffer_id][1].push(issuing_inst);
                    }
                }
                continue;
            }
        }
        else if((issuing_inst->ibuffer_id == 22 || issuing_inst->ibuffer_id == 23) && 
        (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())) {
            if (wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).vecttorWbNums >= 1) {
                if (issuing_inst->numDestRegs() > 0 && !issuing_inst->destRegIdx(0).isZeroReg())
                    wbNums.at(cpu->ew.fuPool->getOpLatency(issuing_inst->opClass()) + 1).vecttorWbNums--;
            } else {
                ++stats.vectorRegWriteBusy;
                DPRINTF(RxuWTB, "fpNormal PC %s can't issue due to lack of vector RegWritePort, "
                    "[sn:%llu], ibuffer_id = %i\n", issuing_inst->pcState(),
                    issuing_inst->seqNum, issuing_inst->ibuffer_id);

                // ibuffer[issuing_inst->threadNumber][issuing_inst->ibuffer_id].push_back(issuing_inst);

                // if (ibuffer[issuing_inst->threadNumber][22].size() <= ibuffer[issuing_inst->threadNumber][23].size()) {
                //     readyVecInst0.push(issuing_inst);
                // } else {
                //     readyVecInst1.push(issuing_inst);
                // }
                // continue;
            }
        }
        // DPRINTF(RxuWTB, "debug10.\n");

        // inDiv/fpDiv/fpSqrt occupying pipelines
        if (issuing_inst->opClass() == 3) {

            DPRINTF(RxuWTB, "instruction PC %s [sn:%llu] will use a int div pipeline 7 cycles, "
            "ibuffer_id = %i\n", issuing_inst->pcState(),
                issuing_inst->seqNum, issuing_inst->ibuffer_id);

            pipelineUseNums.intDivNums--;
            intDivCompletion *intDivUse = new intDivCompletion(this);
            cpu->schedule(intDivUse, cpu->clockEdge(Cycles(initFdivDelay)));

        } else if (issuing_inst->opClass() == 9) {

            DPRINTF(RxuWTB, "instruction PC %s [sn:%llu] will use a float div pipeline 7 cycles, "
            "ibuffer_id = %i\n", issuing_inst->pcState(),
                issuing_inst->seqNum, issuing_inst->ibuffer_id);

            pipelineUseNums.fpDivNums--;
            fpDivCompletion *fpDivUse = new fpDivCompletion(this);
            cpu->schedule(fpDivUse, cpu->clockEdge(Cycles(initFdivDelay)));

        } else if (issuing_inst->opClass() == 11) {

            DPRINTF(RxuWTB, "instruction PC %s [sn:%llu] will use a float sqrt pipeline 7 cycles, "
            "ibuffer_id = %i\n", issuing_inst->pcState(),
                issuing_inst->seqNum, issuing_inst->ibuffer_id);

            // pipelineUseNums.fpSqrtNums--;
            // fpSqrtCompletion *fpSqrtUse = new fpSqrtCompletion(this);
            // cpu->schedule(fpSqrtUse, cpu->clockEdge(Cycles(7)));
            pipelineUseNums.fpDivNums--;
            fpDivCompletion *fpDivUse = new fpDivCompletion(this);
            cpu->schedule(fpDivUse, cpu->clockEdge(Cycles(initFdivDelay)));

        }
        // DPRINTF(RxuWTB, "debug11.\n");

        DPRINTF(RxuWTB, "Thread 0: Issuing instruction PC %s "
        "[sn:%llu], ibuffer_id = %i, compressed: %d\n", issuing_inst->pcState(),
        issuing_inst->seqNum, issuing_inst->ibuffer_id, issuing_inst->pcState().as<RiscvISA::PCState>().compressed());

        issuing_inst->clearInWTB();

        if (issuing_inst->isMemRef() && issuing_inst->staIssued) {
            memDepUnit[0].issue(issuing_inst);
        }
        if(issuing_inst->isStore() && issuing_inst->staIssued && issuing_inst->ifstdready && issuing_inst->inst_to_ew
            && !issuing_inst->if_exe_second){
            DPRINTF(RxuWTB, "Thread 0: Issuing instruction PC %s "
            "[sn:%llu], ibuffer_id = %i had been issued,do not need issue second\n", issuing_inst->pcState(),
            issuing_inst->seqNum, issuing_inst->ibuffer_id);
        }
        else if(issuing_inst->staIssued || !issuing_inst->isStore()){
            Cycles op_latency;
            if (issuing_inst->opClass() == 3 || issuing_inst->opClass() == 9 || issuing_inst->opClass() == 11) {
                op_latency = Cycles(initFdivDelay);
            } else {
                op_latency = cpu->ew.fuPool->getOpLatency(issuing_inst->opClass());
            }

            if (issuing_inst->ibuffer_id != 6 && issuing_inst->ibuffer_id != 7 && issuing_inst->ibuffer_id != 14 && issuing_inst->ibuffer_id != 15 &&
                issuing_inst->ibuffer_id != 16 && issuing_inst->ibuffer_id != 17 && issuing_inst->ibuffer_id != 18 && issuing_inst->ibuffer_id != 19) {
                dispipe3Stage->toEw->insts[toEwIdx] = issuing_inst;
                switch (issuing_inst->ibuffer_id)
                {
                case 0:
                    issueInst0 = true;
                    break;
                case 1:
                    issueInst1 = true;
                    break;
                case 3:
                    issueInst3 = true;
                    break;
                case 2:
                    issueInst2 = true;
                    break;
                case 4:
                    issueInst4 = true;
                    break;
                case 5:
                    issueInst5 = true;
                    break;
                default:
                    break;
                }
                issuing_inst->inst_to_ew = true;
                ++(dispipe3Stage->toEw->size);
                ++toEwIdx;

                if (op_latency == 1) {
                    processEarlyWakeUp(issuing_inst);
                } else {
                    earlyWakeUp *wakeup = new earlyWakeUp(this, issuing_inst);
                    cpu->schedule(wakeup, cpu->clockEdge(Cycles(op_latency - 1)));
                }
            } else {
                if(issuing_inst->stdNeedExeScd_issue && issuing_inst->num_exe ==1){
                    LdstWakeToIssue *wakeToIssue = new LdstWakeToIssue(this, issuing_inst);
                    cpu->schedule(wakeToIssue, cpu->clockEdge(Cycles(1)));
                }
                else{
                    LdstWakeToIssue *wakeToIssue = new LdstWakeToIssue(this, issuing_inst);
                    cpu->schedule(wakeToIssue, cpu->clockEdge(Cycles(1)));
                }
            }


            processed_insts++;
            ++stats.instsIssued;

            if (issuing_inst->intoReadyInstsTick != -1 && issuing_inst->intoWTBTick != -1)
                DPRINTF(RxuWTB, "[sn:%i] from into WTB to into ReadyInsts spended %i cycles.\n",
                        issuing_inst->seqNum, cpu->ticksToCycles(issuing_inst->intoReadyInstsTick - issuing_inst->intoWTBTick));


            if (issuing_inst->staticInst->isBranch()) {
                ++stats.branchInstsIssued;
            }
            if (issuing_inst->isInteger()) {
                ++stats.intInstsIssued;
                if (issuing_inst->isIntNormal()) {
                    ++stats.intNorInstsIssued;
                } else {
                    ++stats.intSpeInstsIssued;
                }
            } else if (issuing_inst->ibuffer_id == 6 || issuing_inst->ibuffer_id == 7
                    || issuing_inst->ibuffer_id == 14 || issuing_inst->ibuffer_id == 15 ||
                    issuing_inst->ibuffer_id == 16 || issuing_inst->ibuffer_id == 17
                    || issuing_inst->ibuffer_id == 18 || issuing_inst->ibuffer_id == 19) {
                ++stats.memInstsIssued;
                if (issuing_inst->isLoad()) {
                    ++stats.loadInstsIssued;
                } else if (issuing_inst->isStore()) {
                    ++stats.storeInstsIssued;
                }
            } else if (issuing_inst->isFloating()) {
                ++stats.fpInstsIssued;
                if (issuing_inst->isIntNormal()) {
                    ++stats.fpNorInstsIssued;
                } else {
                    ++stats.fpSpeInstsIssued;
                }
            }
        }
        if (issuing_inst->issued_tick == -1) {
            issuing_inst->issued_tick = curTick();
        }
        stats.circle_inwtb.sample(cpu->ticksToCycles(
                    issuing_inst->issued_tick - issuing_inst->arrive_wtb));

    }

    if (issueInst2) {
        if (!hasReadyInsts4 && !hasReadyInsts5) {
            if (readyInsts[2][0].size() >= 2) {
                stats.ibuffer2Out.sample(3);
            }
            else if (readyInsts[2][0].size() == 1) {
                stats.ibuffer2Out.sample(2);
            }
        }
        else if (!hasReadyInsts4 || !hasReadyInsts5) {
            if (readyInsts[2][0].size() >= 1) {
                stats.ibuffer2Out.sample(2);
            }
        }
        else {
            stats.ibuffer2Out.sample(1);
        }
    } else {
        stats.ibuffer2Out.sample(0);
    }

    if (issueInst3) {
        if (!hasReadyInsts0 && !hasReadyInsts1) {
            if (readyInsts[3][1].size() >= 2) {
                stats.ibuffer3Out.sample(3);
            }
            else if (readyInsts[3][1].size() == 1) {
                stats.ibuffer3Out.sample(2);
            }
        }
        else if (!hasReadyInsts0 || !hasReadyInsts1) {
            if (readyInsts[3][1].size() >= 1) {
                stats.ibuffer3Out.sample(2);
            }
        }
        else {
            stats.ibuffer3Out.sample(1);
        }
    } else {
        stats.ibuffer3Out.sample(0);
    }

    // DPRINTF(RxuWTB, "debug12.\n");
    int fldWbNums = wbNums.at(3).fpWbNums + wbNums.at(3).fldWbNums;

    while (fldWbNums--) {
        if (!fldFIFO.empty()) {
            DynInstPtr fld = fldFIFO.front();
            fldFIFO.pop_front();
            if (fld->isSquashed()) {
                continue;
            }
            DPRINTF(RxuWTB, "fld wake up.\n");
            DPRINTF(RxuWTB, "Instruction [sn:%i] is: %s.\n", fld->seqNum,
                    fld->staticInst->disassemble(fld->pc->instAddr()));

            dispipe3Stage->rmu->wakeDependents(fld);

            for (int i = 0; i < fld->numDestRegs(); i++) {
                // Mark register as ready if not pinned
                if (fld->renamedDestIdx(i)->
                        getNumPinnedWritesToComplete() == 0) {
                    DPRINTF(RxuWTB,"Setting Destination Register %i (%s)\n",
                            fld->renamedDestIdx(i)->index(),
                            fld->renamedDestIdx(i)->className());
                    cpu->scoreboard.setReg(fld->renamedDestIdx(i));
                }
            }
        }
    }

    // DPRINTF(RxuWTB, "debug13.\n");

    if (processed_insts || !retryMemInsts.empty() || !deferredMemInsts.empty()) {
        cpu->activityThisCycle();
    } else {
        DPRINTF(RxuWTB, "Not able to schedule any instructions.\n");
    }
    // for (int tid = 0; tid < MaxThreads; tid++) {
    for (int tid = 0; tid < 1; tid++) {
        for (int i = 0; i < 24; i++) {
            if(i == 22 || i == 23){
                DPRINTF(RxuWTB, "Thread %i: ibuffer[%i] freeEntries = %i after sent.\n",
                tid, i, numVectorEntries - ibuffer[tid][i].size());
            }
            else if (i != 6 && i != 7 && i != 14 && i != 15 && i != 16 && i != 17 && i != 18 && i != 19) {
                DPRINTF(RxuWTB, "Thread %i: ibuffer[%i] freeEntries = %i after sent.\n",
                tid, i, numWTBEntries - ibuffer[tid][i].size());
            } 
            else {
                DPRINTF(RxuWTB, "Thread %i: ibuffer[%i] freeEntries = %i after sent.\n",
                tid, i, LdStEntries - ibuffer[tid][i].size());
            }

        }
    }

    // for (auto it = ibuffer[0][5].begin(); it != ibuffer[0][5].end(); ++it) {
    //     DPRINTF(RxuWTB, "[sn:%llu] in ibuffer[5].\n", (*it)->seqNum);
    // }
}

void
WTB::scheduleNonSpec(const InstSeqNum &inst)
{
    DPRINTF(RxuWTB, "Marking nonspeculative instruction [sn:%llu] as ready "
            "to execute.\n", inst);

    NonSpecMapIt inst_it = nonSpecInsts.find(inst);

    assert(inst_it != nonSpecInsts.end());

    ThreadID tid = (*inst_it).second->threadNumber;

    (*inst_it).second->setAtCommit();

    (*inst_it).second->setCanIssue();

    if (!(*inst_it).second->isMemRef()) {
        addIfReady((*inst_it).second);
    } else {
        memDepUnit[tid].nonSpecInstReady((*inst_it).second);
    }

    (*inst_it).second = NULL;

    nonSpecInsts.erase(inst_it);
}

void
WTB::addReadyMemInst(const DynInstPtr &inst)
{
    DPRINTF(RxuWTB, "MemRef Instruction is ready to issue, putting it onto "
            "the ready list, PC %s [sn:%llu], ibuffer_id = %i.\n",
            inst->pcState(), inst->seqNum, inst->ibuffer_id);

    // if(inst->isStore()){
    //     if(inst->ifstdataready){
    //         if(readySTdata0.size() <= readySTdata1.size()){
    //             readySTdata0.push(inst);
    //         }
    //         else {
    //             readySTdata1.push(inst);
    //         }
    //     }
    // }
    // if (inst->ibuffer_id == 6 || inst->ibuffer_id == 14 || inst->ibuffer_id == 16 || inst->ibuffer_id == 18) {
    //     const RegId &src_reg = inst->srcRegIdx(0);
    //     inst->odd_inst = src_reg & 1 ? true : false;
    //     if(inst->odd_inst){
    //         readyLDSTeven0.push(inst);
    //         DPRINTF(RxuWTB, "add instruction to LDSTeven0, PC %s [sn:%llu], ibuffer_id = %i.,1003\n",
    //         inst->pcState(), inst->seqNum, inst->ibuffer_id);
    //     }
    //     else {
    //         DPRINTF(RxuWTB, "add instruction to LDSTeven1, PC %s [sn:%llu], ibuffer_id = %i.,1007\n",
    //         inst->pcState(), inst->seqNum, inst->ibuffer_id);
    //          readyLDSTeven1.push(inst);
    //     }
    //     return;
    // }

    if (inst->ibuffer_id == 7 || inst->ibuffer_id == 15 || inst->ibuffer_id == 17 || inst->ibuffer_id == 19 ||
        inst->ibuffer_id == 6 || inst->ibuffer_id == 14 || inst->ibuffer_id == 16 || inst->ibuffer_id == 18) {
        // const RegId &src_reg = inst->srcRegIdx(0);
        // inst->odd_inst = src_reg & 1 ? true : false;

        // if(inst->odd_inst){
        //bool wheOE = inst->renamedSrcIdx(0)->index() & 1;
        if(inst->isStoreConditional() && inst->seqNum != cpu->instList.front()->seqNum){
            sc_queue.push(inst);
            return;
        }

        // if(inst->strictlyOrdered() && inst->lqIdx != dispipe3Stage->ldstQueue.thread.at){
        //     sc_queue.push(inst);
        //     return;
        // }
        //bool wheOE = inst->numSrcRegs() > 0 ? inst->renamedSrcIdx(0)->index() & 1 : false;
        bool wheOE = inst->numDestRegs() > 0 ? inst->renamedDestIdx(0)->index() & 1 : false;
        if (inst->arrive_wtb == -1) {
            inst->arrive_wtb = curTick();
        }
        // if(wheOE){
        //     readyLDSTodd0.push(inst);
        //     DPRINTF(RxuWTB, "size 0f readyldstodd0 :%i\n", readyLDSTodd0.size());
        // }
        // else {
        //     readyLDSTeven0.push(inst);
        //     DPRINTF(RxuWTB, "size 0f readyldsteven0 :%i\n", readyLDSTeven0.size());
        // }
        inst->need_wake_sta = false;
        if(inst->inst_had_inReady ==  false){
            inst->inst_had_inReady = true;
             if(inst->isStore()){
                readyStore.push(inst);
                DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore \n", inst->seqNum);
            }
            else if(!inst->isStore() && !inst->vld_had_issued){
                inst->vld_had_issued = true;
                if(readyLD_Dst_odd.size() <= readyLD_Dst_even.size()){
                    readyLD_Dst_odd.push(inst);
                    DPRINTF(RxuWTB, "size 0f readyLD_Dst_odd :%i\n", readyLD_Dst_odd.size());
                }
                else {
                    readyLD_Dst_even.push(inst);
                    DPRINTF(RxuWTB, "size 0f readyLD_Dst_even :%i\n", readyLD_Dst_even.size());
                }
            }
        }

        return;
    }

    if (inst->odd_inst) {
        readyInsts[inst->ibuffer_id][0].push(inst);
    } else {
        readyInsts[inst->ibuffer_id][1].push(inst);
    }
}

void
WTB::rescheduleMemInst(const DynInstPtr &resched_inst)
{
    DPRINTF(RxuWTB, "Rescheduling mem inst [sn:%llu]\n", resched_inst->seqNum);

    resched_inst->vld_had_issued = false;

    // Reset DTB translation state
    resched_inst->translationStarted(false);
    resched_inst->translationCompleted(false);
    resched_inst->inst_had_inReady = false;

    resched_inst->clearCanIssue();
    memDepUnit[resched_inst->threadNumber].reschedule(resched_inst);
}

void
WTB::replayMemInst(const DynInstPtr &replay_inst)
{
    memDepUnit[replay_inst->threadNumber].replay();
}

void
WTB::deferMemInst(const DynInstPtr &deferred_inst)
{
    deferredMemInsts.push_back(deferred_inst);
}

void
WTB::blockMemInst(const DynInstPtr &blocked_inst)
{
    blocked_inst->clearIssued();
    blocked_inst->clearCanIssue();
    blockedMemInsts.push_back(blocked_inst);
    blocked_inst->inst_had_inReady = false;
    blocked_inst->vld_had_issued = false;
    DPRINTF(RxuWTB, "Memory inst [sn:%llu] PC %s is blocked, will be "
            "reissued later\n", blocked_inst->seqNum,
            blocked_inst->pcState());
}

void
WTB::cacheUnblocked()
{
    DPRINTF(RxuWTB, "Cache is unblocked, rescheduling blocked memory "
            "instructions\n");
    retryMemInsts.splice(retryMemInsts.end(), blockedMemInsts);
    // Get the CPU ticking again
    cpu->wakeCPU();
}

DynInstPtr
WTB::getDeferredMemInstToExecute()
{
    for (ListIt it = deferredMemInsts.begin(); it != deferredMemInsts.end();
         ++it) {
        DPRINTF(RxuWTB, "deferredMemInsts's head is [sn:%i]\n",(*it)->seqNum);
        if ((*it)->translationCompleted() || (*it)->isSquashed()) {
            DynInstPtr mem_inst = std::move(*it);
            deferredMemInsts.erase(it);
            return mem_inst;
        }
    }
    return nullptr;
}

DynInstPtr
WTB::getBlockedMemInstToExecute()
{
    if (retryMemInsts.empty()) {
        return nullptr;
    } else {
        DynInstPtr mem_inst = std::move(retryMemInsts.front());
        retryMemInsts.pop_front();
        return mem_inst;
    }
}


DynInstPtr
WTB::getSCtoExecute()
{
    if (sc_queue.empty()) {
        return nullptr;
    } else if(sc_queue.top() == cpu->instList.front()) {
        DynInstPtr mem_inst = sc_queue.top();
        sc_queue.pop();
        return mem_inst;
    }
    else {
        return nullptr;
    }
}

void
WTB::squash(ThreadID tid, InstSeqNum SeqNum)
{
    DPRINTF(RxuWTB, "[tid:%i] Starting to squash instructions in "
            "the WTB.\n", tid);

    squashedSeqNum[tid] = SeqNum;

    doSquash(tid);

    // Also tell the memory dependence unit to squash.
    memDepUnit[tid].squash(squashedSeqNum[tid], tid);
}

void
WTB::doSquash(ThreadID tid)
{
    DPRINTF(RxuWTB, "[tid:%i] Squashing until sequence number %i!\n",
            tid, squashedSeqNum[tid]);
    for(auto it = cpu->dispipe3.ldstQueue.requestVector.begin(); it != cpu->dispipe3.ldstQueue.requestVector.end() ; it++){
            if((*it)->seqNum <= squashedSeqNum[tid]){
                st_request.push_back((*it));
            }
    }
    cpu->dispipe3.ldstQueue.requestVector.clear();
    for(auto itst = st_request.begin(); itst != st_request.end() ; itst++){
            cpu->dispipe3.ldstQueue.requestVector.push_back((*itst));
    }
    st_request.clear();

    for(auto it_rport = inst_without_rPort.begin(); it_rport != inst_without_rPort.end() ; it_rport++){
            if((*it_rport)->seqNum <= squashedSeqNum[tid]){
                st_request.push_back((*it_rport));
            }
    }
    inst_without_rPort.clear();
    for(auto itst1 = st_request.begin(); itst1 != st_request.end() ; itst1++){
            inst_without_rPort.push_back((*itst1));
    }
    st_request.clear();


    // Squash any instructions younger than the squashed sequence number
    // given.
    for (int i = 0; i < 24; i++) {
        int size = ibuffer[tid][i].size();
        for (int j = 0; j < size; j++) {
            DynInstPtr squashed_inst = ibuffer[tid][i].front();
            ibuffer[tid][i].pop_front();
            if (squashed_inst->seqNum <= squashedSeqNum[tid]) {
                ibuffer[tid][i].push_back(squashed_inst);
            } else {
                if (squashed_inst->threadNumber != tid ||
                    squashed_inst->isSquashedInWTB()) {
                    continue;
                }
                if (!squashed_inst->isIssued() ||
                    (squashed_inst->isMemRef() &&
                    !squashed_inst->memOpDone())) {

                    DPRINTF(RxuWTB, "[tid:%i] Instruction [sn:%llu] PC %s squashed.\n",
                            tid, squashed_inst->seqNum, squashed_inst->pcState());

                    bool is_acq_rel = squashed_inst->isFullMemBarrier() &&
                                (squashed_inst->isLoad() ||
                                (squashed_inst->isStore() &&
                                    !squashed_inst->isStoreConditional()));

                    // Remove the instruction from the dependency list.
                    if (is_acq_rel ||
                        (!squashed_inst->isNonSpeculative() &&
                        !squashed_inst->isStoreConditional() &&
                        !squashed_inst->isAtomic() &&
                        !squashed_inst->isReadBarrier() &&
                        !squashed_inst->isWriteBarrier())) {

                        for (int src_reg_idx = 0;
                            src_reg_idx < squashed_inst->numSrcRegs();
                            src_reg_idx++)
                        {
                            PhysRegIdPtr src_reg =
                                squashed_inst->renamedSrcIdx(src_reg_idx);

                            // Only remove it from the dependency graph if it
                            // was placed there in the first place.

                            // Instead of doing a linked list traversal, we
                            // can just remove these squashed instructions
                            // either at issue time, or when the register is
                            // overwritten.  The only downside to this is it
                            // leaves more room for error.
                            if (!squashed_inst->readySrcIdx(src_reg_idx) &&
                                !src_reg->isFixedMapping()) {
                                dispipe3Stage->rmu->dependGraph.remove(src_reg->flatIndex(),
                                                squashed_inst);
                            }
                        }

                    } else if (!squashed_inst->isStoreConditional() ||
                            !squashed_inst->isCompleted()) {
                        NonSpecMapIt ns_inst_it =
                            nonSpecInsts.find(squashed_inst->seqNum);

                        // we remove non-speculative instructions from
                        // nonSpecInsts already when they are ready, and so we
                        // cannot always expect to find them
                        if (ns_inst_it == nonSpecInsts.end()) {
                            // loads that became ready but stalled on a
                            // blocked cache are alreayd removed from
                            // nonSpecInsts, and have not faulted
                            assert(squashed_inst->getFault() != NoFault ||
                                squashed_inst->isMemRef());
                        } else {

                            (*ns_inst_it).second = NULL;

                            nonSpecInsts.erase(ns_inst_it);
                        }
                    }

                    // Might want to also clear out the head of the dependency graph.

                    // Mark it as squashed within the WTB.
                    squashed_inst->setSquashedInWTB();

                    // @todo: Remove this hack where several statuses are set so the
                    // inst will flow through the rest of the pipeline.
                    squashed_inst->setIssued();
                    squashed_inst->setCanCommit();
                    squashed_inst->clearInWTB();

                    //Update Thread WTB Count
                    // count[squashed_inst->threadNumber][squashed_inst->ibuffer_id]--;

                    // ++freeEntries[squashed_inst->threadNumber][squashed_inst->ibuffer_id];
                }
                for (int dest_reg_idx = 0;
                    dest_reg_idx < squashed_inst->numDestRegs();
                    dest_reg_idx++)
                {
                    PhysRegIdPtr dest_reg =
                        squashed_inst->renamedDestIdx(dest_reg_idx);
                    if (dest_reg->isFixedMapping()){
                        continue;
                    }
                    while(!dispipe3Stage->rmu->dependGraph.empty(dest_reg->flatIndex())) {
                        dispipe3Stage->rmu->dependGraph.remove(dest_reg->flatIndex(),dispipe3Stage->rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next->inst);
                       // dispipe3Stage->rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next = NULL;
                    }
                    assert(dispipe3Stage->rmu->dependGraph.empty(dest_reg->flatIndex()));
                    dispipe3Stage->rmu->dependGraph.clearInst(dest_reg->flatIndex());
                }
                ++stats.squashedInsts;
            }
        }
    }

    std::queue<DynInstPtr> tempQueue;
    DynInstPtr inst;
    for (int i = 0; i < 24; i++) {
        for (int j = 0; j < 2; j++) {
            while (!readyInsts[i][j].empty()) {
                inst = readyInsts[i][j].top();
                readyInsts[i][j].pop();
                if (!inst->isSquashedInWTB()) {
                    tempQueue.push(inst);
                }
            }

            while (!tempQueue.empty()) {
                inst = tempQueue.front();
                tempQueue.pop();
                readyInsts[i][j].push(inst);
            }
        }
    }

    while (!readySTdata0.empty()) {
        inst = readySTdata0.top();
        readySTdata0.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    while (!readySTdata1.empty()) {
        inst = readySTdata1.top();
        readySTdata1.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    while (!tempQueue.empty()) {
        inst = tempQueue.front();
        tempQueue.pop();
        if(inst->isStore()){
            //bool wheOE = inst->numSrcRegs() > 0 ? inst->renamedSrcIdx(0)->index() & 1 : false;
            // const RegId &src_reg = inst->srcRegIdx(0);
            // bool wheOE = src_reg & 1 ? true : false;
            //bool wheOE = (readyLDSTodd0.size() <=  readyLDSTeven0.size()) ? true : false;
            // if(wheOE){
            //     readySTdata1.push(inst);
            // }
            // else {
                readySTdata0.push(inst);
            //}
        }
    }

    // while (!tempQueue.empty()) {
    //     inst = tempQueue.front();
    //     tempQueue.pop();
    //     readySTdata1.push(inst);
    // }

    while (!readyLDSTodd0.empty()) {
        inst = readyLDSTodd0.top();
        readyLDSTodd0.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    while (!readyLD_Dst_even.empty()) {
        inst = readyLD_Dst_even.top();
        readyLD_Dst_even.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    while (!readyLD_Dst_odd.empty()) {
        inst = readyLD_Dst_odd.top();
        readyLD_Dst_odd.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    while (!readyStore.empty()) {
        inst = readyStore.top();
        readyStore.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    // while (!readyLDSTodd1.empty()) {
    //     inst = readyLDSTodd1.top();
    //     readyLDSTodd1.pop();
    //     if (!inst->isSquashedInWTB()) {
    //         tempQueue.push(inst);
    //     }
    // }

    // while (!readyLDSTeven1.empty()) {
    //     inst = readyLDSTeven1.top();
    //     readyLDSTeven1.pop();
    //     if (!inst->isSquashedInWTB()) {
    //         tempQueue.push(inst);
    //     }
    // }

    while (!readyLDSTeven0.empty()) {
        inst = readyLDSTeven0.top();
        readyLDSTeven0.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }


    while (!tempQueue.empty()) {
        inst = tempQueue.front();
        if (inst->ibuffer_id == 7 || inst->ibuffer_id == 15 || inst->ibuffer_id == 17 || inst->ibuffer_id == 19 ||
            inst->ibuffer_id == 6 || inst->ibuffer_id == 14 || inst->ibuffer_id == 16 || inst->ibuffer_id == 18) {
            // const RegId &src_reg = inst->srcRegIdx(0);
            // inst->odd_inst = src_reg & 1 ? true : false;

            // if(inst->odd_inst){
            // bool wheOE = inst->numSrcRegs() > 0 ? inst->renamedSrcIdx(0)->index() & 1 : false;
            bool wheOE = inst->numSrcRegs() > 0 ? inst->renamedDestIdx(0)->index() & 1 : false;

            // if(wheOE){
            //     readyLDSTodd0.push(inst);
            // }
            // else {
            //     readyLDSTeven0.push(inst);
            // }
            if(inst->isStore()){
                readyStore.push(inst);
                DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore\n", inst->seqNum);
            }
            else if(readyLD_Dst_odd.size() <= readyLD_Dst_even.size()){
                readyLD_Dst_odd.push(inst);
            }
            else {
                readyLD_Dst_even.push(inst);
            }
        }
        tempQueue.pop();
    }

    while (!readyVecInst0.empty()) {
        inst = readyVecInst0.top();
        readyVecInst0.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    while (!readyVecInst1.empty()) {
        inst = readyVecInst1.top();
        readyVecInst1.pop();
        if (!inst->isSquashedInWTB()) {
            tempQueue.push(inst);
        }
    }

    while (!tempQueue.empty()) {
        inst = tempQueue.front();
        tempQueue.pop();
        if(readyVecInst0.size() <= readyVecInst1.size()){
            readyVecInst0.push(inst);
        }
        else {
            readyVecInst1.push(inst);
        }
    }
}

bool
WTB::PqCompare::operator()(
        const DynInstPtr &lhs, const DynInstPtr &rhs) const
{
    return lhs->iid > rhs->iid;
}


void
WTB::addIfReady(const DynInstPtr &inst)
{

    inst->intoReadyInstsTick = curTick();
    // If the instruction now has all of its source registers
    // available, then add it to the list of ready instructions.
    if (inst->readyToIssue()) {

        //Add the instruction to the proper ready list.
        if (inst->isMemRef()) {

            DPRINTF(RxuWTB, "Checking if memory instruction can issue.\n");

            // Message to the mem dependence unit that this instruction has
            // its registers ready.
            if(inst->isStore() && inst->staReady == false){
                inst->staReady = true;
                memDepUnit[inst->threadNumber].regsReady(inst);
            }
            else if(!inst->isStore()){
                memDepUnit[inst->threadNumber].regsReady(inst);
            }
            
            return;
        }

        if (inst->needFRM()) {
            if (inst->readyFRM() || cpu->frm.isReady(inst->rn_src_frm)) {
                DPRINTF(RxuWTB, "[sn:%lli] has ready out of [frm] sources.\n",
                    inst->seqNum);
            } else {
                DPRINTF(RxuWTB, "[sn:%lli] has not ready out of [frm] sources.\n",
                    inst->seqNum);
                return;
            }
        }

        if (inst->needVxrm()) {
            if (inst->readyVxrm() || cpu->vecCsr.isReady(Vxrm,inst->rn_src_vxrm)) {
                DPRINTF(RxuWTB, "[sn:%lli] has ready out of [vxrm] sources.\n",
                    inst->seqNum);
            } else {
                DPRINTF(RxuWTB, "[sn:%lli] has not ready out of [vxrm] sources.\n",
                    inst->seqNum);
                return;
            }
        }

        if (inst->isCsrVl()) {
            if (inst->readyVl() || cpu->vecCsr.isReady(Vl,inst->rn_src_vl)) {
                DPRINTF(RxuWTB, "[sn:%lli] has ready out of csr [vl] sources.\n",
                    inst->seqNum);
            } else {
                DPRINTF(RxuWTB, "[sn:%lli] has not ready out of csr [vl] sources.\n",
                    inst->seqNum);
                return;
            }
        }

        if (inst->isCsrVtype()) {
            if (inst->readyVtype() || cpu->vecCsr.isReady(Vtype,inst->rn_src_vtype)) {
                DPRINTF(RxuWTB, "[sn:%lli] has ready out of csr [vtype] sources.\n",
                    inst->seqNum);
            } else {
                DPRINTF(RxuWTB, "[sn:%lli] has not ready out of csr [vtype] sources.\n",
                    inst->seqNum);
                return;
            }
        }
        

        DPRINTF(RxuWTB, "Instruction is ready to issue, putting it into "
                "the ready list, PC %s [sn:%llu], ibuffer_id = %i.\n",
                inst->pcState(), inst->seqNum, inst->ibuffer_id);

        if (inst->ibuffer_id == 7 || inst->ibuffer_id == 15 || inst->ibuffer_id == 17 || inst->ibuffer_id == 19 ||
            inst->ibuffer_id == 6 || inst->ibuffer_id == 14 || inst->ibuffer_id == 16 || inst->ibuffer_id == 18) {
            // const RegId &src_reg = inst->srcRegIdx(0);
            // inst->odd_inst = src_reg & 1 ? true : false;

            // if(inst->odd_inst){
            //bool wheOE = inst->numSrcRegs() > 0 ? inst->renamedSrcIdx(0)->index() & 1 : false;
            bool wheOE = inst->numSrcRegs() > 0 ? inst->renamedDestIdx(0)->index() & 1 : false;
            // const RegId &src_reg = inst->srcRegIdx(0);
            // bool wheOE = src_reg & 1 ? true : false;
            //bool wheOE = (readyLDSTodd0.size() <=  readyLDSTeven0.size()) ? true : false;
            if(inst->isStore()){
                readyStore.push(inst);
                DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore\n", inst->seqNum);
            }
            else if(readyLD_Dst_odd.size() <= readyLD_Dst_even.size()){
                readyLD_Dst_odd.push(inst);
            }
            else {
                readyLD_Dst_even.push(inst);
            }
            return;
        }

        if (inst->ibuffer_id == 22 || inst->ibuffer_id == 23) {
            if(readyVecInst0.size() <= readyVecInst1.size()){
                readyVecInst0.push(inst);
            }
            else {
                readyVecInst1.push(inst);
            }
            return;
        }

        if (inst->arrive_wtb == -1) {
            inst->arrive_wtb = curTick();
        }

        if (inst->ibuffer_id == 20 || inst->ibuffer_id == 21) {
            readyInsts[inst->ibuffer_id][0].push(inst);
        } else if (inst->ibuffer_id == 2 || inst->ibuffer_id == 3) {
            if (inst->isControl()) {
                readyInsts[inst->ibuffer_id][0].push(inst);
            } else {
                readyInsts[inst->ibuffer_id][1].push(inst);
            }
        } else {
            if (inst->odd_inst) {
                readyInsts[inst->ibuffer_id][0].push(inst);
            } else {
                readyInsts[inst->ibuffer_id][1].push(inst);
            }
        }

    }
}

void
WTB::selectOne(int ibuffer_id1, int ibuffer_id2)
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13

    DynInstPtr inst;
    bool inst_flag = false;

    if (readyInsts[ibuffer_id1][0].empty() && readyInsts[ibuffer_id1][1].empty()
        && readyInsts[ibuffer_id2][0].empty() && readyInsts[ibuffer_id2][1].empty())
    {
        return;
    }

    if (!readyInsts[ibuffer_id1][0].empty()) {
        inst = readyInsts[ibuffer_id1][0].top();
        inst_flag = true;
    }

    if (!readyInsts[ibuffer_id1][1].empty()) {
        if (inst_flag) {
            inst = readyInsts[ibuffer_id1][1].top()->iid < inst->iid
                ? readyInsts[ibuffer_id1][1].top() : inst;
        } else {
            inst = readyInsts[ibuffer_id1][1].top();
            inst_flag = true;
        }

    }

    if (!readyInsts[ibuffer_id2][0].empty()) {
        if (inst_flag) {
        inst = readyInsts[ibuffer_id2][0].top()->iid < inst->iid
               ? readyInsts[ibuffer_id2][0].top() : inst;
        } else {
            inst = readyInsts[ibuffer_id2][0].top();
            inst_flag = true;
        }
    }

    if (!readyInsts[ibuffer_id2][1].empty()) {
        if (inst_flag) {
        inst = readyInsts[ibuffer_id2][1].top()->iid < inst->iid
               ? readyInsts[ibuffer_id2][1].top() : inst;
        } else {
            inst = readyInsts[ibuffer_id2][1].top();
            // inst_flag = true;
        }
    }

    arbiterTmpQ.push(inst);
    DPRINTF(RxuWTB, "Instruction add to ibuffertoew selectone, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
    readyInsts[inst->ibuffer_id][inst->odd_inst ? 0 : 1].pop();
    ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
    // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
    // count[inst->threadNumber][inst->ibuffer_id]--;

}

void
WTB::arbiterFloat()        // Check if the read port of fpNormal is sufficient. If not, borrow the read port of fpSpecial
{
    assert(arbiterTmpQ.size() <= 2);

    while (!arbiterTmpQ.empty()) {
        DynInstPtr inst = arbiterTmpQ.top();
        arbiterTmpQ.pop();
        ibufferToEW.push(inst);
        // if (fpNormalRegReadFreeNums >= inst->numSrcRegs()) {
        //     fpNormalRegReadFreeNums -= inst->numSrcRegs();
        // } else {
        //     fpSpecialRegReadFreeNums -= inst->numSrcRegs() - fpNormalRegReadFreeNums;
        //     fpNormalRegReadFreeNums = 0;
        // }
    }

    // if (fpSpecialRegReadFreeNums > 0) {
        selectOne(11, 12);
        if (!arbiterTmpQ.empty()) {
            DynInstPtr inst = arbiterTmpQ.top();
            arbiterTmpQ.pop();
            // if (inst->numSrcRegs() <= fpSpecialRegReadFreeNums) {
                if ((inst->ibuffer_id == 11 || inst->ibuffer_id == 12) && inst->numDestRegs() > 0 && inst->destRegIdx(0).classValue() == IntRegClass) {     // fp2int
                    inst->iid = inst->iid > 1024 ? inst->iid - 1024 : 1;
                }
                ibufferToEW.push(inst);
                //fpSpecialRegReadFreeNums -= inst->numSrcRegs();
                stats.fpRegReadFree++;
            // } else {
            //     ibuffer[inst->threadNumber][inst->ibuffer_id].push_back(inst);
            //     readyInsts[inst->ibuffer_id][inst->odd_inst ? 0 : 1].push(inst);
            //     // freeEntries[inst->threadNumber][inst->ibuffer_id]--;
            //     // count[inst->threadNumber][inst->ibuffer_id]++;
            //     DPRINTF(RxuWTB, "FpSpecial inst PC %s can't issue due to its read port being borrowed by fpNormal, "
            //         "[sn:%llu], ibuffer_id = %i\n", inst->pcState(),
            //         inst->seqNum, inst->ibuffer_id);
            //     stats.fpRegReadBusy++;
            // }
        }
    // }
}

void
WTB::selectTwo(int ibuffer_id1, int ibuffer_id2)
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13

    DynInstPtr inst;

    // 0 : odd
    // 1 : even
    for (int i = 0; i < 2; i++) {
        if (readyInsts[ibuffer_id1][i].empty() && !readyInsts[ibuffer_id2][i].empty()) {

            inst = readyInsts[ibuffer_id2][i].top();
            arbiterTmpQ.push(inst);
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew selecttwo1, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
            readyInsts[ibuffer_id2][i].pop();
            ibuffer[inst->threadNumber][ibuffer_id2].remove(inst);
            // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
            // count[inst->threadNumber][inst->ibuffer_id]--;

        } else if (!readyInsts[ibuffer_id1][i].empty() && readyInsts[ibuffer_id2][i].empty()) {

            inst = readyInsts[ibuffer_id1][i].top();
            arbiterTmpQ.push(inst);
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew selecttwo2, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
            readyInsts[ibuffer_id1][i].pop();
            ibuffer[inst->threadNumber][ibuffer_id1].remove(inst);
            // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
            // count[inst->threadNumber][inst->ibuffer_id]--;

        } else if (!readyInsts[ibuffer_id1][i].empty() && !readyInsts[ibuffer_id2][i].empty()) {
            int ibufferId = readyInsts[ibuffer_id1][i].top()->iid < readyInsts[ibuffer_id2][i].top()->iid
            ? ibuffer_id1 : ibuffer_id2;
            inst = readyInsts[ibufferId][i].top();
            arbiterTmpQ.push(inst);
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew selecttwo3, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
            readyInsts[ibufferId][i].pop();
            ibuffer[inst->threadNumber][ibufferId].remove(inst);
            // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
            // count[inst->threadNumber][inst->ibuffer_id]--;
        }
    }
}

void
WTB::selectVec()
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13

    DynInstPtr inst;
    if (!readyVecInst0.empty()) {
        inst = readyVecInst0.top();
        arbiterTmpQ.push(inst);
        DPRINTF(RxuWTB, "Instruction add to ibuffertoew selectVec, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
        readyVecInst0.pop();
        ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
    } 

    if (!readyVecInst1.empty()) {
        inst = readyVecInst1.top();
        arbiterTmpQ.push(inst);
        DPRINTF(RxuWTB, "Instruction add to ibuffertoew selectVec, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
        readyVecInst1.pop();
        ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
    } 

}

void
WTB::arbiterVec()              // Check if the srcs of two instructions is 2 odd and 2 even
{
    assert(arbiterTmpQ.size() <= 2);
    if (arbiterTmpQ.empty()) {
        return;
    }
    else if (arbiterTmpQ.size() == 1){
        ibufferToEW.push(arbiterTmpQ.top());
        arbiterTmpQ.pop();
    }else {
        DynInstPtr inst1 = arbiterTmpQ.top();
        arbiterTmpQ.pop();
        ibufferToEW.push(inst1);
        DynInstPtr inst2 = arbiterTmpQ.top();
        arbiterTmpQ.pop();
        ibufferToEW.push(inst2);
    }
}

void
WTB::arbiterInt()              // Check if the srcs of two instructions is 2 odd and 2 even
{
    assert(arbiterTmpQ.size() <= 2);
    if (arbiterTmpQ.empty()) {
        return;
    }
    else if (arbiterTmpQ.size() == 1){
        ibufferToEW.push(arbiterTmpQ.top());
        arbiterTmpQ.pop();
    }else {
        DynInstPtr inst1 = arbiterTmpQ.top();
        arbiterTmpQ.pop();
        ibufferToEW.push(inst1);
        DynInstPtr inst2 = arbiterTmpQ.top();
        arbiterTmpQ.pop();
        ibufferToEW.push(inst2);
    }
    // } else if (arbiterTmpQ.size() == 1) {
    //     ibufferToEW.push(arbiterTmpQ.top());
    //     arbiterTmpQ.pop();
    // } else {
    //     DynInstPtr inst1 = arbiterTmpQ.top();
    //     arbiterTmpQ.pop();
    //     if ((inst1->ibuffer_id == 2 || inst1->ibuffer_id == 3) && inst1->numDestRegs() > 0 && inst1->destRegIdx(0).classValue() == FloatRegClass) {     // int2fp
    //         inst1->iid = inst1->iid > 1024 ? inst1->iid - 1024 : 1;
    //     }
    //     ibufferToEW.push(inst1);

    //     DynInstPtr inst2 = arbiterTmpQ.top();
    //     arbiterTmpQ.pop();

        //  int minSrcNums = std::min(inst1->numSrcRegs(), inst2->numSrcRegs());

        // for (int i = 0; i < minSrcNums; i++) {
        //     auto it1 = fwdInsts.at(0).find(inst1->seqNum);
        //     if (it1 != fwdInsts.at(0).end() && it1->second.src[i]) {
        //         DPRINTF(RxuWTB, "inst PC %s can forward src[%i], "
        //             "[sn:%llu], ibuffer_id = %i\n", inst1->pcState(), i,
        //             inst1->seqNum, inst1->ibuffer_id);
        //         continue;
        //     }

    //         // auto it2 = fwdInsts.at(0).find(inst2->seqNum);
    //         // if (it2 != fwdInsts.at(0).end() && it2->second.src[i]) {
    //         //     DPRINTF(RxuWTB, "inst PC %s can forward src[%i], "
    //         //         "[sn:%llu], ibuffer_id = %i\n", inst2->pcState(), i,
    //         //         inst2->seqNum, inst2->ibuffer_id);
    //         //     continue;
    //         // }

    //         if (inst1->srcsLocalData[i] || inst2->srcsLocalData[i]) {
    //             continue;
    //         }

    //         if (inst1->srcRegIdx(i).isZeroReg() || inst2->srcRegIdx(i).isZeroReg()) {
    //             continue;
    //         } else if (!((inst1->renamedSrcIdx(i)->index() & 1)
    //             ^ (inst2->renamedSrcIdx(i)->index() & 1))) {

    //             ibuffer[inst2->threadNumber][inst2->ibuffer_id].push_back(inst2);
    //             readyInsts[inst2->ibuffer_id][inst2->odd_inst ? 0 : 1].push(inst2);
    //             // freeEntries[inst2->threadNumber][inst2->ibuffer_id]--;
    //             // count[inst2->threadNumber][inst2->ibuffer_id]++;

    //             DPRINTF(RxuWTB, "IntNormal inst PC %s can't issue due to not meeting the requirement that the srcs are 2 odd and 2 even, "
    //                 "[sn:%llu], ibuffer_id = %i\n", inst2->pcState(),
    //                 inst2->seqNum, inst2->ibuffer_id);
    //             stats.intRegReadBusy++;
    //             return;
    //         }
    //         // if (!((inst1->renamedSrcIdx(i)->index() & 1)
    //         //     ^ (inst2->renamedSrcIdx(i)->index() & 1))) {

    //         //     ibuffer[inst2->threadNumber][inst2->ibuffer_id].push_back(inst2);
    //         //     readyInsts[inst2->ibuffer_id][inst2->odd_inst ? 0 : 1].push(inst2);
    //         //     // freeEntries[inst2->threadNumber][inst2->ibuffer_id]--;
    //         //     // count[inst2->threadNumber][inst2->ibuffer_id]++;

    //         //     DPRINTF(RxuWTB, "IntNormal inst PC %s can't issue due to not meeting the requirement that the srcs are 2 odd and 2 even, "
    //         //         "[sn:%llu], ibuffer_id = %i\n", inst2->pcState(),
    //         //         inst2->seqNum, inst2->ibuffer_id);
    //         //     stats.intRegReadBusy++;
    //         //     return;
    //         // }
    //     }
    //     if ((inst2->ibuffer_id == 2 || inst2->ibuffer_id == 3) && inst2->numDestRegs() > 0 && inst2->destRegIdx(0).classValue() == FloatRegClass) {     // int2fp
    //         inst2->iid = inst2->iid > 1024 ? inst2->iid - 1024 : 1;
    //     }
    //     ibufferToEW.push(inst2);
    //     ++stats.intRegReadFree;
    // }
}

// void
// WTB::arbiterIntSpecialAndBranch()
// {
//     ReadyInstQueue allTempQ;

//     bool oddSrc0 = false;
//     bool oddSrc1 = false;
//     bool oddDest = false;
//     bool evenSrc0 = false;
//     bool evenSrc1 = false;
//     bool evenDest = false;

//     bool oddBr = false;
//     bool oddSpecial = false;
//     bool evenBr = false;
//     bool evenSpecial = false;

//     DynInstPtr oddBrInst = nullptr;
//     DynInstPtr oddSpecialInst = nullptr;
//     DynInstPtr evenBrInst = nullptr;
//     DynInstPtr evenSpecialInst = nullptr;

//     if (!readyInsts[2][0].empty()) {
//         oddBrInst = readyInsts[2][0].top();
//         readyInsts[2][0].pop();
//         ibuffer[oddBrInst->threadNumber][2].remove(oddBrInst);
//         oddBr = true;
//         allTempQ.push(oddBrInst);
//     }

//     if (!readyInsts[2][1].empty()) {
//         oddSpecialInst = readyInsts[2][1].top();
//         readyInsts[2][1].pop();
//         ibuffer[oddSpecialInst->threadNumber][2].remove(oddSpecialInst);
//         oddSpecial = true;
//         allTempQ.push(oddSpecialInst);
//         // oddSpecialInst->iid += 20000;
//     }

//     if (!readyInsts[3][0].empty()) {
//         evenBrInst = readyInsts[3][0].top();
//         readyInsts[3][0].pop();
//         ibuffer[evenBrInst->threadNumber][3].remove(evenBrInst);
//         evenBr = true;
//         allTempQ.push(evenBrInst);
//     }

//     if (!readyInsts[3][1].empty()) {
//         evenSpecialInst = readyInsts[3][1].top();
//         readyInsts[3][1].pop();
//         ibuffer[evenSpecialInst->threadNumber][3].remove(evenSpecialInst);
//         evenSpecial = true;
//         allTempQ.push(evenSpecialInst);
//         // evenSpecialInst->iid += 20000;
//     }

//     while (!allTempQ.empty()) {
//         DynInstPtr inst1 = allTempQ.top();
//         ibufferToEW.push(inst1);

//         if (inst1->numDestRegs() > 0 && !inst1->destRegIdx(0).isZeroReg()) {
//             if (inst1->odd_inst) {
//                 oddDest = true;
//             } else {
//                 evenDest = true;
//             }
//         }

//         for (int i = 0; i < inst1->numSrcRegs(); i++) {
//             if (i == 0) {
//                 if (!inst1->srcsLocalData[i] && !inst1->srcRegIdx(i).isZeroReg()) {
//                     if (inst1->renamedSrcIdx(i)->index() & 1) {
//                         oddSrc0 = true;
//                     } else {
//                         evenSrc0 = true;
//                     }
//                 }
//             } else if (i == 1) {
//                 if (!inst1->srcsLocalData[i] && !inst1->srcRegIdx(i).isZeroReg()) {
//                     if (inst1->renamedSrcIdx(i)->index() & 1) {
//                         oddSrc1 = true;
//                     } else {
//                         evenSrc1 = true;
//                     }
//                 }
//             }
//         }

//         allTempQ.pop();
//         inst1->iid = inst1->seqNum;
//         std::deque<DynInstPtr> allTempQ_1;
//         int size = allTempQ.size();
//         for (int i = 0; i < size; i++) {
//             DynInstPtr inst2 = allTempQ.top();
//             allTempQ.pop();

//             bool conflict = false;

//             if (inst2->numDestRegs() > 0 && !inst2->destRegIdx(0).isZeroReg()) {
//                 if (inst2->odd_inst && oddDest) {
//                     conflict = true;
//                 } else if (!inst2->odd_inst && evenDest) {
//                     conflict = true;
//                 }
//             }

//             for (int j = 0; j < inst2->numSrcRegs(); j++) {
//                 if (j == 0) {
//                     if (!inst2->srcsLocalData[i] && !inst2->srcRegIdx(i).isZeroReg()) {
//                         if (inst2->renamedSrcIdx(i)->index() & 1) {
//                             if (oddSrc0) {
//                                 conflict = true;
//                             }
//                         } else {
//                             if (evenSrc0) {
//                                 conflict = true;
//                             }
//                         }
//                     }
//                 } else if (j == 1) {
//                     if (!inst2->srcsLocalData[i] && !inst2->srcRegIdx(i).isZeroReg()) {
//                         if (inst2->renamedSrcIdx(i)->index() & 1) {
//                             if (oddSrc1) {
//                                 conflict = true;
//                             }
//                         } else {
//                             if (evenSrc1) {
//                                 conflict = true;
//                             }
//                         }
//                     }
//                 }
//             }

//             if (conflict) {
//                 inst2->iid = inst2->seqNum;
//                 ibuffer[inst2->threadNumber][inst2->ibuffer_id].push_back(inst2);

//                 if (inst2->ibuffer_id == 2 || inst2->ibuffer_id == 3) {
//                     if (inst2->isControl() && (inst2->numDestRegs() == 0 || inst2->destRegIdx(0).isZeroReg())) {
//                         readyInsts[inst2->ibuffer_id][0].push(inst2);
//                     } else {
//                         readyInsts[inst2->ibuffer_id][1].push(inst2);
//                     }
//                 } else {
//                     if (inst2->odd_inst) {
//                         readyInsts[inst2->ibuffer_id][0].push(inst2);
//                     } else {
//                         readyInsts[inst2->ibuffer_id][1].push(inst2);
//                     }
//                 }
//             } else {
//                 allTempQ_1.push_back(inst2);
//             }
//         }

//         while (!allTempQ_1.empty()) {
//             allTempQ.push(allTempQ_1.front());
//             allTempQ_1.pop_front();
//         }
//     }
// }

void
WTB::arbiterIntSpecialAndBranch()
{
    ReadyInstQueue allTempQ;

    bool oddSrc0 = false;
    bool oddSrc1 = false;
    bool oddDest = false;
    bool evenSrc0 = false;
    bool evenSrc1 = false;
    bool evenDest = false;

    bool oddJump = false;
    bool oddBr = false;
    bool oddSpecial = false;
    bool evenJump = false;
    bool evenBr = false;
    bool evenSpecial = false;

    DynInstPtr oddJumpInst = nullptr;
    DynInstPtr oddBrInst = nullptr;
    DynInstPtr oddSpecialInst = nullptr;
    DynInstPtr evenJumpInst = nullptr;
    DynInstPtr evenBrInst = nullptr;
    DynInstPtr evenSpecialInst = nullptr;

    if (!readyInsts[20][0].empty()) {
        oddBrInst = readyInsts[20][0].top();
        readyInsts[20][0].pop();
        ibuffer[oddBrInst->threadNumber][20].remove(oddBrInst);
        oddBr = true;
        allTempQ.push(oddBrInst);
    }

    if ((!readyInsts[2][0].empty() && readyInsts[2][1].empty()) ||
        (!readyInsts[2][0].empty() && !readyInsts[2][1].empty() &&
        readyInsts[2][0].top()->seqNum < readyInsts[2][1].top()->seqNum)) {
        oddJumpInst = readyInsts[2][0].top();
        readyInsts[2][0].pop();
        ibuffer[oddJumpInst->threadNumber][2].remove(oddJumpInst);
        oddJump = true;
        allTempQ.push(oddJumpInst);
    } else {
        if (!readyInsts[2][1].empty()) {
            oddSpecialInst = readyInsts[2][1].top();
            readyInsts[2][1].pop();
            ibuffer[oddSpecialInst->threadNumber][2].remove(oddSpecialInst);
            oddSpecial = true;
            allTempQ.push(oddSpecialInst);
        }
    }

    if (!readyInsts[21][0].empty()) {
        evenBrInst = readyInsts[21][0].top();
        readyInsts[21][0].pop();
        ibuffer[evenBrInst->threadNumber][21].remove(evenBrInst);
        evenBr = true;
        allTempQ.push(evenBrInst);
    }

    if ((!readyInsts[3][0].empty() && readyInsts[3][1].empty()) ||
        (!readyInsts[3][0].empty() && !readyInsts[3][1].empty() &&
        readyInsts[3][0].top()->seqNum < readyInsts[3][1].top()->seqNum)) {
        evenJumpInst = readyInsts[3][0].top();
        readyInsts[3][0].pop();
        ibuffer[evenJumpInst->threadNumber][3].remove(evenJumpInst);
        evenJump = true;
        allTempQ.push(evenJumpInst);
    } else {
        if (!readyInsts[3][1].empty()) {
            evenSpecialInst = readyInsts[3][1].top();
            readyInsts[3][1].pop();
            ibuffer[evenSpecialInst->threadNumber][3].remove(evenSpecialInst);
            evenSpecial = true;
            allTempQ.push(evenSpecialInst);
        }
    }

    while (!allTempQ.empty()) {
        DynInstPtr inst1 = allTempQ.top();
        ibufferToEW.push(inst1);

        if (inst1->numDestRegs() > 0 && !inst1->destRegIdx(0).isZeroReg()) {
            if (inst1->odd_inst) {
                oddDest = true;
            } else {
                evenDest = true;
            }
        }

        for (int i = 0; i < inst1->numSrcRegs(); i++) {
            if (i == 0) {
                if (!inst1->srcsLocalData[i] && !inst1->srcRegIdx(i).isZeroReg()) {
                    if (inst1->renamedSrcIdx(i)->index() & 1) {
                        oddSrc0 = true;
                    } else {
                        evenSrc0 = true;
                    }
                }
            } else if (i == 1) {
                if (!inst1->srcsLocalData[i] && !inst1->srcRegIdx(i).isZeroReg()) {
                    if (inst1->renamedSrcIdx(i)->index() & 1) {
                        oddSrc1 = true;
                    } else {
                        evenSrc1 = true;
                    }
                }
            }
        }

        allTempQ.pop();
        inst1->iid = inst1->seqNum;
        std::deque<DynInstPtr> allTempQ_1;
        int size = allTempQ.size();
        for (int i = 0; i < size; i++) {
            DynInstPtr inst2 = allTempQ.top();
            allTempQ.pop();

            bool conflict = false;

            if (inst2->numDestRegs() > 0 && !inst2->destRegIdx(0).isZeroReg()) {
                if (inst2->odd_inst && oddDest) {
                    conflict = true;
                } else if (!inst2->odd_inst && evenDest) {
                    conflict = true;
                }
            }

            // for (int j = 0; j < inst2->numSrcRegs(); j++) {
            //     if (j == 0) {
            //         if (!inst2->srcsLocalData[i] && !inst2->srcRegIdx(i).isZeroReg()) {
            //             if (inst2->renamedSrcIdx(i)->index() & 1) {
            //                 if (oddSrc0) {
            //                     conflict = true;
            //                 }
            //             } else {
            //                 if (evenSrc0) {
            //                     conflict = true;
            //                 }
            //             }
            //         }
            //     } else if (j == 1) {
            //         if (!inst2->srcsLocalData[i] && !inst2->srcRegIdx(i).isZeroReg()) {
            //             if (inst2->renamedSrcIdx(i)->index() & 1) {
            //                 if (oddSrc1) {
            //                     conflict = true;
            //                 }
            //             } else {
            //                 if (evenSrc1) {
            //                     conflict = true;
            //                 }
            //             }
            //         }
            //     }
            // }

            if (conflict) {
                inst2->iid = inst2->seqNum;
                ibuffer[inst2->threadNumber][inst2->ibuffer_id].push_back(inst2);

                if (inst2->ibuffer_id == 20 || inst2->ibuffer_id == 21) {
                    readyInsts[inst2->ibuffer_id][0].push(inst2);
                } else if (inst2->ibuffer_id == 2 || inst2->ibuffer_id == 3) {
                    if (inst2->isControl()) {
                        readyInsts[inst2->ibuffer_id][0].push(inst2);
                    } else {
                        readyInsts[inst2->ibuffer_id][1].push(inst2);
                    }
                } else {
                    if (inst2->odd_inst) {
                        readyInsts[inst2->ibuffer_id][0].push(inst2);
                    } else {
                        readyInsts[inst2->ibuffer_id][1].push(inst2);
                    }
                }
            } else {
                allTempQ_1.push_back(inst2);
            }
        }

        while (!allTempQ_1.empty()) {
            allTempQ.push(allTempQ_1.front());
            allTempQ_1.pop_front();
        }
    }
}

void
WTB::selectLdSt_dst()
{
    DynInstPtr inst = nullptr;
    DynInstPtr insteven0 = nullptr;
    DynInstPtr insteven1 = nullptr;
    bool evenbuffernull;
    DynInstPtr instodd0 = nullptr;
    DynInstPtr instodd1 = nullptr;
    bool oddbuffernull;
    DynInstPtr st0 = nullptr;
    DynInstPtr st1 = nullptr;
    DynInstPtr st2 = nullptr;
    DynInstPtr st3 = nullptr;
    bool stbuffernull;
    int cnt_issue = 0;
    DynInstPtr instold0 = nullptr;
    DynInstPtr instold1 = nullptr;
    DynInstPtr instold2 = nullptr;
    DynInstPtr instold3 = nullptr;

    evenbuffernull = readyLD_Dst_even.empty();
    oddbuffernull = readyLD_Dst_odd.empty();
    stbuffernull = readyStore.empty();

    DPRINTF(RxuWTB, "readyLD_Dst_even's size:%i \n",readyLD_Dst_even.size());
    DPRINTF(RxuWTB, "readyLD_Dst_odd's size:%i \n",readyLD_Dst_odd.size());
    DPRINTF(RxuWTB, "readyStore's size:%i \n",readyStore.size());

    if((!evenbuffernull)){
        insteven0 = readyLD_Dst_even.top();
        readyLD_Dst_even.pop();
        if(!readyLD_Dst_even.empty()){
            insteven1 = readyLD_Dst_even.top();
            readyLD_Dst_even.pop();
        }
    }

    if((!oddbuffernull)){
        instodd0 = readyLD_Dst_odd.top();
        readyLD_Dst_odd.pop();
        if(!readyLD_Dst_odd.empty()){
            instodd1 = readyLD_Dst_odd.top();
            readyLD_Dst_odd.pop();
        }
    }

    if((!stbuffernull)){
        st0 = readyStore.top();
        readyStore.pop();
        if(!readyStore.empty()){
            st1 = readyStore.top();
            readyStore.pop();
        }
        if(!readyStore.empty()){
            st2 = readyStore.top();
            readyStore.pop();
        }
        if(!readyStore.empty()){
            st3 = readyStore.top();
            readyStore.pop();
        }
    }

    //instodd0 && insteven0 为true
    if(instodd0 || insteven0){
        if(instodd0 && insteven0){
            if(st3){
                instold0 = st0;
                instold1 = st1;
                instold2 = st2;
                instold3 = st3;
            }
            else if(st2){
                instold0 = st0;
                instold1 = st1;
                instold2 = st2;
                if(instodd0->seqNum < insteven0->seqNum){
                    instold3 = instodd0;
                }
                else {
                    instold3 = insteven0;
                }
            }
            else if(st1){
                //if((instodd0->seqNum > st1->seqNum) && (insteven0->seqNum > st1->seqNum)){
                    instold0 = st0;
                    instold1 = st1;
                    // if(!readyStore.empty()){
                    //     if(instodd0->seqNum < readyStore.top()->seqNum){
                            instold2 = instodd0;
                        // }
                        // if(insteven0->seqNum < readyStore.top()->seqNum){
                            instold3 = insteven0;
                        //}
                //     }
                //     else {
                //         instold2 = instodd0;
                //         instold3 = insteven0;
                //     }
                // }
                // else if((instodd0->seqNum > st1->seqNum) && (insteven0->seqNum < st1->seqNum)){
                //     instold0 = st0;
                //     instold1 = insteven0;
                //     instold2 = st1;
                //     if(!readyStore.empty()){
                //         if(insteven1){
                //             if(insteven1->seqNum < readyStore.top()->seqNum){
                //                 instold3 = insteven1;
                //             }
                //             else if(instodd1){
                //                 if(instodd1->seqNum < readyStore.top()->seqNum){
                //                     instold3 = instodd1;
                //                 }
                //             }
                //         }
                //         else if(instodd1){
                //                 if(instodd1->seqNum < readyStore.top()->seqNum){
                //                     instold3 = instodd1;
                //                 }
                //             }
                //     }
                //     else {
                //        if(insteven1){
                //             instold3 = insteven1;
                //         }
                //         else if(instodd1){
                //             instold3 = instodd1;
                //         }
                //     }
                // }
                // else if((instodd0->seqNum < st1->seqNum) && (insteven0->seqNum < st1->seqNum)){
                //     instold0 = instodd0;
                //     instold1 = insteven0;
                //     if(insteven1 || instodd1){
                //         if(insteven1 && instodd1){
                //             if(insteven1 <st0 && instodd1 < st0){
                //                 instold2 = insteven1;
                //                 instold3 = instodd1;
                //             }
                //             if(insteven1 > st0 && instodd1 < st0){
                //                 instold2 = instodd1;
                //                 instold3 = st0;
                //             }
                //             if(insteven1 < st0 && instodd1 > st0){
                //                 instold2 = insteven1;
                //                 instold3 = st0;
                //             }
                //         }
                //         else if(insteven1 && !instodd1){
                //             if(insteven1 < st0){
                //                 instold2 = insteven1;
                //                 instold3 = st0;
                //             }
                //             if(insteven1 > st0){
                //                 instold2 = st0;
                //                 instold3 = insteven1;
                //             }
                //         }
                //         else if(!insteven1 && instodd1){
                //             if(instodd1 < st0){
                //                 instold2 = instodd1;
                //                 instold3 = st0;
                //             }
                //             if(instodd1 > st0){
                //                 instold2 = st0;
                //                 instold3 = instodd1;
                //             }
                //         }
                //     }
                //     else {
                //         instold2 = st0;
                //         instold3 = st1;
                //     }
                // }
                // else if((instodd0->seqNum < st1->seqNum) && (insteven0->seqNum > st1->seqNum)){
                //     instold0 = st0;
                //     instold1 = instodd0;
                //     instold2 = st1;
                //     if(!readyStore.empty()){
                //         if(instodd1){
                //             if(instodd1->seqNum < readyStore.top()->seqNum){
                //                 instold3 = instodd1;
                //             }
                //             else if(insteven0){
                //                 if(insteven0->seqNum < readyStore.top()->seqNum){
                //                     instold3 = insteven0;
                //                 }
                //             }
                //         }
                //         else if(insteven0){
                //                 if(insteven0->seqNum < readyStore.top()->seqNum){
                //                     instold3 = insteven0;
                //                 }
                //             }
                //     }
                //     else {
                //        if(instodd1){
                //             instold3 = instodd1;
                //         }
                //         else if(insteven0){
                //             instold3 = insteven0;
                //         }
                //     }
                // }
            }
            else if(st0){
                //if((instodd0->seqNum > st0->seqNum) && (insteven0->seqNum > st0->seqNum)){
                    instold0 = st0;
                    instold1 = instodd0;
                    instold2 = insteven0;
                    if(instodd1 || insteven1){
                        if(!readyStore.empty()){
                            if(instodd1 && insteven1){
                                if(instodd1->seqNum < insteven1->seqNum){
                                    instold3 = instodd1;
                                }
                                else {
                                    instold3 = insteven1;
                                }
                            }
                            if(instodd1 && !insteven1){
                                instold3 = instodd1;
                            }
                            if(!instodd1 && insteven1){
                                instold3 = insteven1;
                            }
                            if(instold3){
                                if(instold3->seqNum > readyStore.top()->seqNum){
                                    instold3 = nullptr;
                                }
                            }
                        }else {
                            if(instodd1 && insteven1){
                                if(instodd1->seqNum < insteven1->seqNum){
                                    instold3 = instodd1;
                                }
                                else {
                                    instold3 = insteven1;
                                }
                            }
                            if(instodd1 && !insteven1){
                                instold3 = instodd1;
                            }
                            if(!instodd1 && insteven1){
                                instold3 = insteven1;
                            }
                        }
                    }
                // }
                // else if((instodd0->seqNum > st0->seqNum) && (insteven0->seqNum < st0->seqNum)){
                //     instold0 = insteven0;
                //     instold1 = st0;
                //     instold2 = instodd0;
                //     if(!readyStore.empty()){
                //         if(insteven1){
                //             instold3 = insteven1;
                //         }
                //         else if(instodd1){
                //             instold3 = instodd1;
                //         }
                //         if(instold3){
                //             if(instold3->seqNum > readyStore.top()){
                //                 instold3 = nullptr;
                //             }
                //         }
                //     }
                //     else {
                //         if(insteven1){
                //             instold3 = insteven1;
                //         }
                //         else if(instodd1){
                //             instold3 = instodd1;
                //         }
                //     }
                // }
                // else if((instodd0->seqNum < st0->seqNum) && (insteven0->seqNum > st0->seqNum)){
                //     instold0 = instodd0;
                //     instold1 = st0;
                //     instold2 = insteven0;
                //     if(!readyStore.empty()){
                //         if(instodd1){
                //             instold3 = instodd1;
                //         }
                //         else if(insteven1){
                //             instold3 = insteven1;
                //         }
                //         if(instold3){
                //             if(instold3->seqNum > readyStore.top()){
                //                 instold3 = nullptr;
                //             }
                //         }
                //     }
                //     else {
                //         if(instodd1){
                //             instold3 = instodd1;
                //         }
                //         else if(insteven1){
                //             instold3 = insteven1;
                //         }
                //     }
                // }
                // else if((instodd0->seqNum < st0->seqNum) && (insteven0->seqNum < st0->seqNum)){
                //     instold0 = instodd0;
                //     instold1 = insteven0;
                //     instold2 = st0;
                //     if(instodd1 && insteven1){
                //         if(instodd1->seqNum < insteven1){
                //             instold3 = instodd1;
                //         }
                //         else if(instodd1 && !insteven1){
                //             instold3 = instodd1;
                //         }
                //         else if(!instodd1 && insteven1){
                //             instold3 = insteven1;
                //         }
                //     }
                // }
            }
            else {
                if(insteven0){
                    instold0 = insteven0;
                }
                if(instodd0){
                    instold1 = instodd0;
                }
                if(insteven1){
                    instold2 = insteven1;
                }
                if(instodd1){
                    instold3 = instodd1;
                }
            }
        }

        if(instodd0 && !insteven0){
            if(st3){
                instold0 = st0;
                instold1 = st1;
                instold2 = st2;
                instold3 = st3;
            }
            else if(st2){
                instold0 = st0;
                instold1 = st1;
                instold2 = st2;
                instold3 = instodd0;
            }
            else if(st1){
                //if(instodd0->seqNum > st1->seqNum){
                    instold0 = st0;
                    instold1 = st1;
                    if(!readyStore.empty()){
                        if(instodd0->seqNum < readyStore.top()->seqNum){
                            instold2 = instodd0;
                            if(instodd1){
                                if(instodd1->seqNum < readyStore.top()->seqNum){
                                    instold3 = instodd1;
                                }
                            }
                        }
                    }
                    else {
                        instold2 = instodd0;
                        if(instodd1){
                            instold3 = instodd1;
                        }
                    }
                //}
                // else if(instodd0->seqNum < st1->seqNum){
                //     instold0 = st0;
                //     instold1 = instodd0;
                //     instold2 = st1;
                //     if(!readyStore.empty()){
                //         if(instodd1){
                //             if(instodd1->seqNum < readyStore.top()->seqNum){
                //                 instold3 = instodd1;
                //             }
                //         }
                //     }
                //     else {
                //         if(instodd1){
                //             instold3 = instodd1;
                //         }
                //     }
                // }
            }
            else if(st0){
                //if(instodd0->seqNum > st0->seqNum){
                    instold0 = st0;
                    instold1 = instodd0;
                    if(instodd1){
                        instold2 = instodd1;
                    }
                // }
                // else if(instodd0->seqNum < st0->seqNum){
                //     instold0 = instodd0;
                //     instold1 = st0;
                //     if(instodd1){
                //         instold2 = instodd1;
                //     }
                // }
            }
            else {
                if(instodd0){
                    instold0 = instodd0;
                }
                if(instodd1){
                    instold1 = instodd1;
                }
            }
        }

        if(!instodd0 && insteven0){
            if(st3){
                instold0 = st0;
                instold1 = st1;
                instold2 = st2;
                instold3 = st3;
            }
            else if(st2){
                instold0 = st0;
                instold1 = st1;
                instold2 = st2;
                instold3 = insteven0;
            }
            else if(st1){
                //if(insteven0->seqNum > st1->seqNum){
                    instold0 = st0;
                    instold1 = st1;
                    if(!readyStore.empty()){
                        if(insteven0->seqNum < readyStore.top()->seqNum){
                            instold2 = insteven0;
                            if(insteven1){
                                if(insteven1->seqNum < readyStore.top()->seqNum){
                                    instold3 = insteven1;
                                }
                            }
                        }
                    }
                    else {
                        instold2 = insteven0;
                        if(insteven1){
                            instold3 = insteven1;
                        }
                    }
                // }
                // else if(insteven0->seqNum < st1->seqNum){
                //     instold0 = st0;
                //     instold1 = insteven0;
                //     instold2 = st1;
                //     if(insteven1){
                //         instold3 = insteven1;
                //     }
                // }
            }
            else if(st0){
                //if(insteven0->seqNum > st0->seqNum){
                    instold0 = st0;
                    instold1 = insteven0;
                    if(insteven1){
                        instold2 = insteven1;
                    }
            //     }
            //     else if(insteven0->seqNum < st0->seqNum){
            //         instold0 = insteven0;
            //         instold1 = st0;
            //         if(insteven1){
            //             instold2 = insteven1;
            //         }
            //     }
            }
            else {
                if(insteven0){
                    instold0 = insteven0;
                }
                if(insteven1){
                    instold1 = insteven1;
                }
            }
        }
    }
    else if(st0 || st1 || st2 || st3){
        if(st0){
            instold0 = st0;
        }
        if(st1){
            instold1 = st1;
        }
        if(st2){
            instold2 = st2;
        }
        if(st3){
            instold3 = st3;
        }
    }
    else {
        DPRINTF(RxuWTB, "no ld ,no sta\n");
    }

    //choose oldest ready std,and to see whether it is odd or even
    DynInstPtr std0 = nullptr;
    DynInstPtr std1 = nullptr;
    DynInstPtr std2 = nullptr;
    DynInstPtr std3 = nullptr;
    bool stdbuffernull;
    if(!readySTdata0.empty()){
        std0 = readySTdata0.top();
        readySTdata0.pop();
        if(!readySTdata0.empty()){
            std1 = readySTdata0.top();
            readySTdata0.pop();
        }
        if(!readySTdata0.empty()){
            std2 = readySTdata0.top();
            readySTdata0.pop();
        }
        if(!readySTdata0.empty()){
            std3 = readySTdata0.top();
            readySTdata0.pop();
        }
    }

    if(instold0){
        cnt_issue++;
        if(instold0->isLoad()){
            if(instold0->strictlyOrdered() && instold0->isLoad() && instold0->lqIdx != dispipe3Stage->ldstQueue.thread.at(0).loadQueue.head()){
                readyLDSTodd0.push(instold0);
                DPRINTF(RxuWTB, "Load instruction PC %s can't issue due to not at loadqueue.head\n");
            }
            else {
                ibufferToEW.push(instold0);
                instold0->staIssued = true;
                DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold0->seqNum, instold0->ibuffer_id);
                ibuffer[instold0->threadNumber][instold0->ibuffer_id].remove(instold0);
                }
        }
        else if(instold0->isStore()){
            ibufferToEW.push(instold0);
            instold0->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold0->seqNum, instold0->ibuffer_id);
            ibuffer[instold0->threadNumber][instold0->ibuffer_id].remove(instold0);
        }
        else {
            ibufferToEW.push(instold0);
            instold0->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold0->seqNum, instold0->ibuffer_id);
            ibuffer[instold0->threadNumber][instold0->ibuffer_id].remove(instold0);
        }
    }

    if(instold1){
        cnt_issue++;
        if(instold1->isLoad()){
            if(instold1->strictlyOrdered() && instold1->isLoad() && instold1->lqIdx != dispipe3Stage->ldstQueue.thread.at(0).loadQueue.head()){
                readyLDSTodd0.push(instold1);
                DPRINTF(RxuWTB, "Load instruction PC %s can't issue due to not at loadqueue.head\n");
            }
            else {
                ibufferToEW.push(instold1);
                instold1->staIssued = true;
                DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold1->seqNum, instold1->ibuffer_id);
                ibuffer[instold1->threadNumber][instold1->ibuffer_id].remove(instold1);
                }
        }
        else if(instold1->isStore()){
            ibufferToEW.push(instold1);
            instold1->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold1->seqNum, instold1->ibuffer_id);
            ibuffer[instold1->threadNumber][instold1->ibuffer_id].remove(instold1);
        }
        else {
            ibufferToEW.push(instold1);
            instold1->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold1->seqNum, instold1->ibuffer_id);
            ibuffer[instold1->threadNumber][instold1->ibuffer_id].remove(instold1);
        }
    }

    if(instold2){
        cnt_issue++;
        if(instold2->isLoad()){
            if(instold2->strictlyOrdered() && instold2->isLoad() && instold2->lqIdx != dispipe3Stage->ldstQueue.thread.at(0).loadQueue.head()){
                readyLDSTodd0.push(instold2);
                DPRINTF(RxuWTB, "Load instruction PC %s can't issue due to not at loadqueue.head\n");
            }
            else {
                ibufferToEW.push(instold2);
                instold2->staIssued = true;
                DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold2->seqNum, instold2->ibuffer_id);
                ibuffer[instold2->threadNumber][instold2->ibuffer_id].remove(instold2);
                }
        }
        else if(instold2->isStore()){
            ibufferToEW.push(instold2);
            instold2->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold2->seqNum, instold2->ibuffer_id);
            ibuffer[instold2->threadNumber][instold2->ibuffer_id].remove(instold2);
        }
        else {
            ibufferToEW.push(instold2);
            instold2->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold2->seqNum, instold2->ibuffer_id);
            ibuffer[instold2->threadNumber][instold2->ibuffer_id].remove(instold2);
        }
    }

    if(instold3){
        cnt_issue++;
        if(instold3->isLoad()){
            if(instold3->strictlyOrdered() && instold3->isLoad() && instold3->lqIdx != dispipe3Stage->ldstQueue.thread.at(0).loadQueue.head()){
                readyLDSTodd0.push(instold3);
                DPRINTF(RxuWTB, "Load instruction PC %s can't issue due to not at loadqueue.head\n");
            }
            else {
                ibufferToEW.push(instold3);
                instold3->staIssued = true;
                DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold3->seqNum, instold3->ibuffer_id);
                ibuffer[instold3->threadNumber][instold3->ibuffer_id].remove(instold3);
                }
        }
        else if(instold3->isStore()){
            ibufferToEW.push(instold3);
            instold3->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold3->seqNum, instold3->ibuffer_id);
            ibuffer[instold3->threadNumber][instold3->ibuffer_id].remove(instold3);
        }
        else {
            ibufferToEW.push(instold3);
            instold3->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",instold3->seqNum, instold3->ibuffer_id);
            ibuffer[instold3->threadNumber][instold3->ibuffer_id].remove(instold3);
        }
    }

    if(std0){
        std0->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,oddstd0\n",std0->seqNum);
        std0->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready odd0,[sn:%llu]\n",std0->seqNum);
    }

    if(std1){
        std1->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,oddstd0\n",std1->seqNum);
        std1->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready odd0,[sn:%llu]\n",std1->seqNum);
    }

    if(std2){
        std2->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,oddstd0\n",std2->seqNum);
        std2->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready odd0,[sn:%llu]\n",std2->seqNum);
    }

    if(std3){
        std3->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,oddstd0\n",std3->seqNum);
        std3->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready odd0,[sn:%llu]\n",std3->seqNum);
    }

    if(st0){
        if(!st0->ifstdready){
            st0->if_exe_second = true;
        }
    }

    if(st1){
        if(!st1->ifstdready){
            st1->if_exe_second = true;
        }
    }

    if(st2){
        if(!st2->ifstdready){
            st2->if_exe_second = true;
        }
    }

    if(st3){
        if(!st3->ifstdready){
            st3->if_exe_second = true;
        }
    }


    if(insteven0){
        if(insteven0 == instold0 || insteven0 == instold1 ||
        insteven0 == instold2 || insteven0 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",insteven0->seqNum, insteven0->staticInst->disassemble(insteven0->pcState().instAddr()));
        }
        else if(insteven0->isStore()){
            readyStore.push(insteven0);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", insteven0->seqNum);
        }
        else {
            readyLD_Dst_even.push(insteven0);
        }
    }

    if(insteven1){
        if(insteven1 == instold0 || insteven1 == instold1 ||
        insteven1 == instold2 || insteven1 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",insteven1->seqNum, insteven1->staticInst->disassemble(insteven1->pcState().instAddr()));
        }
        else if(insteven1->isStore()){
            readyStore.push(insteven1);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", insteven1->seqNum);
        }
        else {
            readyLD_Dst_even.push(insteven1);
        }
    }

    if(instodd0){
        if(instodd0 == instold0 || instodd0 == instold1 ||
        instodd0 == instold2 || instodd0 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",instodd0->seqNum, instodd0->staticInst->disassemble(instodd0->pcState().instAddr()));
        }
        else if(instodd0->isStore()){
            readyStore.push(instodd0);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", instodd0->seqNum);
        }
        else {
            readyLD_Dst_odd.push(instodd0);
        }
    }

    if(instodd1){
        if(instodd1 == instold0 || instodd1 == instold1 ||
        instodd1 == instold2 || instodd1 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",instodd1->seqNum, instodd1->staticInst->disassemble(instodd1->pcState().instAddr()));
        }
        else if(instodd1->isStore()){
            readyStore.push(instodd1);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", instodd1->seqNum);
        }
        else {
            readyLD_Dst_odd.push(instodd1);
        }
    }

    if(st0){
        if(st0 == instold0 || st0 == instold1 ||
        st0 == instold2 || st0 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",st0->seqNum, st0->staticInst->disassemble(st0->pcState().instAddr()));
        }
        else {
            readyStore.push(st0);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", st0->seqNum);
        }
    }

    if(st1){
        if(st1 == instold0 || st1 == instold1 ||
        st1 == instold2 || st1 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",st1->seqNum, st1->staticInst->disassemble(st1->pcState().instAddr()));
        }
        else {
            readyStore.push(st1);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", st1->seqNum);
        }
    }

    if(st2){
        if(st2 == instold0 || st2 == instold1 ||
        st2 == instold2 || st2 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",st2->seqNum, st2->staticInst->disassemble(st2->pcState().instAddr()));
        }
        else {
            readyStore.push(st2);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", st2->seqNum);
        }
    }

    if(st3){
        if(st3 == instold0 || st3 == instold1 ||
        st3 == instold2 || st3 == instold3){
            DPRINTF(RxuWTB, "inst[sn:%llu] issue: \"%s\"\n",st3->seqNum, st3->staticInst->disassemble(st3->pcState().instAddr()));
        }
        else {
            readyStore.push(st3);
            DPRINTF(RxuWTB, "add store inst[sn:%llu] to readystore :%i\n", st3->seqNum);
        }
    }

    DPRINTF(RxuWTB, "the number of ldst issue this circle : %i \n",cnt_issue);

    for (auto &req : cpu->dispipe3.ldstQueue.requestVector) {
            // if (reqeven0->savedRequest->stdatatrue == true) {
            //     cpu->dispipe3.ldstQueue.write(reqeven0->savedRequest,valeven0,std0->lqIdx);
            //     cpu->dispipe3.ldstQueue.requestVector.erase(cpu->dispipe3.ldstQueue.requestVector.begin()+i);
            //     break;
            // }
            if (req->ifstdready == true) {
                if(req->isSquashed()){
                    continue;
                }
                DPRINTF(RxuWTB, "Execute:std even0 store,[sn:%llu]\n",req->seqNum);
                // Fault fault = cpu->dispipe3.ldstQueue.executeStore(reqeven0);
                // if (reqeven0->isTranslationDelayed() &&
                //     fault == NoFault) {
                //     // A hw page table walk is currently going on; the
                //     // instruction must be deferred.
                //     DPRINTF(RxuEW, "Execute: Delayed translation, deferring "
                //             "store.\n");
                //     dispipe3Stage->wtb.deferMemInst(reqeven0);
                //     continue;
                // }
                // // If the store had a fault then it may not have a mem req
                // if (fault != NoFault || !reqeven0->readPredicate() ||
                //         !reqeven0->isStoreConditional()) {
                //     // If the instruction faulted, then we need to send it
                //     // along to commit without the instruction completing.
                //     // Send this instruction to commit, also make sure iew
                //     // stage realizes there is activity.
                //     reqeven0->setExecuted();
                //     cpu->ew.instToCommit(reqeven0);
                //     cpu->ew.activityThisCycle();
                // }
                // cpu->ew.insts->push_front(reqeven0);

                if (req->srcRegIdx(1).classValue() == FloatRegClass) {
                        ibufferToEW.push(req);
                        req->stdNeedExeScd_issue = true;
                        //cpu->ew.insts[0].push_front(reqeven0);
                    // } else {
                    //     stsrc1notrdy.push_back(req);
                    // }
                } else {
                    // if (ldstRegReadFreeNums >= 1) {
                    //     ldstRegReadFreeNums--;
                        ibufferToEW.push(req);
                        req->stdNeedExeScd_issue = true;
                        //cpu->ew.insts[0].push_front(reqeven0);
                    // } else {
                    //     stsrc1notrdy.push_back(req);
                    // }
                }
            }
            else {
                stsrc1notrdy.push_back(req);
            }
        }


    cpu->dispipe3.ldstQueue.requestVector.clear();
    for (auto &req_return : stsrc1notrdy){
        cpu->dispipe3.ldstQueue.requestVector.push_back(req_return);
    }
    stsrc1notrdy.clear();



}

void
WTB::selectLdSt()
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5  ls3  ls4
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13  14   15
    //choose even oldest ldst instruction
    DynInstPtr inst;
    DynInstPtr insteven0;
    DynInstPtr insteven1;
    DynInstPtr instevenoldest;
    bool evenbuffernull;
    DynInstPtr stEven0_1 = nullptr;
    DynInstPtr stEven1_1 = nullptr;
    DynInstPtr stOdd0_1 = nullptr;
    DynInstPtr stOdd1_1 = nullptr;
    DynInstPtr stEven0_2 = nullptr;
    DynInstPtr stEven1_2 = nullptr;
    DynInstPtr stOdd0_2 = nullptr;
    DynInstPtr stOdd1_2 = nullptr;


    // evenbuffernull = readyLDSTeven0.empty() && readyLDSTeven1.empty();
    evenbuffernull = readyLDSTeven0.empty();

    if(!evenbuffernull){
        if((!readyLDSTeven0.empty())){
                insteven0 = readyLDSTeven0.top();
                instevenoldest = insteven0;
        }
    }
    //choose odd oldest ldst instruction
    DynInstPtr instodd0;
    DynInstPtr instodd1;
    DynInstPtr instoddoldest;
    bool oddbuffernull;
    // oddbuffernull = readyLDSTodd0.empty() && readyLDSTodd1.empty();
    oddbuffernull = readyLDSTodd0.empty();


    if(!oddbuffernull){
        if((!readyLDSTodd0.empty())){
                instodd0 = readyLDSTodd0.top();
                instoddoldest = instodd0;
        }
    }

   //issue choised instruction
    if(!oddbuffernull || !evenbuffernull){

        if(instoddoldest == instodd0 && (instoddoldest != 0)) {              // sta or ld
            inst = readyLDSTodd0.top();
            readyLDSTodd0.pop();
            if (inst->isLoad()) {      // ld
                if(inst->strictlyOrdered() && inst->isLoad() && inst->lqIdx != dispipe3Stage->ldstQueue.thread.at(0).loadQueue.head()){
                    readyLDSTodd0.push(inst);
                    DPRINTF(RxuWTB, "Load instruction PC %s can't issue due to not at loadqueue.head\n");
                }
                else {
                // else if (ldstRegReadFreeNums >= inst->numSrcRegs()) {
                //     ldstRegReadFreeNums -= inst->numSrcRegs();
                    ibufferToEW.push(inst);
                    instodd0->staIssued = true;
                    DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
                    ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
                    // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
                    // count[inst->threadNumber][inst->ibuffer_id]--;
                    stOdd0_1 = inst;
                    // if(!inst->ifstdready){
                    //     inst->if_exe_second = true;
                    // }
                }
            } else if (inst->isStore()) {        // sta
                // if (ldstRegReadFreeNums >= 1) {
                //     ldstRegReadFreeNums--;
                    ibufferToEW.push(inst);
                    instodd0->staIssued = true;
                    DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
                    ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
                    // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
                    // count[inst->threadNumber][inst->ibuffer_id]--;
                    stOdd0_1 = inst;
            } else {                         // other
                // if (ldstRegReadFreeNums >= inst->numSrcRegs()) {
                //     ldstRegReadFreeNums -= inst->numSrcRegs();
                    ibufferToEW.push(inst);
                    instodd0->staIssued = true;
                    DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_1, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
                    ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
                    // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
                    // count[inst->threadNumber][inst->ibuffer_id]--;
                    stOdd0_1 = inst;
            }
        }


        if(instevenoldest == insteven0 && (instevenoldest != 0)){             // sta or ld
            inst = readyLDSTeven0.top();
            readyLDSTeven0.pop();

            if (inst->isLoad()) {      // ld
                if(inst->strictlyOrdered() && inst->isLoad() && inst->lqIdx != dispipe3Stage->ldstQueue.thread.at(0).loadQueue.head()){
                    readyLDSTodd0.push(inst);
                    DPRINTF(RxuWTB, "Load instruction PC %s can't issue due to not at loadqueue.head\n");
                }
                else {
                // else if (ldstRegReadFreeNums >= inst->numSrcRegs()) {
                    //ldstRegReadFreeNums -= inst->numSrcRegs();
                    ibufferToEW.push(inst);
                    insteven0->staIssued = true;
                    DPRINTF(RxuWTB, "Instruction add to ibuffertoew even0_1, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
                    ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
                    // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
                    // count[inst->threadNumber][inst->ibuffer_id]--;
                    stEven0_1 = inst;
                }
                    // if(!inst->ifstdready){
                    //     inst->if_exe_second = true;
                    // }
                // } else {
                //     readyLDSTeven0.push(inst);
                //     DPRINTF(RxuWTB, "Load instruction PC %s can't issue due to lack of ldst read regfile port, "
                //         "[sn:%llu], ibuffer_id = %i\n", inst->pcState(),
                //         inst->seqNum, inst->ibuffer_id);
                // }
            } else if (inst->isStore()) {        // sta
                // if (ldstRegReadFreeNums >= 1) {
                //     ldstRegReadFreeNums--;
                    ibufferToEW.push(inst);
                    insteven0->staIssued = true;
                    DPRINTF(RxuWTB, "Instruction add to ibuffertoew even0_1, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
                    ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
                    // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
                    // count[inst->threadNumber][inst->ibuffer_id]--;
                    stEven0_1 = inst;
                    // if(!inst->ifstdready){
                    //     inst->if_exe_second = true;
                    // }

            }
                // } else {
                //     readyLDSTeven0.push(inst);
                //     DPRINTF(RxuWTB, "Store instruction PC %s can't issue due to lack of ldst read regfile port, "
                //         "[sn:%llu], ibuffer_id = %i\n", inst->pcState(),
                //         inst->seqNum, inst->ibuffer_id);
                // }
            else {                         // other
                // if (ldstRegReadFreeNums >= inst->numSrcRegs()) {
                //     ldstRegReadFreeNums -= inst->numSrcRegs();
                    ibufferToEW.push(inst);
                    insteven0->staIssued = true;
                    DPRINTF(RxuWTB, "Instruction add to ibuffertoew even0_1, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
                    ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
                    // freeEntries[inst->threadNumber][inst->ibuffer_id]++;
                    // count[inst->threadNumber][inst->ibuffer_id]--;
                    stEven0_1 = inst;
                    // if(!inst->ifstdready){
                    //     inst->if_exe_second = true;
                    // }
            }
        }
    }
    insteven0 = nullptr;
    insteven1 = nullptr;
    instevenoldest = nullptr;
    instodd0 = nullptr;
    instodd1 = nullptr;
    instoddoldest = nullptr;
    bool even0isload = false;
    bool even1isload = false;
    bool odd0isload = false;
    bool odd1isload = false;
    //recording first oldest instruction,and choose oldest instruction of remaining part

    DynInstPtr instevenoldestsc = nullptr;
    DynInstPtr instoddoldestsc = nullptr;
    DynInstPtr insteven0sc = nullptr;
    DynInstPtr insteven1sc = nullptr;
    DynInstPtr instodd0sc = nullptr;
    DynInstPtr instodd1sc = nullptr;
    // oddbuffernull = readyLDSTodd0.empty() && readyLDSTodd1.empty();
    // evenbuffernull = readyLDSTeven0.empty() && readyLDSTeven1.empty();
    oddbuffernull = readyLDSTodd0.empty();
    evenbuffernull = readyLDSTeven0.empty();

    if(!evenbuffernull){
            if((!readyLDSTeven0.empty()) ||
            (!readyLDSTeven0.empty() && readyLDSTeven0.top()->isLoad())){
                if(readyLDSTeven0.top()->isLoad()){
                    insteven0sc = readyLDSTeven0.top();
                    instevenoldestsc = insteven0sc;
                    even0isload = true;
                }
            }
        }


    if(!oddbuffernull){
            if((!readyLDSTodd0.empty()) ||
            ((!readyLDSTodd0.empty())&& readyLDSTodd0.top()->isLoad())){
                if(readyLDSTodd0.top()->isLoad()){
                    instodd0sc = readyLDSTodd0.top();
                    instoddoldestsc = instodd0sc;
                    odd0isload = true;
                }
            }
        }
    src1type std0src1type = unknow;
    src1type std1src1type = unknow;
    //choose oldest ready std,and to see whether it is odd or even
    DynInstPtr std0;
    DynInstPtr std1;
    bool ifstd0src1odd;
    bool ifstd1src1odd;
    bool stdbuffernull;
    stdbuffernull = readySTdata0.empty() && readySTdata1.empty();
    if(!readySTdata0.empty()){
        std0 = readySTdata0.top();
        const RegId &src_reg0 = std0->srcRegIdx(1);
        ifstd0src1odd = src_reg0 & 1 ? true : false;
        if(ifstd0src1odd){
            std0src1type = odd;
        }
        else if(ifstd0src1odd == false){
            std0src1type = even;
        }
        else {
            std0src1type = unknow;
        }
    }
    if(!readySTdata1.empty()){
        std1 = readySTdata1.top();
        const RegId &src_reg1 = std1->srcRegIdx(1);
        ifstd1src1odd = src_reg1 & 1 ? true : false;
        if(ifstd1src1odd){
            std1src1type = odd;
        }
        else if(ifstd1src1odd == false){
            std1src1type = even;
        }
        else {
            std1src1type = unknow;
        }
    }

    //choose the second even instruction will be issued
    DynInstPtr secdoldestissue_even;
    DynInstPtr secdoldestissue_even_std;
    bool ifissueevensecond;
    ifissueevensecond = !readySTdata0.empty() || !evenbuffernull || !readySTdata1.empty();

    if(ifissueevensecond){
        if((!stdbuffernull)){
            if(readySTdata0.empty()){
                if(std1src1type == even){
                    secdoldestissue_even_std = std1;
                }
            }
            if(readySTdata1.empty()){
                if(std0src1type == even){
                    secdoldestissue_even_std = std0;
                }
            }
            if((!readySTdata0.empty()) && (!readySTdata1.empty())){
                if((std1src1type == even) && (std0src1type == even)){
                    if(std0->seqNum < std1->seqNum){
                        secdoldestissue_even_std = std0;
                    }
                    else {
                        secdoldestissue_even_std = std1;
                    }
                }
                if((std1src1type != even) && (std0src1type == even)){
                        secdoldestissue_even_std = std0;
                }
                if((std1src1type == even) && (std0src1type != even)){
                        secdoldestissue_even_std = std1;
                }
            }
        }

        if((!evenbuffernull)){
            if(even0isload || even1isload){
                if(instevenoldestsc->isLoad()){
                    secdoldestissue_even = instevenoldestsc;
                }
            }
        }

    }

    if(secdoldestissue_even == instevenoldestsc && instevenoldestsc == insteven0sc
        && (secdoldestissue_even != 0) && instevenoldestsc->isLoad()){              // ld
        inst = readyLDSTeven0.top();
        readyLDSTeven0.pop();

        insteven0sc = nullptr;
        ibufferToEW.push(inst);
        inst->ifstaready = true;
        even0isload = false;
        instevenoldestsc->staIssued = true;
        DPRINTF(RxuWTB, "Instruction add to ibuffertoew even0_2, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
        ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
    }

    if(secdoldestissue_even_std == std0 && (secdoldestissue_even_std != 0)){
        std0->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,evenstd0\n",std0->seqNum);
        std0->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready even0,[sn:%llu]\n",std0->seqNum);
        readySTdata0.pop();
        std0 = nullptr;
        std0src1type = unknow;
        if(!readySTdata0.empty()){
            std0 = readySTdata0.top();
            const RegId &src_reg0 = std0->srcRegIdx(1);
            ifstd0src1odd = src_reg0 & 1 ? true : false;
            if(ifstd0src1odd){
                std0src1type = odd;
            }
            else if(ifstd0src1odd == false){
                std0src1type = even;
            }
            else {
                std0src1type = unknow;
            }
        }
    }
    if(secdoldestissue_even_std == std1 && (secdoldestissue_even_std != 0)){
        std1->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,evenstd1\n",std1->seqNum);
        std1->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready even1,[sn:%llu]\n",std1->seqNum);
        readySTdata1.pop();
        std1 = nullptr;
        std1src1type = unknow;
        if(!readySTdata1.empty()){
            std1 = readySTdata1.top();
            const RegId &src_reg1 = std1->srcRegIdx(1);
            ifstd1src1odd = src_reg1 & 1 ? true : false;
            if(ifstd1src1odd){
                std1src1type = odd;
            }
            else if(ifstd1src1odd == false){
                std1src1type = even;
            }
            else {
                std1src1type = unknow;
            }
        }
    }

    //choose the second even instruction will be issued
    DynInstPtr secdoldestissue_odd;
    DynInstPtr secdoldestissue_odd_std;
    bool ifissueoddsecond;
    ifissueoddsecond = !readySTdata0.empty() || !oddbuffernull || !readySTdata1.empty();

    if(ifissueoddsecond){
        if((!stdbuffernull)){
            if(readySTdata0.empty()){
                if(std1src1type == odd){
                    secdoldestissue_odd_std = std1;
                }
            }
            if(readySTdata1.empty()){
                if(std0src1type == odd){
                    secdoldestissue_odd_std = std0;
                }
            }
            if((!readySTdata0.empty()) && (!readySTdata1.empty())){
                if((std1src1type == odd) && (std0src1type == odd)){
                    if(std0->seqNum < std1->seqNum){
                        secdoldestissue_odd_std = std0;
                    }
                    else {
                        secdoldestissue_odd_std = std1;
                    }
                }
                if((std1src1type != odd) && (std0src1type == odd)){
                        secdoldestissue_odd_std = std0;
                }
                if((std1src1type == odd) && (std0src1type != odd)){
                        secdoldestissue_odd_std = std1;
                }
            }
        }

        if( (!oddbuffernull)){
            if(odd0isload || odd1isload){
                secdoldestissue_odd = instoddoldestsc;
            }
        }

    }

    if(secdoldestissue_odd == instoddoldestsc && instoddoldestsc == instodd0sc
        && (secdoldestissue_odd != 0) && instoddoldestsc->isLoad()){           // ld
        inst = readyLDSTodd0.top();
        readyLDSTodd0.pop();

            instodd0sc = nullptr;
            ibufferToEW.push(inst);
            inst->ifstaready = true;
            odd0isload = false;
            instoddoldestsc->staIssued = true;
            DPRINTF(RxuWTB, "Instruction add to ibuffertoew odd0_2, [sn:%llu], ibuffer_id = %i.\n",inst->seqNum, inst->ibuffer_id);
            ibuffer[inst->threadNumber][inst->ibuffer_id].remove(inst);
    }

    if(secdoldestissue_odd_std == std0 && (secdoldestissue_odd_std != 0)){
        std0->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,oddstd0\n",std0->seqNum);
        std0->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready odd0,[sn:%llu]\n",std0->seqNum);
        readySTdata0.pop();
        std0 = nullptr;
        std0src1type = unknow;
        std1src1type = unknow;
    }
    if(secdoldestissue_odd_std == std1 && (secdoldestissue_odd_std != 0)){
        std1->ifstdissued = true;
        DPRINTF(RxuWTB, "store instruction [sn:%llu] stdready,oddstd1\n",std1->seqNum);
        std1->ifstdready =true;
        DPRINTF(RxuWTB, "Execute:std ready odd1,[sn:%llu]\n",std1->seqNum);
        readySTdata1.pop();
        std1 = nullptr;
        std1src1type = unknow;
        std0src1type = unknow;
    }

    if(stOdd0_1){
        if(!stOdd0_1->ifstdready){
            stOdd0_1->if_exe_second = true;
        }
        stOdd0_1 = nullptr;
    }

    if(stOdd0_2){
        if(!stOdd0_2->ifstdready){
            stOdd0_2->if_exe_second = true;
        }
        stOdd0_2 = nullptr;
    }

    if(stOdd1_1){
        if(!stOdd1_1->ifstdready){
            stOdd1_1->if_exe_second = true;
        }
        stOdd1_1 = nullptr;

    }


    if(stEven0_1){
        if(!stEven0_1->ifstdready){
            stEven0_1->if_exe_second = true;
        }
        stEven0_1 = nullptr;
    }

    if(stEven0_2){
        if(!stEven0_2->ifstdready){
            stEven0_2->if_exe_second = true;
        }
        stEven0_2 = nullptr;
    }

    if(stEven1_1){
        if(!stEven1_1->ifstdready){
            stEven1_1->if_exe_second = true;
        }
        stEven1_1 = nullptr;
    }

    for (auto &req : cpu->dispipe3.ldstQueue.requestVector) {
            if (req->ifstdready == true) {
                if(req->isSquashed()){
                    continue;
                }
                DPRINTF(RxuWTB, "Execute:std even0 store,[sn:%llu]\n",req->seqNum);

                if (req->srcRegIdx(1).classValue() == FloatRegClass) {
                    if (fstRegReadFreeNums >= 1) {
                        fstRegReadFreeNums--;
                        ibufferToEW.push(req);
                        req->stdNeedExeScd_issue = true;
                        //cpu->ew.insts[0].push_front(reqeven0);
                    } else {
                        stsrc1notrdy.push_back(req);
                    }
                } else {
                    // if (ldstRegReadFreeNums >= 1) {
                    //     ldstRegReadFreeNums--;
                        ibufferToEW.push(req);
                        req->stdNeedExeScd_issue = true;
                        //cpu->ew.insts[0].push_front(reqeven0);
                    // } else {
                    //     stsrc1notrdy.push_back(req);
                    // }
                }
            }
            else {
                stsrc1notrdy.push_back(req);
            }
        }


    cpu->dispipe3.ldstQueue.requestVector.clear();
    for (auto &req_return : stsrc1notrdy){
        cpu->dispipe3.ldstQueue.requestVector.push_back(req_return);
    }
    stsrc1notrdy.clear();
}

void
WTB::setFreeReadRegPorts()
{
    intNormalOddRegReadFreeNums  = intNormalOddRegReadNums;
    intSpecialOddRegReadFreeNums = intSpecialOddRegReadNums;
    intNormalEvenRegReadFreeNums   = intNormalEvenRegReadNums;
    intSpecialEvenRegReadFreeNums  = intSpecialEvenRegReadNums;
    ldstRegReadFreeNums  = ldstRegReadNums;
    fstRegReadFreeNums = fstRegReadNums;
    fpNormalRegReadFreeNums   = fpNormalRegReadNums;
    fpSpecialRegReadFreeNums  = fpSpecialRegReadNums;
    vectorRegReadFreeNums = vectorRegWriteNums;

    // fwdInsts.pop_front();
    // fwdInsts.push_back(std::unordered_map<InstSeqNum, Srcs>());



    // intNormalOddRegReadFreeNums  = intNormalOddRegReadNums + 100;
    // intSpecialOddRegReadFreeNums = intSpecialOddRegReadNums + 100;
    // intNormalEvenRegReadFreeNums   = intNormalEvenRegReadNums + 100;
    // intSpecialEvenRegReadFreeNums  = intSpecialEvenRegReadNums + 100;
    // ldstRegReadFreeNums  = ldstRegReadNums + 100;
    // fstRegReadFreeNums = fstRegReadNums + 100;
    // fpNormalRegReadFreeNums   = fpNormalRegReadNums + 100;
    // fpSpecialRegReadFreeNums  = fpSpecialRegReadNums + 100;

}

void
WTB::updateWbNums()
{
    WbNums newWbNums;

    newWbNums.intNormalOddWbNums = 2;
    newWbNums.intSpecialOddWbNums = 1;
    newWbNums.intNormalEvenWbNums = 2;
    newWbNums.intSpecialEvenWbNums = 1;
    newWbNums.fpWbNums = 3;
    newWbNums.fldWbNums = 1;

    wbNums.pop_front();
    wbNums.push_back(newWbNums);
}

void
WTB::processIntDivCompletion()
{
    DPRINTF(RxuWTB, "A int div pipeline turns free.\n");
    pipelineUseNums.intDivNums++;
}

void
WTB::processFpDivCompletion()
{
    DPRINTF(RxuWTB, "A float div pipeline turns free.\n");
    pipelineUseNums.fpDivNums++;
}

// void
// WTB::processFpSqrtCompletion()
// {
//     DPRINTF(RxuWTB, "A float sqrt pipeline turns free.\n");
//     pipelineUseNums.fpSqrtNums++;
// }

void
WTB::processEarlyWakeUp(DynInstPtr &inst)
{
    if (inst->isSquashed()) {
        return;
    }
    DPRINTF(RxuWTB, "Early wake up.\n");
    DPRINTF(RxuWTB, "Instruction [sn:%i] is: %s.\n", inst->seqNum,
            inst->staticInst->disassemble(inst->pc->instAddr()));

    dispipe3Stage->rmu->wakeDependents(inst);

    for (int i = 0; i < inst->numDestRegs(); i++) {
        // Mark register as ready if not pinned
        if (inst->renamedDestIdx(i)->
                getNumPinnedWritesToComplete() == 0) {
            DPRINTF(RxuWTB,"Setting Destination Register %i (%s)\n",
                    inst->renamedDestIdx(i)->index(),
                    inst->renamedDestIdx(i)->className());
            cpu->scoreboard.setReg(inst->renamedDestIdx(i));
        }
    }
}

void
WTB::processreadySrc_readReg(DynInstPtr &inst)
{
    DynInstPtr new_inst = inst;
    if(new_inst->isSquashed()){
        return;
    }
    // DPRINTF(RxuRename,"ibuffer6 size:%i\n", ibuffer[0][6].size());
    // for (const auto& instPtr : ibuffer[0][6]) {
    //     DPRINTF(RxuRename,"ibuffer6 instnum:%i\n", instPtr->seqNum);
    // }
    if(new_inst->ifstdataready && new_inst->stdReady == false && new_inst->isStore()){
        // const RegId &src_reg = new_inst->srcRegIdx(0);
        // bool wheOE = src_reg & 1 ? true : false;
        bool wheOE = new_inst->renamedSrcIdx(0)->index() & 1;
        //bool wheOE = (readyLDSTodd0.size() <=  readyLDSTeven0.size()) ? true : false;
        // if(!wheOE){
            readySTdata0.push(new_inst);
            new_inst->stdReady = true;
            DPRINTF(RxuRename,
            "[tid:%i] "
            "SRC1 Register [sn:%llu] add to readystdata0\n", new_inst->threadNumber, new_inst->seqNum);
        //}
        // else {
        //     readySTdata1.push(new_inst);
        //     new_inst->stdReady = true;
        //     DPRINTF(RxuRename,
        //     "[tid:%i] "
        //     "SRC1 Register [sn:%llu] add to readystdata1\n", new_inst->threadNumber, new_inst->seqNum);
        // }
    }

    if(new_inst->isStore() && new_inst->staReady == false ){
        memDepUnit[new_inst->threadNumber].insert(new_inst);
    }
    else if (new_inst->isMemRef() && !new_inst->isStore()) {
        memDepUnit[new_inst->threadNumber].insert(new_inst);
    } else {
        addIfReady(new_inst);
    }
}

void
WTB::processLdstWakeToIssue(DynInstPtr &inst)
{
    if (inst->isSquashed()) {
        return;
    }
    DPRINTF(RxuWTB, "Load/store instruction issued, reached Issue1.\n");
    DPRINTF(RxuWTB, "Instruction [sn:%i] is: %s.\n", inst->seqNum,
            inst->staticInst->disassemble(inst->pc->instAddr()));

    if (inst->firstIssue == -1) {
            inst->lastWakeDependents = curTick();
    }

    if(inst->isStore() && inst->num_exe == 1){
                    if(inst->stdNeedExeScd_issue){
                        for (auto it = memDepUnit[0].ldstdep.begin();it != memDepUnit[0].ldstdep.end(); ++it) {
                            if(it->first == inst->seqNum){

                                for(auto ldstdeplistit = it->second.begin(); ldstdeplistit != it->second.end();
                                ldstdeplistit++){
                                    for (auto ldqit = memDepUnit[0].ldq.begin();ldqit != memDepUnit[0].ldq.end(); ++ldqit) {
                                            if(ldqit->first == *ldstdeplistit){
                                                ldqit->second->num_stqldq = ldqit->second->num_stqldq -1;
                                                // it->second.erase(ldstdeplistit);
                                                // if(ldstdeplistit != it->second.end()){
                                                //     ldstdeplistit--;
                                                // }
                                                DPRINTF(RxuWTB, "inst seqnum:%i stqldqnum is:%i\n",ldqit->second->seqNum,ldqit->second->num_stqldq);
                                                if(ldqit->second->num_stqldq == 0 && ldqit->second->seqNum > memDepUnit[0].stq.begin()->first
                                                    && memDepUnit[0].stq.begin()->first > ldqit->second->ldfindvio){
                                                    ld_needwait.push_back(ldqit->second);
                                                    ldqit->second->stq_head = memDepUnit[0].stq.begin()->first;
                                                    DPRINTF(RxuWTB, "load [sn:%i] need wait st go\n",ldqit->second->seqNum);
                                                }
                                                else if(ldqit->second->num_stqldq == 0){
                                                    // if (ldstRegReadFreeNums >= ldqit->second->numSrcRegs()) {
                                                    //     ldstRegReadFreeNums -= ldqit->second->numSrcRegs();
                                                        ldqit->second->ststdstat = 1;
                                                        // ldq[it->second]->ststdstat = 1;
                                                        ibufferToEW.push(ldqit->second);
                                                        //ldqit->second->ldfindvio = 0;
                                                        DPRINTF(RxuWTB, "inst seqnum:[sn:%i] to insttoew \n",ldqit->second->seqNum);
                                                        //cpu->ew.insts[0].push_back(ldqit->second);
                                                        //memDepUnit[0].ldq.erase(ldqit->first);
                                                    // } else {
                                                    //     ldWait.emplace_back(ldqit->first, ldqit->second);
                                                    //     DPRINTF(RxuWTB, "inst seqnum:[sn:%i] to ldwait \n",ldqit->second->seqNum);
                                                    // }
                                                }
                                            }
                                    }
                                }
                            }

                        }
                    memDepUnit[0].ldstdep.erase(inst->seqNum);
                    }
    }
    cpu->ew.instsToExecute.push_back(inst);

}

} // namespace rxuo3
} // namespace gem5
// ----------------------------------------------------------------------------

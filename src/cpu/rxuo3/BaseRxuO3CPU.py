from m5.defines import buildEnv
from m5.objects.BaseCPU import BaseCPU

# from m5.objects.O3Checker import O3Checker
from m5.objects.BranchPredictor import *
from m5.objects.Cache import IPrefetch
from m5.objects.RxuFUPool import *
from m5.params import *
from m5.proxy import *


class RxuSMTFetchPolicy(ScopedEnum):
    vals = [
        "RoundRobin",
        "Branch",
        # "IQCount", "LSQCount"
    ]


class RxuSMTQueuePolicy(ScopedEnum):
    vals = ["Dynamic", "Partitioned", "Threshold"]


class RxuCommitPolicy(ScopedEnum):
    vals = ["RoundRobin", "OldestReady"]


class BaseRxuO3CPU(BaseCPU):
    type = "BaseRxuO3CPU"
    cxx_class = "gem5::rxuo3::CPU"
    cxx_header = "cpu/rxuo3/dyn_inst.hh"

    @classmethod
    def memory_mode(cls):
        return "timing"

    @classmethod
    def require_caches(cls):
        return True

    @classmethod
    def support_take_over(cls):
        return True

    activity = Param.Unsigned(0, "Initial count")

    cacheStorePorts = Param.Unsigned(4, "Cache Ports. Constrains stores only.")
    cacheLoadPorts = Param.Unsigned(4, "Cache Ports. Constrains loads only.")

    # dhrystone: 0x70003330     coremark: 0x70004c74
    loopPC = Param.Addr(0x70003330, "Loop PC")

    # dhrystone: 0x70003330     coremark: 0x70004c74
    SpecificPC = Param.Addr(0x70003330, "Loop PC")

    loopstartPC = Param.Addr(0x70003330, "Loop start PC")
    loopendPC = Param.Addr(0x70003330, "Loop end PC")

    # iprefetch
    iprefetch = Param.IPrefetch(NULL, "Iprefetch ")

    # fetch params
    Bpu0ToFetchDelay = Param.Cycles(1, "Bpu0 to fetch delay")
    Bpu1ToFetchDelay = Param.Cycles(1, "Bpu1 to fetch delay")
    IBandLBToFetchDelay = Param.Cycles(1, "IBandLB to fetch delay")
    decodeToFetchDelay = Param.Cycles(1, "Decode to fetch delay")
    predisqToFetchDelay = Param.Cycles(1, "Predisq to fetch delay")
    dispipe0ToFetchDelay = Param.Cycles(1, "Dispipe0 to fetch delay")
    dispipe1ToFetchDelay = Param.Cycles(1, "Dispipe1 to fetch delay")
    dispipe2ToFetchDelay = Param.Cycles(1, "Dispipe2 to fetch delay")
    dispipe3ToFetchDelay = Param.Cycles(1, "Dispipe3 to fetch delay")
    ewToFetchDelay = Param.Cycles(1, "Execute/Writeback to fetch delay")
    commitToFetchDelay = Param.Cycles(1, "Commit to fetch delay")
    fetchWidth = Param.Unsigned(32, "Fetch width")
    fetchBufferSize = Param.Unsigned(64, "Fetch buffer size in bytes")
    fetchQueueSize = Param.Unsigned(
        64, "Fetch queue size in micro-ops per-thread"
    )

    # bpu0 params
    bpu1ToBpu0Delay = Param.Cycles(1, "Bpu1 to bpu0 delay")
    IBandLBToBpu0Delay = Param.Cycles(1, "IBandLB to bpu0 delay")
    decodeToBpu0Delay = Param.Cycles(1, "Decode to bpu0 delay")
    predisqToBpu0Delay = Param.Cycles(1, "Predisq to bpu0 delay")
    dispipe0ToBpu0Delay = Param.Cycles(1, "Dispipe0 to bpu0 delay")
    dispipe1ToBpu0Delay = Param.Cycles(1, "Dispipe1 to bpu0 delay")
    dispipe2ToBpu0Delay = Param.Cycles(1, "Dispipe2 to bpu0 delay")
    dispipe3ToBpu0Delay = Param.Cycles(1, "Dispipe3 to bpu0 delay")
    ewToBpu0Delay = Param.Cycles(1, "Ew to bpu1 delay")
    commitToBpu0Delay = Param.Cycles(1, "Commit to bpu1 delay")
    fetchToBpu0Delay = Param.Cycles(1, "Fetch to bpu1 delay")
    bpu0Width = Param.Unsigned(32, "Bpu1 width")

    # bpu1 params
    IBandLBToBpu1Delay = Param.Cycles(1, "IBandLB to bpu1 delay")
    decodeToBpu1Delay = Param.Cycles(1, "Decode to bpu1 delay")
    predisqToBpu1Delay = Param.Cycles(1, "Predisq to bpu1 delay")
    dispipe0ToBpu1Delay = Param.Cycles(1, "Dispipe0 to bpu1 delay")
    dispipe1ToBpu1Delay = Param.Cycles(1, "Dispipe1 to bpu1 delay")
    dispipe2ToBpu1Delay = Param.Cycles(1, "Dispipe2 to bpu1 delay")
    dispipe3ToBpu1Delay = Param.Cycles(1, "Dispipe3 to bpu1 delay")
    ewToBpu1Delay = Param.Cycles(1, "Ew to bpu1 delay")
    commitToBpu1Delay = Param.Cycles(1, "Commit to bpu1 delay")
    fetchToBpu1Delay = Param.Cycles(1, "Fetch to bpu1 delay")
    bpu0ToBpu1Delay = Param.Cycles(1, "Bpu0 to bpu1 delay")
    bpu1Width = Param.Unsigned(32, "Bpu1 width")

    # IBandLB params
    decodeToIBandLBDelay = Param.Cycles(1, "Decode to IBandLB delay")
    predisqToIBandLBDelay = Param.Cycles(1, "Predisq to IBandLB delay")
    dispipe0ToIBandLBDelay = Param.Cycles(1, "Dispipe0 to IBandLB delay")
    dispipe1ToIBandLBDelay = Param.Cycles(1, "Dispipe1 to IBandLB delay")
    dispipe2ToIBandLBDelay = Param.Cycles(1, "Dispipe2 to IBandLB delay")
    dispipe3ToIBandLBDelay = Param.Cycles(1, "Dispipe3 to IBandLB delay")
    ewToIBandLBDelay = Param.Cycles(1, "Execute/Writeback to IBandLB delay")
    commitToIBandLBDelay = Param.Cycles(1, "Commit to IBandLB delay")
    fetchToIBandLBDelay = Param.Cycles(1, "Fetch to IBandLB delay")
    pcToIBandLBDelay = Param.Cycles(1, "Pc_gen to IBandLB delay")
    bpu1ToIBandLBDelay = Param.Cycles(1, "bpu1 To IBandLB Delay")
    IBWidth = Param.Unsigned(8, "InstBuffer width")
    LBWidth = Param.Unsigned(16, "LoopBuffer width")
    IBSize = Param.Unsigned(128, "Number of InstBuffer entries")
    LBSize = Param.Unsigned(256, "Number of LoopBuffer entries")
    MaxLoopNums = Param.Unsigned(16, "Maximum loop level")
    # LoopBufferUse = Param.Bool(True, "Enable bit of loopbuffer")

    # decode params
    predisqToDecodeDelay = Param.Cycles(1, "Predisq to decode delay")
    dispipe0ToDecodeDelay = Param.Cycles(1, "Dispipe0 to decode delay")
    dispipe1ToDecodeDelay = Param.Cycles(1, "Dispipe1 to decode delay")
    dispipe2ToDecodeDelay = Param.Cycles(1, "Dispipe2 to decode delay")
    dispipe3ToDecodeDelay = Param.Cycles(1, "Dispipe3 to decode delay")
    ewToDecodeDelay = Param.Cycles(1, "Execute/Writeback to decode delay")
    commitToDecodeDelay = Param.Cycles(1, "Commit to decode delay")
    IBandLBToDecodeDelay = Param.Cycles(1, "IBandLB to decode delay")
    decodeWidth = Param.Unsigned(8, "Decode width")

    # predisq params
    dispipe0ToPredisqDelay = Param.Cycles(1, "Dispipe0 to predisq delay")
    dispipe1ToPredisqDelay = Param.Cycles(1, "Dispipe1 to predisq delay")
    dispipe2ToPredisqDelay = Param.Cycles(1, "Dispipe2 to predisq delay")
    dispipe3ToPredisqDelay = Param.Cycles(1, "Dispipe3 to predisq delay")
    ewToPredisqDelay = Param.Cycles(1, "Execute/Writeback to predisq delay")
    commitToPredisqDelay = Param.Cycles(1, "Commit to predisq delay")
    decodeToPredisqDelay = Param.Cycles(1, "Decode to predisq delay")
    predisqWidth = Param.Unsigned(8, "Predisq width")
    predisqueueSize = Param.Unsigned(128, "Number of predisqueue entries")
    predisqGroupNums = Param.Unsigned(16, "Number of predisqueue groups")
    compressFlag = Param.Bool(True, "Does predisq adopt compression mechanism")

    # dispipe0 params
    dispipe1ToDispipe0Delay = Param.Cycles(1, "Dispipe1 to dispipe0 delay")
    dispipe2ToDispipe0Delay = Param.Cycles(1, "Dispipe2 to dispipe0 delay")
    dispipe3ToDispipe0Delay = Param.Cycles(1, "Dispipe3 to dispipe0 delay")
    ewToDispipe0Delay = Param.Cycles(1, "Execute/Writeback to dispipe0 delay")
    commitToDispipe0Delay = Param.Cycles(1, "Commit to dispipe0 delay")
    predisqToDispipe0Delay = Param.Cycles(1, "Predisq to dispipe0 delay")
    dispipe0Width = Param.Unsigned(8, "Dispipe0 width")

    # dispipe1 params
    dispipe2ToDispipe1Delay = Param.Cycles(1, "Dispipe2 to dispipe1 delay")
    dispipe3ToDispipe1Delay = Param.Cycles(1, "Dispipe3 to dispipe1 delay")
    ewToDispipe1Delay = Param.Cycles(1, "Execute/Writeback to dispipe1 delay")
    commitToDispipe1Delay = Param.Cycles(1, "Commit to dispipe1 delay")
    dispipe0ToDispipe1Delay = Param.Cycles(1, "Dispipe0 to dispipe1 delay")
    dispipe1Width = Param.Unsigned(32, "Dispipe1 width")

    # dispipe2 params
    dispipe3ToDispipe2Delay = Param.Cycles(1, "Dispipe3 to dispipe2 delay")
    ewToDispipe2Delay = Param.Cycles(1, "Execute/Writeback to dispipe2 delay")
    commitToDispipe2Delay = Param.Cycles(1, "Commit to dispipe2 delay")
    dispipe1ToDispipe2Delay = Param.Cycles(1, "Dispipe1 to dispipe2 delay")
    dispipe2Width = Param.Unsigned(32, "Dispipe2 width")
    disqueueSize = Param.Unsigned(16, "Number of disqueue entries")
    disqueueSizeBranch = Param.Unsigned(16, "Number of disqueue entries")

    # dispipe3 params
    ewToDispipe3Delay = Param.Cycles(1, "Execute/Writeback to dispipe3 delay")
    commitToDispipe3Delay = Param.Cycles(1, "Commit to dispipe3 delay")
    dispipe2ToDispipe3Delay = Param.Cycles(1, "Dispipe2 to dispipe3 delay")
    dispipe3Width = Param.Unsigned(32, "Dispipe3 width")
    vectorLDSTQ_size = Param.Unsigned(64, "Dispipe3 width")

    # ew params
    commitToEWDelay = Param.Cycles(1, "Commit to Execute/Writeback delay")
    dispipe3ToEWDelay = Param.Cycles(1, "Dispipe3 to Execute/Writeback delay")
    wbWidth = Param.Unsigned(8, "Writeback width")
    fuPool = Param.RxuFUPool(DefaultFUPool(), "Functional Unit pool")
    ewToCommitDelay = Param.Cycles(1, "Execute/Writeback to commit delay")

    # commit params
    dispipe0ToROBDelay = Param.Cycles(2, "Dispipe0 to reorder buffer delay")
    commitWidth = Param.Unsigned(8, "Commit width")
    squashWidth = Param.Unsigned(1024, "Squash width")
    trapLatency = Param.Cycles(13, "Trap latency")
    fetchTrapLatency = Param.Cycles(1, "Fetch trap latency")

    # wtb params
    numWTBEntries = Param.Unsigned(12, "Number of per ibuffer entries")

    numIntSpecialEntries = Param.Unsigned(
        12, "Number of int special ibuffer entries"
    )

    numVectorEntries = Param.Unsigned(32, "Number of per vector entries")

    LdStEntries = Param.Unsigned(32, "Number of per ldstbuffer entries")

    intNormalOddRegReadNums = Param.Unsigned(
        4, "Number of int normal odd regfile read ports"
    )
    intSpecialOddRegReadNums = Param.Unsigned(
        2, "Number of int special odd regfile read ports"
    )
    intNormalEvenRegReadNums = Param.Unsigned(
        4, "Number of int normal even regfile read ports"
    )
    intSpecialEvenRegReadNums = Param.Unsigned(
        2, "Number of int special even regfile read ports"
    )

    intNormalOddRegWriteNums = Param.Unsigned(
        2, "Number of int normal odd regfile write ports"
    )
    intSpecialOddRegWriteNums = Param.Unsigned(
        1, "Number of int special odd regfile write ports"
    )
    intNormalEvenRegWriteNums = Param.Unsigned(
        2, "Number of int normal even regfile write ports"
    )
    intSpecialEvenRegWriteNums = Param.Unsigned(
        1, "Number of int special even regfile write ports"
    )

    ldstOddRegReadNums = Param.Unsigned(
        2, "Number of load/store odd regfile read ports"
    )
    ldstEvenRegReadNums = Param.Unsigned(
        2, "Number of load/store odd regfile read ports"
    )
    fstRegReadNums = Param.Unsigned(2, "Number of fstore regfile read ports")

    fpNormalRegReadNums = Param.Unsigned(
        4, "Number of float normal regfile read ports"
    )
    fpSpecialRegReadNums = Param.Unsigned(
        2, "Number of float special regfile read ports"
    )

    fpRegWriteNums = Param.Unsigned(3, "Number of float regfile write ports")

    fldRegWriteNums = Param.Unsigned(
        1, "Number of float load regfile write ports"
    )

    vectorReadRegNums = Param.Unsigned(
        7, "Number of float load regfile write ports"
    )

    vectorLDSTReadRegNums = Param.Unsigned(
        2, "Number of float load regfile write ports"
    )

    # other params
    backComSize = Param.Unsigned(
        60, "Time buffer size for backwards communication"
    )
    forwardComSize = Param.Unsigned(
        60, "Time buffer size for forward communication"
    )

    LQEntries = Param.Unsigned(256, "Number of load queue entries")
    SQEntries = Param.Unsigned(156, "Number of store queue entries")
    LSQDepCheckShift = Param.Unsigned(
        0, "Number of places to shift addr before check"
    )
    LSQCheckLoads = Param.Bool(
        True,
        "Should dependency violations be checked for "
        "loads & stores or just stores",
    )
    store_set_clear_period = Param.Unsigned(
        2500000000,
        "Number of load/store insts before the dep predictor "
        "should be invalidated",
    )
    LFSTSize = Param.Unsigned(1024, "Last fetched store table size")
    SSITSize = Param.Unsigned(1024, "Store set ID table size")

    numRobs = Param.Unsigned(1, "Number of Reorder Buffers")

    numPhysIntRegs = Param.Unsigned(
        512, "Number of physical integer registers"
    )
    numPhysFloatRegs = Param.Unsigned(
        264, "Number of physical floating point registers"
    )
    numPhysVecRegs = Param.Unsigned(256, "Number of physical vector registers")
    numPhysVecPredRegs = Param.Unsigned(
        32, "Number of physical predicate registers"
    )
    numPhysMatRegs = Param.Unsigned(2, "Number of physical matrix registers")
    # most ISAs don't use condition-code regs, so default is 0
    numPhysCCRegs = Param.Unsigned(0, "Number of physical cc registers")
    numROBEntries = Param.Unsigned(512, "Number of reorder buffer entries")
    numVecRobEntries = Param.Unsigned(
        256, "Number of vector reorder buffer entries"
    )
    numPhysVecCsrRegs = Param.Unsigned(
        32, "Number of physical vector csr registers"
    )

    smtNumFetchingThreads = Param.Unsigned(1, "SMT Number of Fetching Threads")
    smtFetchPolicy = Param.RxuSMTFetchPolicy("RoundRobin", "SMT Fetch policy")
    smtLSQPolicy = Param.RxuSMTQueuePolicy(
        "Partitioned", "SMT LSQ Sharing Policy"
    )
    smtLSQThreshold = Param.Int(100, "SMT LSQ Threshold Sharing Parameter")
    # smtIQPolicy = Param.RxuSMTQueuePolicy("Partitioned", "SMT IQ Sharing Policy")
    # smtIQThreshold = Param.Int(100, "SMT IQ Threshold Sharing Parameter")
    smtROBPolicy = Param.RxuSMTQueuePolicy(
        "Partitioned", "SMT ROB Sharing Policy"
    )
    smtROBThreshold = Param.Int(100, "SMT ROB Threshold Sharing Parameter")
    smtCommitPolicy = Param.RxuCommitPolicy("RoundRobin", "SMT Commit Policy")

    # branchPredLTAGE = Param.BranchPredictor(
    #     LTAGE(), "Branch Predictor"
    # )
    branchPredTournamentBP = Param.BranchPredictor(
        TournamentBP(), "Branch Predictor"
    )

    branchPredTAGE = Param.BranchPredictor(TAGE(), "Branch Predictor")

    branchPredLocalBP = Param.BranchPredictor(
        LocalBP(), "LocalBP Branch Predictor"
    )
    branchPredBiModeBP = Param.BranchPredictor(
        BiModeBP(), "LocalBP Branch Predictor"
    )

    needsTSO = Param.Bool(False, "Enable TSO Memory model")

    ucWidth = Param.Unsigned(16, "uop cache width")
    logSize = Param.Unsigned(9, "log size of uop cache")
    tagBits = Param.Unsigned(14, "tag bits of uop cache")
    setBits = Param.Unsigned(6, "set bits of uop cache")
    assocBits = Param.Unsigned(2, "assoc of uop cache")
    numInstsEntry = Param.Unsigned(16, "number of inst in one uop cache entry")
    initAge = Param.Unsigned(127, "initial age of uop cache entry")
    shift = Param.Unsigned(
        2, "Number of bits to shift PC when calculating index."
    )
    useHashing = Param.Bool(True, "use Hash to calculate index by inst pc")
    # useUopCache = Param.Bool(True, "whether to use uop cache in CPU or not")

    arch_db = Param.ArchDBer(Parent.any, "Arch DB")

from m5.SimObject import SimObject
from m5.params import *
from m5.objects.FuncUnit import *
from m5.objects.RxuFuncUnitConfig import *


class RxuFUPool(SimObject):
    type = "RxuFUPool"
    cxx_class = "gem5::rxuo3::RxuFUPool"
    cxx_header = "cpu/rxuo3/rxu_fu_pool.hh"
    FUList = VectorParam.FUDesc("list of FU's for this pool")


class DefaultFUPool(RxuFUPool):
    FUList = [
        Int_Normal(),
        Int_Special(),
        FP_Normal(),
        FP_Special(),
        ReadPort(),
        WritePort(),
        SIMD_Unit(),
        PredALU(),
        IprPort(),
    ]

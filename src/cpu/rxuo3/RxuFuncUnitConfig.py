from m5.SimObject import SimObject
from m5.defines import buildEnv
from m5.params import *

from m5.objects.FuncUnit import *


class Int_Normal(FUDesc):
    opList = [
        OpDesc(opClass="IntAlu", opLat=1),
        OpDesc(opClass="IntMult", opLat=2),
    ]
    count = 1024


class Int_Special(FUDesc):
    opList = [
        OpDesc(opClass="IntDiv", opLat=12, pipelined=False),
        OpDesc(opClass="No_OpClass", opLat=1),
    ]
    count = 1024


class FP_Normal(FUDesc):
    opList = [
        OpDesc(opClass="FloatAdd", opLat=2),
        OpDesc(opClass="FloatCmp", opLat=2),
        OpDesc(opClass="FloatMult", opLat=4),
        OpDesc(opClass="FloatMultAcc", opLat=4),
        OpDesc(opClass="FloatMisc", opLat=1),
    ]
    count = 1024


class FP_Special(FUDesc):
    opList = [
        OpDesc(opClass="FloatDiv", opLat=12, pipelined=False),
        OpDesc(opClass="FloatSqrt", opLat=12, pipelined=False),
        OpDesc(opClass="FloatCvt", opLat=2),
    ]
    count = 1024


class ReadPort(FUDesc):
    opList = [OpDesc(opClass="MemRead"), OpDesc(opClass="FloatMemRead")]
    count = 1024


class WritePort(FUDesc):
    opList = [OpDesc(opClass="MemWrite"), OpDesc(opClass="FloatMemWrite")]
    count = 1024


class SIMD_Unit(FUDesc):
    opList = [
        OpDesc(opClass="SimdAdd", opLat=2),
        OpDesc(opClass="SimdAddAcc", opLat=2),
        OpDesc(opClass="SimdAlu", opLat=2),
        OpDesc(opClass="SimdCmp", opLat=2),
        OpDesc(opClass="SimdCvt", opLat=2),
        OpDesc(opClass="SimdMisc", opLat=2),
        OpDesc(opClass="SimdMult", opLat=2),
        OpDesc(opClass="SimdWMult", opLat=3),
        OpDesc(opClass="SimdSMult", opLat=3),
        OpDesc(opClass="SimdMultAcc", opLat=3),
        OpDesc(opClass="SimdMatMultAcc", opLat=1),
        OpDesc(opClass="SimdShift", opLat=2),
        OpDesc(opClass="SimdShiftAcc", opLat=2),
        OpDesc(opClass="SimdDiv", opLat=10),
        OpDesc(opClass="SimdSqrt", opLat=2),
        OpDesc(opClass="SimdFloatAdd", opLat=2),
        OpDesc(opClass="SimdFloatAlu", opLat=2),
        OpDesc(opClass="SimdFloatCmp", opLat=2),
        OpDesc(opClass="SimdFloatCvt", opLat=2),
        OpDesc(opClass="SimdFloatDiv", opLat=10),
        OpDesc(opClass="SimdFloatMisc", opLat=1),
        OpDesc(opClass="SimdFloatMisc", opLat=2),
        OpDesc(opClass="SimdFloatMult", opLat=4),
        OpDesc(opClass="SimdFloatMultAcc", opLat=4),
        OpDesc(opClass="SimdFloatMatMultAcc", opLat=4),
        OpDesc(opClass="SimdFloatSqrt", opLat=2),
        OpDesc(opClass="SimdReduceAdd", opLat=4),
        OpDesc(opClass="SimdReduceAlu", opLat=3),
        OpDesc(opClass="SimdReduceCmp", opLat=4),
        OpDesc(opClass="SimdFloatReduceAdd", opLat=4),
        OpDesc(opClass="SimdFloatReduceCmp", opLat=4),
        OpDesc(opClass="SimdExt", opLat=2),
        OpDesc(opClass="SimdFloatExt", opLat=2),
        OpDesc(opClass="SimdConfig", opLat=1),
        OpDesc(opClass="SimdVmvSx", opLat=2),
    ]
    count = 4


class PredALU(FUDesc):
    opList = [OpDesc(opClass="SimdPredAlu", opLat=1)]
    count = 4


class IprPort(FUDesc):
    opList = [OpDesc(opClass="IprAccess", opLat=3, pipelined=False)]
    count = 0

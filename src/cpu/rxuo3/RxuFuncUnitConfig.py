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
        OpDesc(opClass="SimdAdd", opLat=1),
        OpDesc(opClass="SimdAddAcc", opLat=1),
        OpDesc(opClass="SimdAlu", opLat=1),
        OpDesc(opClass="SimdCmp", opLat=1),
        OpDesc(opClass="SimdCvt", opLat=1),
        OpDesc(opClass="SimdMisc", opLat=1),
        OpDesc(opClass="SimdMult", opLat=1),
        OpDesc(opClass="SimdMultAcc", opLat=1),
        OpDesc(opClass="SimdMatMultAcc", opLat=1),
        OpDesc(opClass="SimdShift", opLat=1),
        OpDesc(opClass="SimdShiftAcc", opLat=1),
        OpDesc(opClass="SimdDiv", opLat=1),
        OpDesc(opClass="SimdSqrt", opLat=1),
        OpDesc(opClass="SimdFloatAdd", opLat=1),
        OpDesc(opClass="SimdFloatAlu", opLat=1),
        OpDesc(opClass="SimdFloatCmp", opLat=1),
        OpDesc(opClass="SimdFloatCvt", opLat=1),
        OpDesc(opClass="SimdFloatDiv", opLat=1),
        OpDesc(opClass="SimdFloatMisc", opLat=1),
        OpDesc(opClass="SimdFloatMult", opLat=1),
        OpDesc(opClass="SimdFloatMultAcc", opLat=1),
        OpDesc(opClass="SimdFloatMatMultAcc", opLat=1),
        OpDesc(opClass="SimdFloatSqrt", opLat=1),
        OpDesc(opClass="SimdReduceAdd", opLat=1),
        OpDesc(opClass="SimdReduceAlu", opLat=1),
        OpDesc(opClass="SimdReduceCmp", opLat=1),
        OpDesc(opClass="SimdFloatReduceAdd", opLat=1),
        OpDesc(opClass="SimdFloatReduceCmp", opLat=1),
        OpDesc(opClass="SimdExt", opLat=1),
        OpDesc(opClass="SimdFloatExt", opLat=1),
        OpDesc(opClass="SimdConfig", opLat=1),
    ]
    count = 1024


class PredALU(FUDesc):
    opList = [OpDesc(opClass="SimdPredAlu", opLat=1)]
    count = 1024


class IprPort(FUDesc):
    opList = [OpDesc(opClass="IprAccess", opLat=3, pipelined=False)]
    count = 0

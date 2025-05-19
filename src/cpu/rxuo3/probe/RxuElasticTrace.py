from m5.objects.Probe import *


class RxuElasticTrace(ProbeListenerObject):
    type = "RxuElasticTrace"
    cxx_class = "gem5::rxuo3::RxuElasticTrace"
    cxx_header = "cpu/rxuo3/probe/rxu_elastic_trace.hh"

    # Trace files for the following params are created in the output directory.
    # User is forced to provide these when an instance of this class is created.
    instFetchTraceFile = Param.String(
        desc="Protobuf trace file name for instruction fetch tracing"
    )
    dataDepTraceFile = Param.String(
        desc="Protobuf trace file name for data dependency tracing"
    )
    # The dependency window size param must be equal to or greater than the
    # number of entries in the RxuO3CPU ROB, a typical value is 3 times ROB size
    depWindowSize = Param.Unsigned(
        desc="Instruction window size used for "
        "recording and processing data "
        "dependencies"
    )
    # The committed instruction count from which to start tracing
    startTraceInst = Param.UInt64(
        0,
        "The number of committed instructions "
        "after which to start tracing. Default "
        "zero means start tracing from first "
        "committed instruction.",
    )
    # Whether to trace virtual addresses for memory accesses
    traceVirtAddr = Param.Bool(
        False, "Set to true if virtual addresses are to be traced."
    )

from m5.objects.Probe import *


class RxuSimpleTrace(ProbeListenerObject):
    type = "RxuSimpleTrace"
    cxx_class = "gem5::rxuo3::RxuSimpleTrace"
    cxx_header = "cpu/rxuo3/probe/rxu_simple_trace.hh"

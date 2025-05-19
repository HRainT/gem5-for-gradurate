from m5.params import *
from m5.objects.CheckerCPU import CheckerCPU


class BaseRxuO3Checker(CheckerCPU):
    type = "BaseRxuO3Checker"
    cxx_class = "gem5::rxuo3::Checker"
    cxx_header = "cpu/rxuo3/checker.hh"

import m5.defines

arch_vars = [
    "USE_ARM_ISA",
    "USE_MIPS_ISA",
    "USE_POWER_ISA",
    "USE_RISCV_ISA",
    "USE_SPARC_ISA",
    "USE_X86_ISA",
    # "USE_RXU_ISA",
]

enabled = list(filter(lambda var: m5.defines.buildEnv[var], arch_vars))

if len(enabled) == 1:
    arch = enabled[0]
    if arch == "USE_ARM_ISA":
        from m5.objects.ArmCPU import ArmO3CPU as RxuO3CPU
    elif arch == "USE_MIPS_ISA":
        from m5.objects.MipsCPU import MipsO3CPU as RxuO3CPU
    elif arch == "USE_POWER_ISA":
        from m5.objects.PowerCPU import PowerO3CPU as RxuO3CPU
    elif arch == "USE_RISCV_ISA":
        from m5.objects.RiscvCPU import RiscvRxuO3CPU as RxuO3CPU
    elif arch == "USE_SPARC_ISA":
        from m5.objects.SparcCPU import SparcO3CPU as RxuO3CPU
    elif arch == "USE_X86_ISA":
        from m5.objects.X86CPU import X86O3CPU as RxuO3CPU
    # elif arch == "USE_RXU_ISA":
    #     from m5.objects.RxuCPU import RxuCPU as RxuO3CPU

    DerivO3CPU = RxuO3CPU

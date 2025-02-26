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
        from m5.objects.ArmCPU import ArmO3Checker as RxuO3Checker

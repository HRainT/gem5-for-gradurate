from common import ObjectList
from common.Caches import *

import m5
from m5.objects import *
from m5.objects.Prefetcher import (
    XSPhysicalSmallBOP,
    XSVirtualLargeBOP,
)


def _get_hwp(hwp_option):
    if hwp_option == None:
        return NULL

    hwpClass = ObjectList.hwp_list.get(hwp_option)
    return hwpClass()


def create_prefetcher(cpu, cache_level, options):
    prefetcher_attr = f"{cache_level}_hwp_type"
    prefetcher_name = ""
    prefetcher = NULL
    if hasattr(options, prefetcher_attr):
        prefetcher_name = getattr(options, prefetcher_attr)
        prefetcher = _get_hwp(prefetcher_name)
        print(f"create_prefetcher at {cache_level}: {prefetcher_name}")

    if prefetcher == NULL:
        return NULL

    if cpu != NULL:
        prefetcher.registerTLB(cpu.mmu.dtb)

    prefetcher.queue_size = 64

    if prefetcher_name == "XSCompositePrefetcher":
        if options.l1d_enable_spp:
            prefetcher.enable_spp = True
        if options.l1d_enable_cplx:
            prefetcher.enable_cplx = True
        prefetcher.pht_pf_level = options.pht_pf_level
        prefetcher.short_stride_thres = options.short_stride_thres
        prefetcher.fuzzy_stride_matching = False
        prefetcher.stream_pf_ahead = True
        prefetcher.bop_large.delay_queue_enable = True
        prefetcher.bop_large.bad_score = 10
        prefetcher.bop_small.delay_queue_enable = True
        prefetcher.bop_small.bad_score = 5
        prefetcher.queue_size = 128
        prefetcher.max_prefetch_requests_with_pending_translation = 128
        prefetcher.region_size = 64 * 16  # 64B * blocks per region

        prefetcher.berti.use_byte_addr = True
        prefetcher.berti.aggressive_pf = False
        prefetcher.berti.trigger_pht = True

    if cache_level == "l2":
        if options.l2prefetch:
            assert prefetcher_name == "L2CompositeWithWorkerPrefetcher"
            prefetcher.enable_bop = True
            prefetcher.enable_cdp = False
            prefetcher.bop_large = XSVirtualLargeBOP(is_sub_prefetcher=True)
            prefetcher.bop_small = XSPhysicalSmallBOP(is_sub_prefetcher=True)
        if options.l1_to_l2_pf_hint:
            prefetcher.queue_size = 64
            prefetcher.max_prefetch_requests_with_pending_translation = 128

    return prefetcher

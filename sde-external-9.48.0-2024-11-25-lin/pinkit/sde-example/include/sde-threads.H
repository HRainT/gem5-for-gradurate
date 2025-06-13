// INTEL CONFIDENTIAL
//
// Copyright (C) 2024-2024 Intel Corporation.
//
// This software and the related documents are Intel copyrighted materials, and your
// use of them is governed by the express license under which they were provided to
// you ("License"). Unless the License provides otherwise, you may not use, modify,
// copy, publish, distribute, disclose or transmit this software or the related
// documents without Intel's prior written permission.
//
// This software and the related documents are provided as is, with no express or
// implied warranties, other than those that are expressly stated in the License.

#if !defined(_SDE_THREADS_H_)
#define _SDE_THREADS_H_
#include "pin.H"

// Those values are taken from PIN3 because in PIN4 this value is 64K
// SDE-4248
#if (defined(TARGET_IA32) || !(defined(TARGET_LINUX) || defined(TARGET_WINDOWS)))
const UINT32 SDE_MAX_THREADS = 2048;
#else
const UINT32 SDE_MAX_THREADS = 8192;
#endif

#endif

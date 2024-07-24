// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2017 - 2024 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------


#ifndef dealii_cuda_tensor_product_kernels_h
#define dealii_cuda_tensor_product_kernels_h


#include <deal.II/matrix_free/portable_tensor_product_kernels.h>

DEAL_II_NAMESPACE_OPEN


// GCC 9 and before do not recognize the [[deprecated]] attribute
#if defined(__GNUC__) && (__GNUC__ < 10)
namespace CUDAWrappers
#else
namespace DEAL_II_DEPRECATED CUDAWrappers
#endif
{
  using namespace Portable;
} // namespace CUDAWrappers

DEAL_II_NAMESPACE_CLOSE

#endif

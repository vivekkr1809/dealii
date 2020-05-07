// ---------------------------------------------------------------------
//
// Copyright (C) 2020 by the deal.II authors
//
// This file is part of the deal.II library.
//
// The deal.II library is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE at
// the top level of the deal.II distribution.
//
// ---------------------------------------------------------------------


// Check that serialization for the BatchOptimizer works as expected.
// In this test, we destroy only the optimizer and keep the other expressions
// that were used to initialize it.
//
// Here we use only lambda substitution, but invoke all symbolic optimizations.

#include "../tests.h"

#include "sd_common_tests/batch_optimizer_05.h"

int
main()
{
  initlog();

  const enum SD::OptimizerType     opt_method = SD::OptimizerType::lambda;
  const enum SD::OptimizationFlags opt_flags =
    SD::OptimizationFlags::optimize_all;

  const int dim = 2;
  run_tests<dim, opt_method, opt_flags>();
}
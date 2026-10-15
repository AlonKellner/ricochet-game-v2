#pragma once

namespace rc {

// Returns a*b+c with the product rounded to double BEFORE the addition, i.e.
// two IEEE-754 roundings, never one fused rounding. The solver's bit-identity
// with the GDScript FastSolver depends on this everywhere; rc_tests checks it
// on an FMA-sensitive triple so a toolchain that silently contracts fails loudly.
double fma_sensitive(double a, double b, double c);

} // namespace rc

#include "rc/fp_check.hpp"

namespace rc {

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

double fma_sensitive(double a, double b, double c) {
    // Two statements and -ffp-contract=off (cmake/fp.cmake): the product is a
    // rounded double before c is added. A fused a*b+c would round only once.
    const double product = a * b;
    return product + c;
}

} // namespace rc

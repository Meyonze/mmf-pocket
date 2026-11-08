// SPDX-License-Identifier: Apache-2.0
// Opt-in VM35 nested6 stationary envelope; not a transient error bound.
#pragma once
namespace fxchain::smaf::nested_density {
// beta1,beta2 <=9; all audible operator frequencies <=q cycles/native frame.
// Independent-frequency absolute alias-tail bounds at factor-.5 are:
// (16,.05):3.04e-10; (32,.1):1.16e-10; (64,.2):7.48e-11;
// (128,.5):4.14e-6. Tests recompute these with the independent analytic oracle.
// TL>=12 limits VM35 beta to8.918, leaving float/held-gain slack below9.
// The native source still mutes operators at/above.5. No render-loop search.
inline int factor(double maximumFrequency) {
    return maximumFrequency<=.05?16:maximumFrequency<=.1?32:maximumFrequency<=.2?64:128;
}
}

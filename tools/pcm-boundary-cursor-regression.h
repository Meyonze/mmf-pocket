// SPDX-License-Identifier: Apache-2.0
// Original source-index fixtures; no frozen production implementation.
#pragma once
#include "pcm_interpolation.h"
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <vector>

namespace pcm_boundary_cursor_regression {
inline void run() {
    using fxchain::smaf::pcm_detail::PcmBoundaryCursor;
    size_t checks = 0;
    for (size_t end : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(15), size_t(16),
                      size_t(17), size_t(31), size_t(32), size_t(33), size_t(63), size_t(64),
                      size_t(65), size_t(127), size_t(128), size_t(129), size_t(257), size_t(2048)}) {
        for (bool loop : {false, true}) {
            for (size_t start : {size_t(0), end / 2, end - 1}) {
                // Build the expected timeline by appending complete source
                // ranges. This does not use the cursor, sampleIndex(), or a
                // copy of the production modulo/history-resolution formula.
                std::vector<size_t> attack(end);
                std::iota(attack.begin(), attack.end(), size_t(0));
                std::vector<size_t> expanded = attack;
                const size_t length = end - start;
                const size_t cap = ((end + 64 + length - 1) / length) * length;
                const size_t required = end + 2 * cap + 512;
                if (loop) {
                    while (expanded.size() < required)
                        expanded.insert(expanded.end(), attack.begin() + start, attack.end());
                } else {
                    expanded.resize(required, end - 1);
                }
                for (size_t travel : {size_t(0), length, cap, 2 * cap}) {
                    for (int64_t first : {int64_t(-129), int64_t(-64), int64_t(-31), int64_t(-15),
                                          int64_t(-7), int64_t(-1), int64_t(0), int64_t(1),
                                          int64_t(end - 1), int64_t(end), int64_t(end + 17)}) {
                        PcmBoundaryCursor cursor(first, end, loop, start, travel);
                        // Covers every filter reach and repeated short-loop
                        // wraps, including the negative-to-zero transition.
                        for (int tap = 0; tap < 128; ++tap) {
                            const int64_t virtualIndex = first + int64_t(travel) + tap;
                            const size_t expected = virtualIndex < 0 ? 0 : expanded[size_t(virtualIndex)];
                            if (cursor.source != expected) {
                                std::cerr << "FAIL: boundary source cursor: end=" << end
                                          << " loop=" << loop << " start=" << start
                                          << " travel=" << travel << " first=" << first
                                          << " tap=" << tap << '\n';
                                std::exit(1);
                            }
                            cursor.advance();
                            ++checks;
                        }
                    }
                }
            }
        }
    }
    if (checks != 608256) {
        std::cerr << "FAIL: boundary cursor fixture coverage changed\n";
        std::exit(1);
    }
    std::cout << "Boundary PCM cursor tests PASS: " << checks << " expanded-timeline indices\n";
}
} // namespace pcm_boundary_cursor_regression

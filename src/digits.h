// 4x6 pixel digits (bit 3 = left column). Crisp at small sizes where TrueType text would blur.
#pragma once
#include <cstdint>

constexpr int DIGIT_W = 4, DIGIT_H = 6;

inline const uint8_t kDigits[10][DIGIT_H] = {
    {0x6, 0x9, 0x9, 0x9, 0x9, 0x6},  // 0
    {0x2, 0x6, 0x2, 0x2, 0x2, 0x7},  // 1
    {0x6, 0x9, 0x1, 0x2, 0x4, 0xF},  // 2
    {0xE, 0x1, 0x6, 0x1, 0x1, 0xE},  // 3
    {0x9, 0x9, 0xF, 0x1, 0x1, 0x1},  // 4
    {0xF, 0x8, 0xE, 0x1, 0x1, 0xE},  // 5
    {0x6, 0x8, 0xE, 0x9, 0x9, 0x6},  // 6
    {0xF, 0x1, 0x2, 0x2, 0x4, 0x4},  // 7
    {0x6, 0x9, 0x6, 0x9, 0x9, 0x6},  // 8
    {0x6, 0x9, 0x9, 0x7, 0x1, 0x6},  // 9
};

// Width in pixels of `len` digits at pixel scale k (1px gap between digits).
inline int DigitsWidth(int len, int k) { return (len * (DIGIT_W + 1) - 1) * k; }

// Calls plot(x, y) for every lit pixel of the number text, top-left at (x0, y0), pixel scale k.
template <class Plot>
void StampDigits(const char* txt, int len, int x0, int y0, int k, Plot plot) {
    for (int c = 0; c < len; ++c) {
        const uint8_t* rows = kDigits[txt[c] - '0'];
        for (int ry = 0; ry < DIGIT_H; ++ry)
            for (int rx = 0; rx < DIGIT_W; ++rx)
                if (rows[ry] & ((1 << (DIGIT_W - 1)) >> rx))
                    for (int dy = 0; dy < k; ++dy)
                        for (int dx = 0; dx < k; ++dx)
                            plot(x0 + (c * (DIGIT_W + 1) + rx) * k + dx, y0 + ry * k + dy);
    }
}

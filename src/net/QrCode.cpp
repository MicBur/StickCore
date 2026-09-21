// ---------------------------------------------------------------------------
//  StickCore  –  QrCode.cpp
//
//  Byte-mode, ECC level L, versions 1–4, single ECC block. Implements the
//  standard QR pipeline: data encoding, Reed-Solomon over GF(256), function
//  pattern layout, data zig-zag, all 8 data masks with penalty scoring, and
//  format-information (BCH). Validated by decoding the rendered output.
// ---------------------------------------------------------------------------
#include "net/QrCode.h"
#include <array>
#include <cstdint>

namespace stick {
namespace {

// Per-version tables for ECC level L (index by version 1..4).
constexpr int kDataCw[5] = { 0, 19, 34, 55,  80 };   // data codewords
constexpr int kEccCw [5] = { 0,  7, 10, 15,  20 };   // ecc codewords / block

using Bytes = std::vector<uint8_t>;

uint8_t rsMul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return uint8_t(z & 0xFF);
}

Bytes rsDivisor(int degree) {
    Bytes result(degree, 0);
    result[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[j] = rsMul(result[j], root);
            if (j + 1 < degree) result[j] ^= result[j + 1];
        }
        root = rsMul(root, 0x02);
    }
    return result;
}

Bytes rsRemainder(const Bytes& data, const Bytes& divisor) {
    Bytes result(divisor.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); ++i)
            result[i] ^= rsMul(divisor[i], factor);
    }
    return result;
}

struct Matrix {
    int n;
    std::vector<std::vector<int>>  mod;   // 0/1
    std::vector<std::vector<bool>> fun;   // function module?
    explicit Matrix(int size) : n(size),
        mod(size, std::vector<int>(size, 0)),
        fun(size, std::vector<bool>(size, false)) {}

    void set(int col, int row, bool dark) {   // (x=col, y=row)
        mod[row][col] = dark ? 1 : 0;
        fun[row][col] = true;
    }
};

int getBit(int x, int i) { return (x >> i) & 1; }

void drawFinder(Matrix& m, int cr, int cc) {
    for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx) {
            int r = cr + dy, c = cc + dx;
            if (r < 0 || r >= m.n || c < 0 || c >= m.n) continue;
            int dist = std::max(std::abs(dx), std::abs(dy));
            m.set(c, r, dist != 2 && dist != 4);
        }
}

void drawFormat(Matrix& m, int mask) {
    // 5 data bits (ecl L = 0b01) -> 15-bit BCH.
    int data = (0b01 << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;

    for (int i = 0; i <= 5; ++i) m.set(8, i, getBit(bits, i));
    m.set(8, 7, getBit(bits, 6));
    m.set(8, 8, getBit(bits, 7));
    m.set(7, 8, getBit(bits, 8));
    for (int i = 9; i < 15; ++i) m.set(14 - i, 8, getBit(bits, i));

    int size = m.n;
    for (int i = 0; i < 8; ++i)  m.set(size - 1 - i, 8, getBit(bits, i));
    for (int i = 8; i < 15; ++i) m.set(8, size - 15 + i, getBit(bits, i));
    m.set(8, size - 8, true);   // dark module
}

bool maskCond(int mask, int r, int c) {
    switch (mask) {
        case 0: return (r + c) % 2 == 0;
        case 1: return r % 2 == 0;
        case 2: return c % 3 == 0;
        case 3: return (r + c) % 3 == 0;
        case 4: return (r / 2 + c / 3) % 2 == 0;
        case 5: return (r * c) % 2 + (r * c) % 3 == 0;
        case 6: return ((r * c) % 2 + (r * c) % 3) % 2 == 0;
        case 7: return ((r + c) % 2 + (r * c) % 3) % 2 == 0;
    }
    return false;
}

long penalty(const Matrix& m) {
    const int n = m.n;
    long p = 0;
    // Rule 1: runs of 5+ in rows and columns.
    for (int r = 0; r < n; ++r) {
        int runC = 1, runR = 1;
        for (int c = 1; c < n; ++c) {
            if (m.mod[r][c] == m.mod[r][c-1]) { if (++runC >= 5) p += (runC == 5 ? 3 : 1); }
            else runC = 1;
            if (m.mod[c][r] == m.mod[c-1][r]) { if (++runR >= 5) p += (runR == 5 ? 3 : 1); }
            else runR = 1;
        }
    }
    // Rule 2: 2x2 blocks.
    for (int r = 0; r < n-1; ++r)
        for (int c = 0; c < n-1; ++c) {
            int v = m.mod[r][c];
            if (v == m.mod[r][c+1] && v == m.mod[r+1][c] && v == m.mod[r+1][c+1]) p += 3;
        }
    // Rule 3: finder-like 1:1:3:1:1 pattern with 4 light, in rows and cols.
    const int pat1[11] = {1,0,1,1,1,0,1,0,0,0,0};
    const int pat2[11] = {0,0,0,0,1,0,1,1,1,0,1};
    for (int r = 0; r < n; ++r)
        for (int c = 0; c <= n-11; ++c) {
            bool a = true, b = true;
            for (int k = 0; k < 11; ++k) {
                if (m.mod[r][c+k] != pat1[k]) a = false;
                if (m.mod[r][c+k] != pat2[k]) b = false;
                if (m.mod[c+k][r] != pat1[k]) ; // handled below separately
            }
            if (a || b) p += 40;
        }
    for (int c = 0; c < n; ++c)
        for (int r = 0; r <= n-11; ++r) {
            bool a = true, b = true;
            for (int k = 0; k < 11; ++k) {
                if (m.mod[r+k][c] != pat1[k]) a = false;
                if (m.mod[r+k][c] != pat2[k]) b = false;
            }
            if (a || b) p += 40;
        }
    // Rule 4: dark proportion.
    long dark = 0;
    for (int r = 0; r < n; ++r) for (int c = 0; c < n; ++c) dark += m.mod[r][c];
    int percent = int(dark * 100 / (long(n) * n));
    int k = 0;
    while (std::abs(percent - 50) > k * 5 + 5) ++k;  // not used; compute below
    int lo = (percent / 5) * 5, hi = lo + 5;
    int kk = std::min(std::abs(lo - 50), std::abs(hi - 50)) / 5;
    p += long(kk) * 10;
    (void)k;
    return p;
}

} // namespace

std::vector<std::vector<bool>> QrCode::encode(const std::string& text)
{
    const int len = int(text.size());

    // choose smallest version (1..4) that fits.
    int ver = 0;
    for (int v = 1; v <= 4; ++v)
        if (len + 2 <= kDataCw[v]) { ver = v; break; }
    if (ver == 0) return {};

    const int dataCw = kDataCw[ver];
    const int eccCw  = kEccCw[ver];
    const int N      = 17 + 4 * ver;

    // --- data bit stream (byte mode) --------------------------------------
    std::vector<bool> bits;
    auto put = [&](int value, int n){ for (int i = n-1; i >= 0; --i) bits.push_back((value>>i)&1); };
    put(0b0100, 4);            // byte mode
    put(len, 8);               // count (8 bits for v1..9)
    for (unsigned char ch : text) put(ch, 8);
    const int cap = dataCw * 8;
    for (int i = 0; i < 4 && int(bits.size()) < cap; ++i) bits.push_back(false); // terminator
    while (bits.size() % 8) bits.push_back(false);
    Bytes data;
    for (size_t i = 0; i < bits.size(); i += 8) {
        int b = 0; for (int k = 0; k < 8; ++k) b = (b<<1) | bits[i+k];
        data.push_back(uint8_t(b));
    }
    for (int pad = 0xEC; int(data.size()) < dataCw; pad ^= 0xEC ^ 0x11) data.push_back(uint8_t(pad));

    // --- ecc (single block) -----------------------------------------------
    Bytes ecc = rsRemainder(data, rsDivisor(eccCw));
    Bytes all = data; all.insert(all.end(), ecc.begin(), ecc.end());

    // --- function patterns -------------------------------------------------
    Matrix base(N);
    drawFinder(base, 3, 3);
    drawFinder(base, 3, N - 4);
    drawFinder(base, N - 4, 3);
    for (int i = 0; i < N; ++i) {           // timing
        if (!base.fun[6][i]) base.set(i, 6, i % 2 == 0);
        if (!base.fun[i][6]) base.set(6, i, i % 2 == 0);
    }
    if (ver >= 2) {                         // one alignment pattern
        int a = N - 7;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                base.set(a + dx, a + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
    drawFormat(base, 0);                    // reserve format modules

    // --- place data bits in zig-zag ---------------------------------------
    {
        int bit = 0;
        const int total = int(all.size()) * 8;
        bool up = true;
        for (int right = N - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;      // skip vertical timing column
            for (int v = 0; v < N; ++v) {
                int row = up ? (N - 1 - v) : v;
                for (int j = 0; j < 2; ++j) {
                    int col = right - j;
                    if (base.fun[row][col]) continue;
                    bool dark = false;
                    if (bit < total) dark = getBit(all[bit / 8], 7 - (bit % 8));
                    base.mod[row][col] = dark ? 1 : 0;
                    ++bit;
                }
            }
            up = !up;
        }
    }

    // --- choose best mask --------------------------------------------------
    Matrix best(N); long bestPen = -1;
    for (int mask = 0; mask < 8; ++mask) {
        Matrix m = base;
        for (int r = 0; r < N; ++r)
            for (int c = 0; c < N; ++c)
                if (!m.fun[r][c] && maskCond(mask, r, c)) m.mod[r][c] ^= 1;
        drawFormat(m, mask);
        long pen = penalty(m);
        if (bestPen < 0 || pen < bestPen) { bestPen = pen; best = m; }
    }

    std::vector<std::vector<bool>> out(N, std::vector<bool>(N, false));
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c) out[r][c] = best.mod[r][c] != 0;
    return out;
}

} // namespace stick

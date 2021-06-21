// Written in 2018 by David Blackman and Sebastiano Vigna (vigna@acm.org)

// To the extent possible under law, the author has dedicated all copyright
// and related and neighbouring rights to this software to the public domain
// worldwide. This software is distributed without any warranty.

// See <http://creativecommons.org/publicdomain/zero/1.0/>.

#include <stdint.h>

namespace riften {

// This is xoroshiro128** 1.0, one of our all-purpose, rock-solid,
// small-state generators. It is extremely (sub-ns) fast and it passes all
// tests we are aware of, but its state space is large enough only for
// mild parallelism.

// For generating just floating-point numbers, xoroshiro128+ is even
// faster (but it has a very mild bias, see notes in the comments).

// The state must be seeded so that it is not everywhere zero. If you have
// a 64-bit seed, we suggest to seed a splitmix64 generator and use its
// output to fill s.

inline uint64_t rotl(const uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static thread_local uint64_t s[2] = {42, 42};  // The answer to the greatest question

inline uint64_t xoroshiro128() {
    const uint64_t s0 = s[0];
    uint64_t s1 = s[1];
    const uint64_t result = rotl(s0 * 5, 7) * 9;

    s1 ^= s0;
    s[0] = rotl(s0, 24) ^ s1 ^ (s1 << 16);  // a, b
    s[1] = rotl(s1, 37);                    // c

    return result;
}

// This is the jump function for the generator. It is equivalent
// to 2^64 calls to next(); it can be used to generate 2^64
// non-overlapping sub-sequences for parallel computations.
inline void jump(uint64_t jumps) {
    static constexpr uint64_t JUMP[] = {0xdf900294d8f554a5, 0x170865df4b3201fc};

    while (jumps--) {
        uint64_t s0 = 0;
        uint64_t s1 = 0;

        for (uint64_t i = 0; i < sizeof(JUMP) / sizeof(*JUMP); i++)
            for (int b = 0; b < 64; b++) {
                if (JUMP[i] & UINT64_C(1) << b) {
                    s0 ^= s[0];
                    s1 ^= s[1];
                }
                xoroshiro128();
            }

        s[0] = s0;
        s[1] = s1;
    }
}

}  // namespace riften

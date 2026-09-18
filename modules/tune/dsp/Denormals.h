#pragma once

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define BMO_TUNE_HAS_SSE 1
#endif

namespace bmo::tune
{

/** Flush-to-zero and denormals-are-zero for the duration of a process() call,
    restored on exit (spec §8).

    The core is full of one-pole smoothers whose state decays toward zero on
    silence, and a decaying one-pole in float is the textbook source of
    denormal stalls: on x86 a single subnormal operand can cost a hundred
    cycles. The host may or may not have set these flags for us, and a host
    that has set them expects them back as it left them, hence the RAII
    rather than setting them once in prepare(). */
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if BMO_TUNE_HAS_SSE
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040);   // FTZ (bit 15) | DAZ (bit 6)
#elif defined(__aarch64__)
        asm volatile ("mrs %0, fpcr" : "=r" (saved));
        const unsigned long long withFz = saved | (1ull << 24);
        asm volatile ("msr fpcr, %0" : : "r" (withFz));
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if BMO_TUNE_HAS_SSE
        _mm_setcsr (saved);
#elif defined(__aarch64__)
        asm volatile ("msr fpcr, %0" : : "r" (saved));
#endif
    }

    ScopedNoDenormals (const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator= (const ScopedNoDenormals&) = delete;

private:
#if BMO_TUNE_HAS_SSE
    unsigned int saved = 0;
#else
    unsigned long long saved = 0;
#endif
};

} // namespace bmo::tune

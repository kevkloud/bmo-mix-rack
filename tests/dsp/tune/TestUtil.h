#pragma once

/*
    The suite's test style, as tests/dsp/*Tests.cpp in BMO Mix Rack write it:
    a plain executable, one `check` per claim with the claim written out as a
    sentence, a failure count as the exit code. No framework -- CTest runs the
    binary and reads the exit code, and a failure prints the sentence that
    stopped being true.

    Measured numbers are printed with `report`, so a passing run still shows
    how much margin there is. A test that only says "pass" hides the drift
    toward failing that a trend would have shown.
*/

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace bmo::tune::test
{

inline int failures = 0;

inline void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

inline void report (const std::string& what, double value, const char* unit = "")
{
    std::printf ("  %-58s %12.5g %s\n", what.c_str(), value, unit);
}

inline bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

inline int finish (const char* suite)
{
    if (failures == 0)
        std::printf ("%s: all checks passed\n", suite);
    else
        std::printf ("%s: %d check(s) FAILED\n", suite, failures);

    return failures == 0 ? 0 : 1;
}

} // namespace bmo::tune::test

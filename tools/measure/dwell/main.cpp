/*
    Offline measurement harness for BMO Dwell.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/eq, /sat, /opto, /dim, /deq and /vcomp.

        measure_dwell schema     the permanent parameter table, printed
        measure_dwell latency    the reported latency across the grid

    **Stage 1: these are the two modes that can exist before the loop does.**
    docs/delay/11-integration-and-test-plan.md §4 lists what this harness grows
    into -- time accuracy by sub-sample peak fit, the feedback decay table,
    accumulated alias floors, the ducking envelope, the FX candidates' cost --
    and each of those arrives with the DSP step it measures.

    `schema` exists because the parameter list is permanent from this release,
    and the cheapest way to check a table against a specification is to print
    it and read it beside the document. `latency` exists because "0 at every
    setting" is the module's loudest claim and the one a delay is most likely
    to break later, by reporting its own delay time.

    Results name the machine they were taken on -- see the root AGENTS.md.
*/

#include "modules/dwell/dsp/DspCore.h"
#include "modules/dwell/dsp/DwellDsp.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace P = bmo::dwell;

namespace
{

std::vector<float> defaults()
{
    std::vector<float> v;
    for (const auto& s : P::specs())
        v.push_back (s.def);
    return v;
}

void printSchema()
{
    const auto& specs = P::specs();

    std::printf ("BMO Dwell -- %d parameters, ids 0-%d, permanent\n\n",
                 (int) specs.size(), (int) specs.size() - 1);
    std::printf ("  # id           kind     min        max        default    law\n");

    for (size_t i = 0; i < specs.size(); ++i)
    {
        const auto& s = specs[i];
        const char* kind = s.kind == bmo::ParamKind::Bool   ? "bool"
                         : s.kind == bmo::ParamKind::Choice ? "choice"
                                                            : "float";

        std::printf ("%3d %-12s %-8s %-10.4g %-10.4g %-10.4g %s\n",
                     (int) i, s.id, kind, (double) s.min, (double) s.max, (double) s.def,
                     s.logarithmic ? "log" : "lin");

        for (int c = 0; c < s.numChoices(); ++c)
            std::printf ("        [%d] %s\n", c, s.choices[(size_t) c]);
    }

    std::printf ("\nSYNC is %s.\n", P::kSyncIsEnabled ? "enabled" : "disabled (docs/delay/12 is not in yet)");
    std::printf ("Maximum delay %.0f ms.\n", (double) P::kMaxTimeMs);
}

/** The latency claim, walked rather than asserted: every character, the ends
    and middle of TIME, DRIVE at both ends, HOLD and FX on and off. Anything
    but a column of zeros is a fault. */
int printLatency()
{
    P::DwellDsp dsp;
    auto v = defaults();
    int worst = 0, cases = 0;

    for (const auto ms : { 1.0f, 17.5f, 375.0f, 1000.0f, P::kMaxTimeMs })
    {
        v[P::Index::time] = ms;

        for (int character = 0; character < 3; ++character)
        {
            v[P::Index::character] = (float) character;

            for (const auto drive : { 0.0f, 100.0f })
            {
                v[P::Index::drive] = drive;

                for (const auto on : { 0.0f, 1.0f })
                {
                    v[P::Index::fx]   = on;
                    v[P::Index::hold] = on;
                    v[P::Index::mix]  = on > 0.5f ? 100.0f : 0.0f;

                    const auto latency = dsp.latencyForParams (v.data(), (int) v.size());
                    worst = latency > worst ? latency : worst;
                    ++cases;
                }
            }
        }
    }

    std::printf ("latency: %d cases, worst %d samples\n", cases, worst);
    std::printf ("%s\n", worst == 0
                            ? "zero at every setting, as specified"
                            : "FAULT: the module is reporting latency it should not have");

    return worst == 0 ? 0 : 1;
}

} // namespace

int main (int argc, char** argv)
{
    const std::string mode { argc > 1 ? argv[1] : "" };

    if (mode == "schema")
    {
        printSchema();
        return 0;
    }

    if (mode == "latency")
        return printLatency();

    std::printf ("usage: measure_dwell <schema|latency>\n");
    return 1;
}

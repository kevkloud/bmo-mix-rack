#pragma once

#include "modules/dwell/params.h"
#include <vector>

namespace bmo::dwell
{

/** **Init only, for now, and deliberately.**

    A factory preset is a sound, and this module has no sound yet -- stage 1 is
    the schema, the identity and a pass-through core. Presets written against a
    loop that does not exist would be numbers nobody had heard, and
    docs/delay/14 is the workflow that decides what the module's settings
    should be: measurements first, then blind level-matched rounds on a named
    machine. They are written there, against the finished loop, so that the
    level-matching rule in modules/AGENTS.md step 6 means something when the
    plugin tests check it.

    Init is index 0 and is every default, which is the one thing about this
    list that is already true and already tested. */
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },
    };

    return presets;
}

} // namespace bmo::dwell

#pragma once

#include <memory>

namespace bmo
{

class ParamSet;

//==============================================================================
/** A module's own object that watches its parameters and writes other ones.

    **The only reason this exists is that one parameter can be a voicing.** BMO
    Linger's TYPE is the case and so far the only one: selecting a type
    re-applies that type's ten constants over the parameters that hold them, so
    a type behaves as a voicing rather than as a label on a table lookup
    (`modules/reverb/TypeVoicing.h`). Nothing else in the suite links one
    parameter to another, and nothing should start without the same argument.

    It lives here, above `ModuleEngine`, because **a panel is the wrong owner**.
    A module has to behave the same with no editor open: automation runs,
    sessions load and renders happen with no window anywhere, and a link that a
    panel owned would apply a type only while someone was looking at it. One
    engine is created per running module in both products -- the standalone's
    own and one per occupied rack slot -- so owning the link there covers both
    from a single line.

    The interface is nearly empty on purpose. A link is a **lifetime**, not a
    service: it subscribes in its constructor, unsubscribes in its destructor,
    and the engine that holds it calls it at one moment only -- after a state
    restore, `stateRestored` below. Anything a link wants to expose for a test
    it exposes on its own type, which the test already knows.

    **Message thread.** Created and destroyed with the engine, which is a
    message-thread operation in both products (`RackProcessor::rebuild` says so
    for the rack), and every write a link makes must reach the parameters from
    there too -- a parameter written from the audio thread is a host's problem
    and not a small one. `juce::ParameterAttachment` is the tool for that and is
    what the one implementation uses. */
struct ParamLink
{
    virtual ~ParamLink() = default;

    /** Called by the engine after a saved state has been applied, on whichever
        thread applied it.

        **The one call a link gets, and why it needs it.** A restore writes the
        linked parameter and the parameters it writes, in file order. A link
        that reacts through a `juce::ParameterAttachment` reacts at once on the
        message thread but only *queues* the reaction anywhere else -- and a host
        may restore from any thread; the rack's MessageManagerLock is a mutex,
        not a change of thread. The queued reaction then lands after the file's
        own values and overwrites them. This is where a link records that what
        the parameters hold now is a restored whole, so that the queued reaction
        finds nothing left to do.

        It must not write a parameter: the file's values are final. The default
        does nothing, which is right for any link that is not a voicing. */
    virtual void stateRestored() {}
};

/** What a `ModuleDef` supplies when its module has one. Null for every module
    but BMO Linger, and a null one is simply not made -- there is no
    do-nothing link to pay for. */
using ParamLinkFactory = std::unique_ptr<ParamLink> (*) (const ParamSet&);

} // namespace bmo

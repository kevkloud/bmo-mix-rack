#include "Product.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return bmo::products::createFetcomp().release();
}

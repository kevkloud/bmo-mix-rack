#include "Registry.h"
#include "modules/deq/Module.h"
#include "modules/dim/Module.h"
#include "modules/eq/Module.h"
#include "modules/opto/Module.h"
#include "modules/sat/Module.h"
#include "modules/util/Module.h"
#include "modules/vcomp/Module.h"

namespace bmo::products
{

const std::vector<const ModuleDef*>& registry()
{
    static const std::vector<const ModuleDef*> defs {
        &util::module(),
        &eq::module(),
        &sat::module(),
        &opto::module(),
        &dim::module(),
        &deq::module(),
        &vcomp::module(),
    };

    return defs;
}

} // namespace bmo::products

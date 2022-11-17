//
// Created by Antti on 21.10.21.
//

#include "LATHandler.h"
#include "TreeOps.h"
#include "LASolver.h"
#include "OsmtInternalException.h"

LATHandler::LATHandler(SMTConfig & c, ArithLogic & l)
    : TSolverHandler(c)
    , logic(l)
{
    lasolver = new LASolver(config, logic);
    setSolverSchedule({lasolver});
}

PTRef LATHandler::getInterpolant(ipartitions_t const & mask, ItpColorMap * labels, PartitionManager & pmanager) {
    if (logic.hasReals() and not logic.hasIntegers()) {
        return lasolver->getRealInterpolant(mask, labels, pmanager);
    } else if (logic.hasIntegers() and not logic.hasReals()) {
        if (labels == nullptr) {
            throw OsmtInternalException("LIA interpolation requires partitioning map, but no map was provided");
        }
        return lasolver->getIntegerInterpolant(*labels);
    } else {
        throw OsmtInternalException("Mixed arithmetic interpolation not supported yet");
    }
}

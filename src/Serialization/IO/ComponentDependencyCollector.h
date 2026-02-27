#ifndef SERIALIZATION_IO_COMPONENT_DEPENDENCY_COLLECTOR_H
#define SERIALIZATION_IO_COMPONENT_DEPENDENCY_COLLECTOR_H

#include <string>
#include <vector>

#include "neoecs.hpp"

namespace Serialization {

void CollectAssetDependenciesFromEntities(
    NeoECS::ECSComponentManager* manager,
    const std::vector<NeoECS::ECSEntity*>& entities,
    std::vector<std::string>& outDependencies
);

} // namespace Serialization

#endif // SERIALIZATION_IO_COMPONENT_DEPENDENCY_COLLECTOR_H

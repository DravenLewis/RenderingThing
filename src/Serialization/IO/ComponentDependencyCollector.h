/**
 * @file src/Serialization/IO/ComponentDependencyCollector.h
 * @brief Declarations for ComponentDependencyCollector.
 */

#ifndef SERIALIZATION_IO_COMPONENT_DEPENDENCY_COLLECTOR_H
#define SERIALIZATION_IO_COMPONENT_DEPENDENCY_COLLECTOR_H

#include <string>
#include <vector>

#include "neoecs.hpp"

namespace Serialization {

/**
 * @brief Collects asset dependencies from entities.
 * @param manager Value for manager.
 * @param entities Value for entities.
 * @param outDependencies Output value for dependencies.
 */
void CollectAssetDependenciesFromEntities(
    NeoECS::ECSComponentManager* manager,
    const std::vector<NeoECS::ECSEntity*>& entities,
    std::vector<std::string>& outDependencies
);

} // namespace Serialization

#endif // SERIALIZATION_IO_COMPONENT_DEPENDENCY_COLLECTOR_H

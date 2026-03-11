/**
 * @file src/Rendering/Materials/MaterialRegistry.h
 * @brief Declarations for MaterialRegistry.
 */

#ifndef MATERIAL_REGISTRY_H
#define MATERIAL_REGISTRY_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

#include "Rendering/Materials/Material.h"

/// @brief Holds data for MaterialRegistryEntry.
struct MaterialRegistryEntry {
    std::string id;
    std::string displayName;
    bool isBuiltIn = false;
    std::string assetRef;
};

/// @brief Represents the MaterialRegistry type.
class MaterialRegistry {
    public:
        /**
         * @brief Returns the registry singleton instance.
         * @return Reference to the resulting value.
         */
        static MaterialRegistry& Instance();

        /**
         * @brief Refreshes registry state.
         * @param force Flag controlling force.
         */
        void Refresh(bool force = false);
        /**
         * @brief Returns the entries.
         * @return Reference to the resulting value.
         */
        const std::vector<MaterialRegistryEntry>& GetEntries() const;
        /**
         * @brief Creates by id.
         * @param id Identifier or index value.
         * @param outError Output value for error.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<Material> CreateById(const std::string& id, std::string* outError = nullptr) const;

    private:
        /**
         * @brief Constructs a new MaterialRegistry instance.
         */
        MaterialRegistry() = default;

        /**
         * @brief Ensures built ins.
         */
        void ensureBuiltIns();
        /**
         * @brief Rebuilds registry entries from scene data.
         */
        void rebuildEntries();

        bool builtInsInitialized = false;
        mutable std::vector<MaterialRegistryEntry> entries;
        mutable std::vector<std::pair<std::string, std::function<std::shared_ptr<Material>()>>> builtInFactories;
        std::chrono::steady_clock::time_point nextRefreshTime = std::chrono::steady_clock::time_point::min();
};

#endif // MATERIAL_REGISTRY_H

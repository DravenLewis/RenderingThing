#ifndef MATERIAL_REGISTRY_H
#define MATERIAL_REGISTRY_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

#include "Material.h"

struct MaterialRegistryEntry {
    std::string id;
    std::string displayName;
    bool isBuiltIn = false;
    std::string assetRef;
};

class MaterialRegistry {
    public:
        static MaterialRegistry& Instance();

        void Refresh(bool force = false);
        const std::vector<MaterialRegistryEntry>& GetEntries() const;
        std::shared_ptr<Material> CreateById(const std::string& id, std::string* outError = nullptr) const;

    private:
        MaterialRegistry() = default;

        void ensureBuiltIns();
        void rebuildEntries();

        bool builtInsInitialized = false;
        mutable std::vector<MaterialRegistryEntry> entries;
        mutable std::vector<std::pair<std::string, std::function<std::shared_ptr<Material>()>>> builtInFactories;
        std::chrono::steady_clock::time_point nextRefreshTime = std::chrono::steady_clock::time_point::min();
};

#endif // MATERIAL_REGISTRY_H

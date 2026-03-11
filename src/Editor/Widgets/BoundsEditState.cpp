/**
 * @file src/Editor/Widgets/BoundsEditState.cpp
 * @brief Implementation for BoundsEditState.
 */

#include "Editor/Widgets/BoundsEditState.h"

namespace {
    std::string g_activeBoundsEntityId;
}

namespace BoundsEditState {
    void ActivateForEntity(const std::string& entityId){
        if(entityId.empty()){
            g_activeBoundsEntityId.clear();
            return;
        }
        g_activeBoundsEntityId = entityId;
    }

    void Deactivate(){
        g_activeBoundsEntityId.clear();
    }

    bool IsActiveForEntity(const std::string& entityId){
        return !entityId.empty() &&
               !g_activeBoundsEntityId.empty() &&
               g_activeBoundsEntityId == entityId;
    }

    const std::string& GetActiveEntityId(){
        return g_activeBoundsEntityId;
    }
}

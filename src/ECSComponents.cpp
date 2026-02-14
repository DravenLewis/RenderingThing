#include "ECSComponents.h"
#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>

void TransformComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(ImGui::CollapsingHeader("Transform Component", ImGuiTreeNodeFlags_DefaultOpen)){

        auto transform = this;

        Math3D::Vec3 pos = transform->local.position;
        Math3D::Vec3 rot = transform->local.rotation.ToEuler();
        Math3D::Vec3 scale = transform->local.scale;

        if(ImGui::DragFloat3("Position", &pos.x, 0.1f)){
            transform->local.position = pos;
        }

        if(ImGui::DragFloat3("Rotation", &rot.x, 0.5f)){
            transform->local.setRotation(rot);
        }

        if(ImGui::DragFloat3("Scale", &scale.x, 0.1f)){
            transform->local.scale = scale;
        }
    }
}

void MeshRendererComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(ImGui::CollapsingHeader("Mesh Renderer Component", ImGuiTreeNodeFlags_DefaultOpen)){
        auto renderer = this;

        ImGui::Separator();
        ImGui::TextUnformatted("Mesh Renderer");
        ImGui::Checkbox("Visible", &renderer->visible);
        ImGui::Checkbox("Backface Cull", &renderer->enableBackfaceCulling);
    }
}

void LightComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Separator();
        ImGui::TextUnformatted("Light");

        auto self = this;
        auto entity = self->getParentEntity();
        if(!ecsPtr || !scene || !entity){
            return;
        }
        auto components = ecsPtr->getComponentManager();

        static std::unordered_set<std::string> migratedLightSyncTransform;
        static std::unordered_set<std::string> migratedLightDefaults;
        auto makeMigrationKey = [&](const std::string& id) -> std::string {
            uintptr_t sceneId = reinterpret_cast<uintptr_t>(scene.get());
            return std::to_string(sceneId) + ":" + id;
        };

        auto getWorldLightBasis = [&](Math3D::Vec3& outPos, Math3D::Vec3& outForward) -> bool {
            auto* tx = components->getECSComponent<TransformComponent>(entity);
            if(!tx){
                return false;
            }
            Math3D::Mat4 world = scene->getWorldMatrix(entity, components);
            outPos = world.getPosition();
            Math3D::Vec3 forward = Math3D::Transform::transformPoint(world, Math3D::Vec3(0,0,1)) - outPos;
            if(!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
               forward.length() <= 0.0001f){
                outForward = Math3D::Vec3(0, -1, 0);
            }else{
                outForward = forward.normalize();
            }
            return true;
        };
        auto syncLightFromTransform = [&]() {
            Math3D::Vec3 worldPos;
            Math3D::Vec3 worldForward;
            if(!getWorldLightBasis(worldPos, worldForward)){
                return;
            }
            if(self->syncTransform){
                self->light.position = worldPos;
            }
            if(self->syncDirection && self->light.type != LightType::POINT){
                self->light.direction = worldForward;
            }
        };
        auto ensurePointLightBounds = [&](NeoECS::ECSEntity* target, float radius){
            if(!target){
                return;
            }
            auto* manager = ecsPtr->getComponentManager();
            if(auto* bounds = manager->getECSComponent<BoundsComponent>(target)){
                bounds->type = BoundsType::Sphere;
                bounds->radius = radius;
            }else{
                auto* ctx = ecsPtr->getContext();
                std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ctx, target));
                if(wrapper && wrapper->addComponent<BoundsComponent>()){
                    if(auto* newBounds = manager->getECSComponent<BoundsComponent>(target)){
                        newBounds->type = BoundsType::Sphere;
                        newBounds->radius = radius;
                    }
                }
            }
        };

        const std::string entityId = entity->getNodeUniqueID();
        if(!entityId.empty()){
            const std::string key = makeMigrationKey(entityId);
            if(migratedLightSyncTransform.find(key) == migratedLightSyncTransform.end()){
                if(!self->syncTransform){
                    self->syncTransform = true;
                }
                if(self->light.shadowRange <= 0.0f){
                    self->light.shadowRange = 200.0f;
                }
                migratedLightSyncTransform.insert(key);
            }
            if(migratedLightDefaults.find(key) == migratedLightDefaults.end()){
                if(self->light.range <= 0.0f){
                    self->light.range = 20.0f;
                }
                if(self->light.shadowRange <= 0.0f){
                    self->light.shadowRange = 200.0f;
                }
                if(self->light.intensity <= 0.0f){
                    self->light.intensity = 4.0f;
                }
                if(self->light.falloff <= 0.0f){
                    self->light.falloff = 2.0f;
                }
                if(self->light.type == LightType::SPOT && self->light.spotAngle <= 0.0f){
                    self->light.spotAngle = 45.0f;
                }
                migratedLightDefaults.insert(key);
            }
        }
        syncLightFromTransform();
        const char* typeLabels[] = {"Point", "Directional", "Spot"};
        int typeIndex = static_cast<int>(self->light.type);
        if(ImGui::Combo("Type", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels))){
            LightType newType = static_cast<LightType>(typeIndex);
            if(newType != self->light.type){
                LightType prevType = self->light.type;
                self->light.type = newType;
                self->syncDirection = (newType != LightType::POINT);
                self->syncTransform = true;
                if(!std::isfinite(self->light.direction.x) || !std::isfinite(self->light.direction.y) || !std::isfinite(self->light.direction.z)){
                    self->light.direction = Math3D::Vec3(0,-1,0);
                }
                if(newType == LightType::POINT){
                    if(prevType == LightType::DIRECTIONAL || self->light.range <= 0.1f){
                        self->light.range = 20.0f;
                    }
                    ensurePointLightBounds(entity, self->light.range);
                    if(self->light.shadowRange <= 0.0f){
                        self->light.shadowRange = 200.0f;
                    }
                }else if(newType == LightType::DIRECTIONAL){
                    if(self->light.direction.length() < Math3D::EPSILON){
                        self->light.direction = Math3D::Vec3(0, -1, 0);
                    }
                    if(prevType != LightType::DIRECTIONAL){
                        self->light.range = 20.0f;
                        self->light.shadowRange = 200.0f;
                    }
                }else if(newType == LightType::SPOT){
                    if(self->light.direction.length() < Math3D::EPSILON){
                        self->light.direction = Math3D::Vec3(0, -1, 0);
                    }
                    if(prevType == LightType::DIRECTIONAL || self->light.range <= 0.1f){
                        self->light.range = 20.0f;
                    }
                    if(self->light.spotAngle <= 0.1f || prevType == LightType::DIRECTIONAL){
                        self->light.spotAngle = 45.0f;
                    }
                    if(self->light.shadowRange <= 0.0f){
                        self->light.shadowRange = 200.0f;
                    }
                }
            }
            syncLightFromTransform();
        }
        ImGui::ColorEdit4("Color", &self->light.color.x);
        ImGui::DragFloat("Intensity", &self->light.intensity, 0.05f, 0.0f, 10.0f);
        bool prevSyncTransform = self->syncTransform;
        if(ImGui::Checkbox("Sync Transform", &self->syncTransform)){
            if(prevSyncTransform && !self->syncTransform){
                Math3D::Vec3 worldPos;
                Math3D::Vec3 worldForward;
                if(getWorldLightBasis(worldPos, worldForward)){
                    self->light.position = worldPos;
                }
            }
            if(self->syncTransform && self->light.type == LightType::POINT){
                ensurePointLightBounds(entity, self->light.range);
            }
            syncLightFromTransform();
        }
        if(self->light.type == LightType::POINT){
            float range = self->light.range;
            if(ImGui::DragFloat("Range", &range, 0.1f, 0.1f, 1000.0f)){
                self->light.range = range;
                ensurePointLightBounds(entity, range);
            }
            ImGui::DragFloat("Falloff", &self->light.falloff, 0.05f, 0.1f, 3.0f);
            ImGui::DragFloat("Shadow Range", &self->light.shadowRange, 0.1f, 0.0f, 2000.0f);
        }else if(self->light.type == LightType::SPOT){
            ImGui::DragFloat("Range", &self->light.range, 0.1f, 0.1f, 1000.0f);
            ImGui::DragFloat("Spot Angle", &self->light.spotAngle, 0.25f, 1.0f, 170.0f);
            ImGui::DragFloat("Falloff", &self->light.falloff, 0.05f, 0.1f, 3.0f);
            ImGui::DragFloat("Shadow Range", &self->light.shadowRange, 0.1f, 0.0f, 2000.0f);
        }else if(self->light.type == LightType::DIRECTIONAL){
            ImGui::DragFloat("Shadow Range", &self->light.shadowRange, 0.5f, 10.0f, 2000.0f);
        }
        if(self->light.type != LightType::POINT){
            bool prevSyncDirection = self->syncDirection;
            if(ImGui::Checkbox("Sync Direction", &self->syncDirection)){
                Math3D::Vec3 worldPos;
                Math3D::Vec3 worldForward;
                if(getWorldLightBasis(worldPos, worldForward)){
                    if(self->syncDirection){
                        self->light.direction = worldForward;
                    }else if(prevSyncDirection){
                        self->light.direction = worldForward;
                    }
                }
            }
            bool dirEditable = !self->syncDirection;
            if(!dirEditable){
                ImGui::BeginDisabled();
            }
            if(ImGui::DragFloat3("Direction", &self->light.direction.x, 0.02f, -1.0f, 1.0f)){
                if(self->light.direction.length() < Math3D::EPSILON){
                    self->light.direction = Math3D::Vec3(0,-1,0);
                }else{
                    self->light.direction = self->light.direction.normalize();
                }
            }
            if(!dirEditable){
                ImGui::EndDisabled();
            }
        }
        ImGui::Checkbox("Cast Shadows", &self->light.castsShadows);
        const char* shadowLabels[] = {"Hard", "Standard", "Smooth"};
        int shadowIndex = static_cast<int>(self->light.shadowType);
        if(ImGui::Combo("Shadow Type", &shadowIndex, shadowLabels, IM_ARRAYSIZE(shadowLabels))){
            self->light.shadowType = static_cast<ShadowType>(shadowIndex);
        }
        ImGui::DragFloat("Shadow Bias", &self->light.shadowBias, 0.0005f, 0.0f, 0.01f, "%.6f");
        ImGui::DragFloat("Shadow Normal Bias", &self->light.shadowNormalBias, 0.0005f, 0.0f, 0.01f, "%.6f");
        ImGui::DragFloat("Shadow Strength", &self->light.shadowStrength, 0.01f, 0.0f, 1.0f);
    }
}

void BoundsComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if (ImGui::CollapsingHeader("Bounds Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        // 1. Type Selector
        const char* typeNames[] = { "Box", "Sphere", "Capsule" };
        
        // Convert Enum to int for ImGui
        int currentItem = static_cast<int>(type);
        
        if (ImGui::Combo("Shape Type", &currentItem, typeNames, IM_ARRAYSIZE(typeNames))) {
            type = static_cast<BoundsType>(currentItem);
        }

        ImGui::Separator();

        // 2. Context-Sensitive Fields
        switch (type) {
            case BoundsType::Box:
                ImGui::TextDisabled("(Half-Size Extents)");
                // Clamp min to 0 to prevent inverted boxes
                ImGui::DragFloat3("Extents", &size.x, 0.05f, 0.0f, 0.0f, "%.2f");
                break;

            case BoundsType::Sphere:
                ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 0.0f, "%.2f");
                break;

            case BoundsType::Capsule:
                ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 0.0f, "%.2f");
                ImGui::DragFloat("Total Height", &height, 0.05f, 0.0f, 0.0f, "%.2f");
                break;
        }

        ImGui::Separator();
        // Assuming you have a debug rendering flag somewhere
        static bool debugDraw = false; 
        ImGui::Checkbox("Show Debug Lines", &debugDraw);
        if(debugDraw && scene) { 
            // Call your debug drawer here / none yet will add.
        }
    }
};
void CameraComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){};

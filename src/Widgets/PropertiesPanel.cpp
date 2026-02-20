#include "Widgets/PropertiesPanel.h"

#include "ECSComponents.h"
#include "Logbot.h"
#include "StringUtils.h"
#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <memory>

namespace {
    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    enum class AddComponentKind {
        Transform,
        MeshRenderer,
        Light,
        Bounds,
        Camera,
        SSAO,
        DepthOfField,
        AntiAliasing,
        Script
    };

    struct ScriptAssetOption {
        std::string assetRef;
        std::string displayName;
    };

    template<typename T>
    bool hasComponent(NeoECS::ECSComponentManager* manager, NeoECS::ECSEntity* entity){
        return manager && entity && (manager->getECSComponent<T>(entity) != nullptr);
    }

    bool ensureTransformComponent(NeoECS::GameObject* wrapper, NeoECS::ECSComponentManager* manager, NeoECS::ECSEntity* entity){
        if(!wrapper || !manager || !entity){
            return false;
        }
        if(hasComponent<TransformComponent>(manager, entity)){
            return true;
        }
        if(!wrapper->addComponent<TransformComponent>()){
            return false;
        }
        return hasComponent<TransformComponent>(manager, entity);
    }

    bool addComponentToEntity(PScene scene, NeoECS::ECSEntity* entity, AddComponentKind kind, std::string* outError = nullptr){
        auto fail = [&](const char* message) -> bool {
            if(outError){
                *outError = message ? message : "Unknown add-component failure.";
            }
            return false;
        };

        if(!scene || !scene->getECS() || !entity){
            return fail("Invalid scene/entity.");
        }

        auto* ecs = scene->getECS();
        auto* manager = ecs->getComponentManager();
        if(!manager){
            return fail("Missing ECS component manager.");
        }

        std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ecs->getContext(), entity));
        if(!wrapper){
            return fail("Failed to create entity wrapper.");
        }

        switch(kind){
            case AddComponentKind::Transform:
                if(hasComponent<TransformComponent>(manager, entity)){
                    return fail("Transform Component already exists.");
                }
                return wrapper->addComponent<TransformComponent>();
            case AddComponentKind::MeshRenderer:
                if(hasComponent<MeshRendererComponent>(manager, entity)){
                    return fail("Mesh Renderer Component already exists.");
                }
                if(!ensureTransformComponent(wrapper.get(), manager, entity)){
                    return fail("Failed to ensure Transform Component.");
                }
                return wrapper->addComponent<MeshRendererComponent>();
            case AddComponentKind::Light:
                if(hasComponent<LightComponent>(manager, entity)){
                    return fail("Light Component already exists.");
                }
                if(!ensureTransformComponent(wrapper.get(), manager, entity)){
                    return fail("Failed to ensure Transform Component.");
                }
                return wrapper->addComponent<LightComponent>();
            case AddComponentKind::Bounds:
                if(hasComponent<BoundsComponent>(manager, entity)){
                    return fail("Bounds Component already exists.");
                }
                if(!ensureTransformComponent(wrapper.get(), manager, entity)){
                    return fail("Failed to ensure Transform Component.");
                }
                return wrapper->addComponent<BoundsComponent>();
            case AddComponentKind::Camera:
                if(hasComponent<CameraComponent>(manager, entity)){
                    return fail("Camera Component already exists.");
                }
                if(!ensureTransformComponent(wrapper.get(), manager, entity)){
                    return fail("Failed to ensure Transform Component.");
                }
                if(!wrapper->addComponent<CameraComponent>()){
                    return fail("Failed to add Camera Component.");
                }
                if(auto* cameraComp = manager->getECSComponent<CameraComponent>(entity)){
                    if(!cameraComp->camera){
                        float width = 1280.0f;
                        float height = 720.0f;
                        if(scene->getWindow()){
                            width = static_cast<float>(scene->getWindow()->getWindowWidth());
                            height = static_cast<float>(scene->getWindow()->getWindowHeight());
                        }
                        cameraComp->camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(width, height), 0.1f, 1000.0f);
                    }
                }
                return true;
            case AddComponentKind::SSAO:
                if(hasComponent<SSAOComponent>(manager, entity)){
                    return fail("SSAO Component already exists.");
                }
                if(!hasComponent<CameraComponent>(manager, entity)){
                    return fail("SSAO requires Camera Component.");
                }
                return wrapper->addComponent<SSAOComponent>();
            case AddComponentKind::DepthOfField:
                if(hasComponent<DepthOfFieldComponent>(manager, entity)){
                    return fail("Depth Of Field Component already exists.");
                }
                if(!hasComponent<CameraComponent>(manager, entity)){
                    return fail("Depth Of Field requires Camera Component.");
                }
                return wrapper->addComponent<DepthOfFieldComponent>();
            case AddComponentKind::AntiAliasing:
                if(hasComponent<AntiAliasingComponent>(manager, entity)){
                    return fail("Anti-Aliasing Component already exists.");
                }
                if(!hasComponent<CameraComponent>(manager, entity)){
                    return fail("Anti-Aliasing requires Camera Component.");
                }
                return wrapper->addComponent<AntiAliasingComponent>();
            case AddComponentKind::Script:
                if(hasComponent<ScriptComponent>(manager, entity)){
                    return fail("Script Component already exists.");
                }
                return wrapper->addComponent<ScriptComponent>();
        }

        return fail("Unsupported component type.");
    }

    bool addScriptAssetToEntity(PScene scene, NeoECS::ECSEntity* entity, const std::string& scriptAssetRef, std::string* outError = nullptr){
        auto fail = [&](const char* message) -> bool {
            if(outError){
                *outError = message ? message : "Unknown script add failure.";
            }
            return false;
        };

        if(scriptAssetRef.empty()){
            return fail("Invalid script path.");
        }
        if(!scene || !scene->getECS() || !entity){
            return fail("Invalid scene/entity.");
        }

        auto* ecs = scene->getECS();
        auto* manager = ecs->getComponentManager();
        if(!manager){
            return fail("Missing ECS component manager.");
        }

        std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ecs->getContext(), entity));
        if(!wrapper){
            return fail("Failed to create entity wrapper.");
        }

        auto* scripts = manager->getECSComponent<ScriptComponent>(entity);
        if(!scripts){
            if(!wrapper->addComponent<ScriptComponent>()){
                return fail("Failed to add Script Component.");
            }
            scripts = manager->getECSComponent<ScriptComponent>(entity);
        }

        if(!scripts){
            return fail("Script Component unavailable after add.");
        }

        if(!scripts->addScriptAsset(scriptAssetRef)){
            return fail("Script already attached or invalid.");
        }
        return true;
    }

    std::vector<ScriptAssetOption> collectLuaScriptAssets(const std::filesystem::path& assetRoot){
        std::vector<ScriptAssetOption> scripts;
        if(assetRoot.empty()){
            return scripts;
        }

        std::error_code ec;
        if(!std::filesystem::exists(assetRoot, ec) || !std::filesystem::is_directory(assetRoot, ec)){
            return scripts;
        }

        std::filesystem::recursive_directory_iterator iter(assetRoot, ec);
        std::filesystem::recursive_directory_iterator end;
        for(; iter != end; iter.increment(ec)){
            if(ec){
                ec.clear();
                continue;
            }

            const std::filesystem::directory_entry& entry = *iter;
            if(!entry.is_regular_file(ec)){
                ec.clear();
                continue;
            }

            const std::filesystem::path path = entry.path();
            if(StringUtils::ToLowerCase(path.extension().string()) != ".lua"){
                continue;
            }

            std::filesystem::path rel = path.lexically_relative(assetRoot);
            if(rel.empty()){
                continue;
            }
            const std::string relStr = StringUtils::ReplaceAll(rel.generic_string(), "\\", "/");
            if(StringUtils::BeginsWith(relStr, "..")){
                continue;
            }

            ScriptAssetOption option;
            option.assetRef = std::string(ASSET_DELIMITER) + "/" + relStr;
            option.displayName = BuildScriptDisplayNameFromPath(path.filename().string());
            scripts.push_back(option);
        }

        std::sort(scripts.begin(), scripts.end(), [](const ScriptAssetOption& a, const ScriptAssetOption& b){
            if(a.displayName == b.displayName){
                return a.assetRef < b.assetRef;
            }
            return a.displayName < b.displayName;
        });

        scripts.erase(std::unique(scripts.begin(), scripts.end(), [](const ScriptAssetOption& a, const ScriptAssetOption& b){
            return a.assetRef == b.assetRef;
        }), scripts.end());

        return scripts;
    }
}

void PropertiesPanel::draw(float x,
                           float y,
                           float w,
                           float h,
                           PScene targetScene,
                           const std::filesystem::path& assetRoot,
                           std::filesystem::path& selectedAssetPath,
                           const std::string& selectedEntityId,
                           const std::function<NeoECS::ECSEntity*(const std::string&)>& findEntityById){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Properties", nullptr, kPanelFlags);

    filePreviewWidget.setAssetRoot(assetRoot);
    if(!selectedAssetPath.empty()){
        std::error_code ec;
        bool exists = std::filesystem::exists(selectedAssetPath, ec);
        if(!exists || ec){
            selectedAssetPath.clear();
        }else{
            if(std::filesystem::is_regular_file(selectedAssetPath, ec)){
                ImGui::TextUnformatted("Asset Preview");
                ImGui::Separator();
                filePreviewWidget.setFilePath(selectedAssetPath);
                filePreviewWidget.draw();
                ImGui::End();
                return;
            }
            if(std::filesystem::is_directory(selectedAssetPath, ec)){
                ImGui::Text("Directory: %s", selectedAssetPath.filename().string().c_str());
                ImGui::TextDisabled("%s", selectedAssetPath.string().c_str());
                ImGui::End();
                return;
            }
        }
    }

    NeoECS::ECSEntity* entity = nullptr;
    if(findEntityById){
        entity = findEntityById(selectedEntityId);
    }
    if(!entity){
        ImGui::TextUnformatted("No entity selected.");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity: %s", entity->getName().c_str());
    ImGui::Separator();

    if(targetScene && targetScene->getECS()){
        auto* componentMgr = targetScene->getECS()->getComponentManager();
        auto componentsForEntity = componentMgr->getECSComponents(entity);

        std::sort(componentsForEntity.begin(), componentsForEntity.end(),
        [](NeoECS::ECSComponent* a, NeoECS::ECSComponent* b) {
            if(dynamic_cast<TransformComponent*>(a)) return true;
            if(dynamic_cast<TransformComponent*>(b)) return false;
            return false;
        });

        for(auto component : componentsForEntity){
            IEditorCompatibleComponent* editorComponentPtr = dynamic_cast<IEditorCompatibleComponent*>(component);
            if(!editorComponentPtr) continue;

            ImGui::PushID(component);
            editorComponentPtr->drawPropertyWidget(targetScene->getECS(), targetScene);
            ImGui::PopID();
        }

        ImGui::Dummy(ImVec2(0.0f, 20.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        static std::vector<ScriptAssetOption> cachedScriptAssets;
        if(ImGui::Button("Add Component", ImVec2(-FLT_MIN, 30.0f))){
            cachedScriptAssets = collectLuaScriptAssets(assetRoot);
            ImGui::OpenPopup("Add Component Popup");
        }

        if(ImGui::BeginPopup("Add Component Popup")){
            const bool hasTransform = (componentMgr->getECSComponent<TransformComponent>(entity) != nullptr);
            const bool hasMesh = (componentMgr->getECSComponent<MeshRendererComponent>(entity) != nullptr);
            const bool hasLight = (componentMgr->getECSComponent<LightComponent>(entity) != nullptr);
            const bool hasBounds = (componentMgr->getECSComponent<BoundsComponent>(entity) != nullptr);
            const bool hasCamera = (componentMgr->getECSComponent<CameraComponent>(entity) != nullptr);
            const bool hasSsao = (componentMgr->getECSComponent<SSAOComponent>(entity) != nullptr);
            const bool hasDof = (componentMgr->getECSComponent<DepthOfFieldComponent>(entity) != nullptr);
            const bool hasAa = (componentMgr->getECSComponent<AntiAliasingComponent>(entity) != nullptr);
            auto* scriptComp = componentMgr->getECSComponent<ScriptComponent>(entity);
            const bool hasScriptComponent = (scriptComp != nullptr);

            auto drawAddMenuItem = [&](const char* label, AddComponentKind kind, bool enabled, const char* disabledReason){
                if(!enabled){
                    ImGui::BeginDisabled();
                }
                if(ImGui::MenuItem(label)){
                    std::string error;
                    if(!addComponentToEntity(targetScene, entity, kind, &error)){
                        LogBot.Log(LOG_ERRO, "Failed to add component '%s' to '%s': %s",
                                   label, entity->getName().c_str(), error.c_str());
                    }else{
                        ImGui::CloseCurrentPopup();
                    }
                }
                if(!enabled){
                    if(disabledReason && disabledReason[0] != '\0' && ImGui::IsItemHovered()){
                        ImGui::SetTooltip("%s", disabledReason);
                    }
                    ImGui::EndDisabled();
                }
            };

            if(ImGui::BeginMenu("Core")){
                drawAddMenuItem("Transform Component", AddComponentKind::Transform, !hasTransform, "Already added");
                drawAddMenuItem("Bounds Component", AddComponentKind::Bounds, !hasBounds, "Already added");
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Rendering")){
                drawAddMenuItem("Mesh Renderer Component", AddComponentKind::MeshRenderer, !hasMesh, "Already added");
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Lighting")){
                drawAddMenuItem("Light Component", AddComponentKind::Light, !hasLight, "Already added");
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Camera")){
                drawAddMenuItem("Camera Component", AddComponentKind::Camera, !hasCamera, "Already added");
                ImGui::Separator();
                drawAddMenuItem("SSAO / GI Component", AddComponentKind::SSAO, !hasSsao && hasCamera, hasCamera ? "Already added" : "Requires Camera Component");
                drawAddMenuItem("Depth Of Field Component", AddComponentKind::DepthOfField, !hasDof && hasCamera, hasCamera ? "Already added" : "Requires Camera Component");
                drawAddMenuItem("Anti-Aliasing Component", AddComponentKind::AntiAliasing, !hasAa && hasCamera, hasCamera ? "Already added" : "Requires Camera Component");
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Scripts")){
                drawAddMenuItem("Script Component", AddComponentKind::Script, !hasScriptComponent, "Already added");
                ImGui::Separator();
                if(cachedScriptAssets.empty()){
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("No .lua scripts found");
                    ImGui::EndDisabled();
                }else{
                    for(const auto& script : cachedScriptAssets){
                        const bool alreadyAttached = scriptComp && scriptComp->hasScriptAsset(script.assetRef);
                        if(alreadyAttached){
                            ImGui::BeginDisabled();
                        }
                        if(ImGui::MenuItem(script.displayName.c_str())){
                            std::string error;
                            if(!addScriptAssetToEntity(targetScene, entity, script.assetRef, &error)){
                                LogBot.Log(LOG_ERRO, "Failed to add script '%s' to '%s': %s",
                                           script.assetRef.c_str(), entity->getName().c_str(), error.c_str());
                            }else{
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        if(ImGui::IsItemHovered()){
                            if(alreadyAttached){
                                ImGui::SetTooltip("Already attached");
                            }else{
                                ImGui::SetTooltip("%s", script.assetRef.c_str());
                            }
                        }
                        if(alreadyAttached){
                            ImGui::EndDisabled();
                        }
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
    }

    ImGui::End();
}

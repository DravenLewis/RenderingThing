#include "Editor/Widgets/ECSViewPanel.h"

#include "ECS/Core/ECSComponents.h"
#include "imgui.h"

#include <cstring>
#include <memory>

namespace {
    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    constexpr const char* kEntityDragPayloadType = "ECS_ENTITY_DND";

    struct EntityDragPayload {
        char entityId[64] = {};
    };

    NeoECS::ECSEntity* findEntityById(PScene scene, const std::string& id){
        if(!scene || !scene->getECS() || id.empty()){
            return nullptr;
        }

        auto* entityManager = scene->getECS()->getEntityManager();
        const auto& entities = entityManager->getEntities();
        for(const auto& entityPtr : entities){
            auto* entity = entityPtr.get();
            if(entity && entity->getNodeUniqueID() == id){
                return entity;
            }
        }
        return nullptr;
    }

    bool isDescendantOf(NeoECS::ECSEntity* candidateDescendant, NeoECS::ECSEntity* candidateAncestor){
        if(!candidateDescendant || !candidateAncestor){
            return false;
        }
        for(auto* current = candidateDescendant->getParent(); current != nullptr; current = current->getParent()){
            if(current == candidateAncestor){
                return true;
            }
        }
        return false;
    }

    std::string makeUniqueChildName(NeoECS::ECSEntity* parentEntity, const std::string& baseName){
        if(!parentEntity){
            return baseName;
        }

        auto nameExists = [&](const std::string& candidateName) -> bool {
            for(const auto& kv : parentEntity->children()){
                auto* child = kv.second;
                if(child && child->getName() == candidateName){
                    return true;
                }
            }
            return false;
        };

        if(!nameExists(baseName)){
            return baseName;
        }

        int suffix = 1;
        for(;;){
            std::string candidate = baseName + " " + std::to_string(suffix);
            if(!nameExists(candidate)){
                return candidate;
            }
            suffix++;
        }
    }

    std::unique_ptr<NeoECS::GameObject> createEntityWrapper(PScene scene, NeoECS::ECSEntity* entity){
        if(!scene || !scene->getECS() || !entity){
            return nullptr;
        }
        return std::unique_ptr<NeoECS::GameObject>(
            NeoECS::GameObject::CreateFromECSEntity(scene->getECS()->getContext(), entity)
        );
    }
}

void ECSViewPanel::draw(float x,
                        float y,
                        float w,
                        float h,
                        PScene targetScene,
                        const std::string& selectedEntityId,
                        const std::function<void(const std::string&)>& onSelectEntity){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("ECS Graph", nullptr, kPanelFlags);

    pendingActions.clear();

    if(!targetScene || !targetScene->getECS()){
        ImGui::TextUnformatted("No ECS loaded.");
        ImGui::End();
        return;
    }

    auto* entityManager = targetScene->getECS()->getEntityManager();
    const auto& entities = entityManager->getEntities();

    ImGui::Checkbox("Show Hidden Part Icons", &showHiddenModelPartsInTree);
    ImGui::Separator();

    if(entities.empty()){
        ImGui::TextUnformatted("No entities.");
        ImGui::End();
        return;
    }

    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity || entity->getParent() != nullptr){
            continue;
        }
        drawEntityTree(entity, targetScene, selectedEntityId, onSelectEntity);
    }

    if(ImGui::BeginPopupContextWindow("ECSGraphContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)){
        auto* sceneRoot = targetScene->getSceneRootEntity();
        const std::string rootId = sceneRoot ? sceneRoot->getNodeUniqueID() : std::string();

        if(ImGui::BeginMenu("Create")){
            if(ImGui::MenuItem("Empty Game Object")){
                pendingActions.push_back({PendingActionKind::CreateEmpty, "", rootId});
            }
            if(ImGui::MenuItem("Light")){
                pendingActions.push_back({PendingActionKind::CreateLight, "", rootId});
            }
            if(ImGui::MenuItem("Camera")){
                pendingActions.push_back({PendingActionKind::CreateCamera, "", rootId});
            }
            ImGui::EndMenu();
        }

        NeoECS::ECSEntity* selectedEntity = findEntityById(targetScene, selectedEntityId);
        const bool canDeleteSelected = selectedEntity && !targetScene->isSceneRootEntity(selectedEntity);
        if(ImGui::MenuItem("Delete Selected", nullptr, false, canDeleteSelected)){
            pendingActions.push_back({PendingActionKind::DeleteEntity, selectedEntityId, ""});
        }

        const bool canReparentToRoot =
            selectedEntity &&
            !targetScene->isSceneRootEntity(selectedEntity) &&
            selectedEntity->getParent() != sceneRoot;
        if(ImGui::MenuItem("Parent Selected To Scene Root", nullptr, false, canReparentToRoot)){
            pendingActions.push_back({PendingActionKind::ReparentToRoot, selectedEntityId, ""});
        }

        ImGui::EndPopup();
    }

    applyPendingActions(targetScene, selectedEntityId, onSelectEntity);

    ImGui::End();
}

void ECSViewPanel::drawEntityTree(NeoECS::ECSEntity* entity,
                                  PScene targetScene,
                                  const std::string& selectedEntityId,
                                  const std::function<void(const std::string&)>& onSelectEntity){
    if(!entity || !targetScene || !targetScene->getECS()){
        return;
    }

    bool hasModelParts = false;
    const MeshRendererComponent* renderer = nullptr;
    auto* componentManager = targetScene->getECS()->getComponentManager();
    renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
    if(renderer && renderer->model){
        const auto& parts = renderer->model->getParts();
        for(const auto& part : parts){
            if(!part){
                continue;
            }
            if(part->hideInEditorTree && !showHiddenModelPartsInTree){
                continue;
            }
            hasModelParts = true;
            break;
        }
    }

    const bool hasEntityChildren = !entity->children().empty();
    const bool hasChildren = hasEntityChildren || hasModelParts;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if(!hasChildren){
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if(entity->getNodeUniqueID() == selectedEntityId){
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void*)entity, flags, "%s", entity->getName().c_str());
    if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && onSelectEntity){
        onSelectEntity(entity->getNodeUniqueID());
    }

    const bool isSceneRoot = targetScene->isSceneRootEntity(entity);
    const std::string entityId = entity->getNodeUniqueID();
    if(ImGui::BeginPopupContextItem("EntityContextMenu")){
        if(ImGui::BeginMenu("Create Child")){
            if(ImGui::MenuItem("Empty Game Object")){
                pendingActions.push_back({PendingActionKind::CreateEmpty, "", entityId});
            }
            if(ImGui::MenuItem("Light")){
                pendingActions.push_back({PendingActionKind::CreateLight, "", entityId});
            }
            if(ImGui::MenuItem("Camera")){
                pendingActions.push_back({PendingActionKind::CreateCamera, "", entityId});
            }
            ImGui::EndMenu();
        }

        NeoECS::ECSEntity* selectedEntity = findEntityById(targetScene, selectedEntityId);
        const bool canParentSelectedHere =
            selectedEntity &&
            selectedEntity != entity &&
            !isDescendantOf(entity, selectedEntity) &&
            !targetScene->isSceneRootEntity(selectedEntity);
        if(ImGui::MenuItem("Parent Selected Here", nullptr, false, canParentSelectedHere)){
            pendingActions.push_back({PendingActionKind::ReparentToEntity, selectedEntityId, entityId});
        }

        const bool canDeleteThis = !isSceneRoot;
        if(ImGui::MenuItem("Delete", nullptr, false, canDeleteThis)){
            pendingActions.push_back({PendingActionKind::DeleteEntity, entityId, ""});
        }

        ImGui::EndPopup();
    }

    if(!isSceneRoot && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){
        EntityDragPayload payload;
        std::strncpy(payload.entityId, entityId.c_str(), sizeof(payload.entityId) - 1);
        payload.entityId[sizeof(payload.entityId) - 1] = '\0';
        ImGui::SetDragDropPayload(kEntityDragPayloadType, &payload, sizeof(payload));
        ImGui::TextUnformatted(entity->getName().c_str());
        ImGui::EndDragDropSource();
    }

    if(ImGui::BeginDragDropTarget()){
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityDragPayloadType);
        if(payload && payload->Data && payload->DataSize == sizeof(EntityDragPayload)){
            const auto* dragPayload = static_cast<const EntityDragPayload*>(payload->Data);
            const std::string draggedId = dragPayload->entityId;
            if(!draggedId.empty() && draggedId != entityId){
                pendingActions.push_back({PendingActionKind::ReparentToEntity, draggedId, entityId});
            }
        }
        ImGui::EndDragDropTarget();
    }

    if(hasChildren && opened){
        if(hasModelParts && renderer && renderer->model){
            const auto& parts = renderer->model->getParts();
            for(size_t i = 0; i < parts.size(); ++i){
                const auto& part = parts[i];
                if(!part){
                    continue;
                }
                if(part->hideInEditorTree && !showHiddenModelPartsInTree){
                    continue;
                }
                ImGuiTreeNodeFlags partFlags = ImGuiTreeNodeFlags_Leaf |
                                              ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                              ImGuiTreeNodeFlags_SpanAvailWidth;
                ImGui::PushID(static_cast<int>(i));
                if(part->hideInEditorTree){
                    const ImVec4 hiddenTint(0.62f, 0.62f, 0.62f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, hiddenTint);
                    ImGui::TreeNodeEx("part_leaf", partFlags, "[H] Part %zu", i);
                    ImGui::PopStyleColor();
                }else{
                    ImGui::TreeNodeEx("part_leaf", partFlags, "Part %zu", i);
                }
                ImGui::PopID();
            }
        }

        if(hasEntityChildren){
            for(const auto& kv : entity->children()){
                drawEntityTree(kv.second, targetScene, selectedEntityId, onSelectEntity);
            }
        }
        ImGui::TreePop();
    }
}

void ECSViewPanel::applyPendingActions(PScene targetScene,
                                       const std::string& selectedEntityId,
                                       const std::function<void(const std::string&)>& onSelectEntity){
    if(!targetScene || !targetScene->getECS() || pendingActions.empty()){
        pendingActions.clear();
        return;
    }

    auto* sceneRoot = targetScene->getSceneRootEntity();

    auto resolveParentEntity = [&](const std::string& explicitParentId) -> NeoECS::ECSEntity* {
        if(explicitParentId.empty()){
            return sceneRoot;
        }
        return findEntityById(targetScene, explicitParentId);
    };

    auto selectEntity = [&](const std::string& id){
        if(onSelectEntity){
            onSelectEntity(id);
        }
    };

    for(const PendingAction& action : pendingActions){
        switch(action.kind){
            case PendingActionKind::CreateEmpty:
            case PendingActionKind::CreateLight:
            case PendingActionKind::CreateCamera: {
                NeoECS::ECSEntity* parentEntity = resolveParentEntity(action.targetEntityId);
                if(!action.targetEntityId.empty() && !parentEntity){
                    break;
                }
                std::unique_ptr<NeoECS::GameObject> parentWrapper = createEntityWrapper(targetScene, parentEntity);

                if(action.kind == PendingActionKind::CreateEmpty){
                    const std::string uniqueName = makeUniqueChildName(parentEntity, "GameObject");
                    auto* created = targetScene->createECSGameObject(uniqueName, parentWrapper.get());
                    if(created){
                        created->addComponent<TransformComponent>();
                        if(created->gameobject()){
                            selectEntity(created->gameobject()->getNodeUniqueID());
                        }
                    }
                }else if(action.kind == PendingActionKind::CreateLight){
                    const std::string uniqueName = makeUniqueChildName(parentEntity, "Light");
                    Light defaultLight = Light::CreatePointLight(
                        Math3D::Vec3(0.0f, 2.0f, 0.0f),
                        Color::WHITE,
                        3.0f,
                        12.0f,
                        2.0f
                    );
                    auto* created = targetScene->createLightGameObject(uniqueName, defaultLight, parentWrapper.get(), true, false);
                    if(created && created->gameobject()){
                        selectEntity(created->gameobject()->getNodeUniqueID());
                    }
                }else{
                    const std::string uniqueName = makeUniqueChildName(parentEntity, "Camera");
                    auto* created = targetScene->createCameraGameObject(uniqueName, parentWrapper.get());
                    if(created && created->gameobject()){
                        selectEntity(created->gameobject()->getNodeUniqueID());
                    }
                }
                break;
            }
            case PendingActionKind::DeleteEntity: {
                NeoECS::ECSEntity* entity = findEntityById(targetScene, action.entityId);
                if(!entity || targetScene->isSceneRootEntity(entity)){
                    break;
                }

                NeoECS::ECSEntity* selectedEntity = findEntityById(targetScene, selectedEntityId);
                bool deletingSelectionTree = false;
                if(selectedEntity){
                    deletingSelectionTree = (selectedEntity == entity) || isDescendantOf(selectedEntity, entity);
                }

                std::unique_ptr<NeoECS::GameObject> wrapper = createEntityWrapper(targetScene, entity);
                if(wrapper && targetScene->destroyECSGameObject(wrapper.get()) && deletingSelectionTree){
                    selectEntity("");
                }
                break;
            }
            case PendingActionKind::ReparentToEntity: {
                NeoECS::ECSEntity* entity = findEntityById(targetScene, action.entityId);
                NeoECS::ECSEntity* parentEntity = findEntityById(targetScene, action.targetEntityId);
                if(!entity || !parentEntity){
                    break;
                }
                if(entity == parentEntity || targetScene->isSceneRootEntity(entity)){
                    break;
                }
                if(isDescendantOf(parentEntity, entity)){
                    break;
                }

                std::unique_ptr<NeoECS::GameObject> wrapper = createEntityWrapper(targetScene, entity);
                std::unique_ptr<NeoECS::GameObject> parentWrapper = createEntityWrapper(targetScene, parentEntity);
                if(wrapper){
                    wrapper->setParent(parentWrapper.get());
                }
                break;
            }
            case PendingActionKind::ReparentToRoot: {
                NeoECS::ECSEntity* entity = findEntityById(targetScene, action.entityId);
                if(!entity || !sceneRoot || entity == sceneRoot || targetScene->isSceneRootEntity(entity)){
                    break;
                }

                std::unique_ptr<NeoECS::GameObject> wrapper = createEntityWrapper(targetScene, entity);
                std::unique_ptr<NeoECS::GameObject> rootWrapper = createEntityWrapper(targetScene, sceneRoot);
                if(wrapper){
                    wrapper->setParent(rootWrapper.get());
                }
                break;
            }
        }
    }

    pendingActions.clear();
}

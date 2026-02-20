#include "Widgets/ECSViewPanel.h"

#include "ECSComponents.h"
#include "imgui.h"

namespace {
    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;
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

    ImGui::End();
}

void ECSViewPanel::drawEntityTree(NeoECS::ECSEntity* entity,
                                  PScene targetScene,
                                  const std::string& selectedEntityId,
                                  const std::function<void(const std::string&)>& onSelectEntity){
    if(!entity){
        return;
    }

    bool hasModelParts = false;
    const MeshRendererComponent* renderer = nullptr;
    if(targetScene && targetScene->getECS()){
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
    if(ImGui::IsItemClicked() && onSelectEntity){
        onSelectEntity(entity->getNodeUniqueID());
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

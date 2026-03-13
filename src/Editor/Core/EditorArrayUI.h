/**
 * @file src/Editor/Core/EditorArrayUI.h
 * @brief Lightweight array editor helpers for ImGui-backed asset editors.
 */

#ifndef EDITOR_ARRAY_UI_H
#define EDITOR_ARRAY_UI_H

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

namespace EditorArrayUI {

inline bool DrawValueEditor(const char* label, bool& value){
    return ImGui::Checkbox(label, &value);
}

inline bool DrawValueEditor(const char* label, int& value){
    return ImGui::InputInt(label, &value);
}

inline bool DrawValueEditor(const char* label, float& value){
    return ImGui::DragFloat(label, &value, 0.01f);
}

inline bool DrawValueEditor(const char* label, std::string& value){
    const size_t capacity = std::max<size_t>(256, value.size() + 64);
    std::vector<char> buffer(capacity, 0);
    if(!value.empty()){
        std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
        buffer.back() = '\0';
    }

    if(!ImGui::InputText(label, buffer.data(), buffer.size())){
        return false;
    }

    value = buffer.data();
    return true;
}

template<typename T>
bool DrawValueEditor(const char* label, T& value){
    (void)value;
    ImGui::TextDisabled("%s", label ? label : "Value");
    ImGui::TextDisabled("No default editor for this value type.");
    return false;
}

template<typename T, typename DrawElementFn>
bool DrawArray(const char* label,
               std::vector<T>& values,
               int maxSize,
               const char* elementLabel,
               DrawElementFn&& drawElement){
    if(!label){
        return false;
    }

    bool changed = false;
    ImGui::PushID(label);

    int size = static_cast<int>(values.size());
    if(ImGui::InputInt("Size", &size)){
        size = std::clamp(size, 0, std::max(maxSize, 0));
        if(size != static_cast<int>(values.size())){
            values.resize(static_cast<size_t>(size));
            changed = true;
        }
    }

    for(size_t i = 0; i < values.size(); ++i){
        ImGui::PushID(static_cast<int>(i));
        const std::string nodeLabel = std::string(elementLabel ? elementLabel : "Element") + " " + std::to_string(i);
        if(ImGui::TreeNodeEx("##ArrayElement", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", nodeLabel.c_str())){
            changed |= drawElement(values[i], i);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::PopID();
    return changed;
}

template<typename T>
bool DrawArray(const char* label,
               std::vector<T>& values,
               int maxSize,
               const char* elementLabel = "Element"){
    return DrawArray(label, values, maxSize, elementLabel, [](T& value, size_t) -> bool {
        return DrawValueEditor("Value", value);
    });
}

} // namespace EditorArrayUI

#endif // EDITOR_ARRAY_UI_H

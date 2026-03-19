/**
 * @file src/Editor/Core/EditorPropertyUI.h
 * @brief Shared stacked-label property helpers for ImGui-backed editors.
 */

#ifndef EDITOR_PROPERTY_UI_H
#define EDITOR_PROPERTY_UI_H

#include "imgui.h"

#include <algorithm>
#include <string>

namespace EditorPropertyUI {

inline constexpr float kMinRightMargin = 3.0f;

inline std::string VisibleLabel(const char* label){
    if(!label){
        return {};
    }

    std::string out(label);
    const size_t idMarker = out.find("##");
    if(idMarker != std::string::npos){
        out.erase(idMarker);
    }
    return out;
}

inline std::string HiddenLabel(const char* label){
    if(!label){
        return "##";
    }

    std::string out(label);
    const size_t idMarker = out.find("##");
    if(idMarker == std::string::npos){
        return "##" + out;
    }
    // Keep the complete original identifier while hiding the rendered text.
    return "##" + out;
}

inline void DrawLabel(const char* label){
    std::string visible = VisibleLabel(label);
    if(visible.empty()){
        return;
    }
    ImGui::TextUnformatted(visible.c_str());
}

inline float GetFieldWidth(float reservedWidth = 0.0f){
    return (std::max)(1.0f, ImGui::GetContentRegionAvail().x - reservedWidth - kMinRightMargin);
}

template<typename DrawFn>
inline bool DrawLabeled(const char* label, DrawFn&& drawFn, float reservedWidth = 0.0f){
    DrawLabel(label);
    ImGui::PushItemWidth(GetFieldWidth(reservedWidth));
    const bool changed = drawFn(HiddenLabel(label).c_str());
    ImGui::PopItemWidth();
    return changed;
}

inline bool InputText(const char* label, char* buffer, size_t bufferSize, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::InputText(hiddenLabel, buffer, bufferSize, flags);
    });
}

inline bool InputInt(const char* label, int* value, int step = 1, int stepFast = 100, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::InputInt(hiddenLabel, value, step, stepFast, flags);
    });
}

inline bool DragFloat(const char* label,
                      float* value,
                      float speed = 1.0f,
                      float minValue = 0.0f,
                      float maxValue = 0.0f,
                      const char* format = "%.3f",
                      ImGuiSliderFlags flags = ImGuiSliderFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::DragFloat(hiddenLabel, value, speed, minValue, maxValue, format, flags);
    });
}

inline bool DragFloat2(const char* label,
                       float value[2],
                       float speed = 1.0f,
                       float minValue = 0.0f,
                       float maxValue = 0.0f,
                       const char* format = "%.3f",
                       ImGuiSliderFlags flags = ImGuiSliderFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::DragFloat2(hiddenLabel, value, speed, minValue, maxValue, format, flags);
    });
}

inline bool DragFloat3(const char* label,
                       float value[3],
                       float speed = 1.0f,
                       float minValue = 0.0f,
                       float maxValue = 0.0f,
                       const char* format = "%.3f",
                       ImGuiSliderFlags flags = ImGuiSliderFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::DragFloat3(hiddenLabel, value, speed, minValue, maxValue, format, flags);
    });
}

inline bool DragFloat4(const char* label,
                       float value[4],
                       float speed = 1.0f,
                       float minValue = 0.0f,
                       float maxValue = 0.0f,
                       const char* format = "%.3f",
                       ImGuiSliderFlags flags = ImGuiSliderFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::DragFloat4(hiddenLabel, value, speed, minValue, maxValue, format, flags);
    });
}

inline bool DragInt(const char* label,
                    int* value,
                    float speed = 1.0f,
                    int minValue = 0,
                    int maxValue = 0,
                    const char* format = "%d",
                    ImGuiSliderFlags flags = ImGuiSliderFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::DragInt(hiddenLabel, value, speed, minValue, maxValue, format, flags);
    });
}

inline bool SliderFloat(const char* label,
                        float* value,
                        float minValue,
                        float maxValue,
                        const char* format = "%.3f",
                        ImGuiSliderFlags flags = ImGuiSliderFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::SliderFloat(hiddenLabel, value, minValue, maxValue, format, flags);
    });
}

inline bool SliderInt(const char* label,
                      int* value,
                      int minValue,
                      int maxValue,
                      const char* format = "%d",
                      ImGuiSliderFlags flags = ImGuiSliderFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::SliderInt(hiddenLabel, value, minValue, maxValue, format, flags);
    });
}

inline bool Checkbox(const char* label, bool* value){
    return ImGui::Checkbox(label ? label : "##", value);
}

inline bool ColorEdit3(const char* label, float color[3], ImGuiColorEditFlags flags = ImGuiColorEditFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::ColorEdit3(hiddenLabel, color, flags);
    });
}

inline bool ColorEdit4(const char* label, float color[4], ImGuiColorEditFlags flags = ImGuiColorEditFlags_None){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::ColorEdit4(hiddenLabel, color, flags);
    });
}

inline bool Combo(const char* label, int* currentItem, const char* const items[], int itemsCount, int popupMaxHeightInItems = -1){
    return DrawLabeled(label, [&](const char* hiddenLabel){
        return ImGui::Combo(hiddenLabel, currentItem, items, itemsCount, popupMaxHeightInItems);
    });
}

inline bool BeginCombo(const char* label, const char* previewValue, ImGuiComboFlags flags = ImGuiComboFlags_None){
    DrawLabel(label);
    ImGui::PushItemWidth(GetFieldWidth());
    const bool opened = ImGui::BeginCombo(HiddenLabel(label).c_str(), previewValue, flags);
    ImGui::PopItemWidth();
    return opened;
}

} // namespace EditorPropertyUI

#endif // EDITOR_PROPERTY_UI_H

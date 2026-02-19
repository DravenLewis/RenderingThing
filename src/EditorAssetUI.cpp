#include "EditorAssetUI.h"
#include "Asset.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace {
    constexpr const char* kAssetPayloadType = "EDITOR_ASSET_DND";
    constexpr size_t kMaxPathChars = 512;

    struct AssetDragPayload {
        int kind = static_cast<int>(EditorAssetUI::AssetKind::Unknown);
        int isDirectory = 0;
        char absolutePath[kMaxPathChars] = {};
        char assetRef[kMaxPathChars] = {};
    };

    std::string toLower(std::string value){
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool endsWith(const std::string& value, const std::string& suffix){
        if(value.size() < suffix.size()){
            return false;
        }
        return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
    }

    const char* kindGlyph(EditorAssetUI::AssetKind kind){
        using EditorAssetUI::AssetKind;
        switch(kind){
            case AssetKind::Directory:      return "DIR";
            case AssetKind::Image:          return "IMG";
            case AssetKind::Model:          return "MOD";
            case AssetKind::ShaderVertex:   return "VTX";
            case AssetKind::ShaderFragment: return "FRG";
            case AssetKind::ShaderGrometry: return "GEO";
            case AssetKind::ShaderCompute:  return "CMP";
            case AssetKind::ShaderTesselation:return "TES";
            case AssetKind::ShaderTask:     return "TSK";
            case AssetKind::ShaderRaytrace: return "RTX";
            case AssetKind::ShaderGeneric:  return "SHD";
            case AssetKind::ShaderAsset:    return "SAS";
            case AssetKind::MaterialAsset:  return "MAT";
            case AssetKind::Font:           return "FNT";
            case AssetKind::Text:           return "TXT";
            case AssetKind::Any:
            case AssetKind::Unknown:
            default:                        return "FIL";
        }
    }

    ImU32 kindColor(EditorAssetUI::AssetKind kind){
        using EditorAssetUI::AssetKind;
        switch(kind){
            case AssetKind::Directory:      return IM_COL32(88, 130, 255, 255);
            case AssetKind::Image:          return IM_COL32(72, 190, 124, 255);
            case AssetKind::Model:          return IM_COL32(244, 160, 70, 255);
            case AssetKind::ShaderVertex:   return IM_COL32(188, 112, 255, 255);
            case AssetKind::ShaderFragment: return IM_COL32(255, 116, 156, 255);
            case AssetKind::ShaderGrometry: return IM_COL32(255, 160, 98, 255);
            case AssetKind::ShaderCompute:  return IM_COL32(82, 204, 190, 255);
            case AssetKind::ShaderTesselation:return IM_COL32(196, 170, 255, 255);
            case AssetKind::ShaderTask:     return IM_COL32(122, 174, 255, 255);
            case AssetKind::ShaderRaytrace: return IM_COL32(255, 94, 94, 255);
            case AssetKind::ShaderGeneric:  return IM_COL32(166, 96, 242, 255);
            case AssetKind::ShaderAsset:    return IM_COL32(237, 132, 255, 255);
            case AssetKind::MaterialAsset:  return IM_COL32(90, 201, 155, 255);
            case AssetKind::Font:           return IM_COL32(104, 212, 219, 255);
            case AssetKind::Text:           return IM_COL32(152, 152, 152, 255);
            case AssetKind::Any:
            case AssetKind::Unknown:
            default:                        return IM_COL32(132, 132, 132, 255);
        }
    }

    void copyStringFixed(char* dst, size_t dstSize, const std::string& src){
        if(!dst || dstSize == 0){
            return;
        }
        std::memset(dst, 0, dstSize);
        if(src.empty()){
            return;
        }
        std::strncpy(dst, src.c_str(), dstSize - 1);
        dst[dstSize - 1] = '\0';
    }

    bool kindMatchesShader(EditorAssetUI::AssetKind offered, EditorAssetUI::AssetKind requested){
        using EditorAssetUI::AssetKind;
        auto isSpecificShaderStage = [](AssetKind kind){
            return kind == AssetKind::ShaderVertex ||
                   kind == AssetKind::ShaderFragment ||
                   kind == AssetKind::ShaderGrometry ||
                   kind == AssetKind::ShaderCompute ||
                   kind == AssetKind::ShaderTesselation ||
                   kind == AssetKind::ShaderTask ||
                   kind == AssetKind::ShaderRaytrace;
        };
        if(requested == AssetKind::ShaderGeneric){
            return isSpecificShaderStage(offered) || offered == AssetKind::ShaderGeneric;
        }
        if(requested == AssetKind::ShaderVertex){
            return (offered == AssetKind::ShaderVertex || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderFragment){
            return (offered == AssetKind::ShaderFragment || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderGrometry){
            return (offered == AssetKind::ShaderGrometry || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderCompute){
            return (offered == AssetKind::ShaderCompute || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderTesselation){
            return (offered == AssetKind::ShaderTesselation || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderTask){
            return (offered == AssetKind::ShaderTask || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderRaytrace){
            return (offered == AssetKind::ShaderRaytrace || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderAsset){
            return offered == AssetKind::ShaderAsset;
        }
        if(requested == AssetKind::MaterialAsset){
            return offered == AssetKind::MaterialAsset;
        }
        return false;
    }

    std::string requestedKindsTooltip(const EditorAssetUI::AssetKind* requestedKinds, size_t requestedKindCount){
        if(!requestedKinds || requestedKindCount == 0){
            return "any";
        }
        std::string out;
        for(size_t i = 0; i < requestedKindCount; ++i){
            if(i > 0){
                out += "/";
            }
            out += kindGlyph(requestedKinds[i]);
        }
        return out;
    }
}

namespace EditorAssetUI {

AssetKind ClassifyPath(const std::filesystem::path& path, bool isDirectory){
    if(isDirectory){
        return AssetKind::Directory;
    }

    std::string pathLower = toLower(path.generic_string());
    if(endsWith(pathLower, ".shader.asset")){
        return AssetKind::ShaderAsset;
    }
    if(endsWith(pathLower, ".material.asset")){
        return AssetKind::MaterialAsset;
    }

    std::string ext = toLower(path.extension().string());
    if(ext.empty()){
        return AssetKind::Unknown;
    }

    if(ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".dds" || ext == ".hdr"){
        return AssetKind::Image;
    }
    if(ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb"){
        return AssetKind::Model;
    }
    if(ext == ".vert"){
        return AssetKind::ShaderVertex;
    }
    if(ext == ".frag"){
        return AssetKind::ShaderFragment;
    }
    if(ext == ".geom" || ext == ".geo" || ext == ".gs" || ext == ".geometry"){
        return AssetKind::ShaderGrometry;
    }
    if(ext == ".tesc" || ext == ".tese" || ext == ".tess" || ext == ".tes" || ext == ".tcs"){
        return AssetKind::ShaderTesselation;
    }
    if(ext == ".comp" || ext == ".compute" || ext == ".cs"){
        return AssetKind::ShaderCompute;
    }
    if(ext == ".task" || ext == ".mesh" || ext == ".ms"){
        return AssetKind::ShaderTask;
    }
    if(ext == ".rtx" || ext == ".rgen" || ext == ".rchit" || ext == ".rmiss" || ext == ".raytrace" || ext == ".ray"){
        return AssetKind::ShaderRaytrace;
    }
    if(ext == ".glsl" || ext == ".shader"){
        return AssetKind::ShaderGeneric;
    }
    if(ext == ".ttf" || ext == ".otf" || ext == ".fnt"){
        return AssetKind::Font;
    }
    if(ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".xml" || ext == ".yaml" || ext == ".yml"){
        return AssetKind::Text;
    }
    return AssetKind::Unknown;
}

bool IsKindCompatible(AssetKind offered, AssetKind requested){
    if(requested == AssetKind::Any){
        return true;
    }
    if(kindMatchesShader(offered, requested)){
        return true;
    }
    return offered == requested;
}

bool IsKindCompatibleAny(AssetKind offered, const AssetKind* requestedKinds, size_t requestedKindCount){
    if(!requestedKinds || requestedKindCount == 0){
        return true;
    }
    for(size_t i = 0; i < requestedKindCount; ++i){
        if(IsKindCompatible(offered, requestedKinds[i])){
            return true;
        }
    }
    return false;
}

bool BuildTransaction(const std::filesystem::path& absolutePath, const std::filesystem::path& assetRoot, AssetTransaction& out){
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(absolutePath, ec);
    if(ec){
        normalized = absolutePath.lexically_normal();
    }
    std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(assetRoot, ec);
    if(ec){
        normalizedRoot = assetRoot.lexically_normal();
    }

    std::filesystem::path rel = normalized.lexically_relative(normalizedRoot);
    if(rel.empty() || rel.string().find("..") == 0){
        return false;
    }

    bool isDir = std::filesystem::is_directory(normalized, ec);
    if(ec){
        isDir = false;
    }
    out.absolutePath = normalized;
    out.kind = ClassifyPath(normalized, isDir);
    out.isDirectory = isDir;
    out.extension = toLower(normalized.extension().string());
    std::string relStr = rel.generic_string();
    out.assetRef = std::string(ASSET_DELIMITER) + "/" + relStr;
    return true;
}

bool DrawAssetTile(const char* id, const AssetTransaction& tx, float iconSize, bool selected, bool* outDoubleClicked){
    if(outDoubleClicked){
        *outDoubleClicked = false;
    }

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 size(iconSize, iconSize);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if(outDoubleClicked){
        *outDoubleClicked = doubleClicked;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 base = kindColor(tx.kind);
    ImU32 frame = hovered ? IM_COL32(248, 248, 248, 255) : IM_COL32(44, 44, 44, 255);
    if(selected){
        frame = IM_COL32(90, 160, 255, 255);
    }

    drawList->AddRectFilled(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y), IM_COL32(18, 18, 24, 255), 6.0f);
    drawList->AddRect(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y), frame, 6.0f, 0, 2.0f);

    float inset = 8.0f;
    ImVec2 innerMin(cursor.x + inset, cursor.y + inset);
    ImVec2 innerMax(cursor.x + size.x - inset, cursor.y + size.y - inset - 16.0f);
    drawList->AddRectFilled(innerMin, innerMax, base, 4.0f);

    const char* glyph = kindGlyph(tx.kind);
    ImVec2 textSz = ImGui::CalcTextSize(glyph);
    ImVec2 textPos(
        innerMin.x + (innerMax.x - innerMin.x - textSz.x) * 0.5f,
        innerMin.y + (innerMax.y - innerMin.y - textSz.y) * 0.5f
    );
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), glyph);

    if(hovered){
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tx.absolutePath.filename().string().c_str());
        ImGui::TextDisabled("%s", tx.assetRef.c_str());
        ImGui::EndTooltip();
    }

    return clicked;
}

void BeginAssetDragSource(const AssetTransaction& tx){
    if(!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){
        return;
    }

    AssetDragPayload payload;
    payload.kind = static_cast<int>(tx.kind);
    payload.isDirectory = tx.isDirectory ? 1 : 0;
    copyStringFixed(payload.absolutePath, sizeof(payload.absolutePath), tx.absolutePath.generic_string());
    copyStringFixed(payload.assetRef, sizeof(payload.assetRef), tx.assetRef);

    ImGui::SetDragDropPayload(kAssetPayloadType, &payload, sizeof(payload));
    ImGui::TextUnformatted(tx.absolutePath.filename().string().c_str());
    ImGui::TextDisabled("%s", tx.assetRef.c_str());
    ImGui::EndDragDropSource();
}

bool AcceptAssetDrop(AssetKind requestedKind, AssetTransaction& out){
    return AcceptAssetDrop(&requestedKind, 1, out);
}

bool AcceptAssetDrop(const AssetKind* requestedKinds, size_t requestedKindCount, AssetTransaction& out){
    if(!ImGui::BeginDragDropTarget()){
        return false;
    }

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayloadType);
    bool accepted = false;
    if(payload && payload->Data && payload->DataSize == sizeof(AssetDragPayload)){
        const AssetDragPayload* data = static_cast<const AssetDragPayload*>(payload->Data);
        AssetKind kind = static_cast<AssetKind>(data->kind);
        if(IsKindCompatibleAny(kind, requestedKinds, requestedKindCount)){
            out.kind = kind;
            out.isDirectory = (data->isDirectory != 0);
            out.absolutePath = std::filesystem::path(data->absolutePath);
            out.assetRef = data->assetRef;
            out.extension = toLower(out.absolutePath.extension().string());
            accepted = true;
        }
    }

    ImGui::EndDragDropTarget();
    return accepted;
}

bool DrawAssetDropInput(const char* label, char* buffer, size_t bufferSize, AssetKind requestedKind, bool readOnly, bool* outDropped){
    if(outDropped){
        *outDropped = false;
    }

    ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    bool changed = ImGui::InputText(label, buffer, bufferSize, flags);

    AssetTransaction tx;
    if(AcceptAssetDrop(requestedKind, tx)){
        std::memset(buffer, 0, bufferSize);
        std::strncpy(buffer, tx.assetRef.c_str(), bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
    }

    if(ImGui::IsItemHovered()){
        ImGui::SetTooltip("Drop %s asset here", (requestedKind == AssetKind::Any) ? "any" : kindGlyph(requestedKind));
    }

    return changed;
}

bool DrawAssetDropInput(const char* label, std::string& value, const AssetKind* requestedKinds, size_t requestedKindCount, bool readOnly, bool* outDropped){
    if(outDropped){
        *outDropped = false;
    }

    const size_t minCapacity = 256;
    const size_t maxCapacity = 2048;
    size_t capacity = value.size() + 64;
    if(capacity < minCapacity){
        capacity = minCapacity;
    }else if(capacity > maxCapacity){
        capacity = maxCapacity;
    }

    std::vector<char> buffer(capacity, 0);
    if(!value.empty()){
        std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
        buffer.back() = '\0';
    }

    ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    bool changed = ImGui::InputText(label, buffer.data(), buffer.size(), flags);
    if(changed){
        value = buffer.data();
    }

    AssetTransaction tx;
    if(AcceptAssetDrop(requestedKinds, requestedKindCount, tx)){
        value = tx.assetRef;
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
    }

    if(ImGui::IsItemHovered()){
        const std::string wanted = requestedKindsTooltip(requestedKinds, requestedKindCount);
        ImGui::SetTooltip("Drop %s asset here", wanted.c_str());
    }

    return changed;
}

bool DrawAssetDropInput(const char* label, std::string& value, std::initializer_list<AssetKind> requestedKinds, bool readOnly, bool* outDropped){
    const AssetKind* requestedPtr = requestedKinds.size() > 0 ? requestedKinds.begin() : nullptr;
    return DrawAssetDropInput(label, value, requestedPtr, requestedKinds.size(), readOnly, outDropped);
}

} // namespace EditorAssetUI

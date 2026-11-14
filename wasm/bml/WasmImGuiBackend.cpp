#include "WasmImGuiBackend.h"

#include "CKMaterial.h"
#include "CKTexture.h"

#include <emscripten.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace WasmImGuiBackend {
namespace {
int JsInit() {
    return EM_ASM_INT({
        return globalThis.BallanceOverlay && globalThis.BallanceOverlay.init() ? 1 : 0;
    });
}
void JsShutdown() {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.shutdown(); });
}
void JsNewFrame(int width, int height) {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.newFrame($0, $1); }, width, height);
}
void JsUploadTexture(void *id, int width, int height, const void *pixels, int pitch, int swapChannels) {
    EM_ASM({
        globalThis.BallanceOverlay && globalThis.BallanceOverlay.uploadTexture($0, $1, $2, $3, $4, $5);
    }, id, width, height, pixels, pitch, swapChannels);
}
void JsDestroyTexture(void *id) {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.destroyTexture($0); }, id);
}
void JsUploadVertices(const void *data, int count) {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.uploadVertices($0, $1); }, data, count);
}
void JsUploadIndices(const void *data, int count) {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.uploadIndices($0, $1); }, data, count);
}
void JsSetTexture(void *id) {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.setTexture($0); }, id);
}
void JsSetClipRect(float left, float top, float right, float bottom, int height) {
    EM_ASM({
        globalThis.BallanceOverlay && globalThis.BallanceOverlay.setClipRect($0, $1, $2, $3, $4);
    }, left, top, right, bottom, height);
}
void JsDrawElements(int count, int offset) {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.drawElements($0, $1); }, count, offset);
}
void JsResetRenderState() {
    EM_ASM({ globalThis.BallanceOverlay && globalThis.BallanceOverlay.resetRenderState(); });
}

bool g_Enabled = false;
std::unordered_set<uintptr_t> g_OwnedTextureIds;
std::vector<std::uint32_t> g_Indices;

void UploadTexture(uintptr_t id, int width, int height, const void *pixels, int pitch, bool swapChannels) {
    JsUploadTexture(reinterpret_cast<void *>(id), width, height, pixels, pitch,
                    swapChannels ? 1 : 0);
}

void UpdateImTexture(ImTextureData *texture) {
    if (!texture)
        return;
    const uintptr_t id = reinterpret_cast<uintptr_t>(texture);
    if (texture->Status == ImTextureStatus_WantDestroy) {
        JsDestroyTexture(reinterpret_cast<void *>(id));
        g_OwnedTextureIds.erase(id);
        texture->SetTexID(ImTextureID_Invalid);
        texture->SetStatus(ImTextureStatus_Destroyed);
        return;
    }
    if (texture->Status != ImTextureStatus_WantCreate &&
        texture->Status != ImTextureStatus_WantUpdates) {
        return;
    }
    if (!texture->Pixels || texture->Width <= 0 || texture->Height <= 0 ||
        texture->BytesPerPixel != 4) {
        return;
    }
    UploadTexture(id, texture->Width, texture->Height, texture->GetPixels(),
                  texture->GetPitch(), texture->UseColors);
    g_OwnedTextureIds.insert(id);
    texture->SetTexID(static_cast<ImTextureID>(id));
    texture->SetStatus(ImTextureStatus_OK);
}

void UpdateCkTexture(CKTexture *texture) {
    if (!texture)
        return;
    const uintptr_t id = reinterpret_cast<uintptr_t>(texture);
    VxImageDescEx description;
    if (!texture->GetSystemTextureDesc(description) || description.Width <= 0 ||
        description.Height <= 0 || description.BitsPerPixel != 32) {
        return;
    }
    CKBYTE *pixels = texture->LockSurfacePtr();
    if (!pixels)
        return;
    UploadTexture(id, description.Width, description.Height, pixels,
                  description.BytesPerLine, true);
    texture->ReleaseSurfacePtr();
}

void EnsureTexture(ImTextureID textureId) {
    const uintptr_t id = static_cast<uintptr_t>(textureId);
    if (id == 0 || g_OwnedTextureIds.contains(id)) {
        JsSetTexture(reinterpret_cast<void *>(id));
        return;
    }

    auto *object = reinterpret_cast<CKObject *>(textureId);
    if (!object) {
        JsSetTexture(nullptr);
        return;
    }
    if (object->GetClassID() == CKCID_TEXTURE) {
        UpdateCkTexture(static_cast<CKTexture *>(object));
    } else if (object->GetClassID() == CKCID_MATERIAL) {
        UpdateCkTexture(static_cast<CKMaterial *>(object)->GetTexture());
    }
    JsSetTexture(reinterpret_cast<void *>(id));
}
}

bool Init(CKContext *) {
    g_Enabled = JsInit() != 0;
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_browser_webgl2";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGuiPlatformIO &platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = 4096;
    platform.Renderer_TextureMaxHeight = 4096;
    return true;
}

void Shutdown() {
    for (uintptr_t id : g_OwnedTextureIds)
        JsDestroyTexture(reinterpret_cast<void *>(id));
    g_OwnedTextureIds.clear();
    JsShutdown();
    g_Enabled = false;
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset |
                         ImGuiBackendFlags_RendererHasTextures);
    ImGui::GetPlatformIO().ClearRendererHandlers();
}

void NewFrame() {
}

void RenderDrawData(ImDrawData *drawData) {
    if (!g_Enabled || !drawData)
        return;
    const int width = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int height = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (width <= 0 || height <= 0)
        return;

    if (drawData->Textures) {
        for (ImTextureData *texture : *drawData->Textures)
            UpdateImTexture(texture);
    }
    JsNewFrame(width, height);
    JsResetRenderState();

    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList *list = drawData->CmdLists[listIndex];
        if (!list || list->VtxBuffer.Size == 0)
            continue;
        JsUploadVertices(list->VtxBuffer.Data, list->VtxBuffer.Size);

        g_Indices.clear();
        g_Indices.reserve(list->IdxBuffer.Size);
        for (const ImDrawCmd &command : list->CmdBuffer) {
            if (command.UserCallback || command.ElemCount == 0)
                continue;
            const ImDrawIdx *indices = list->IdxBuffer.Data + command.IdxOffset;
            for (unsigned int index = 0; index < command.ElemCount; ++index)
                g_Indices.push_back(command.VtxOffset + indices[index]);
        }
        if (!g_Indices.empty()) {
            JsUploadIndices(g_Indices.data(), static_cast<int>(g_Indices.size()));
        }

        std::uint32_t offset = 0;
        for (const ImDrawCmd &command : list->CmdBuffer) {
            if (command.UserCallback == ImDrawCallback_ResetRenderState) {
                JsResetRenderState();
                continue;
            }
            if (command.UserCallback) {
                command.UserCallback(list, &command);
                continue;
            }
            if (command.ElemCount == 0)
                continue;

            const float left = (command.ClipRect.x - clipOffset.x) * clipScale.x;
            const float top = (command.ClipRect.y - clipOffset.y) * clipScale.y;
            const float right = (command.ClipRect.z - clipOffset.x) * clipScale.x;
            const float bottom = (command.ClipRect.w - clipOffset.y) * clipScale.y;
            if (right <= left || bottom <= top) {
                offset += command.ElemCount;
                continue;
            }
            JsSetClipRect(left, top, right, bottom, height);
            EnsureTexture(command.GetTexID());
            JsDrawElements(static_cast<int>(command.ElemCount),
                           static_cast<int>(offset * sizeof(std::uint32_t)));
            offset += command.ElemCount;
        }
    }
}
}

#include "UI/Overlay.h"
#include "UI/Ime/Presentation.h"
#include "WasmImGuiBackend.h"
#include "Loader/ModContext.h"

#include "CKContext.h"
#include "CKRenderContext.h"
#include "VxWindowFunctions.h"

#include "backends/imgui_impl_sdl3.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>
#include <emscripten/em_macros.h>

#include <chrono>
#include <cfloat>

namespace Overlay {
namespace {
ImGuiContext *g_Context = nullptr;
CKContext *g_GameContext = nullptr;
SDL_Window *g_Window = nullptr;
bool g_PlatformInitialized = false;
bool g_RendererInitialized = false;
bool g_DrawDataReady = false;
bool g_FrameActive = false;
bool g_TextInputActive = false;
std::chrono::steady_clock::time_point g_LastFrameTime;

void UpdateTextInputState(bool active) {
    if (!active && !g_TextInputActive)
        return;
    g_TextInputActive = active;
    int textLength = -1;
    if (active) {
        if (const ImGuiInputTextState *state = ImGui::GetInputTextState(ImGui::GetActiveID()))
            textLength = state->TextLen;
    }
    EM_ASM({
        const canvas = document.getElementById("canvas");
        if (canvas) {
            canvas.dataset.bmlTextInput = $0 ? "true" : "false";
            if ($1 >= 0)
                canvas.dataset.bmlTextLength = String($1);
            else
                delete canvas.dataset.bmlTextLength;
        }
    }, active, textLength);
}

bool SDLCALL ObserveSdlEvent(void *, SDL_Event *event) {
    if (!event)
        return true;
    if (event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL) {
        return true;
    }
    ImGuiContextScope scope;
    if (scope.IsActive() && g_PlatformInitialized)
        ImGui_ImplSDL3_ProcessEvent(event);
    return true;
}

void BeginHeadlessFrame() {
    const auto now = std::chrono::steady_clock::now();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = g_LastFrameTime.time_since_epoch().count() == 0
        ? 1.0f / 60.0f
        : std::chrono::duration<float>(now - g_LastFrameTime).count();
    if (io.DeltaTime <= 0.0f)
        io.DeltaTime = 1.0f / 60.0f;
    g_LastFrameTime = now;
}
}

ImGuiContext *GetImGuiContext() {
    return g_Context;
}

bool IsImGuiReady() {
    return g_RendererInitialized;
}

bool IsImGuiFrameActive() {
    return g_FrameActive;
}

ImGuiContext *ImGuiCreateContext() {
    if (g_Context)
        return g_Context;

    IMGUI_CHECKVERSION();
    ImGuiContext *previous = ImGui::GetCurrentContext();
    g_Context = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::SetCurrentContext(previous);
    return g_Context;
}

void ImGuiDestroyContext() {
    if (!g_Context)
        return;

    ImGuiShutdownRenderer(nullptr);
    ImGuiShutdownPlatform(nullptr);
    {
        ImGuiContextScope scope;
        ImGuiEndFrame();
    }
    ImGui::DestroyContext(g_Context);
    g_Context = nullptr;
    g_DrawDataReady = false;
    g_FrameActive = false;
    g_LastFrameTime = {};
}

bool ImGuiInitPlatform(CKContext *context) {
    if (g_PlatformInitialized)
        return true;

    ImGuiContextScope scope;
    g_Window = context ? static_cast<SDL_Window *>(context->GetMainWindow()) : nullptr;
    if (!g_Window)
        return true;
    if (!ImGui_ImplSDL3_InitForOther(g_Window))
        return false;

    SDL_AddEventWatch(&ObserveSdlEvent, nullptr);
    g_PlatformInitialized = true;
    return true;
}

bool ImGuiInitRenderer(CKContext *context) {
    if (g_RendererInitialized)
        return true;

    ImGuiContextScope scope;
    g_GameContext = context;
    if (!WasmImGuiBackend::Init(context)) {
        g_GameContext = nullptr;
        return false;
    }
    g_RendererInitialized = true;
    g_DrawDataReady = false;
    return true;
}

void ImGuiShutdownPlatform(CKContext *) {
    if (g_Window)
        SDL_RemoveEventWatch(&ObserveSdlEvent, nullptr);
    if (g_PlatformInitialized) {
        ImGuiContextScope scope;
        ImGui_ImplSDL3_Shutdown();
    }
    g_PlatformInitialized = false;
    g_Window = nullptr;
    UpdateTextInputState(false);
}

void ImGuiShutdownRenderer(CKContext *) {
    if (!g_RendererInitialized)
        return;

    ImGuiContextScope scope;
    WasmImGuiBackend::Shutdown();
    g_RendererInitialized = false;
    g_DrawDataReady = false;
    g_GameContext = nullptr;
}

void ImGuiNewFrame() {
    if (g_FrameActive)
        return;

    ImGuiContextScope scope;
    if (!scope.IsActive())
        return;

    g_DrawDataReady = false;
    if (g_PlatformInitialized)
        ImGui_ImplSDL3_NewFrame();
    else
        BeginHeadlessFrame();
    if (g_RendererInitialized)
        WasmImGuiBackend::NewFrame();
    if (ModContext *context = BML_GetModContext()) {
        if (auto *fonts = context->GetUiFontRuntime())
            fonts->Synchronize(*g_Context, ImGui::GetIO().DisplaySize.y);
    }
    ImGui::NewFrame();
    g_FrameActive = true;
}

void ImGuiEndFrame() {
    if (!g_FrameActive)
        return;
    ImGui::EndFrame();
    g_FrameActive = false;
}

void ImGuiRender() {
    if (!g_FrameActive)
        return;

    ImGui::Render();
    g_FrameActive = false;
    g_DrawDataReady = g_RendererInitialized;
}

void ImGuiOnRender() {
    if (!g_DrawDataReady)
        return;

    ImGuiContextScope scope;
    WasmImGuiBackend::RenderDrawData(ImGui::GetDrawData());
}
}

namespace Overlay::Ime::Presentation {
void ReservePlacement(const ImVec2 &, float) {}
void Draw(const ImGuiPlatformImeData &) {}
bool IsActive() {
    return false;
}
}

bool BMLWasmCapturesMouse() {
    Overlay::ImGuiContextScope scope;
    return scope.IsActive() && ImGui::GetIO().WantCaptureMouse;
}

void SetOverlayCursorVisible(bool visible) {
    static bool lastEffectiveVisible = true;
    const bool effectiveVisible = VxGetCursorVisibleState() || visible;
    if (effectiveVisible != lastEffectiveVisible) {
        if (effectiveVisible)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
        lastEffectiveVisible = effectiveVisible;
    }
    Overlay::UpdateTextInputState(ImGui::GetIO().WantTextInput);
}

extern "C" EMSCRIPTEN_KEEPALIVE void BMLWasmOverlaySetMouse(float x, float y, std::uint32_t buttons,
                                       float wheelX, float wheelY) {
    Overlay::ImGuiContextScope scope;
    if (!scope.IsActive())
        return;
    ImGuiIO &io = ImGui::GetIO();
    static std::uint32_t previousButtons = 0;
    if (x < 0.0f || y < 0.0f || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    } else {
        io.AddMousePosEvent(x * io.DisplaySize.x, y * io.DisplaySize.y);
    }
    for (int button = 0; button < 3; ++button) {
        const std::uint32_t mask = 1u << button;
        if ((buttons & mask) != (previousButtons & mask))
            io.AddMouseButtonEvent(button, (buttons & mask) != 0);
    }
    if (wheelX != 0.0f || wheelY != 0.0f)
        io.AddMouseWheelEvent(wheelX, wheelY);
    previousButtons = buttons;
}

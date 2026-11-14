#include "Hooks/RenderHook.h"
#include "Loader/ModContext.h"
#include "RCKRenderContext.h"

#include <utility>

namespace {
bool g_SkipNextRender = false;
}

bool BMLWasmConsumeSkipRender() {
    return std::exchange(g_SkipNextRender, false);
}

bool RenderHook::IsSkipRenderAvailable() {
    return BML_GetRenderContext() != nullptr;
}

void RenderHook::SkipNextRender() {
    g_SkipNextRender = true;
}

void RenderHook::EnableWidescreenFix(bool enable) {
    RCKRenderContext::SetWidescreenFix(enable);
    if (auto *context = BML_GetRenderContext())
        static_cast<RCKRenderContext *>(context)->UpdateProjection(TRUE);
}

bool RenderHook::IsWidescreenFixAvailable() {
    return true;
}

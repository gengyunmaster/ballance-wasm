#ifndef BML_WASM_IMGUI_BACKEND_H
#define BML_WASM_IMGUI_BACKEND_H

#include "imgui.h"

class CKContext;

namespace WasmImGuiBackend {
bool Init(CKContext *context);
void Shutdown();
void NewFrame();
void RenderDrawData(ImDrawData *drawData);
}

#endif

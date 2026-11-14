#include "WasmBuiltinMod.h"

void WasmBuiltinMod::OnProcess() {
    if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
        if (IsModsMenuOpen())
            CloseModsMenu();
        else
            OpenModsMenu();
    }
    BMLMod::OnProcess();
}

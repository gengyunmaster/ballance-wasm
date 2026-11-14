#ifndef BML_WASM_BUILTIN_MOD_H
#define BML_WASM_BUILTIN_MOD_H

#include "Mods/BMLMod.h"

class WasmBuiltinMod final : public BMLMod {
public:
    explicit WasmBuiltinMod(ModContext *context) : BMLMod(context) {}
    void OnProcess() override;
};

#endif

#ifndef BML_WASM_MOD_REGISTRY_H
#define BML_WASM_MOD_REGISTRY_H

#include "BML/IMod.h"

#include <span>

struct WasmModDefinition {
    const char *Id;
    IMod *(*Create)(IBML *);
    void (*Destroy)(IMod *);
};

std::span<const WasmModDefinition> GetOfficialWasmMods();

#endif

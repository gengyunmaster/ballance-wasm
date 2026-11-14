#include "WasmModRegistry.h"

#define BML_WASM_OFFICIAL_MODS(X) \
    X(BallSticky) \
    X(BMLModuls) \
    X(CameraUtilities) \
    X(DebugUtilities) \
    X(DeformedWB) \
    X(DualBallControl) \
    X(DynamicFov) \
    X(IVP) \
    X(MapScripts) \
    X(NewSpawn) \
    X(Segment) \
    X(SpiritTrail) \
    X(TravelMode)

#define BML_WASM_DECLARE_MOD(Name) \
    extern "C" IMod *BMLWasmCreate##Name(IBML *bml); \
    extern "C" void BMLWasmDestroy##Name(IMod *mod);
BML_WASM_OFFICIAL_MODS(BML_WASM_DECLARE_MOD)
#undef BML_WASM_DECLARE_MOD

std::span<const WasmModDefinition> GetOfficialWasmMods() {
    static constexpr WasmModDefinition mods[] = {
#define BML_WASM_REGISTER_MOD(Name) \
        {#Name, &BMLWasmCreate##Name, &BMLWasmDestroy##Name},
        BML_WASM_OFFICIAL_MODS(BML_WASM_REGISTER_MOD)
#undef BML_WASM_REGISTER_MOD
    };
    return mods;
}

#ifndef BML_WASM_RUNTIME_H
#define BML_WASM_RUNTIME_H

#include "CKAll.h"

extern "C" void BMLWasmOnObjectLoad(
    CKContext *context, const char *filename, CKBOOL isMap, const char *masterName,
    CK_CLASSID filterClass, CKBOOL addToScene, CKBOOL reuseMeshes,
    CKBOOL reuseMaterials, CKBOOL dynamic, XObjectArray *objects, CKObject *masterObject);

void BMLWasmRemoveHookBlocks(const CK_ID *ids, int count);
void BMLWasmResetGameEventHooks();

#endif

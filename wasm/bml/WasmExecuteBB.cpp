#include "BML/ExecuteBB.h"
#include "BMLWasmRuntime.h"

#include <unordered_map>

namespace {
struct HookEntry {
    ExecuteBB::CKBehaviorCallback Callback = nullptr;
    void *Argument = nullptr;
};

std::unordered_map<CK_ID, HookEntry> &Hooks() {
    static std::unordered_map<CK_ID, HookEntry> hooks;
    return hooks;
}

int RunHook(const CKBehaviorContext &context) {
    CKBehavior *behavior = context.Behavior;
    for (int index = 0; index < behavior->GetInputCount(); ++index)
        behavior->ActivateInput(index, FALSE);

    int result = CKBR_OK;
    const auto found = Hooks().find(behavior->GetID());
    if (found != Hooks().end() && found->second.Callback)
        result = found->second.Callback(&context, found->second.Argument);

    for (int index = 0; index < behavior->GetOutputCount(); ++index)
        behavior->ActivateOutput(index);
    return result;
}
}

namespace ExecuteBB {
CKBehavior *CreateHookBlock(CKBehavior *script, CKBehaviorCallback callback,
                            void *arg, int inCount, int outCount) {
    if (!script || !callback || inCount < 0 || outCount < 0)
        return nullptr;
    CKContext *context = script->GetCKContext();
    auto *behavior = static_cast<CKBehavior *>(context->CreateObject(
        CKCID_BEHAVIOR, const_cast<CKSTRING>("BML_HookBlock")));
    if (!behavior)
        return nullptr;
    behavior->UseFunction();
    behavior->SetFunction(RunHook);
    behavior->SetCompatibleClassID(CKCID_BEOBJECT);
    for (int index = 0; index < inCount; ++index)
        behavior->CreateInput(const_cast<CKSTRING>("In"));
    for (int index = 0; index < outCount; ++index)
        behavior->CreateOutput(const_cast<CKSTRING>("Out"));
    Hooks()[behavior->GetID()] = {callback, arg};
    if (script->AddSubBehavior(behavior) != CK_OK) {
        Hooks().erase(behavior->GetID());
        context->DestroyObject(behavior);
        return nullptr;
    }
    return behavior;
}
}

void BMLWasmRemoveHookBlocks(const CK_ID *ids, int count) {
    if (!ids || count <= 0)
        return;
    for (int index = 0; index < count; ++index)
        Hooks().erase(ids[index]);
}

#include "BMLWasmVersion.h"
#include "HashUtils.h"
#include "Loader/ModContext.h"
#include "Behavior/Blocks/HookBlock.h"
#include "Hooks/InputCursor.h"
#include "UI/Overlay.h"

#include "CKBaseManager.h"
#include "CKContext.h"
#include "CKPluginManager.h"

#include <cstdio>
#include <new>

namespace {
const CKGUID kBMLWasmManagerGuid(0x424d4c50, 0x5741534d);
constexpr char kBMLIdentity[] = "doyaGu/BallanceModLoaderPlus";

class BMLWasmManager final : public CKBaseManager {
public:
    explicit BMLWasmManager(CKContext *context)
        : CKBaseManager(context, kBMLWasmManagerGuid, const_cast<CKSTRING>("BML+ WASM")),
          m_ModContext(new ModContext(context)) {
        context->RegisterNewManager(this);
    }

    ~BMLWasmManager() override {
        delete m_ModContext;
    }

    CKERROR OnCKInit() override {
        const std::string identityHash = utils::Fnv1a64Hex(kBMLIdentity, sizeof(kBMLIdentity) - 1);
        std::printf("BML+ WASM module initialized: version %s, identity %s\n",
                    BML_WASM_VERSION, identityHash.c_str());
        Overlay::ImGuiCreateContext();
        if (!Overlay::ImGuiInitPlatform(m_Context)) {
            std::printf("BML+ WASM input backend initialization failed\n");
            return CKERR_NOTINITIALIZED;
        }
        return CK_OK;
    }

    CKERROR Shutdown() {
        Overlay::ImGuiContextScope scope;
        const bool stopped = m_ModContext->Shutdown();
        Overlay::ImGuiShutdownRenderer(m_Context);
        Overlay::ImGuiShutdownPlatform(m_Context);
        Overlay::ImGuiDestroyContext();
        return stopped ? CK_OK : CKERR_NOTINITIALIZED;
    }

    CKERROR OnCKEnd() override {
        return Shutdown();
    }

    CKERROR OnCKPlay() override {
        if (m_Context->GetPlayerRenderContext() && !Overlay::IsImGuiReady() &&
            !Overlay::ImGuiInitRenderer(m_Context)) {
            m_ModContext->GetLogger()->Warn("Cannot initialize the CK2 ImGui renderer");
        }
        if (m_Context->GetPlayerRenderContext() && !Overlay::IsImGuiReady() &&
            !Overlay::ImGuiInitRenderer(m_Context)) {
            m_ModContext->GetLogger()->Warn("Cannot initialize the CK2 ImGui renderer");
        }
        if (!m_Initialized) {
            if (!m_ModContext->Init()) {
                std::printf("BML+ WASM static runtime initialization failed\n");
                return CKERR_NOTINITIALIZED;
            }
            m_Initialized = true;
            std::printf("BML+ WASM static runtime initialized: %d official mods\n",
                        m_ModContext->GetOfficialModCount());
        }
        m_ModContext->OnLoadGame();
        return CK_OK;
    }

    CKERROR OnCKReset() override {
        Overlay::ImGuiEndFrame();
        m_ModContext->OnReset();
        return CK_OK;
    }

    CKERROR PreClearAll() override {
        m_ModContext->OnReset();
        return CK_OK;
    }

    CKERROR SequenceToBeDeleted(CK_ID *ids, int count) override {
        m_ModContext->RemoveDeletedObjects(ids, count);
        return CK_OK;
    }

    CKERROR PreProcess() override {
        Overlay::ImGuiContextScope scope;
        Overlay::ImGuiNewFrame();
        return CK_OK;
    }

    CKERROR PostProcess() override {
        {
            Overlay::ImGuiContextScope scope;
            m_ModContext->OnProcess();
            SetOverlayCursorVisible(ImGui::GetIO().WantCaptureMouse);
            Overlay::ImGuiRender();
        }
        return CK_OK;
    }

    CKERROR OnPostRender(CKRenderContext *dev) override {
        m_ModContext->OnRender(dev);
        return CK_OK;
    }

    CKERROR OnPostSpriteRender(CKRenderContext *) override {
        Overlay::ImGuiOnRender();
        return CK_OK;
    }

    CKDWORD GetValidFunctionsMask() override {
        return CKMANAGER_FUNC_OnCKInit | CKMANAGER_FUNC_OnCKEnd |
               CKMANAGER_FUNC_OnCKPlay | CKMANAGER_FUNC_OnCKReset |
               CKMANAGER_FUNC_PreClearAll | CKMANAGER_FUNC_OnSequenceToBeDeleted |
               CKMANAGER_FUNC_PreProcess | CKMANAGER_FUNC_PostProcess |
               CKMANAGER_FUNC_OnPostRender | CKMANAGER_FUNC_OnPostSpriteRender;
    }

    int GetFunctionPriority(CKMANAGER_FUNCTIONS function) override {
        if (function == CKMANAGER_FUNC_PreProcess)
            return -10000;
        return function == CKMANAGER_FUNC_PostProcess ? 10000 : 0;
    }

private:
    ModContext *m_ModContext = nullptr;
    bool m_Initialized = false;
};

CKERROR CreateBMLWasmManager(CKContext *context) {
    if (!context)
        return CKERR_INVALIDPARAMETER;
    if (context->GetManagerByGuid(kBMLWasmManagerGuid))
        return CK_OK;

    try {
        new BMLWasmManager(context);
        return CK_OK;
    } catch (...) {
        std::printf("BML+ WASM module initialization failed\n");
        return CKERR_NOTINITIALIZED;
    }
}

CKERROR DestroyBMLWasmManager(CKContext *context) {
    if (!context)
        return CKERR_INVALIDPARAMETER;
    if (auto *manager = static_cast<BMLWasmManager *>(context->GetManagerByGuid(kBMLWasmManagerGuid)))
        delete manager;
    return CK_OK;
}
}

void BMLWasmShutdown() {
    CKContext *context = BML_GetCKContext();
    if (!context)
        return;
    auto *manager = static_cast<BMLWasmManager *>(context->GetManagerByGuid(kBMLWasmManagerGuid));
    if (manager)
        (void) manager->Shutdown();
}

void RegisterBMLWasmBehaviorDeclarations(XObjectDeclarationArray *registry) {
    BML::Behavior::Internal::HookBlock::Register(registry);
}

int CKGet_BMLWasm_PluginInfoCount() {
    return 2;
}

CKPluginInfo *CKGet_BMLWasm_PluginInfo(int index) {
    static CKPluginInfo behaviorInfo;
    behaviorInfo.m_Author = "Kakuty";
    behaviorInfo.m_Description = "BML+ behavior hooks";
    behaviorInfo.m_Extension = "";
    behaviorInfo.m_Version = 0x000001;
    behaviorInfo.m_Type = CKPLUGIN_BEHAVIOR_DLL;
    behaviorInfo.m_GUID = CKGUID(0x3a086b4d, 0x2f4a4f01);
    behaviorInfo.m_Summary = "BML+ native behavior hook building blocks";

    static CKPluginInfo managerInfo;
    managerInfo.m_Author = "doyaGu";
    managerInfo.m_Description = "Ballance Mod Loader Plus WASM";
    managerInfo.m_Extension = "";
    managerInfo.m_Version = 0x000001;
    managerInfo.m_InitInstanceFct = CreateBMLWasmManager;
    managerInfo.m_Type = CKPLUGIN_MANAGER_DLL;
    managerInfo.m_ExitInstanceFct = DestroyBMLWasmManager;
    managerInfo.m_GUID = kBMLWasmManagerGuid;
    managerInfo.m_Summary = "BML+ WASM static native mod runtime";
    if (index == 0)
        return &behaviorInfo;
    return index == 1 ? &managerInfo : nullptr;
}

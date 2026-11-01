#include "BMLWasmVersion.h"
#include "HashUtils.h"
#include "Loader/ModContext.h"

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
        if (!m_ModContext->Init()) {
            std::printf("BML+ WASM static runtime initialization failed\n");
            return CKERR_NOTINITIALIZED;
        }
        std::printf("BML+ WASM static runtime initialized: %d official mods\n",
                    m_ModContext->GetModCount());
        return CK_OK;
    }

    CKERROR OnCKEnd() override {
        return m_ModContext->Shutdown() ? CK_OK : CKERR_NOTINITIALIZED;
    }

    CKERROR OnCKPlay() override {
        m_ModContext->OnLoadGame();
        return CK_OK;
    }

    CKERROR OnCKReset() override {
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

    CKERROR PostProcess() override {
        m_ModContext->OnProcess();
        return CK_OK;
    }

    CKERROR OnPostRender(CKRenderContext *dev) override {
        m_ModContext->OnRender(dev);
        return CK_OK;
    }

    CKDWORD GetValidFunctionsMask() override {
        return CKMANAGER_FUNC_OnCKInit | CKMANAGER_FUNC_OnCKEnd |
               CKMANAGER_FUNC_OnCKPlay | CKMANAGER_FUNC_OnCKReset |
               CKMANAGER_FUNC_PreClearAll | CKMANAGER_FUNC_OnSequenceToBeDeleted |
               CKMANAGER_FUNC_PostProcess | CKMANAGER_FUNC_OnPostRender;
    }

    int GetFunctionPriority(CKMANAGER_FUNCTIONS function) override {
        return function == CKMANAGER_FUNC_PostProcess ? 10000 : 0;
    }

private:
    ModContext *m_ModContext = nullptr;
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

int CKGet_BMLWasm_PluginInfoCount() {
    return 1;
}

CKPluginInfo *CKGet_BMLWasm_PluginInfo(int index) {
    if (index != 0)
        return nullptr;

    static CKPluginInfo info;
    info.m_Author = "doyaGu";
    info.m_Description = "Ballance Mod Loader Plus WASM";
    info.m_Extension = "";
    info.m_Version = 0x000001;
    info.m_InitInstanceFct = CreateBMLWasmManager;
    info.m_Type = CKPLUGIN_MANAGER_DLL;
    info.m_ExitInstanceFct = DestroyBMLWasmManager;
    info.m_GUID = kBMLWasmManagerGuid;
    info.m_Summary = "BML+ WASM static native mod runtime";
    return &info;
}

#include "Loader/ModContext.h"

#include "BML/Timer.h"
#include "Api/CommandApi.h"
#include "Behavior/Lifecycle.h"
#include "Config/Config.h"
#include "Console/Shell/ShellExecutor.h"
#include "Console/Shell/ShellIo.h"
#include "CustomMaps/CustomMapLoad.h"
#include "Hooks/RenderHook.h"
#include "Logging/Logger.h"
#include "Mods/NewBallTypeMod.h"
#include "PathUtils.h"
#include "StringUtils.h"
#include "WasmBuiltinMod.h"
#include "WasmModRegistry.h"
#include "WasmPersistentStore.h"
#ifdef BALLANCE_WASM_BROWSER_TESTING
#include "WasmBrowserTest.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#include <oniguruma.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


void BMLWasmInstallGameEventHooks(CKBehavior *script);

namespace {
ModContext *g_ModContext = nullptr;

bool ResolveStaticModOwner(const void *callerAddress, const char *ownerId,
                           const void *, const void *, std::string &owner) {
    ModContextLease context;
    if (!context)
        return false;
    owner = context->GetNativeModOwnerId(callerAddress, ownerId);
    return !owner.empty() && (!ownerId || owner == ownerId);
}

IMod *CreateBuiltinMod(IBML *bml) {
    return new WasmBuiltinMod(static_cast<ModContext *>(bml));
}

void DestroyBuiltinMod(IMod *mod) {
    delete mod;
}

template <typename T>
T *GetObject(ModContext *context, const char *name, CK_CLASSID classId) {
    return static_cast<T *>(context->GetCKContext()->GetObjectByNameAndClass(
        const_cast<CKSTRING>(name), classId));
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

BMLVersion ParseVersion(const char *text) {
    BMLVersion version(0, 0, 0);
    if (!text)
        return version;
    int *parts[3] = {&version.major, &version.minor, &version.patch};
    int part = 0;
    const char *cursor = text;
    while (*cursor && part < 3) {
        if (!std::isdigit(static_cast<unsigned char>(*cursor))) {
            ++cursor;
            continue;
        }
        char *end = nullptr;
        *parts[part++] = static_cast<int>(std::strtol(cursor, &end, 10));
        cursor = end;
    }
    return version;
}

std::wstring ConfigPath(IMod *mod) {
    return utils::Utf8ToUtf16("/ModLoader/Configs/" + std::string(mod->GetID()) + ".cfg");
}
}

ModContextLease::ModContextLease() : m_Context(BML_GetModContext()) {}

ModContext *BML_GetModContext() {
    return g_ModContext;
}

CKContext *BML_GetCKContext() {
    return g_ModContext ? g_ModContext->GetCKContext() : nullptr;
}

CKRenderContext *BML_GetRenderContext() {
    return g_ModContext ? g_ModContext->GetRenderContext() : nullptr;
}

ModContext::ModContext(CKContext *context)
    : m_CKContext(context),
      m_ObjectRefs(context),
      m_BehaviorPrototypes(BML::Behavior::Internal::MakeCKPrototypeSource(context)),
      m_Behaviors(context, [this](const void *object) {
          if (!object)
              return BML::Behavior::Internal::ObjectRef{};
          const BML_ObjectRef reference = m_ObjectRefs.Issue(
              const_cast<CKObject *>(static_cast<const CKObject *>(object)));
          return BML::Behavior::Internal::ObjectRef{
              reference.Domain, reference.Slot, reference.Generation};
      }, &m_BehaviorPrototypes),
      m_BehaviorSessions(
          m_Behaviors, &m_BehaviorPrototypes,
          BML::Behavior::Internal::MakeCKGraphSource(
              context, m_Behaviors, [this](const void *object) {
                  if (!object)
                      return BML::Behavior::Internal::ObjectRef{};
                  const BML_ObjectRef reference = m_ObjectRefs.Issue(
                      const_cast<CKObject *>(static_cast<const CKObject *>(object)));
                  return BML::Behavior::Internal::ObjectRef{
                      reference.Domain, reference.Slot, reference.Generation};
              })),
      m_BehaviorInstallations(
          context, m_Behaviors, &m_BehaviorPrototypes, *m_BehaviorSessions.Graph(),
          [this](const BML::Behavior::Internal::ObjectRef &reference) {
              return m_ObjectRefs.Resolve({reference.Domain, reference.Slot, reference.Generation});
          },
          [this](CKObject *object) {
              const BML_ObjectRef reference = m_ObjectRefs.Issue(object);
              return BML::Behavior::Internal::ObjectRef{
                  reference.Domain, reference.Slot, reference.Generation};
          }),
      m_BehaviorScripts(
          BML::Behavior::Internal::MakeCKScriptWorld(
              context, m_BehaviorInstallations, [this](const void *object) {
                  if (!object)
                      return BML::Behavior::Internal::ObjectRef{};
                  const BML_ObjectRef reference = m_ObjectRefs.Issue(
                      const_cast<CKObject *>(static_cast<const CKObject *>(object)));
                  return BML::Behavior::Internal::ObjectRef{
                      reference.Domain, reference.Slot, reference.Generation};
              }),
          [this](std::string_view name, const BML::Behavior::Internal::ObjectRef &script) {
              const auto status = m_BehaviorInstallations.LoadScript(std::string(name), script);
              if (!status && m_Logger)
                  m_Logger->Error("Failed to publish an authored Behavior Script: %s", status.Message.c_str());
          }),
      m_PhysicsForce(context, m_Behaviors),
      m_ExecuteBB(m_BehaviorSessions, m_PhysicsForce) {
    m_CommandApi = std::make_unique<BML::Api::CommandApi>(*this);
    m_ImcRuntime.SetInvocationGate(&m_ModInvocationGate);
    m_DataShare = BML::DataShareStore::GetInstance("BML");
    if (m_DataShare)
        m_DataShare->AddRef();
    m_ShellDispatcher = std::make_unique<ShellDispatcher>(*this);
    m_Shell = std::make_unique<BML::Shell::Executor>(*m_ShellDispatcher, &m_ShellEnvironment, &m_ShellEnvironment);
    g_ModContext = this;
}

class ModContext::ShellDispatcher final : public BML::Shell::IDispatcher {
public:
    explicit ShellDispatcher(ModContext &context) : m_Context(context) {}

    int Invoke(const std::vector<std::string> &args, const std::string *input) override {
        return m_Context.InvokeCommandArgs(args, input);
    }

    void WriteError(std::string_view message) override {
        m_Context.WriteShellError(message);
    }

    void PushSink(BML::Shell::OutputSink *sink) override {
        m_Context.m_OutputSinks.push_back(sink);
    }

    void PopSink() override {
        if (!m_Context.m_OutputSinks.empty())
            m_Context.m_OutputSinks.pop_back();
    }

    void LogExecute(std::string_view line) override {
        if (m_Context.m_Logger)
            m_Context.m_Logger->Info("Execute Command: %s", std::string(line).c_str());
    }

private:
    ModContext &m_Context;
};

ModContext::~ModContext() {
    Shutdown();
    if (m_Logfile) {
        std::fclose(m_Logfile);
        m_Logfile = nullptr;
    }
    Logger::SetDefault(nullptr);
    delete m_Logger;
    m_Logger = nullptr;
    if (g_ModContext == this)
        g_ModContext = nullptr;
}

bool ModContext::Init() {
    if (m_Inited)
        return true;
    utils::CreateFileTreeUtf8("/ModLoader/Configs");
    m_Logfile = std::fopen("/ModLoader/ModLoader.log", "w");
    m_Logger = new Logger("BML");
    Logger::SetDefault(m_Logger);
    BML::DataShareStore::SetOwnerResolver(&ResolveStaticModOwner);
    BML::DataShareStore::SetInvocationGate(&m_ModInvocationGate);
    OnigEncoding encodings[] = {ONIG_ENCODING_ASCII, ONIG_ENCODING_UTF8, ONIG_ENCODING_UTF16_LE};
    if (onig_initialize(encodings, 3) != ONIG_NORMAL) {
        m_Logger->Error("Failed to initialize regular expression functionality");
        return false;
    }

    m_AttributeManager = m_CKContext->GetAttributeManager();
    m_BehaviorManager = m_CKContext->GetBehaviorManager();
    m_CollisionManager = static_cast<CKCollisionManager *>(m_CKContext->GetManagerByGuid(COLLISION_MANAGER_GUID));
    CKInputManager *input = static_cast<CKInputManager *>(m_CKContext->GetManagerByGuid(INPUT_MANAGER_GUID));
    m_MessageManager = m_CKContext->GetMessageManager();
    m_PathManager = m_CKContext->GetPathManager();
    m_ParameterManager = m_CKContext->GetParameterManager();
    m_RenderManager = m_CKContext->GetRenderManager();
    m_SoundManager = static_cast<CKSoundManager *>(m_CKContext->GetManagerByGuid(SOUND_MANAGER_GUID));
    m_TimeManager = m_CKContext->GetTimeManager();
    if (!m_AttributeManager || !m_BehaviorManager || !m_CollisionManager || !input ||
        !m_MessageManager || !m_PathManager || !m_ParameterManager || !m_RenderManager ||
        !m_SoundManager || !m_TimeManager) {
        m_Logger->Error("One or more Virtools managers are unavailable");
        return false;
    }
    m_Input = std::make_unique<InputHook>(input);
    m_UiFonts = std::make_unique<BML::UI::FontRuntime>("/ModLoader/Fonts");
    WasmPersistentStore::Synchronize();
    std::string shellError;
    if (!m_ShellEnvironment.Load(L"/ModLoader/CommandBar.shell.json", shellError))
        m_Logger->Warn("Failed to load the shell state file: %s", shellError.c_str());
    m_Inited = true;
    if (!LoadMods()) {
        m_Inited = false;
        return false;
    }
    FlushConfigChanges();
    WasmPersistentStore::Synchronize();
    return m_Inited;
}

bool ModContext::LoadMods() {
    const auto construct = [this](ModFactory create, ModDestructor destroy, bool official) {
        try {
            IMod *mod = create(this);
            if (!mod || m_BehaviorSessions.RegisterOwner(mod->GetID()) == 0)
                return false;
            BML::DataShareStore::ActivateCallbacksFromOwner(mod->GetID());
            ++m_ModRegistryRevision;
            m_ModRecords.push_back({mod, destroy, m_ModRegistryRevision, official});
            if (official)
                ++m_OfficialModCount;
            if (std::strcmp(mod->GetID(), "BML") == 0)
                m_BuiltinMod = static_cast<WasmBuiltinMod *>(mod);
            return true;
        } catch (const std::exception &error) {
            m_Logger->Error("Static mod construction failed: %s", error.what());
            return false;
        }
    };
    if (!construct(CreateBuiltinMod, DestroyBuiltinMod, false))
        return false;
    for (const WasmModDefinition &definition : GetOfficialWasmMods()) {
        if (!construct(definition.Create, definition.Destroy, true))
            return false;
    }

    for (const ModRecord &record : m_ModRecords) {
        IMod *mod = record.Mod;
        if (!CheckDependencies(mod)) {
            m_Logger->Error("Skipping %s because its dependencies are unavailable", mod->GetID());
            continue;
        }
        const BMLVersion required = mod->GetBMLVersion();
        const BMLVersion runtime(BML_MAJOR_VERSION, BML_MINOR_VERSION, BML_PATCH_VERSION);
        if (!(runtime >= required)) {
            m_Logger->Error("Skipping %s because it requires BML %s", mod->GetID(), required.ToString().c_str());
            continue;
        }
        m_Mods.push_back(mod);
    }

    const auto executeSession = m_ExecuteBB.Open("BML");
    if (!executeSession) {
        m_Logger->Error("Cannot open the built-in Behavior session: %s", executeSession.Message.c_str());
        return false;
    }
    m_NewBallTypes = std::make_unique<NewBallTypeMod>(this);
    if (m_BehaviorSessions.RegisterOwner("NewBallType") == 0)
        return false;
    InvokeMod(m_NewBallTypes.get(), [&] { m_NewBallTypes->OnLoad(); });

    for (IMod *mod : m_Mods) {
        InvokeMod(mod, [&] {
            mod->OnLoad();
            m_Logger->Info("BML+ WASM mod loaded: %s %s", mod->GetID(), mod->GetVersion());
        });
    }
#ifdef BALLANCE_WASM_BROWSER_TESTING
    for (auto &command : WasmBrowserTesting::CreateCommands(*this)) {
        BML::CommandContext::CommandInfo testCommandInfo;
        testCommandInfo.Name = command->GetName();
        testCommandInfo.Description = command->GetDescription();
        testCommandInfo.Hidden = true;
        if (!m_CommandContext.RegisterCommand(m_BuiltinMod, command.get(),
                                              std::move(testCommandInfo), command)) {
            m_Logger->Error("Cannot register the browser verification command: %s", command->GetName().c_str());
            return false;
        }
        if (!m_BrowserTestCommand)
            m_BrowserTestCommand = command;
        else
            m_BrowserTestCommands.push_back(command);
    }
#endif
    return true;
}

bool ModContext::Shutdown() {
    if (!m_Inited && m_ModRecords.empty())
        return true;
    for (auto mod = m_Mods.rbegin(); mod != m_Mods.rend(); ++mod) {
        InvokeMod(*mod, [&] { (*mod)->OnUnload(); });
        m_BehaviorSessions.RetireOwner((*mod)->GetID());
    }
    if (m_NewBallTypes) {
        InvokeMod(m_NewBallTypes.get(), [&] { m_NewBallTypes->OnUnload(); });
        m_BehaviorSessions.RetireOwner("NewBallType");
        m_NewBallTypes.reset();
    }
    FlushConfigChanges(true);
    WasmPersistentStore::Synchronize();
    BML::Timer::CancelAll();
    OnReset();
    for (auto record = m_ModRecords.rbegin(); record != m_ModRecords.rend(); ++record) {
        if (record->Destroy)
            record->Destroy(record->Mod);
    }
    m_ModRecords.clear();
    m_Mods.clear();
    m_Dependencies.clear();
    m_CommandContext.ClearCommands();
    m_BuiltinMod = nullptr;
    m_OfficialModCount = 0;
    m_Input.reset();
    m_UiFonts.reset();
    m_GameFonts.Reset();
    m_ImcRuntime.Shutdown();
    if (m_DataShare) {
        m_DataShare->Release();
        m_DataShare = nullptr;
    }
    BML::DataShareStore::SetOwnerResolver(nullptr);
    BML::DataShareStore::SetInvocationGate(nullptr);
    m_ImcRuntime.SetInvocationGate(nullptr);
    m_Inited = false;
    return true;
}

void ModContext::OnLoadGame() {
    if (m_BaseLoaded || !m_Inited)
        return;
    m_BaseLoaded = true;
    BroadcastCallback(&IMod::OnLoadObject, "base.cmo", FALSE, "", CKCID_3DOBJECT,
                      TRUE, TRUE, TRUE, FALSE, nullptr, nullptr);
    const int count = m_CKContext->GetObjectsCountByClassID(CKCID_BEHAVIOR);
    CK_ID *ids = m_CKContext->GetObjectsListByClassID(CKCID_BEHAVIOR);
    std::vector<CK_ID> scripts;
    if (ids && count > 0)
        scripts.assign(ids, ids + count);
    for (CK_ID id : scripts) {
        auto *script = static_cast<CKBehavior *>(m_CKContext->GetObject(id));
        if (script && script->GetType() == CKBEHAVIORTYPE_SCRIPT) {
            BehaviorScriptLoaded(script);
            BroadcastCallback(&IMod::OnLoadScript, "base.cmo", script);
        }
    }
}

void ModContext::InstallGameEventHooks(CKBehavior *script) {
    BehaviorScriptLoaded(script);
}

void ModContext::OnProcess() {
    if (!m_Inited || !m_TimeManager)
        return;
    BML::Timer::ProcessAll(m_TimeManager->GetMainTickCount(), m_TimeManager->GetAbsoluteTime() / 1000.0f);
    BroadcastCallback(&IMod::OnProcess);
    ProcessVirtoolsFrame();
    FlushConfigChanges();
    if (m_TimeManager->GetMainTickCount() % 60 == 0)
        WasmPersistentStore::Synchronize();
    if (m_Input)
        m_Input->Process();
}

void ModContext::OnRender(CKRenderContext *dev) {
    if (m_Inited && dev)
        BroadcastCallback(&IMod::OnRender, static_cast<CK_RENDER_FLAGS>(dev->GetCurrentRenderOptions()));
}

void ModContext::OnReset() {
    if (m_BuiltinMod)
        (void) m_BuiltinMod->CloseModsMenuForShutdown();
    BML::Behavior::Internal::ReleaseScope releases;
    m_GamePhase = GamePhase::FrontEnd;
    m_BaseLoaded = false;
    m_ExecuteBB.Reset();
    m_PhysicsForce.Reset();
    m_BehaviorInstallations.LeaveWorld();
    m_BehaviorScripts.ResetWorld();
    m_BehaviorInstallations.ResetWorld();
    m_BehaviorSessions.ResetWorld();
    m_ObjectRefs.Reset();
}

void ModContext::RemoveDeletedObjects(const CK_ID *ids, int count) {
    BML::Behavior::Internal::ReleaseScope releases;
    m_PhysicsForce.ObjectsToBeDeleted(ids, count);
    for (int index = 0; ids && index < count; ++index)
        m_BehaviorScripts.ObjectToBeDeleted(static_cast<std::uint32_t>(ids[index]));
    m_BehaviorInstallations.ObjectsToBeDeleted(ids, count);
    for (int index = 0; ids && index < count; ++index) {
        m_BehaviorInstallations.RemoveObject(
            BML_OBJECT_DOMAIN_VIRTOOLS, static_cast<std::uint32_t>(ids[index]));
    }
    m_BehaviorSessions.ObjectsToBeDeleted(ids, count);
    m_Behaviors.ObjectsToBeDeleted(ids, count);
    m_ObjectRefs.Invalidate(ids, count);
}

Config *ModContext::AddConfig(std::unique_ptr<Config> config) {
    if (!config || !config->GetMod())
        return nullptr;
    Config *result = m_Configs.Add(config->GetModID(), config->GetMod(), std::move(config));
    if (!result)
        return nullptr;
    result->SnapshotModMetadata();
    const std::wstring path = ConfigPath(result->GetMod());
    (void) WasmPersistentStore::Restore(utils::Utf16ToUtf8(path));
    if (!result->Load(path.c_str()) && result->GetLoadStatus() != Config::LoadStatus::Missing) {
        m_Logger->Warn("Cannot load config for %s (error %u)", result->GetModID().c_str(), result->GetLoadError());
    }
    return result;
}

Config *ModContext::GetConfig(IMod *mod) {
    return mod ? m_Configs.Find(mod->GetID(), mod) : nullptr;
}

void ModContext::FlushConfigChanges(bool saveAll) {
    for (Config *config : m_Configs.Snapshot()) {
        for (const Config::PendingNotification &change : config->TakePendingNotifications()) {
            m_Logger->Info("Config changed: %s/%s", change.Category.c_str(), change.Key.c_str());
            IMod *mod = config->GetMod();
            if (!mod || std::find(m_Mods.begin(), m_Mods.end(), mod) == m_Mods.end())
                continue;
            try {
                mod->OnModifyConfig(change.Category.c_str(), change.Key.c_str(), change.ChangedProperty);
            } catch (...) {
                m_Logger->Error("%s OnModifyConfig failed", mod->GetID());
            }
        }
        if (!config->TakeSaveRequest(saveAll))
            continue;
        const std::wstring path = ConfigPath(config->GetMod());
        const std::string utf8Path = utils::Utf16ToUtf8(path);
        m_Logger->Info("Saving config: %s", utf8Path.c_str());
        if (!config->Save(path.c_str())) {
            m_Logger->Warn("Cannot save config for %s (error %u)", config->GetModID().c_str(), config->GetSaveError());
        } else {
            m_Logger->Info("Config saved: %s", utf8Path.c_str());
            if (!WasmPersistentStore::Persist(utf8Path))
                m_Logger->Warn("Cannot persist config for %s in browser storage", config->GetModID().c_str());
        }
    }
}

const wchar_t *ModContext::GetDirectory(BML_LoaderDirectory directory) const {
    switch (directory) {
    case BML_DIR_WORKING:
    case BML_DIR_GAME:
        return L"/";
    case BML_DIR_TEMP:
        return L"/tmp";
    case BML_DIR_LOADER:
        return L"/ModLoader";
    case BML_DIR_CONFIG:
        return L"/ModLoader/Configs";
    default:
        return nullptr;
    }
}

const char *ModContext::GetDirectoryUtf8(BML_LoaderDirectory directory) const {
    switch (directory) {
    case BML_DIR_WORKING:
    case BML_DIR_GAME:
        return "/";
    case BML_DIR_TEMP:
        return "/tmp";
    case BML_DIR_LOADER:
        return "/ModLoader";
    case BML_DIR_CONFIG:
        return "/ModLoader/Configs";
    default:
        return nullptr;
    }
}

#define BML_WASM_OBJECT_GETTER(Type, Name, Class) \
    Type *ModContext::Name(const char *name) { return GetObject<Type>(this, name, Class); }
BML_WASM_OBJECT_GETTER(CKDataArray, GetArrayByName, CKCID_DATAARRAY)
BML_WASM_OBJECT_GETTER(CKGroup, GetGroupByName, CKCID_GROUP)
BML_WASM_OBJECT_GETTER(CKMaterial, GetMaterialByName, CKCID_MATERIAL)
BML_WASM_OBJECT_GETTER(CKMesh, GetMeshByName, CKCID_MESH)
BML_WASM_OBJECT_GETTER(CK2dEntity, Get2dEntityByName, CKCID_2DENTITY)
BML_WASM_OBJECT_GETTER(CK3dEntity, Get3dEntityByName, CKCID_3DENTITY)
BML_WASM_OBJECT_GETTER(CK3dObject, Get3dObjectByName, CKCID_3DOBJECT)
BML_WASM_OBJECT_GETTER(CKCamera, GetCameraByName, CKCID_CAMERA)
BML_WASM_OBJECT_GETTER(CKTargetCamera, GetTargetCameraByName, CKCID_TARGETCAMERA)
BML_WASM_OBJECT_GETTER(CKLight, GetLightByName, CKCID_LIGHT)
BML_WASM_OBJECT_GETTER(CKTargetLight, GetTargetLightByName, CKCID_TARGETLIGHT)
BML_WASM_OBJECT_GETTER(CKSound, GetSoundByName, CKCID_SOUND)
BML_WASM_OBJECT_GETTER(CKTexture, GetTextureByName, CKCID_TEXTURE)
BML_WASM_OBJECT_GETTER(CKBehavior, GetScriptByName, CKCID_BEHAVIOR)
#undef BML_WASM_OBJECT_GETTER

void ModContext::SetIC(CKBeObject *obj, bool hierarchy) {
    if (!obj)
        return;
    m_CKContext->GetCurrentScene()->SetObjectInitialValue(obj, CKSaveObjectState(obj));
    if (!hierarchy)
        return;
    if (CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        auto *entity = static_cast<CK2dEntity *>(obj);
        for (int index = 0; index < entity->GetChildrenCount(); ++index)
            SetIC(entity->GetChild(index), true);
    }
    if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
        auto *entity = static_cast<CK3dEntity *>(obj);
        for (int index = 0; index < entity->GetChildrenCount(); ++index)
            SetIC(entity->GetChild(index), true);
    }
}

void ModContext::RestoreIC(CKBeObject *obj, bool hierarchy) {
    if (!obj)
        return;
    CKStateChunk *chunk = m_CKContext->GetCurrentScene()->GetObjectInitialValue(obj);
    if (chunk)
        CKReadObjectState(obj, chunk);
    if (!hierarchy)
        return;
    if (CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        auto *entity = static_cast<CK2dEntity *>(obj);
        for (int index = 0; index < entity->GetChildrenCount(); ++index)
            RestoreIC(entity->GetChild(index), true);
    }
    if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
        auto *entity = static_cast<CK3dEntity *>(obj);
        for (int index = 0; index < entity->GetChildrenCount(); ++index)
            RestoreIC(entity->GetChild(index), true);
    }
}

void ModContext::Show(CKBeObject *obj, CK_OBJECT_SHOWOPTION show, bool hierarchy) {
    if (!obj)
        return;
    obj->Show(show);
    if (!hierarchy)
        return;
    if (CKIsChildClassOf(obj, CKCID_2DENTITY)) {
        auto *entity = static_cast<CK2dEntity *>(obj);
        for (int index = 0; index < entity->GetChildrenCount(); ++index)
            Show(entity->GetChild(index), show, true);
    }
    if (CKIsChildClassOf(obj, CKCID_3DENTITY)) {
        auto *entity = static_cast<CK3dEntity *>(obj);
        for (int index = 0; index < entity->GetChildrenCount(); ++index)
            Show(entity->GetChild(index), show, true);
    }
}

void ModContext::AddTimer(CKDWORD delay, std::function<void()> callback) {
    if (m_Inited)
        BML::Delay(static_cast<size_t>(delay), std::move(callback), m_TimeManager->GetMainTickCount());
}
void ModContext::AddTimerLoop(CKDWORD delay, std::function<bool()> callback) {
    if (m_Inited)
        BML::Interval(static_cast<size_t>(delay), std::move(callback), m_TimeManager->GetMainTickCount());
}
void ModContext::AddTimer(float delay, std::function<void()> callback) {
    if (m_Inited)
        BML::Delay(delay / 1000.0f, std::move(callback), m_TimeManager->GetAbsoluteTime() / 1000.0f);
}
void ModContext::AddTimerLoop(float delay, std::function<bool()> callback) {
    if (m_Inited)
        BML::Interval(delay / 1000.0f, std::move(callback), m_TimeManager->GetAbsoluteTime() / 1000.0f);
}

void ModContext::ExitGame() {
    OnExitGame();
}

void ModContext::EnableCheat(bool enable) {
    if (!m_CommandContext.SetCheatEnabled(enable))
        return;
    BroadcastCallback(&IMod::OnCheatEnabled, enable);
}

void ModContext::SendIngameMessage(const char *msg) {
    const char *text = msg ? msg : "";
    if (!m_OutputSinks.empty()) {
        m_OutputSinks.back()->Write(text);
        return;
    }
    if (m_BuiltinMod) {
        m_BuiltinMod->AddIngameMessage(text);
        return;
    }
    m_Logger->Info("Mod message: %s", text);
}

void ModContext::SkipRenderForNextTick() {
    if (RenderHook::IsSkipRenderAvailable())
        RenderHook::SkipNextRender();
}

IMod *ModContext::GetMod(int index) {
    return index >= 0 && index < GetModCount() ? m_Mods[index] : nullptr;
}
IMod *ModContext::FindMod(const char *id) const {
    if (!id)
        return nullptr;
    if (m_NewBallTypes && std::strcmp(m_NewBallTypes->GetID(), id) == 0)
        return m_NewBallTypes.get();
    for (const ModRecord &record : m_ModRecords) {
        if (record.Mod && std::strcmp(record.Mod->GetID(), id) == 0)
            return record.Mod;
    }
    return nullptr;
}

std::uint64_t ModContext::GetModGeneration(const IMod *mod) const {
    if (!mod)
        return 0;
    for (const ModRecord &record : m_ModRecords) {
        if (record.Mod == mod)
            return record.Generation;
    }
    return 0;
}

void ModContext::OpenModsMenu() {
    if (m_BuiltinMod)
        m_BuiltinMod->OpenModsMenu();
}

void ModContext::CloseModsMenu() {
    if (m_BuiltinMod)
        m_BuiltinMod->CloseModsMenu();
}

int ModContext::GetHUD() {
    return m_BuiltinMod ? m_BuiltinMod->GetHUD() : 0;
}

void ModContext::SetHUD(int mode) {
    if (m_BuiltinMod)
        m_BuiltinMod->SetHUD(mode);
}

void ModContext::ShowTitle(bool show) {
    if (m_BuiltinMod)
        m_BuiltinMod->ShowTitle(show);
}

void ModContext::ShowFPS(bool show) {
    if (m_BuiltinMod)
        m_BuiltinMod->ShowFPS(show);
}

void ModContext::ShowSRTimer(bool show) {
    if (m_BuiltinMod)
        m_BuiltinMod->ShowSRTimer(show);
}

void ModContext::StartSRTimer() {
    if (m_BuiltinMod)
        m_BuiltinMod->StartSRTimer();
}

void ModContext::PauseSRTimer() {
    if (m_BuiltinMod)
        m_BuiltinMod->PauseSRTimer();
}

void ModContext::ResetSRTimer() {
    if (m_BuiltinMod)
        m_BuiltinMod->ResetSRTimer();
}

float ModContext::GetSRTime() {
    return m_BuiltinMod ? m_BuiltinMod->GetSRTime() : 0.0f;
}

float ModContext::GetSRScore() {
    return GetSRTime();
}

int ModContext::GetHSScore() {
    CKDataArray *energy = GetArrayByName("Energy");
    if (!energy)
        return 0;
    int points = 0;
    int lives = 0;
    energy->GetElementValue(0, 0, &points);
    energy->GetElementValue(0, 1, &lives);
    return points + lives * 200;
}

int ModContext::RegisterDependency(IMod *mod, const char *id, int major, int minor, int patch) {
    if (!mod || !id || !*id)
        return BML_ERROR_INVALID_PARAMETER;
    try {
        m_Dependencies[mod].emplace_back(id, BMLVersion(major, minor, patch), 0);
        return BML_OK;
    } catch (...) {
        return BML_ERROR_OUT_OF_MEMORY;
    }
}
int ModContext::RegisterOptionalDependency(IMod *mod, const char *id, int major, int minor, int patch) {
    if (!mod || !id || !*id)
        return BML_ERROR_INVALID_PARAMETER;
    try {
        m_Dependencies[mod].emplace_back(id, BMLVersion(major, minor, patch), 1);
        return BML_OK;
    } catch (...) {
        return BML_ERROR_OUT_OF_MEMORY;
    }
}
int ModContext::CheckDependencies(IMod *mod) const {
    if (!mod)
        return 0;
    const auto found = m_Dependencies.find(mod);
    if (found == m_Dependencies.end())
        return 1;
    for (const ModDependency &dependency : found->second) {
        if (dependency.optional)
            continue;
        IMod *required = FindMod(dependency.id);
        if (!required || !(ParseVersion(required->GetVersion()) >= dependency.minVersion))
            return 0;
    }
    return 1;
}
int ModContext::GetDependencyCount(IMod *mod) const {
    if (!mod)
        return -1;
    const auto found = m_Dependencies.find(mod);
    return found == m_Dependencies.end() ? 0 : static_cast<int>(found->second.size());
}
int ModContext::GetDependencyInfo(IMod *mod, int index, char *id, int idSize,
                                  int *major, int *minor, int *patch, int *optional) const {
    if (!mod || index < 0)
        return BML_ERROR_INVALID_PARAMETER;
    const auto found = m_Dependencies.find(mod);
    if (found == m_Dependencies.end() || index >= static_cast<int>(found->second.size()))
        return BML_ERROR_NOT_FOUND;
    const ModDependency &dependency = found->second[index];
    if (id && idSize > 0) {
        std::strncpy(id, dependency.id ? dependency.id : "", static_cast<size_t>(idSize - 1));
        id[idSize - 1] = '\0';
    }
    if (major) *major = dependency.minVersion.major;
    if (minor) *minor = dependency.minVersion.minor;
    if (patch) *patch = dependency.minVersion.patch;
    if (optional) *optional = dependency.optional;
    return BML_OK;
}
int ModContext::ClearDependencies(IMod *mod) {
    if (!mod)
        return BML_ERROR_INVALID_PARAMETER;
    m_Dependencies.erase(mod);
    return BML_OK;
}

void ModContext::BroadcastMessage(const char *name, void (IMod::*callback)()) {
    m_Logger->Info("Game event: %s", name);
    BroadcastCallback(callback);
}
void ModContext::OnPreStartMenu() { BroadcastMessage("PreStartMenu", &IMod::OnPreStartMenu); }
void ModContext::OnPostStartMenu() { BroadcastMessage("PostStartMenu", &IMod::OnPostStartMenu); }
void ModContext::OnExitGame() {
    BroadcastMessage("ExitGame", &IMod::OnExitGame);
    // A normal, user-initiated exit (main menu EXIT -> YES, or the console
    // "exit" command): flush pending config changes and persisted data before
    // asking the host page to return to the home screen instead of
    // restarting the game.
    FlushConfigChanges(true);
    WasmPersistentStore::Synchronize();
#ifdef __EMSCRIPTEN__
    EM_ASM({ globalThis.dispatchEvent?.(new CustomEvent("ballance:exit-requested")); });
#endif
}
void ModContext::OnPreLoadLevel() { BroadcastMessage("PreLoadLevel", &IMod::OnPreLoadLevel); }
void ModContext::OnPostLoadLevel() { BroadcastMessage("PostLoadLevel", &IMod::OnPostLoadLevel); }
void ModContext::OnStartLevel() { m_GamePhase = GamePhase::ActiveLevel; BroadcastMessage("StartLevel", &IMod::OnStartLevel); }
void ModContext::OnPreResetLevel() { BroadcastMessage("PreResetLevel", &IMod::OnPreResetLevel); m_GamePhase = GamePhase::Transitioning; }
void ModContext::OnPostResetLevel() { BroadcastMessage("PostResetLevel", &IMod::OnPostResetLevel); }
void ModContext::OnPauseLevel() { if (m_GamePhase == GamePhase::ActiveLevel) m_GamePhase = GamePhase::PausedLevel; BroadcastMessage("PauseLevel", &IMod::OnPauseLevel); }
void ModContext::OnUnpauseLevel() { if (m_GamePhase == GamePhase::PausedLevel) m_GamePhase = GamePhase::ActiveLevel; BroadcastMessage("UnpauseLevel", &IMod::OnUnpauseLevel); }
void ModContext::OnPreExitLevel() { BroadcastMessage("PreExitLevel", &IMod::OnPreExitLevel); }
void ModContext::OnPostExitLevel() { m_GamePhase = GamePhase::FrontEnd; BroadcastMessage("PostExitLevel", &IMod::OnPostExitLevel); }
void ModContext::OnPreNextLevel() { BroadcastMessage("PreNextLevel", &IMod::OnPreNextLevel); }
void ModContext::OnPostNextLevel() { m_GamePhase = GamePhase::Transitioning; BroadcastMessage("PostNextLevel", &IMod::OnPostNextLevel); }
void ModContext::OnDead() { m_GamePhase = GamePhase::FrontEnd; BroadcastMessage("Dead", &IMod::OnDead); }
void ModContext::OnPreEndLevel() { BroadcastMessage("PreEndLevel", &IMod::OnPreEndLevel); }
void ModContext::OnPostEndLevel() { m_GamePhase = GamePhase::FrontEnd; BroadcastMessage("PostEndLevel", &IMod::OnPostEndLevel); }
void ModContext::OnCounterActive() { BroadcastMessage("CounterActive", &IMod::OnCounterActive); }
void ModContext::OnCounterInactive() { BroadcastMessage("CounterInactive", &IMod::OnCounterInactive); }
void ModContext::OnBallNavActive() { BroadcastMessage("BallNavActive", &IMod::OnBallNavActive); }
void ModContext::OnBallNavInactive() { BroadcastMessage("BallNavInactive", &IMod::OnBallNavInactive); }
void ModContext::OnCamNavActive() { BroadcastMessage("CamNavActive", &IMod::OnCamNavActive); }
void ModContext::OnCamNavInactive() { BroadcastMessage("CamNavInactive", &IMod::OnCamNavInactive); }
void ModContext::OnBallOff() { BroadcastMessage("BallOff", &IMod::OnBallOff); }
void ModContext::OnPreCheckpointReached() { BroadcastMessage("PreCheckpoint", &IMod::OnPreCheckpointReached); }
void ModContext::OnPostCheckpointReached() { BroadcastMessage("PostCheckpoint", &IMod::OnPostCheckpointReached); }
void ModContext::OnLevelFinish() { BroadcastMessage("LevelFinish", &IMod::OnLevelFinish); m_GamePhase = GamePhase::Transitioning; }
void ModContext::OnGameOver() { BroadcastMessage("GameOver", &IMod::OnGameOver); }
void ModContext::OnExtraPoint() { BroadcastMessage("ExtraPoint", &IMod::OnExtraPoint); }
void ModContext::OnPreSubLife() { BroadcastMessage("PreSubLife", &IMod::OnPreSubLife); }
void ModContext::OnPostSubLife() { BroadcastMessage("PostSubLife", &IMod::OnPostSubLife); }
void ModContext::OnPreLifeUp() { BroadcastMessage("PreLifeUp", &IMod::OnPreLifeUp); }
void ModContext::OnPostLifeUp() { BroadcastMessage("PostLifeUp", &IMod::OnPostLifeUp); }

extern "C" void BMLWasmOnObjectLoad(
    CKContext *context, const char *filename, CKBOOL isMap, const char *masterName,
    CK_CLASSID filterClass, CKBOOL addToScene, CKBOOL reuseMeshes,
    CKBOOL reuseMaterials, CKBOOL dynamic, XObjectArray *objects, CKObject *masterObject) {
    ModContext *runtime = BML_GetModContext();
    if (!runtime || runtime->GetCKContext() != context || !runtime->IsInited())
        return;
    std::string callbackName = filename ? filename : "";
    (void) CustomMapLoad::CompleteRequest(*runtime, isMap != FALSE, objects != nullptr,
                                          callbackName, callbackName);
    if (!objects)
        return;
    runtime->BroadcastCallback(&IMod::OnLoadObject, callbackName.c_str(), isMap,
                               masterName ? masterName : "", filterClass, addToScene,
                               reuseMeshes, reuseMaterials, dynamic, objects, masterObject);
    for (CK_ID *id = objects->Begin(); id != objects->End(); ++id) {
        auto *script = static_cast<CKBehavior *>(context->GetObject(*id));
        if (script && script->GetClassID() == CKCID_BEHAVIOR &&
            (script->GetType() & CKBEHAVIORTYPE_SCRIPT) != 0) {
            runtime->InstallGameEventHooks(script);
            runtime->BroadcastCallback(&IMod::OnLoadScript, callbackName.c_str(), script);
        }
    }
}

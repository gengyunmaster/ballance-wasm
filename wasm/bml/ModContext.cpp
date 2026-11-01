#include "Loader/ModContext.h"

#include "BML/Timer.h"
#include "BMLWasmRuntime.h"
#include "Config/Config.h"
#include "Logging/Logger.h"
#include "PathUtils.h"
#include "StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern "C" {
IMod *BMLWasmCreateMapScripts(IBML *bml);
void BMLWasmDestroyMapScripts(IMod *mod);
IMod *BMLWasmCreateDeformedWB(IBML *bml);
void BMLWasmDestroyDeformedWB(IMod *mod);
IMod *BMLWasmCreateCameraUtilities(IBML *bml);
void BMLWasmDestroyCameraUtilities(IMod *mod);
}

void BMLWasmInstallGameEventHooks(CKBehavior *script);

namespace {
ModContext *g_ModContext = nullptr;

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

ModContext::ModContext(CKContext *context) : m_CKContext(context) {
    g_ModContext = this;
}

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
    m_Inited = LoadMods();
    FlushConfigChanges();
    return m_Inited;
}

bool ModContext::LoadMods() {
    static const struct {
        ModFactory Create;
        ModDestructor Destroy;
    } factories[] = {
        {BMLWasmCreateMapScripts, BMLWasmDestroyMapScripts},
        {BMLWasmCreateDeformedWB, BMLWasmDestroyDeformedWB},
        {BMLWasmCreateCameraUtilities, BMLWasmDestroyCameraUtilities},
    };

    for (const auto &factory : factories) {
        try {
            IMod *mod = factory.Create(this);
            if (!mod)
                return false;
            m_ModRecords.push_back({mod, factory.Destroy});
        } catch (const std::exception &error) {
            m_Logger->Error("Static mod construction failed: %s", error.what());
            return false;
        }
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

    for (IMod *mod : m_Mods) {
        try {
            mod->OnLoad();
            m_Logger->Info("BML+ WASM mod loaded: %s %s", mod->GetID(), mod->GetVersion());
        } catch (const std::exception &error) {
            m_Logger->Error("%s OnLoad failed: %s", mod->GetID(), error.what());
            return false;
        } catch (...) {
            m_Logger->Error("%s OnLoad failed with an unknown exception", mod->GetID());
            return false;
        }
    }
    return true;
}

bool ModContext::Shutdown() {
    if (!m_Inited && m_ModRecords.empty())
        return true;
    for (auto mod = m_Mods.rbegin(); mod != m_Mods.rend(); ++mod) {
        try {
            (*mod)->OnUnload();
        } catch (...) {
            if (m_Logger)
                m_Logger->Error("%s OnUnload failed", (*mod)->GetID());
        }
    }
    FlushConfigChanges(true);
    BML::Timer::CancelAll();
    for (auto record = m_ModRecords.rbegin(); record != m_ModRecords.rend(); ++record) {
        if (record->Destroy)
            record->Destroy(record->Mod);
    }
    m_ModRecords.clear();
    m_Mods.clear();
    m_Commands.clear();
    m_Dependencies.clear();
    m_EventHookScripts.clear();
    BMLWasmResetGameEventHooks();
    m_Input.reset();
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
    for (int index = 0; index < count; ++index) {
        auto *script = static_cast<CKBehavior *>(m_CKContext->GetObject(ids[index]));
        if (script && script->GetType() == CKBEHAVIORTYPE_SCRIPT) {
            InstallGameEventHooks(script);
            BroadcastCallback(&IMod::OnLoadScript, "base.cmo", script);
        }
    }
}

void ModContext::InstallGameEventHooks(CKBehavior *script) {
    if (!script || m_EventHookScripts.count(script->GetID()))
        return;
    m_EventHookScripts.insert(script->GetID());
    BMLWasmInstallGameEventHooks(script);
}

void ModContext::OnProcess() {
    if (!m_Inited || !m_TimeManager)
        return;
    BML::Timer::ProcessAll(m_TimeManager->GetMainTickCount(), m_TimeManager->GetAbsoluteTime() / 1000.0f);
    BroadcastCallback(&IMod::OnProcess);
    FlushConfigChanges();
    if (m_Input)
        m_Input->Process();
}

void ModContext::OnRender(CKRenderContext *dev) {
    if (m_Inited && dev)
        BroadcastCallback(&IMod::OnRender, static_cast<CK_RENDER_FLAGS>(dev->GetCurrentRenderOptions()));
}

void ModContext::OnReset() {
    m_GamePhase = GamePhase::FrontEnd;
    m_BaseLoaded = false;
    m_EventHookScripts.clear();
    BMLWasmResetGameEventHooks();
}

void ModContext::RemoveDeletedObjects(const CK_ID *ids, int count) {
    BMLWasmRemoveHookBlocks(ids, count);
    for (int index = 0; ids && index < count; ++index)
        m_EventHookScripts.erase(ids[index]);
}

Config *ModContext::AddConfig(std::unique_ptr<Config> config) {
    if (!config || !config->GetMod())
        return nullptr;
    Config *result = m_Configs.Add(config->GetModID(), config->GetMod(), std::move(config));
    if (!result)
        return nullptr;
    result->SnapshotModMetadata();
    if (!result->Load(ConfigPath(result->GetMod()).c_str()) &&
        result->GetLoadStatus() != Config::LoadStatus::Missing) {
        m_Logger->Warn("Cannot load config for %s (error %u)", result->GetModID().c_str(), result->GetLoadError());
    }
    return result;
}

void ModContext::FlushConfigChanges(bool saveAll) {
    for (Config *config : m_Configs.Snapshot()) {
        for (const Config::PendingNotification &change : config->TakePendingNotifications()) {
            IMod *mod = config->GetMod();
            if (!mod || std::find(m_Mods.begin(), m_Mods.end(), mod) == m_Mods.end())
                continue;
            try {
                mod->OnModifyConfig(change.Category.c_str(), change.Key.c_str(), change.ChangedProperty);
            } catch (...) {
                m_Logger->Error("%s OnModifyConfig failed", mod->GetID());
            }
        }
        if (config->TakeSaveRequest(saveAll) && !config->Save(ConfigPath(config->GetMod()).c_str()))
            m_Logger->Warn("Cannot save config for %s (error %u)", config->GetModID().c_str(), config->GetSaveError());
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
#ifdef __EMSCRIPTEN__
    EM_ASM({ globalThis.dispatchEvent?.(new CustomEvent("ballance:exit-requested")); });
#endif
}

void ModContext::EnableCheat(bool enable) {
    if (m_CheatEnabled == enable)
        return;
    m_CheatEnabled = enable;
    BroadcastCallback(&IMod::OnCheatEnabled, enable);
}

void ModContext::SendIngameMessage(const char *msg) {
    const char *text = msg ? msg : "";
    m_Logger->Info("Mod message: %s", text);
#ifdef __EMSCRIPTEN__
    EM_ASM({
        globalThis.dispatchEvent?.(new CustomEvent("ballance:mod-message", {
            detail: UTF8ToString($0),
        }));
    }, text);
#endif
}

void ModContext::SkipRenderForNextTick() {
    CKRenderContext *render = GetRenderContext();
    if (!render)
        return;
    render->ChangeCurrentRenderOptions(0, CK_RENDER_DEFAULTSETTINGS);
    AddTimer(static_cast<CKDWORD>(1), [render]() { render->ChangeCurrentRenderOptions(CK_RENDER_DEFAULTSETTINGS, 0); });
}

void ModContext::RegisterCommand(ICommand *cmd) {
    if (!cmd)
        return;
    CommandRecord record{cmd, Lower(cmd->GetName()), Lower(cmd->GetAlias()), cmd->IsCheat()};
    if (record.Name.empty() || FindCommand(record.Name.c_str())) {
        m_Logger->Warn("Cannot register duplicate or empty command %s", record.Name.c_str());
        return;
    }
    m_Commands.push_back(std::move(record));
}

int ModContext::UnregisterCommand(const char *name) {
    if (!name || !*name)
        return BML_ERROR_INVALID_PARAMETER;
    const std::string key = Lower(name);
    const auto found = std::find_if(m_Commands.begin(), m_Commands.end(), [&](const CommandRecord &record) {
        return record.Name == key || record.Alias == key;
    });
    if (found == m_Commands.end())
        return BML_ERROR_NOT_FOUND;
    m_Commands.erase(found);
    return BML_OK;
}

int ModContext::GetCommandCount() const { return static_cast<int>(m_Commands.size()); }
ICommand *ModContext::GetCommand(int index) const {
    return index >= 0 && index < GetCommandCount() ? m_Commands[index].Command : nullptr;
}
ICommand *ModContext::FindCommand(const char *name) const {
    if (!name)
        return nullptr;
    const std::string key = Lower(name);
    const auto found = std::find_if(m_Commands.begin(), m_Commands.end(), [&](const CommandRecord &record) {
        return record.Name == key || (!record.Alias.empty() && record.Alias == key);
    });
    return found == m_Commands.end() ? nullptr : found->Command;
}

void ModContext::ExecuteCommand(const char *cmd) {
    if (!cmd)
        return;
    std::istringstream input(cmd);
    std::vector<std::string> args;
    std::string word;
    while (input >> word)
        args.push_back(word);
    if (args.empty())
        return;
    ICommand *command = FindCommand(args[0].c_str());
    if (!command) {
        SendIngameMessage(("Unknown command: " + args[0]).c_str());
        return;
    }
    if (command->IsCheat() && !m_CheatEnabled) {
        SendIngameMessage("Can not execute cheat command");
        return;
    }
    try {
        BroadcastCallback(&IMod::OnPreCommandExecute, command, args);
        command->Execute(this, args);
        BroadcastCallback(&IMod::OnPostCommandExecute, command, args);
    } catch (const std::exception &error) {
        m_Logger->Error("Command %s failed: %s", args[0].c_str(), error.what());
    }
}

IMod *ModContext::GetMod(int index) {
    return index >= 0 && index < GetModCount() ? m_Mods[index] : nullptr;
}
IMod *ModContext::FindMod(const char *id) const {
    if (!id)
        return nullptr;
    for (const ModRecord &record : m_ModRecords) {
        if (record.Mod && std::strcmp(record.Mod->GetID(), id) == 0)
            return record.Mod;
    }
    return nullptr;
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

void ModContext::RegisterBallType(const char *, const char *, const char *, const char *,
                                  float, float, float, const char *, float, float, float, float) {
    m_Logger->Warn("Ball type registration is not available in the WASM runtime yet");
}
void ModContext::RegisterFloorType(const char *, float, float, float, const char *, bool) {
    m_Logger->Warn("Floor type registration is not available in the WASM runtime yet");
}
void ModContext::RegisterModulBall(const char *, bool, float, float, float, const char *,
                                   bool, bool, bool, float, float, float) {
    m_Logger->Warn("Modul ball registration is not available in the WASM runtime yet");
}
void ModContext::RegisterModulConvex(const char *, bool, float, float, float, const char *,
                                     bool, bool, bool, float, float) {
    m_Logger->Warn("Modul convex registration is not available in the WASM runtime yet");
}
void ModContext::RegisterTrafo(const char *) {
    m_Logger->Warn("Ball transformer registration is not available in the WASM runtime yet");
}
void ModContext::RegisterModul(const char *) {
    m_Logger->Warn("Modul registration is not available in the WASM runtime yet");
}

void ModContext::BroadcastMessage(const char *name, void (IMod::*callback)()) {
    m_Logger->Info("Game event: %s", name);
    BroadcastCallback(callback);
}
void ModContext::OnPreStartMenu() { BroadcastMessage("PreStartMenu", &IMod::OnPreStartMenu); }
void ModContext::OnPostStartMenu() { BroadcastMessage("PostStartMenu", &IMod::OnPostStartMenu); }
void ModContext::OnExitGame() { BroadcastMessage("ExitGame", &IMod::OnExitGame); }
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
    runtime->BroadcastCallback(&IMod::OnLoadObject, filename ? filename : "", isMap,
                               masterName ? masterName : "", filterClass, addToScene,
                               reuseMeshes, reuseMaterials, dynamic, objects, masterObject);
    if (!objects)
        return;
    for (CK_ID *id = objects->Begin(); id != objects->End(); ++id) {
        auto *script = static_cast<CKBehavior *>(context->GetObject(*id));
        if (script && script->GetClassID() == CKCID_BEHAVIOR &&
            (script->GetType() & CKBEHAVIORTYPE_SCRIPT) != 0) {
            runtime->InstallGameEventHooks(script);
            runtime->BroadcastCallback(&IMod::OnLoadScript, filename ? filename : "", script);
        }
    }
}

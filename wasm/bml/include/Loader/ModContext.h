#ifndef BML_WASM_MODCONTEXT_H
#define BML_WASM_MODCONTEXT_H

#include "BML/IBML.h"
#include "BML/IMod.h"
#include "Api/ExecuteBBAdapter.h"
#include "Api/ObjectRefs.h"
#include "Behavior/Blocks/PhysicsForce.h"
#include "Behavior/Install/Installations.h"
#include "Behavior/PrototypeCatalog.h"
#include "Behavior/Runtime.h"
#include "Behavior/Script.h"
#include "Behavior/Sessions.h"
#include "Config/ConfigStore.h"
#include "Console/CommandContext.h"
#include "Console/Shell/ShellEnvironment.h"
#include "DataShare/DataShare.h"
#include "Imc/ImcRuntime.h"
#include "Loader/ModInvocationGate.h"
#include "Logging/Logger.h"
#include "ModMenu/ModMenuPages.h"
#include "UI/FontRuntime.h"
#include "UI/GameFontCatalog.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

class Logger;
class WasmBuiltinMod;
class NewBallTypeMod;

namespace BML::Api {
class CommandApi;
}
namespace BML::Shell {
class Executor;
class OutputSink;
}

class ModContext final : public IBML {
public:
    using ModFactory = IMod *(*)(IBML *);
    using ModDestructor = void (*)(IMod *);

    explicit ModContext(CKContext *context);
    ~ModContext() override;

    bool Init();
    bool Shutdown();
    void OnLoadGame();
    void OnProcess();
    void OnRender(CKRenderContext *dev);
    void OnReset();
    void RemoveDeletedObjects(const CK_ID *ids, int count);

    bool IsInited() const { return m_Inited; }
    bool AreModsLoaded() const { return m_Inited; }
    bool AreModsInited() const { return m_Inited; }
    bool IsMainThread() const { return std::this_thread::get_id() == m_MainThreadId; }
    bool IsInLevel() const {
        return m_GamePhase == GamePhase::ActiveLevel || m_GamePhase == GamePhase::PausedLevel;
    }
    FILE *GetLogFile() const { return m_Logfile; }
    ILogger *GetLogger() const { return m_Logger; }
    Config *AddConfig(std::unique_ptr<Config> config);
    Config *GetConfig(IMod *mod);
    void FlushConfigChanges(bool saveAll = false);

    const wchar_t *GetDirectory(BML_LoaderDirectory directory) const;
    const char *GetDirectoryUtf8(BML_LoaderDirectory directory) const;
    std::wstring GetModRootDirectory(const void *callerAddress, const char *modId) const;

    BML::ObjectRefs &ObjectRefs() noexcept { return m_ObjectRefs; }
    BML::Behavior::Internal::Sessions &BehaviorSessions() noexcept { return m_BehaviorSessions; }
    BML::Behavior::Internal::Scripts &BehaviorScripts() noexcept { return m_BehaviorScripts; }
    BML::Behavior::Internal::Installations &BehaviorInstallations() noexcept { return m_BehaviorInstallations; }
    BML::Behavior::Internal::PrototypeCatalog &BehaviorPrototypes() noexcept { return m_BehaviorPrototypes; }
    BML::ExecuteBBAdapter &ExecuteBB() noexcept { return m_ExecuteBB; }
    BML::GameFontCatalog &GetGameFonts() noexcept { return m_GameFonts; }
    BML::UI::FontRuntime *GetUiFontRuntime() noexcept { return m_UiFonts.get(); }
    const BML::UI::FontRuntime *GetUiFontRuntime() const noexcept { return m_UiFonts.get(); }
    BML::CommandContext &GetCommandContext() { return m_CommandContext; }
    BML::Shell::Environment &GetShellEnvironment() { return m_ShellEnvironment; }
    BML::Api::CommandApi &GetCommandApi() { return *m_CommandApi; }
    BML::ImcRuntime &GetImcRuntime() { return m_ImcRuntime; }
    BML_DataShare *GetDataShare(const char *name = nullptr);
    std::string GetNativeModOwnerId(const void *callerAddress, const char *ownerId = nullptr) const;
    std::string GetNativeImcOwnerId(const void *callerAddress, const char *ownerId = nullptr) const;
    bool NativeModOwnsAddress(const std::string &ownerId, const void *address) const;
    bool IsModInvocationActiveOnCurrentThread() const {
        return m_ModInvocationGate.IsCallActiveOnCurrentThread();
    }

    CKContext *GetCKContext() override { return m_CKContext; }
    CKRenderContext *GetRenderContext() override { return m_CKContext->GetPlayerRenderContext(); }
    CKAttributeManager *GetAttributeManager() override { return m_AttributeManager; }
    CKBehaviorManager *GetBehaviorManager() override { return m_BehaviorManager; }
    CKCollisionManager *GetCollisionManager() override { return m_CollisionManager; }
    InputHook *GetInputManager() override { return m_Input.get(); }
    CKMessageManager *GetMessageManager() override { return m_MessageManager; }
    CKPathManager *GetPathManager() override { return m_PathManager; }
    CKParameterManager *GetParameterManager() override { return m_ParameterManager; }
    CKRenderManager *GetRenderManager() override { return m_RenderManager; }
    CKSoundManager *GetSoundManager() override { return m_SoundManager; }
    CKTimeManager *GetTimeManager() override { return m_TimeManager; }

    CKDataArray *GetArrayByName(const char *name) override;
    CKGroup *GetGroupByName(const char *name) override;
    CKMaterial *GetMaterialByName(const char *name) override;
    CKMesh *GetMeshByName(const char *name) override;
    CK2dEntity *Get2dEntityByName(const char *name) override;
    CK3dEntity *Get3dEntityByName(const char *name) override;
    CK3dObject *Get3dObjectByName(const char *name) override;
    CKCamera *GetCameraByName(const char *name) override;
    CKTargetCamera *GetTargetCameraByName(const char *name) override;
    CKLight *GetLightByName(const char *name) override;
    CKTargetLight *GetTargetLightByName(const char *name) override;
    CKSound *GetSoundByName(const char *name) override;
    CKTexture *GetTextureByName(const char *name) override;
    CKBehavior *GetScriptByName(const char *name) override;

    void SetIC(CKBeObject *obj, bool hierarchy = false) override;
    void RestoreIC(CKBeObject *obj, bool hierarchy = false) override;
    void Show(CKBeObject *obj, CK_OBJECT_SHOWOPTION show, bool hierarchy = false) override;

    using IBML::AddTimer;
    using IBML::AddTimerLoop;
    void AddTimer(CKDWORD delay, std::function<void()> callback) override;
    void AddTimerLoop(CKDWORD delay, std::function<bool()> callback) override;
    void AddTimer(float delay, std::function<void()> callback) override;
    void AddTimerLoop(float delay, std::function<bool()> callback) override;

    void ExitGame() override;
    bool IsIngame() override { return m_GamePhase != GamePhase::FrontEnd; }
    bool IsPaused() override { return m_GamePhase == GamePhase::PausedLevel; }
    bool IsPlaying() override { return m_GamePhase == GamePhase::ActiveLevel; }
    bool IsCheatEnabled() override { return m_CommandContext.IsCheatEnabled(); }
    void EnableCheat(bool enable) override;
    void SendIngameMessage(const char *msg) override;
    void ClearIngameMessages();
    void SkipRenderForNextTick() override;

    void OpenModsMenu();
    void CloseModsMenu();
    void OpenMapMenu();
    void CloseMapMenu();
    int GetHUD();
    void SetHUD(int mode);
    void ShowTitle(bool show);
    void ShowFPS(bool show);
    void ShowSRTimer(bool show);
    void StartSRTimer();
    void PauseSRTimer();
    void ResetSRTimer();
    float GetSRTime();

    void RegisterCommand(ICommand *cmd) override;
    int GetCommandCount() const override;
    ICommand *GetCommand(int index) const override;
    ICommand *FindCommand(const char *name) const override;
    void ExecuteCommand(const char *cmd) override;
    int ExecuteCommandLine(const char *line);
    int InvokeCommandArgs(const std::vector<std::string> &args, const std::string *input = nullptr);
    int UnregisterCommand(const void *callerAddress, const char *name);
    int UnregisterCommand(const char *name) { return UnregisterCommand(nullptr, name); }
    std::vector<BML::CommandContext::CommandInfo> GetCommandSnapshot() const;
    bool GetCommandInfo(int index, BML::CommandContext::CommandInfo &info) const;
    bool FindCommandInfo(const char *name, BML::CommandContext::CommandInfo &info) const;
    bool SetCommandEnabled(ICommand *command, bool enabled);
    std::vector<std::string> CompleteCommand(const char *name, const std::vector<std::string> &args);
    int RegisterCallbackCommand(const void *registrar, ICommand *command,
                                BML::CommandContext::CommandInfo info,
                                std::shared_ptr<void> lifetime);
    BML::CommandContext::UnregisterResult UnregisterCallbackCommand(
        const void *registrar, const char *name);
    bool RetireCallbackCommands(const void *registrar) noexcept;
    bool IsCommandInvocationActiveOnCurrentThread() const {
        return m_CommandInvocationGate.IsCallActiveOnCurrentThread();
    }
    void WriteShellError(std::string_view message);

    int GetModCount() override { return static_cast<int>(m_Mods.size()); }
    int GetOfficialModCount() const { return m_OfficialModCount; }
    IMod *GetMod(int index) override;
    IMod *FindMod(const char *id) const override;
    std::uint64_t GetModGeneration(const IMod *mod) const;
    std::uint64_t GetModRegistryRevision() const { return m_ModRegistryRevision; }
    float GetSRScore() override;
    int GetHSScore() override;

    int RegisterDependency(IMod *mod, const char *dependencyId, int major, int minor, int patch) override;
    int RegisterOptionalDependency(IMod *mod, const char *dependencyId, int major, int minor, int patch) override;
    int CheckDependencies(IMod *mod) const override;
    int GetDependencyCount(IMod *mod) const override;
    int GetDependencyInfo(IMod *mod, int index, char *dependencyId, int idSize,
                          int *major, int *minor, int *patch, int *optional) const override;
    int ClearDependencies(IMod *mod) override;

    ModMenuPages &GetModMenuPages() { return m_ModMenuPages; }
    const ModMenuPages &GetModMenuPages() const { return m_ModMenuPages; }
    BML::ModInvocationGate::CallLock LockModInvocation() const {
        return m_ModInvocationGate.LockCall();
    }

    void RegisterBallType(const char *ballFile, const char *ballId, const char *ballName, const char *objName,
                          float friction, float elasticity, float mass, const char *collGroup,
                          float linearDamp, float rotDamp, float force, float radius) override;
    void RegisterFloorType(const char *floorName, float friction, float elasticity, float mass,
                           const char *collGroup, bool enableColl) override;
    void RegisterModulBall(const char *modulName, bool fixed, float friction, float elasticity, float mass,
                           const char *collGroup, bool frozen, bool enableColl, bool calcMassCenter,
                           float linearDamp, float rotDamp, float radius) override;
    void RegisterModulConvex(const char *modulName, bool fixed, float friction, float elasticity, float mass,
                             const char *collGroup, bool frozen, bool enableColl, bool calcMassCenter,
                             float linearDamp, float rotDamp) override;
    void RegisterTrafo(const char *modulName) override;
    void RegisterModul(const char *modulName) override;

    void OnPreStartMenu() override;
    void OnPostStartMenu() override;
    void OnExitGame() override;
    void OnPreLoadLevel() override;
    void OnPostLoadLevel() override;
    void OnStartLevel() override;
    void OnPreResetLevel() override;
    void OnPostResetLevel() override;
    void OnPauseLevel() override;
    void OnUnpauseLevel() override;
    void OnPreExitLevel() override;
    void OnPostExitLevel() override;
    void OnPreNextLevel() override;
    void OnPostNextLevel() override;
    void OnDead() override;
    void OnPreEndLevel() override;
    void OnPostEndLevel() override;
    void OnCounterActive() override;
    void OnCounterInactive() override;
    void OnBallNavActive() override;
    void OnBallNavInactive() override;
    void OnCamNavActive() override;
    void OnCamNavInactive() override;
    void OnBallOff() override;
    void OnPreCheckpointReached() override;
    void OnPostCheckpointReached() override;
    void OnLevelFinish() override;
    void OnGameOver() override;
    void OnExtraPoint() override;
    void OnPreSubLife() override;
    void OnPostSubLife() override;
    void OnPreLifeUp() override;
    void OnPostLifeUp() override;

    template <typename T, typename... Args>
    void BroadcastCallback(T callback, Args &&...args) {
        for (IMod *mod : m_Mods) {
            InvokeMod(mod, [&] { (mod->*callback)(std::forward<Args>(args)...); });
        }
        if (m_NewBallTypes)
            InvokeMod(m_NewBallTypes.get(), [&] { (m_NewBallTypes.get()->*callback)(std::forward<Args>(args)...); });
    }

    void InstallGameEventHooks(CKBehavior *script);

private:
    struct ModRecord {
        IMod *Mod = nullptr;
        ModDestructor Destroy = nullptr;
        std::uint64_t Generation = 0;
        bool Official = false;
    };
    enum class GamePhase { FrontEnd, Transitioning, ActiveLevel, PausedLevel };

    template <typename Function>
    void InvokeMod(IMod *mod, Function &&function) {
        IMod *previous = m_ActiveMod;
        m_ActiveMod = mod;
        try {
            function();
        } catch (const std::exception &error) {
            m_Logger->Error("%s callback failed: %s", mod->GetID(), error.what());
        } catch (...) {
            m_Logger->Error("%s callback failed with an unknown exception", mod->GetID());
        }
        m_ActiveMod = previous;
    }

    bool LoadMods();
    void ProcessVirtoolsFrame();
    void BehaviorScriptLoaded(CKBehavior *script);
    void BroadcastMessage(const char *name, void (IMod::*callback)());

    CKContext *m_CKContext = nullptr;
    CKAttributeManager *m_AttributeManager = nullptr;
    CKBehaviorManager *m_BehaviorManager = nullptr;
    CKCollisionManager *m_CollisionManager = nullptr;
    CKMessageManager *m_MessageManager = nullptr;
    CKPathManager *m_PathManager = nullptr;
    CKParameterManager *m_ParameterManager = nullptr;
    CKRenderManager *m_RenderManager = nullptr;
    CKSoundManager *m_SoundManager = nullptr;
    CKTimeManager *m_TimeManager = nullptr;

    BML::ObjectRefs m_ObjectRefs;
    BML::Behavior::Internal::PrototypeCatalog m_BehaviorPrototypes;
    BML::Behavior::Internal::Runtime m_Behaviors;
    BML::Behavior::Internal::Sessions m_BehaviorSessions;
    BML::Behavior::Internal::Installations m_BehaviorInstallations;
    BML::Behavior::Internal::Scripts m_BehaviorScripts;
    BML::Behavior::Internal::PhysicsForce::Sessions m_PhysicsForce;
    BML::ExecuteBBAdapter m_ExecuteBB;
    std::unique_ptr<BML::UI::FontRuntime> m_UiFonts;
    BML::GameFontCatalog m_GameFonts;
    std::shared_ptr<ICommand> m_BrowserTestCommand;
    std::vector<std::shared_ptr<ICommand>> m_BrowserTestCommands;
    BML::CommandContext m_CommandContext;
    BML::Shell::Environment m_ShellEnvironment;
    BML::ImcRuntime m_ImcRuntime;
    std::unique_ptr<BML::Api::CommandApi> m_CommandApi;
    std::unique_ptr<BML::Shell::Executor> m_Shell;
    class ShellDispatcher;
    std::unique_ptr<ShellDispatcher> m_ShellDispatcher;
    BML::DataShareStore *m_DataShare = nullptr;
    std::vector<BML::Shell::OutputSink *> m_OutputSinks;
    mutable std::mutex m_CommandMutex;
    BML::ModInvocationGate m_CommandInvocationGate;
    IMod *m_ActiveMod = nullptr;

    std::unique_ptr<InputHook> m_Input;
    std::unique_ptr<NewBallTypeMod> m_NewBallTypes;
    std::vector<ModRecord> m_ModRecords;
    std::vector<IMod *> m_Mods;
    std::unordered_map<IMod *, std::vector<ModDependency>> m_Dependencies;
    ConfigStore m_Configs;
    ModMenuPages m_ModMenuPages;
    mutable BML::ModInvocationGate m_ModInvocationGate;
    WasmBuiltinMod *m_BuiltinMod = nullptr;
    Logger *m_Logger = nullptr;
    FILE *m_Logfile = nullptr;
    GamePhase m_GamePhase = GamePhase::FrontEnd;
    bool m_Inited = false;
    bool m_BaseLoaded = false;
    int m_OfficialModCount = 0;
    std::uint64_t m_ModRegistryRevision = 0;
    const std::thread::id m_MainThreadId = std::this_thread::get_id();
};

class ModContextLease {
public:
    ModContextLease();
    ~ModContextLease() = default;
    explicit operator bool() const { return m_Context != nullptr; }
    ModContext *operator->() const { return m_Context; }
    ModContext *get() const { return m_Context; }
    ModContext &operator*() const { return *m_Context; }
private:
    ModContext *m_Context = nullptr;
};

ModContext *BML_GetModContext();
CKContext *BML_GetCKContext();
CKRenderContext *BML_GetRenderContext();

#endif

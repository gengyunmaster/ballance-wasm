#ifndef BML_WASM_MODCONTEXT_H
#define BML_WASM_MODCONTEXT_H

#include "BML/IBML.h"
#include "BML/IMod.h"
#include "Config/ConfigStore.h"
#include "Logging/Logger.h"

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Logger;

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
    FILE *GetLogFile() const { return m_Logfile; }
    ILogger *GetLogger() const { return m_Logger; }
    Config *AddConfig(std::unique_ptr<Config> config);
    void FlushConfigChanges(bool saveAll = false);

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

    void AddTimer(CKDWORD delay, std::function<void()> callback) override;
    void AddTimerLoop(CKDWORD delay, std::function<bool()> callback) override;
    void AddTimer(float delay, std::function<void()> callback) override;
    void AddTimerLoop(float delay, std::function<bool()> callback) override;

    void ExitGame() override;
    bool IsIngame() override { return m_GamePhase != GamePhase::FrontEnd; }
    bool IsPaused() override { return m_GamePhase == GamePhase::PausedLevel; }
    bool IsPlaying() override { return m_GamePhase == GamePhase::ActiveLevel; }
    bool IsCheatEnabled() override { return m_CheatEnabled; }
    void EnableCheat(bool enable) override;
    void SendIngameMessage(const char *msg) override;
    void SkipRenderForNextTick() override;

    void RegisterCommand(ICommand *cmd) override;
    int GetCommandCount() const override;
    ICommand *GetCommand(int index) const override;
    ICommand *FindCommand(const char *name) const override;
    void ExecuteCommand(const char *cmd) override;
    int UnregisterCommand(const char *name);

    int GetModCount() override { return static_cast<int>(m_Mods.size()); }
    IMod *GetMod(int index) override;
    IMod *FindMod(const char *id) const override;
    float GetSRScore() override { return 0.0f; }
    int GetHSScore() override;

    int RegisterDependency(IMod *mod, const char *dependencyId, int major, int minor, int patch) override;
    int RegisterOptionalDependency(IMod *mod, const char *dependencyId, int major, int minor, int patch) override;
    int CheckDependencies(IMod *mod) const override;
    int GetDependencyCount(IMod *mod) const override;
    int GetDependencyInfo(IMod *mod, int index, char *dependencyId, int idSize,
                          int *major, int *minor, int *patch, int *optional) const override;
    int ClearDependencies(IMod *mod) override;

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
            try {
                (mod->*callback)(std::forward<Args>(args)...);
            } catch (const std::exception &error) {
                m_Logger->Error("%s callback failed: %s", mod->GetID(), error.what());
            } catch (...) {
                m_Logger->Error("%s callback failed with an unknown exception", mod->GetID());
            }
        }
    }

public:
    void InstallGameEventHooks(CKBehavior *script);

private:
    struct ModRecord {
        IMod *Mod = nullptr;
        ModDestructor Destroy = nullptr;
    };
    struct CommandRecord {
        ICommand *Command = nullptr;
        std::string Name;
        std::string Alias;
        bool Cheat = false;
    };
    enum class GamePhase { FrontEnd, Transitioning, ActiveLevel, PausedLevel };

    bool LoadMods();
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

    std::unique_ptr<InputHook> m_Input;
    std::vector<ModRecord> m_ModRecords;
    std::vector<IMod *> m_Mods;
    std::unordered_map<IMod *, std::vector<ModDependency>> m_Dependencies;
    std::vector<CommandRecord> m_Commands;
    ConfigStore m_Configs;
    std::unordered_set<CK_ID> m_EventHookScripts;
    Logger *m_Logger = nullptr;
    FILE *m_Logfile = nullptr;
    GamePhase m_GamePhase = GamePhase::FrontEnd;
    bool m_CheatEnabled = false;
    bool m_Inited = false;
    bool m_BaseLoaded = false;
};

class ModContextLease {
public:
    ModContextLease();
    explicit operator bool() const { return m_Context != nullptr; }
    ModContext *operator->() const { return m_Context; }
private:
    ModContext *m_Context = nullptr;
};

ModContext *BML_GetModContext();
CKContext *BML_GetCKContext();
CKRenderContext *BML_GetRenderContext();

#endif

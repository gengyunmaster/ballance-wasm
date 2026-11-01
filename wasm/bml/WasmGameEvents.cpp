#include "Loader/ModContext.h"
#include "BML/ExecuteBB.h"
#include "BML/ScriptHelper.h"
#include "BMLWasmRuntime.h"

#include <memory>
#include <string>
#include <vector>

namespace {
struct EventBinding {
    ModContext *Receiver;
    void (ModContext::*Method)();
};

std::vector<std::unique_ptr<EventBinding>> &Bindings() {
    static std::vector<std::unique_ptr<EventBinding>> bindings;
    return bindings;
}

int RunEvent(const CKBehaviorContext *, void *argument) {
    auto *binding = static_cast<EventBinding *>(argument);
    (binding->Receiver->*binding->Method)();
    return CKBR_OK;
}

void LogMissing(const char *script, const char *item) {
    if (ModContext *context = BML_GetModContext())
        context->GetLogger()->Warn("Cannot install %s hook in %s", item, script);
}

CKBehavior *Require(CKBehavior *script, const char *name) {
    CKBehavior *behavior = ScriptHelper::FindFirstBB(script, name);
    if (!behavior)
        LogMissing(script->GetName(), name);
    return behavior;
}

CKBehavior *Next(CKBehavior *script, CKBehavior *behavior, int output = 0,
                 const char *name = nullptr) {
    return behavior ? ScriptHelper::FindNextBB(script, behavior, name, output) : nullptr;
}

CKBehavior *Advance(CKBehavior *script, CKBehavior *behavior, int count) {
    while (behavior && count-- > 0)
        behavior = Next(script, behavior);
    return behavior;
}

CKBehavior *Branch(CKBehavior *script, CKBehavior *dispatch, int output) {
    return Next(script, dispatch, output);
}

CKBehaviorLink *Outgoing(CKBehavior *script, CKBehavior *behavior, int output = 0,
                         const char *target = nullptr) {
    return behavior ? ScriptHelper::FindNextLink(script, behavior, target, output) : nullptr;
}

CKBehaviorLink *Between(CKBehavior *script, CKBehavior *source, CKBehavior *target) {
    return source && target ? Outgoing(script, source, 0, target->GetName()) : nullptr;
}

CKBehavior *CreateEventHook(CKBehavior *script, void (ModContext::*method)(),
                            const char *label) {
    auto binding = std::make_unique<EventBinding>(EventBinding{BML_GetModContext(), method});
    CKBehavior *hook = ExecuteBB::CreateHookBlock(script, RunEvent, binding.get());
    if (!hook) {
        LogMissing(script->GetName(), label);
        return nullptr;
    }
    Bindings().push_back(std::move(binding));
    return hook;
}

bool HookLink(CKBehavior *script, CKBehaviorLink *link, void (ModContext::*method)(),
              const char *label) {
    if (!link) {
        LogMissing(script->GetName(), label);
        return false;
    }
    CKBehavior *hook = CreateEventHook(script, method, label);
    if (!hook)
        return false;
    ScriptHelper::InsertBB(script, link, hook);
    return true;
}

bool HookOutput(CKBehavior *script, CKBehavior *behavior, int output,
                void (ModContext::*method)(), const char *label) {
    if (!behavior || output < 0 || output >= behavior->GetOutputCount()) {
        LogMissing(script->GetName(), label);
        return false;
    }
    CKBehavior *hook = CreateEventHook(script, method, label);
    return hook && ScriptHelper::CreateLink(script, behavior, hook, output);
}

bool HookOutgoing(CKBehavior *script, CKBehavior *behavior, void (ModContext::*method)(),
                  const char *label, int output = 0, const char *target = nullptr) {
    return HookLink(script, Outgoing(script, behavior, output, target), method, label);
}

bool HookAfter(CKBehavior *script, CKBehavior *behavior, void (ModContext::*method)(),
               const char *label) {
    CKBehaviorLink *last = nullptr;
    CKBehaviorLink *current = behavior ? Outgoing(script, behavior) : nullptr;
    for (int remaining = 0; current && remaining < 256; ++remaining) {
        last = current;
        CKBehaviorIO *destination = current->GetOutBehaviorIO();
        CKBehavior *next = destination ? destination->GetOwner() : nullptr;
        if (!next || next == script)
            break;
        current = Outgoing(script, next);
    }
    return last ? HookLink(script, last, method, label)
                : HookOutput(script, behavior, 0, method, label);
}

CKBehavior *WaitMessage(CKBehavior *script, CKMessageType message) {
    CKBehavior *result = nullptr;
    ScriptHelper::FindBB(script, [&](CKBehavior *behavior) {
        int expected = 0;
        if (behavior->GetInputParameterCount() > 0 &&
            behavior->GetInputParameterValue(0, &expected) == CK_OK && expected == message) {
            result = behavior;
            return false;
        }
        return true;
    }, "Wait Message");
    if (!result)
        LogMissing(script->GetName(), "Wait Message");
    return result;
}

void InstallEventHandler(CKBehavior *script) {
    CKBehavior *dispatch = Require(script, "Switch On Message");
    if (!dispatch)
        return;

    CKBehavior *startMenu = Branch(script, dispatch, 0);
    HookAfter(script, startMenu, &ModContext::OnPostStartMenu, "PostStartMenu");
    HookOutgoing(script, startMenu, &ModContext::OnPreStartMenu, "PreStartMenu");

    CKBehavior *exitGame = Branch(script, dispatch, 1);
    HookOutgoing(script, exitGame, &ModContext::OnExitGame, "ExitGame");

    CKBehavior *loadLevel = Branch(script, dispatch, 2);
    HookOutgoing(script, Advance(script, loadLevel, 2), &ModContext::OnPreLoadLevel, "PreLoadLevel");
    HookAfter(script, loadLevel, &ModContext::OnPostLoadLevel, "PostLoadLevel");
    HookAfter(script, Branch(script, dispatch, 3), &ModContext::OnStartLevel, "StartLevel");

    CKBehavior *reset = Require(script, "reset Level");
    CKBehavior *resetFirst = nullptr;
    if (reset) {
        CKBehaviorLink *entry = ScriptHelper::FindNextLink(reset, reset->GetInput(0));
        resetFirst = entry ? entry->GetOutBehaviorIO()->GetOwner() : nullptr;
    }
    CKBehavior *resetSecond = reset ? Next(reset, resetFirst) : nullptr;
    HookOutgoing(reset, resetSecond, &ModContext::OnPreResetLevel, "PreResetLevel");
    HookAfter(script, Branch(script, dispatch, 4), &ModContext::OnPostResetLevel, "PostResetLevel");
    HookAfter(script, Branch(script, dispatch, 5), &ModContext::OnPauseLevel, "PauseLevel");
    HookAfter(script, Branch(script, dispatch, 6), &ModContext::OnUnpauseLevel, "UnpauseLevel");

    CKBehavior *deleteSurfaces = Require(script, "DeleteCollisionSurfaces");
    CKBehavior *exitBranches = Next(script, deleteSurfaces);
    HookOutgoing(script, Advance(script, Branch(script, dispatch, 7), 4),
                 &ModContext::OnPreExitLevel, "PreExitLevel");
    HookOutgoing(script, Next(script, exitBranches, 0), &ModContext::OnPostExitLevel, "PostExitLevel");
    HookOutgoing(script, Advance(script, Branch(script, dispatch, 8), 4),
                 &ModContext::OnPreNextLevel, "PreNextLevel");
    HookOutgoing(script, Next(script, exitBranches, 1), &ModContext::OnPostNextLevel, "PostNextLevel");
    HookAfter(script, Branch(script, dispatch, 9), &ModContext::OnDead, "Dead");

    HookOutgoing(script, Branch(script, dispatch, 10), &ModContext::OnPreEndLevel, "PreEndLevel");
    CKBehavior *highscore = Require(script, "Highscore");
    if (highscore) {
        CKBehaviorIO *completed = highscore->CreateOutput(const_cast<CKSTRING>("Out"));
        if (completed) {
            ScriptHelper::FindBB(highscore, [&](CKBehavior *behavior) {
                ScriptHelper::CreateLink(highscore, behavior, completed);
                return true;
            }, "Activate Script");
            HookOutput(script, highscore, highscore->GetOutputCount() - 1,
                       &ModContext::OnPostEndLevel, "PostEndLevel");
        } else {
            LogMissing(script->GetName(), "PostEndLevel");
        }
    }
}

void InstallGameplayIngame(CKBehavior *script, CKMessageType cameraOn, CKMessageType cameraOff,
                           CKMessageType ballOn, CKMessageType ballOff) {
    CKBehavior *camera = Require(script, "CamNav On/Off");
    if (camera) {
        HookOutgoing(camera, WaitMessage(camera, cameraOn), &ModContext::OnCamNavActive, "CamNavActive");
        HookOutgoing(camera, WaitMessage(camera, cameraOff), &ModContext::OnCamNavInactive, "CamNavInactive");
    }
    CKBehavior *ball = Require(script, "BallNav On/Off");
    if (ball) {
        HookOutgoing(ball, WaitMessage(ball, ballOn), &ModContext::OnBallNavActive, "BallNavActive");
        HookOutgoing(ball, WaitMessage(ball, ballOff), &ModContext::OnBallNavInactive, "BallNavInactive");
    }
}

void InstallGameplayEnergy(CKBehavior *script, CKMessageType lifeUpMessage,
                           CKMessageType ballOffMessage, CKMessageType subLifeMessage,
                           CKMessageType extraPointMessage) {
    CKBehavior *dispatch = Require(script, "Switch On Message");
    if (dispatch) {
        HookOutgoing(script, dispatch, &ModContext::OnCounterActive, "CounterActive", 3);
        HookOutgoing(script, dispatch, &ModContext::OnCounterInactive, "CounterInactive", 1);
    }

    CKBehavior *lifeUpWait = WaitMessage(script, lifeUpMessage);
    CKBehavior *addLife = Next(script, lifeUpWait, 0, "add Life");
    HookLink(script, Between(script, lifeUpWait, addLife), &ModContext::OnPreLifeUp, "PreLifeUp");
    HookAfter(script, addLife, &ModContext::OnPostLifeUp, "PostLifeUp");

    CKBehavior *ballOffWait = WaitMessage(script, ballOffMessage);
    CKBehavior *delay = Next(script, ballOffWait, 0, "Delayer");
    HookLink(script, Between(script, ballOffWait, delay), &ModContext::OnBallOff, "BallOff");

    CKBehavior *subLifeWait = WaitMessage(script, subLifeMessage);
    CKBehavior *subLife = Next(script, subLifeWait, 0, "sub Life");
    HookLink(script, Between(script, subLifeWait, subLife), &ModContext::OnPreSubLife, "PreSubLife");
    HookAfter(script, subLife, &ModContext::OnPostSubLife, "PostSubLife");

    CKBehavior *extraPointWait = WaitMessage(script, extraPointMessage);
    CKBehavior *show = Next(script, extraPointWait, 0, "Show");
    HookLink(script, Between(script, extraPointWait, show), &ModContext::OnExtraPoint, "ExtraPoint");
}

void InstallGameplayEvents(CKBehavior *script, CKMessageType checkpointMessage,
                           CKMessageType gameOverMessage, CKMessageType levelFinishMessage) {
    CKBehavior *checkpointWait = WaitMessage(script, checkpointMessage);
    CKBehavior *resetpoint = Next(script, checkpointWait, 0, "set Resetpoint");
    HookLink(script, Between(script, checkpointWait, resetpoint),
             &ModContext::OnPreCheckpointReached, "PreCheckpoint");
    HookAfter(script, resetpoint, &ModContext::OnPostCheckpointReached, "PostCheckpoint");

    CKBehavior *gameOverWait = WaitMessage(script, gameOverMessage);
    CKBehavior *gameOverSend = Next(script, gameOverWait, 0, "Send Message");
    HookLink(script, Between(script, gameOverWait, gameOverSend), &ModContext::OnGameOver, "GameOver");

    CKBehavior *levelFinishWait = WaitMessage(script, levelFinishMessage);
    CKBehavior *levelFinishSend = Next(script, levelFinishWait, 0, "Send Message");
    HookLink(script, Between(script, levelFinishWait, levelFinishSend),
             &ModContext::OnLevelFinish, "LevelFinish");
}
}

void BMLWasmInstallGameEventHooks(CKBehavior *script) {
    ModContext *context = BML_GetModContext();
    if (!context || !script || !script->GetName())
        return;
    CKMessageManager *messages = context->GetMessageManager();
    if (!messages)
        return;

    const std::string name = script->GetName();
    if (name == "Event_handler") {
        InstallEventHandler(script);
    } else if (name == "Gameplay_Ingame") {
        InstallGameplayIngame(
            script,
            messages->AddMessageType("CamNav activate"),
            messages->AddMessageType("CamNav deactivate"),
            messages->AddMessageType("BallNav activate"),
            messages->AddMessageType("BallNav deactivate"));
    } else if (name == "Gameplay_Energy") {
        InstallGameplayEnergy(
            script,
            messages->AddMessageType("Life_Up"),
            messages->AddMessageType("Ball Off"),
            messages->AddMessageType("Sub Life"),
            messages->AddMessageType("Extrapoint"));
    } else if (name == "Gameplay_Events") {
        InstallGameplayEvents(
            script,
            messages->AddMessageType("Checkpoint reached"),
            messages->AddMessageType("Game Over"),
            messages->AddMessageType("Level_Finish"));
    }
}

void BMLWasmResetGameEventHooks() {
    Bindings().clear();
}

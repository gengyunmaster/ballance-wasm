#include "Loader/ModContext.h"

#include "Api/CommandApi.h"
#include "Behavior/Lifecycle.h"
#include "Console/Shell/ShellExecutor.h"
#include "Console/Shell/ShellIo.h"
#include "Mods/NewBallTypeMod.h"
#include "PathUtils.h"
#include "StringUtils.h"
#include "WasmBuiltinMod.h"
#include "WasmPersistentStore.h"

namespace {
int CommandResult(BML::CommandContext::UnregisterResult result) {
    using Result = BML::CommandContext::UnregisterResult;
    switch (result) {
    case Result::Success: return BML_OK;
    case Result::InvalidName: return BML_ERROR_INVALID_PARAMETER;
    case Result::NotFound: return BML_ERROR_NOT_FOUND;
    case Result::AccessDenied: return BML_ERROR_ACCESS_DENIED;
    case Result::Busy: return BML_ERROR_BUSY;
    case Result::InternalError: return BML_ERROR_FAIL;
    }
    return BML_ERROR_FAIL;
}
}

void ModContext::ProcessVirtoolsFrame() {
    BML::Behavior::Internal::ReleaseScope releases;
    m_BehaviorPrototypes.ProcessFrame();
    m_PhysicsForce.ProcessFrame();
    m_Behaviors.ProcessFrame();
    m_BehaviorSessions.ProcessFrame();
    const auto plans = m_BehaviorInstallations.ProcessFrame();
    if (!plans)
        m_Logger->Error("Failed to reconcile Behavior Plans: %s", plans.Message.c_str());
    m_BehaviorScripts.ProcessFrame();
    m_ExecuteBB.ProcessFrame();
    m_CommandApi->FlushPending();
}

void ModContext::BehaviorScriptLoaded(CKBehavior *script) {
    if (!script)
        return;
    BML::Behavior::Internal::ReleaseScope releases;
    const BML_ObjectRef reference = m_ObjectRefs.Issue(script);
    const auto status = m_BehaviorInstallations.LoadScript(
        script->GetName() ? script->GetName() : "",
        {reference.Domain, reference.Slot, reference.Generation});
    if (!status)
        m_Logger->Error("Failed to retain a live Behavior script: %s", status.Message.c_str());
}

BML_DataShare *ModContext::GetDataShare(const char *name) {
    if (!name || !*name)
        return reinterpret_cast<BML_DataShare *>(m_DataShare);
    return reinterpret_cast<BML_DataShare *>(BML::DataShareStore::GetInstance(name));
}

std::string ModContext::GetNativeModOwnerId(const void *, const char *ownerId) const {
    if (ownerId && *ownerId)
        return FindMod(ownerId) ? ownerId : std::string();
    return m_ActiveMod ? m_ActiveMod->GetID() : std::string();
}

std::string ModContext::GetNativeImcOwnerId(const void *callerAddress, const char *ownerId) const {
    return GetNativeModOwnerId(callerAddress, ownerId);
}

bool ModContext::NativeModOwnsAddress(const std::string &ownerId, const void *address) const {
    return address && FindMod(ownerId.c_str());
}

std::wstring ModContext::GetModRootDirectory(const void *, const char *modId) const {
    IMod *mod = modId && *modId ? FindMod(modId) : m_ActiveMod;
    return mod ? utils::Utf8ToUtf16("/ModLoader/Data/" + std::string(mod->GetID())) : std::wstring();
}

void ModContext::RegisterCommand(ICommand *command) {
    if (!command || !m_ActiveMod)
        return;
    m_CommandContext.RegisterCommand(m_ActiveMod, command);
}

int ModContext::UnregisterCommand(const void *callerAddress, const char *name) {
    if (!IsMainThread())
        return BML_ERROR_WRONG_THREAD;
    const std::string owner = GetNativeModOwnerId(callerAddress, nullptr);
    ICommand *command = FindCommand(name);
    IMod *registrar = owner.empty() ? nullptr : FindMod(owner.c_str());
    if (!registrar)
        registrar = m_ActiveMod;
    return CommandResult(m_CommandContext.UnregisterCommand(registrar, name));
}

int ModContext::GetCommandCount() const {
    return static_cast<int>(m_CommandContext.GetCommandCount());
}

ICommand *ModContext::GetCommand(int index) const {
    return index >= 0 ? m_CommandContext.GetCommandByIndex(static_cast<std::size_t>(index)) : nullptr;
}

ICommand *ModContext::FindCommand(const char *name) const {
    return m_CommandContext.GetCommandByName(name);
}

std::vector<BML::CommandContext::CommandInfo> ModContext::GetCommandSnapshot() const {
    return m_CommandContext.GetCommandSnapshot();
}

bool ModContext::GetCommandInfo(int index, BML::CommandContext::CommandInfo &info) const {
    return index >= 0 && m_CommandContext.GetCommandInfoByIndex(static_cast<std::size_t>(index), info);
}

bool ModContext::FindCommandInfo(const char *name, BML::CommandContext::CommandInfo &info) const {
    return m_CommandContext.GetCommandInfoByName(name, info);
}

bool ModContext::SetCommandEnabled(ICommand *command, bool enabled) {
    return m_CommandContext.SetCommandEnabled(command, enabled);
}

int ModContext::RegisterCallbackCommand(const void *registrar, ICommand *command,
                                        BML::CommandContext::CommandInfo info,
                                        std::shared_ptr<void> lifetime) {
    if (!IsMainThread())
        return BML_ERROR_WRONG_THREAD;
    if (!registrar || !command)
        return BML_ERROR_INVALID_PARAMETER;
    if (FindCommand(info.Name.c_str()) || (!info.Alias.empty() && FindCommand(info.Alias.c_str())))
        return BML_ERROR_ALREADY_EXISTS;
    return m_CommandContext.RegisterCommand(registrar, command, std::move(info), std::move(lifetime))
        ? BML_OK : BML_ERROR_FAIL;
}

BML::CommandContext::UnregisterResult ModContext::UnregisterCallbackCommand(
    const void *registrar, const char *name) {
    return m_CommandContext.UnregisterCommand(registrar, name);
}

bool ModContext::RetireCallbackCommands(const void *registrar) noexcept {
    try {
        m_CommandContext.UnregisterCommands(registrar);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> ModContext::CompleteCommand(
    const char *name, const std::vector<std::string> &args) {
    auto invocation = m_CommandInvocationGate.LockCall();
    BML::CommandContext::CommandCall call;
    m_CommandContext.AcquireCommand(name, call);
    std::vector<std::string> result;
    if (call.Command && call.Info.Enabled)
        result = call.Command->GetTabCompletion(this, args);
    m_CommandApi->FlushPending();
    return result;
}

void ModContext::ExecuteCommand(const char *command) {
    if (command && *command)
        (void) ExecuteCommandLine(command);
}

int ModContext::ExecuteCommandLine(const char *line) {
    if (!line || !m_Shell || !IsMainThread())
        return BML::Shell::Status::Failure;
    auto invocation = m_CommandInvocationGate.LockCall();
    const int result = m_Shell->Execute(line);
    if (m_ShellEnvironment.IsDirty()) {
        std::string error;
        if (m_ShellEnvironment.Save(L"/ModLoader/CommandBar.shell.json", error)) {
            m_ShellEnvironment.ClearDirty();
            if (!WasmPersistentStore::Persist("/ModLoader/CommandBar.shell.json"))
                m_Logger->Warn("Cannot persist the command shell environment");
        } else {
            m_Logger->Warn("Failed to save the shell state file: %s", error.c_str());
        }
    }
    return result;
}

int ModContext::InvokeCommandArgs(const std::vector<std::string> &args, const std::string *input) {
    if (!IsMainThread() || args.empty() || args[0].empty())
        return BML::Shell::Status::Failure;
    BML::Shell::CommandDispatchScope dispatchScope;
    if (!dispatchScope) {
        WriteShellError(BML::Shell::FormatError("Error: Command dispatch nested too deeply"));
        return BML::Shell::Status::Failure;
    }

    auto invocation = m_CommandInvocationGate.LockCall();
    BML::CommandContext::CommandCall call;
    if (!m_CommandContext.AcquireCommand(args[0].c_str(), call) || !call.Command) {
        WriteShellError(BML::Shell::FormatError("Error: Unknown Command " + args[0]));
        return BML::Shell::Status::Unknown;
    }
    if (!call.Info.Enabled) {
        WriteShellError(BML::Shell::FormatError("Error: Command is disabled " + call.Info.Name));
        return BML::Shell::Status::Disabled;
    }
    if (call.Info.Cheat && !IsCheatEnabled()) {
        WriteShellError(BML::Shell::FormatError("Error: Can not execute cheat command " + args[0]));
        return BML::Shell::Status::CheatRefused;
    }

    int status = BML::Shell::Status::Failure;
    try {
        BroadcastCallback(&IMod::OnPreCommandExecute, call.Command, args);
        {
            BML::Shell::InvocationScope scope(input);
            call.Command->Execute(this, args);
            status = scope.Status();
        }
        BroadcastCallback(&IMod::OnPostCommandExecute, call.Command, args);
    } catch (const std::exception &error) {
        m_Logger->Error("Exception executing command '%s': %s", args[0].c_str(), error.what());
        WriteShellError(BML::Shell::FormatError("Error: Command failed - " + std::string(error.what())));
    } catch (...) {
        WriteShellError(BML::Shell::FormatError("Error: Command failed with unknown exception"));
    }
    m_CommandApi->FlushPending();
    return status;
}

void ModContext::WriteShellError(std::string_view message) {
    if (!m_OutputSinks.empty()) {
        m_OutputSinks.back()->Write(message);
        return;
    }
    SendIngameMessage(std::string(message).c_str());
}

void ModContext::ClearIngameMessages() {
    if (m_BuiltinMod)
        m_BuiltinMod->ClearIngameMessages();
}

void ModContext::OpenMapMenu() {
    if (m_BuiltinMod)
        m_BuiltinMod->OpenMapMenu();
}

void ModContext::CloseMapMenu() {
    if (m_BuiltinMod)
        m_BuiltinMod->CloseMapMenu();
}

void ModContext::RegisterBallType(const char *ballFile, const char *ballId, const char *ballName,
                                  const char *objName, float friction, float elasticity, float mass,
                                  const char *collGroup, float linearDamp, float rotDamp, float force,
                                  float radius) {
    if (m_NewBallTypes)
        m_NewBallTypes->RegisterBallType(ballFile, ballId, ballName, objName, friction, elasticity,
                                         mass, collGroup, linearDamp, rotDamp, force, radius);
}

void ModContext::RegisterFloorType(const char *floorName, float friction, float elasticity, float mass,
                                   const char *collGroup, bool enableColl) {
    if (m_NewBallTypes)
        m_NewBallTypes->RegisterFloorType(floorName, friction, elasticity, mass, collGroup, enableColl);
}

void ModContext::RegisterModulBall(const char *modulName, bool fixed, float friction, float elasticity,
                                   float mass, const char *collGroup, bool frozen, bool enableColl,
                                   bool calcMassCenter, float linearDamp, float rotDamp, float radius) {
    if (m_NewBallTypes)
        m_NewBallTypes->RegisterModulBall(modulName, fixed, friction, elasticity, mass, collGroup,
                                          frozen, enableColl, calcMassCenter, linearDamp, rotDamp, radius);
}

void ModContext::RegisterModulConvex(const char *modulName, bool fixed, float friction, float elasticity,
                                     float mass, const char *collGroup, bool frozen, bool enableColl,
                                     bool calcMassCenter, float linearDamp, float rotDamp) {
    if (m_NewBallTypes)
        m_NewBallTypes->RegisterModulConvex(modulName, fixed, friction, elasticity, mass, collGroup,
                                            frozen, enableColl, calcMassCenter, linearDamp, rotDamp);
}

void ModContext::RegisterTrafo(const char *modulName) {
    if (m_NewBallTypes)
        m_NewBallTypes->RegisterTrafo(modulName);
}

void ModContext::RegisterModul(const char *modulName) {
    if (m_NewBallTypes)
        m_NewBallTypes->RegisterModul(modulName);
}

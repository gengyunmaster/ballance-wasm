#include "WasmBrowserTest.h"

#include "Loader/ModContext.h"
#include "WasmBuiltinMod.h"

#include "BML/IVP.h"
#include "CKAll.h"
#include "Config/Config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace WasmBrowserTesting {
namespace {
bool HasRow(ModContext &context, const char *arrayName, const char *expected) {
    CKDataArray *array = context.GetArrayByName(arrayName);
    if (!array)
        return false;
    for (int row = 0; row < array->GetRowCount(); ++row) {
        const int size = array->GetElementStringValue(row, 0, nullptr);
        if (size <= 0)
            continue;
        std::string value(static_cast<std::size_t>(size), '\0');
        array->GetElementStringValue(row, 0, value.data());
        if (std::string(value.c_str()) == expected)
            return true;
    }
    return false;
}
}

class Command final : public ICommand {
public:
    explicit Command(ModContext &context) : m_Context(context) {}

    std::string GetName() override { return "bml-test"; }
    std::string GetAlias() override { return {}; }
    std::string GetDescription() override { return "Read native game state for browser verification"; }
    bool IsCheat() override { return false; }
    const std::vector<std::string> GetTabCompletion(
        IBML *, const std::vector<std::string> &) override { return {}; }

    void Execute(IBML *bml, const std::vector<std::string> &) override {
        CKCamera *camera = m_Context.GetRenderContext()
            ? m_Context.GetRenderContext()->GetAttachedCamera() : nullptr;
        CKDataArray *currentLevel = m_Context.GetArrayByName("CurrentLevel");
        CK3dEntity *ball = currentLevel
            ? static_cast<CK3dEntity *>(currentLevel->GetElementObject(0, 1)) : nullptr;

        unsigned int physics = 0;
        BML_IvpApiInfo info{};
        if (BML::IVP::ReadApiInfo(info) == BML_OK &&
            info.Architecture == BML_IVP_ARCH_WASM32 &&
            (info.Capabilities & BML_IVP_CAP_NATIVE_OBJECTS) != 0)
            physics |= 1;
        if (BML::IVP::Manager())
            physics |= 2;
        if (BML::IVP::Environment())
            physics |= 4;
        if (BML::IVP::PhysicsObject(ball))
            physics |= 8;
        if (BML::IVP::RealObject(ball))
            physics |= 16;
        if (BML::IVP::Core(ball))
            physics |= 32;
        if (BML::IVP::Material(ball))
            physics |= 64;
        CK3dEntity *spirit = nullptr;
        const char *spiritName = "none";
        int spiritCount = 0;
        int spiritVisibleCount = 0;
        const auto &entities = m_Context.GetCKContext()->GetObjectListByType(CKCID_3DENTITY, TRUE);
        for (auto iterator = entities.Begin(); iterator != entities.End(); ++iterator) {
            auto *object = static_cast<CKObject *>(*iterator);
            if (!object || !CKIsChildClassOf(object, CKCID_3DENTITY))
                continue;
            const char *name = object->GetName();
            if (!name || std::string(name).find("_Spirit") == std::string::npos)
                continue;
            auto *candidate = static_cast<CK3dEntity *>(object);
            ++spiritCount;
            if (candidate->IsVisible()) {
                ++spiritVisibleCount;
                if (!spirit) {
                    spirit = candidate;
                    spiritName = candidate->GetName();
                }
            }
        }
        IMod *spiritMod = m_Context.FindMod("SpiritTrail");
        IConfig *spiritConfig = spiritMod ? m_Context.GetConfig(spiritMod) : nullptr;
        IProperty *spiritEnabled = spiritConfig
            ? spiritConfig->GetProperty("Misc", "Enable") : nullptr;
        IProperty *spiritSr = spiritConfig
            ? spiritConfig->GetProperty("Misc", "HS_SR") : nullptr;
        auto *builtin = dynamic_cast<WasmBuiltinMod *>(m_Context.FindMod("BML"));
        const bool sticky = m_Context.Get3dObjectByName("Ball_Sticky") &&
            HasRow(m_Context, "Physicalize_GameBall", "Ball_Sticky");
        const bool moduls =
            HasRow(m_Context, "PH_Groups", "P_Rotboard_Wood") &&
            HasRow(m_Context, "PH_Groups", "P_Rotboard_Wood_Inv") &&
            HasRow(m_Context, "PH_Groups", "P_Dropboard_Wood") &&
            HasRow(m_Context, "Physicalize_Floors", "Phys_Floors_F0") &&
            HasRow(m_Context, "Physicalize_Floors", "Phys_Floors_E80") &&
            HasRow(m_Context, "Physicalize_Floors", "Phys_Floors_E400");
        const bool dual = m_Context.Get3dObjectByName("Ball_Wood_Dual") != nullptr;
        VxVector ballPosition(0.0f, 0.0f, 0.0f);
        if (ball)
            ball->GetPosition(&ballPosition);

        std::ostringstream output;
        output << "BML browser test state:"
               << " playing=" << (m_Context.IsPlaying() ? 1 : 0)
               << " cheat=" << (m_Context.IsCheatEnabled() ? 1 : 0)
               << " camera=" << (camera && camera->GetName() ? camera->GetName() : "none")
               << " fov=" << (camera ? camera->GetFov() : 0.0f)
               << " ball=" << (ball && ball->GetName() ? ball->GetName() : "none")
               << " ivp=" << physics
               << " spirit=" << (spirit ? "visible" : "hidden")
               << " spiritName=" << spiritName
               << " spiritCount=" << spiritCount
               << " spiritVisibleCount=" << spiritVisibleCount
               << " spiritEnabled=" << (spiritEnabled && spiritEnabled->GetBoolean() ? 1 : 0)
               << " spiritSR=" << (spiritSr && spiritSr->GetBoolean() ? 1 : 0)
               << " sticky=" << (sticky ? 1 : 0)
               << " moduls=" << (moduls ? 1 : 0)
               << " dual=" << (dual ? 1 : 0)
               << " hud=" << (builtin ? builtin->GetHUD() : 0)
               << " sr=" << (builtin ? builtin->GetSRTime() : 0.0f)
               << " bx=" << ballPosition.x
               << " by=" << ballPosition.y
               << " bz=" << ballPosition.z;
        bml->SendIngameMessage(output.str().c_str());
    }

private:
    ModContext &m_Context;
};
bool ContainsCaseInsensitive(const std::string &text, const std::string &filter) {
    if (filter.empty())
        return true;
    const auto compare = [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) ==
               std::tolower(static_cast<unsigned char>(rhs));
    };
    return std::search(text.begin(), text.end(), filter.begin(), filter.end(), compare) != text.end();
}

class ObjectCommand final : public ICommand {
public:
    explicit ObjectCommand(ModContext &context) : m_Context(context) {}

    std::string GetName() override { return "bml-test-obj"; }
    std::string GetAlias() override { return {}; }
    std::string GetDescription() override { return "List native objects for browser verification"; }
    bool IsCheat() override { return false; }
    const std::vector<std::string> GetTabCompletion(
        IBML *, const std::vector<std::string> &) override { return {}; }

    void Execute(IBML *bml, const std::vector<std::string> &args) override {
        const std::string filter = args.size() >= 2 ? args[1] : std::string();
        CKContext *context = m_Context.GetCKContext();
        if (!context) {
            bml->SendIngameMessage("BML browser test objects: no CK context");
            return;
        }

        CKLevel *level = context->GetCurrentLevel();
        CKScene *scene = context->GetCurrentScene();
        std::ostringstream header;
        header << "BML browser test objects: filter=" << (filter.empty() ? "*" : filter)
               << " level=" << (level && level->GetName() ? level->GetName() : "none")
               << " scene=" << (scene && scene->GetName() ? scene->GetName() : "none")
               << " scenes=";
        int sceneCount = 0;
        const auto &scenes = context->GetObjectListByType(CKCID_SCENE, TRUE);
        for (auto iterator = scenes.Begin(); iterator != scenes.End(); ++iterator) {
            auto *object = static_cast<CKScene *>(*iterator);
            if (!object || sceneCount >= 24)
                continue;
            const char *sceneName = object->GetName();
            char fallback[16];
            if (!sceneName) {
                std::snprintf(fallback, sizeof(fallback), "#%u", object->GetID());
                sceneName = fallback;
            }
            header << (sceneCount > 0 ? "|" : "") << sceneName;
            ++sceneCount;
        }
        if (scene && scene->GetName())
            header << " current=" << scene->GetName();
        bml->SendIngameMessage(header.str().c_str());

        int matched = 0;
        int shown = 0;
        const auto &entities = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
        for (auto iterator = entities.Begin(); iterator != entities.End(); ++iterator) {
            auto *object = static_cast<CK3dEntity *>(*iterator);
            if (!object || !CKIsChildClassOf(object, CKCID_3DENTITY))
                continue;
            const char *rawName = object->GetName();
            const std::string name = rawName ? rawName : "";
            if (!ContainsCaseInsensitive(name, filter))
                continue;
            ++matched;
            if (shown >= 160)
                continue;
            ++shown;
            VxVector position(0.0f, 0.0f, 0.0f);
            object->GetPosition(&position);
            CK3dEntity *parent = object->GetParent();
            std::ostringstream line;
            line << "obj " << name
                 << " class=" << object->GetClassID()
                 << " vis=" << (object->IsVisible() ? 1 : 0)
                 << " meshes=" << object->GetMeshCount()
                 << " parent=" << (parent && parent->GetName() ? parent->GetName() : "-")
                 << " pos=" << position.x << "," << position.y << "," << position.z;
            if (CKIsChildClassOf(object, CKCID_SPRITE3D)) {
                auto *sprite = static_cast<CKSprite3D *>(object);
                Vx2DVector size(0.0f, 0.0f);
                sprite->GetSize(size);
                line << " size=" << size.x << "x" << size.y;
                if (CKMaterial *material = sprite->GetMaterial()) {
                    line << " mat=" << (material->GetName() ? material->GetName() : "?");
                    if (CKTexture *texture = material->GetTexture())
                        line << " tex=" << (texture->GetName() ? texture->GetName() : "?");
                    else
                        line << " tex=none";
                } else {
                    line << " mat=none";
                }
            }
            bml->SendIngameMessage(line.str().c_str());
        }

        int groups = 0;
        const auto &groupList = context->GetObjectListByType(CKCID_GROUP, TRUE);
        for (auto iterator = groupList.Begin(); iterator != groupList.End(); ++iterator) {
            auto *group = static_cast<CKGroup *>(*iterator);
            if (!group || !CKIsChildClassOf(group, CKCID_GROUP))
                continue;
            const char *rawName = group->GetName();
            const std::string name = rawName ? rawName : "";
            if (!ContainsCaseInsensitive(name, filter))
                continue;
            ++groups;
            std::ostringstream line;
            line << "group " << name << " objects=" << group->GetObjectCount();
            bml->SendIngameMessage(line.str().c_str());
        }

        int entities2d = 0;
        const auto &list2d = context->GetObjectListByType(CKCID_2DENTITY, TRUE);
        for (auto iterator = list2d.Begin(); iterator != list2d.End(); ++iterator) {
            auto *entity = static_cast<CK2dEntity *>(*iterator);
            if (!entity || !CKIsChildClassOf(entity, CKCID_2DENTITY))
                continue;
            const char *rawName = entity->GetName();
            const std::string name = rawName ? rawName : "";
            if (!ContainsCaseInsensitive(name, filter))
                continue;
            ++entities2d;
            if (entities2d > 80)
                continue;
            std::string parents;
            for (CK2dEntity *parent = entity->GetParent(); parent; parent = parent->GetParent()) {
                const char *parentName = parent->GetName();
                if (!parents.empty())
                    parents.insert(0, "<");
                parents = (parentName ? parentName : "?") + parents;
            }
            std::ostringstream line;
            line << "2d " << name
                 << " vis=" << (entity->IsVisible() ? 1 : 0)
                 << " flags=0x" << std::hex << entity->GetObjectFlags() << std::dec
                 << " parent=" << (parents.empty() ? "-" : parents)
                 << " inScene=" << (scene && entity->IsInScene(scene) ? 1 : 0);
            bml->SendIngameMessage(line.str().c_str());
        }

        int arrays = 0;
        const auto &dataArrays = context->GetObjectListByType(CKCID_DATAARRAY, TRUE);
        for (auto iterator = dataArrays.Begin(); iterator != dataArrays.End(); ++iterator) {
            auto *array = static_cast<CKDataArray *>(*iterator);
            if (!array || !CKIsChildClassOf(array, CKCID_DATAARRAY))
                continue;
            const char *rawName = array->GetName();
            const std::string name = rawName ? rawName : "";
            if (!ContainsCaseInsensitive(name, filter))
                continue;
            ++arrays;
            std::ostringstream line;
            line << "array " << name << " rows=" << array->GetRowCount()
                 << " cols=" << array->GetColumnCount();
            bml->SendIngameMessage(line.str().c_str());
            const int rows = std::min(array->GetRowCount(), 48);
            const int cols = std::min(array->GetColumnCount(), 4);
            for (int row = 0; row < rows; ++row) {
                std::ostringstream entry;
                entry << "  row " << row;
                for (int col = 0; col < cols; ++col) {
                    if (CKObject *element = array->GetElementObject(row, col))
                        entry << " c" << col << "obj=" << (element->GetName() ? element->GetName() : "?");
                    const int size = array->GetElementStringValue(row, col, nullptr, 0);
                    if (size > 0) {
                        std::string value(static_cast<std::size_t>(size), '\0');
                        array->GetElementStringValue(row, col, value.data(), size);
                        entry << " c" << col << "=" << value.c_str();
                    }
                }
                bml->SendIngameMessage(entry.str().c_str());
            }
        }

        std::ostringstream summary;
        summary << "BML browser test objects matched=" << matched
                << " shown=" << shown << " groups=" << groups
                << " arrays=" << arrays;
        bml->SendIngameMessage(summary.str().c_str());
    }

private:
    ModContext &m_Context;
};

void DumpParameterValues(IBML *bml, CKBehavior *behavior, int depth, int &shown) {
    auto dumpOne = [&](const char *kind, int index, CKParameter *param) {
        if (!param || shown >= 400)
            return;
        const int size = param->GetStringValue(nullptr);
        if (size <= 0)
            return;
        std::string value(static_cast<std::size_t>(size), '\0');
        param->GetStringValue(value.data());
        std::ostringstream line;
        for (int j = 0; j < depth; ++j)
            line << "  ";
        line << kind << "[" << index << "] " << (param->GetName() ? param->GetName() : "?")
             << "=" << value.c_str();
        bml->SendIngameMessage(line.str().c_str());
        ++shown;
    };
    const int inCount = behavior->GetInputParameterCount();
    for (int i = 0; i < inCount; ++i) {
        CKParameterIn *input = behavior->GetInputParameter(i);
        dumpOne("in", i, input ? input->GetRealSource() : nullptr);
    }
    const int localCount = behavior->GetLocalParameterCount();
    for (int i = 0; i < localCount; ++i)
        dumpOne("setting", i, behavior->GetLocalParameter(i));
}

void DumpBehaviorTree(IBML *bml, CKBehavior *behavior, int depth, int maxDepth, int &shown) {
    if (!behavior || depth > maxDepth || shown >= 400)
        return;
    CKBehaviorPrototype *prototype = behavior->GetPrototype();
    const char *protoName = prototype && prototype->GetName() ? prototype->GetName()
                          : behavior->GetName() ? behavior->GetName() : "?";
    std::ostringstream line;
    for (int i = 0; i < depth; ++i)
        line << "  ";
    line << "- " << protoName;
    if (behavior->GetName() && prototype && behavior->GetName() != prototype->GetName())
        line << " [" << behavior->GetName() << "]";
    bml->SendIngameMessage(line.str().c_str());
    ++shown;
    if (depth <= 2)
        DumpParameterValues(bml, behavior, depth + 1, shown);
    if (shown >= 1600)
        return;
    const int subCount = behavior->GetSubBehaviorCount();
    for (int i = 0; i < subCount; ++i)
        DumpBehaviorTree(bml, behavior->GetSubBehavior(i), depth + 1, maxDepth, shown);
}

class ScriptCommand final : public ICommand {
public:
    explicit ScriptCommand(ModContext &context) : m_Context(context) {}

    std::string GetName() override { return "bml-test-script"; }
    std::string GetAlias() override { return {}; }
    std::string GetDescription() override { return "Dump a behavior script tree for browser verification"; }
    bool IsCheat() override { return false; }
    const std::vector<std::string> GetTabCompletion(
        IBML *, const std::vector<std::string> &) override { return {}; }

    void Execute(IBML *bml, const std::vector<std::string> &args) override {
        const std::string scriptName = args.size() >= 2 ? args[1] : std::string();
        const std::string nodeFilter = args.size() >= 3 ? args[2] : std::string();
        CKContext *context = m_Context.GetCKContext();
        if (!context) {
            bml->SendIngameMessage("BML browser test script: no CK context");
            return;
        }
        int shown = 0;
        const int count = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
        CK_ID *ids = context->GetObjectsListByClassID(CKCID_BEHAVIOR);
        for (int i = 0; ids && i < count; ++i) {
            auto *behavior = static_cast<CKBehavior *>(context->GetObject(ids[i]));
            if (!behavior || behavior->GetType() != CKBEHAVIORTYPE_SCRIPT)
                continue;
            const char *name = behavior->GetName();
            if (!name || !ContainsCaseInsensitive(name, scriptName))
                continue;
            std::ostringstream header;
            header << "BML browser test script: " << name;
            bml->SendIngameMessage(header.str().c_str());
            if (nodeFilter.empty()) {
                DumpBehaviorTree(bml, behavior, 1, 4, shown);
            } else {
                DumpMatchingSubtrees(bml, behavior, nodeFilter, shown);
            }
            if (shown >= 400)
                break;
        }
        std::ostringstream summary;
        summary << "BML browser test script nodes=" << shown;
        bml->SendIngameMessage(summary.str().c_str());
    }

private:
    static bool DumpMatchingSubtrees(IBML *bml, CKBehavior *behavior, const std::string &filter, int &shown) {
        if (!behavior || shown >= 400)
            return false;
        const char *name = behavior->GetName();
        CKBehaviorPrototype *prototype = behavior->GetPrototype();
        const char *protoName = prototype && prototype->GetName() ? prototype->GetName() : "";
        if ((name && ContainsCaseInsensitive(name, filter)) ||
            (protoName && ContainsCaseInsensitive(protoName, filter))) {
            DumpBehaviorTree(bml, behavior, 1, 8, shown);
            return true;
        }
        const int subCount = behavior->GetSubBehaviorCount();
        for (int i = 0; i < subCount; ++i) {
            if (DumpMatchingSubtrees(bml, behavior->GetSubBehavior(i), filter, shown))
                return true;
        }
        return false;
    }

private:
    ModContext &m_Context;
};

class TeleportCommand final : public ICommand {
public:
    explicit TeleportCommand(ModContext &context) : m_Context(context) {}

    std::string GetName() override { return "bml-test-teleport"; }
    std::string GetAlias() override { return {}; }
    std::string GetDescription() override { return "Move a 3D object for browser verification"; }
    bool IsCheat() override { return false; }
    const std::vector<std::string> GetTabCompletion(
        IBML *, const std::vector<std::string> &) override { return {}; }

    void Execute(IBML *bml, const std::vector<std::string> &args) override {
        if (args.size() < 6) {
            bml->SendIngameMessage("BML browser test teleport: usage: bml-test-teleport <name> <x> <y> <z>");
            return;
        }
        CKContext *context = m_Context.GetCKContext();
        CK3dEntity *entity = context ? static_cast<CK3dEntity *>(context->GetObjectByNameAndClass(
                const_cast<CKSTRING>(args[1].c_str()), CKCID_3DENTITY)) : nullptr;
        if (!entity) {
            bml->SendIngameMessage("BML browser test teleport: object not found");
            return;
        }
        VxVector position(0.0f, 0.0f, 0.0f);
        position.x = std::strtof(args[2].c_str(), nullptr);
        position.y = std::strtof(args[3].c_str(), nullptr);
        position.z = std::strtof(args[4].c_str(), nullptr);
        entity->SetPosition(&position);
        bml->SendIngameMessage("BML browser test teleport: moved object");
    }

private:
    ModContext &m_Context;
};

std::vector<std::shared_ptr<ICommand>> CreateCommands(ModContext &context) {
    return {std::make_shared<Command>(context), std::make_shared<ObjectCommand>(context),
            std::make_shared<ScriptCommand>(context), std::make_shared<TeleportCommand>(context)};
}
}

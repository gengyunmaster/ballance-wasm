#include "IVP/Runtime.h"

#include "CK3dEntity.h"
#include "CKGlobals.h"
#include "CKIpionManager.h"

namespace BML::IVP {
struct Runtime::Storage {
    explicit Storage(CKContext *context) : Context(context) {}

    CKIpionManager *Manager() const {
        return Context ? CKIpionManager::GetManager(Context) : nullptr;
    }

    CK3dEntity *ValidateEntity(CK3dEntity *candidate) const {
        if (!Context || !candidate)
            return nullptr;
        const auto &entities = Context->GetObjectListByType(CKCID_3DENTITY, TRUE);
        for (auto iterator = entities.Begin(); iterator != entities.End(); ++iterator) {
            if (static_cast<CKObject *>(*iterator) != candidate)
                continue;
            auto *entity = CK3dEntity::Cast(*iterator);
            return entity && !entity->IsToBeDeleted() &&
                   CKGetObject(Context, entity->GetID()) == entity ? entity : nullptr;
        }
        return nullptr;
    }

    ::PhysicsObject *Resolve(CK3dEntity *entity) const {
        CK3dEntity *valid = ValidateEntity(entity);
        CKIpionManager *manager = Manager();
        return valid && manager ? manager->GetPhysicsObject(valid) : nullptr;
    }

    CKContext *Context = nullptr;
};

Runtime::Runtime(CKContext *context) noexcept : m_Storage(new(std::nothrow) Storage(context)) {}
Runtime::~Runtime() = default;

int Runtime::ReadApiInfo(BML_IvpApiInfo &out) noexcept {
    if (!m_Storage)
        return BML_ERROR_OUT_OF_MEMORY;
    out = {};
    out.Capabilities = BML_IVP_CAP_NATIVE_OBJECTS;
    out.Architecture = BML_IVP_ARCH_WASM32;
    out.AbiRevision = BML_IVP_ABI_REVISION;
    return BML_OK;
}

int Runtime::GetManager(uintptr_t &out) noexcept {
    out = 0;
    if (!m_Storage)
        return BML_ERROR_OUT_OF_MEMORY;
    CKIpionManager *manager = m_Storage->Manager();
    if (!manager)
        return BML_ERROR_UNAVAILABLE;
    out = reinterpret_cast<uintptr_t>(manager);
    return BML_OK;
}

int Runtime::GetEnvironment(uintptr_t &out) noexcept {
    out = 0;
    uintptr_t managerAddress = 0;
    const int status = GetManager(managerAddress);
    if (status != BML_OK)
        return status;
    auto *environment = reinterpret_cast<CKIpionManager *>(managerAddress)->GetEnvironment();
    if (!environment)
        return BML_ERROR_UNAVAILABLE;
    out = reinterpret_cast<uintptr_t>(environment);
    return BML_OK;
}

int Runtime::GetPhysicsObject(CK3dEntity *entity, uintptr_t &out) noexcept {
    out = 0;
    if (!m_Storage)
        return BML_ERROR_OUT_OF_MEMORY;
    if (!entity)
        return BML_ERROR_INVALID_PARAMETER;
    ::PhysicsObject *object = m_Storage->Resolve(entity);
    if (!object)
        return BML_ERROR_NOT_FOUND;
    out = reinterpret_cast<uintptr_t>(object);
    return BML_OK;
}

int Runtime::GetRealObject(CK3dEntity *entity, uintptr_t &out) noexcept {
    out = 0;
    if (!m_Storage)
        return BML_ERROR_OUT_OF_MEMORY;
    if (!entity)
        return BML_ERROR_INVALID_PARAMETER;
    ::PhysicsObject *object = m_Storage->Resolve(entity);
    if (!object || !object->m_RealObject)
        return BML_ERROR_NOT_FOUND;
    out = reinterpret_cast<uintptr_t>(object->m_RealObject);
    return BML_OK;
}

int Runtime::GetCore(CK3dEntity *entity, uintptr_t &out) noexcept {
    out = 0;
    uintptr_t realAddress = 0;
    const int status = GetRealObject(entity, realAddress);
    if (status != BML_OK)
        return status;
    auto *core = reinterpret_cast<IVP_Real_Object *>(realAddress)->get_core();
    if (!core)
        return BML_ERROR_UNAVAILABLE;
    out = reinterpret_cast<uintptr_t>(core);
    return BML_OK;
}

int Runtime::GetMaterial(CK3dEntity *entity, uintptr_t &out) noexcept {
    out = 0;
    if (!m_Storage)
        return BML_ERROR_OUT_OF_MEMORY;
    CK3dEntity *valid = m_Storage->ValidateEntity(entity);
    CKIpionManager *manager = m_Storage->Manager();
    if (!valid || !manager)
        return BML_ERROR_OBJECT_INVALID;
    for (int index = 0; index < manager->m_MaterialOwners.len(); ++index) {
        const PhysicsMaterialOwner *owner = manager->m_MaterialOwners.element_at(index);
        if (owner && owner->m_Owner == valid->GetID() && owner->m_Material) {
            out = reinterpret_cast<uintptr_t>(owner->m_Material);
            return BML_OK;
        }
    }
    return BML_ERROR_NOT_FOUND;
}

int Runtime::ResolveSymbol(const char *, uintptr_t &out) noexcept {
    out = 0;
    return BML_ERROR_NOT_IMPLEMENTED;
}

int Runtime::ResolveRva(uint32_t, uintptr_t &out) noexcept {
    out = 0;
    return BML_ERROR_NOT_IMPLEMENTED;
}

uint32_t Runtime::GetSymbolCount() const noexcept {
    return 0;
}

int Runtime::GetSymbol(uint32_t, BML_IvpSymbol &out) const noexcept {
    out = {};
    return BML_ERROR_NOT_FOUND;
}
}

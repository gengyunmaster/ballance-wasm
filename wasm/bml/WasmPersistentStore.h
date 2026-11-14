#ifndef BML_WASM_PERSISTENT_STORE_H
#define BML_WASM_PERSISTENT_STORE_H

#include <string>

// Browser persistence for mod state. The store reconciles the in-memory
// Emscripten files under /ModLoader with the origin's localStorage on every
// call: files missing from the filesystem are restored from storage, and
// files created or modified under /ModLoader are written back. The path
// arguments of Restore/Persist are accepted for call-site readability; the
// synchronization always covers the whole managed tree.
namespace WasmPersistentStore {
bool Restore(const std::string &path);
bool Persist(const std::string &path);
void PersistIfChanged(const std::string &path);
void Synchronize();
}

#endif

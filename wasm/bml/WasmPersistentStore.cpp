#include "WasmPersistentStore.h"

#include <emscripten.h>

#include <cstdlib>
#include <string>

namespace WasmPersistentStore {
namespace {
// The whole store runs in JavaScript: it reconciles the in-memory Emscripten
// files under /ModLoader with the origin's localStorage. Files are stored
// under keys "ballance-wasm:file:<path>" using a text encoding when possible
// and base64 otherwise. A small content cache avoids rewriting unchanged
// entries, and a beforeunload hook flushes pending changes when the page
// reloads or closes.
EM_JS(void, RunStoreSynchronization, (), {
    const prefix = "ballance-wasm:file:";
    const root = "/ModLoader";
    const cache = new Map();
    let initialized = false;

    // Only mod state is persisted: configurations, records, and the small
    // shell/UI state files at the root of /ModLoader. Fonts, maps, and logs
    // are static or disposable and must not fill the storage quota. Custom
    // maps are kept in IndexedDB instead (see game.html/home.js) and copied
    // into the game filesystem at startup.
    function isPersistedPath(path) {
        if (path.startsWith("/ModLoader/Configs/"))
            return true;
        if (path.startsWith("/ModLoader/Trails/"))
            return true;
        if (!path.startsWith("/ModLoader/") || path.indexOf("/", 11) !== -1)
            return false;
        return /\.(ini|json|history)$/i.test(path);
    }

    function normalize(path) {
        const parts = [];
        for (const part of path.split("/")) {
            if (!part || part === ".")
                continue;
            if (part === "..")
                parts.pop();
            else
                parts.push(part);
        }
        return "/" + parts.join("/");
    }

    function readStored(path) {
        const stored = localStorage.getItem(prefix + path);
        if (stored === null)
            return undefined;
        if (stored.startsWith("text:"))
            return new TextEncoder().encode(stored.slice(5));
        if (stored.startsWith("base64:")) {
            const binary = atob(stored.slice(7));
            const bytes = new Uint8Array(binary.length);
            for (let i = 0; i < binary.length; ++i)
                bytes[i] = binary.charCodeAt(i);
            return bytes;
        }
        return new TextEncoder().encode(stored);
    }

    function encodeStored(bytes) {
        const text = new TextDecoder("utf-8", { fatal: true });
        try {
            const decoded = text.decode(bytes);
            return decoded.includes("\0") ? null : "text:" + decoded;
        } catch {
            return null;
        }
    }

    function writeStored(path, bytes) {
        let value = encodeStored(bytes);
        if (value === null) {
            let binary = "";
            for (let i = 0; i < bytes.length; i += 32768)
                binary += String.fromCharCode(...bytes.subarray(i, i + 32768));
            value = "base64:" + btoa(binary);
        }
        if (localStorage.getItem(prefix + path) !== value) {
            localStorage.setItem(prefix + path, value);
            return true;
        }
        return false;
    }

    function listStoredPaths() {
        const paths = [];
        for (let i = 0; i < localStorage.length; ++i) {
            const key = localStorage.key(i);
            if (key && key.startsWith(prefix))
                paths.push(key.slice(prefix.length));
        }
        return paths;
    }

    function listFiles(dir, out) {
        let entries;
        try {
            entries = FS.readdir(dir);
        } catch {
            return;
        }
        for (const entry of entries) {
            if (entry === "." || entry === "..")
                continue;
            const full = normalize(dir + "/" + entry);
            let stat;
            try {
                stat = FS.stat(full);
            } catch {
                continue;
            }
            if (FS.isDir(stat.mode))
                listFiles(full, out);
            else
                out.push(full);
        }
    }

    function ensureDirectory(path) {
        const parent = path.slice(0, path.lastIndexOf("/"));
        if (parent)
            try {
                FS.mkdirTree(parent);
            } catch {
            }
    }

    function readFileBytes(path) {
        try {
            return FS.readFile(path, { encoding: "binary" });
        } catch {
            return null;
        }
    }

    function bytesEqual(a, b) {
        if (!a || !b || a.length !== b.length)
            return false;
        for (let i = 0; i < a.length; ++i)
            if (a[i] !== b[i])
                return false;
        return true;
    }

    function synchronize() {
        if (typeof FS === "undefined" || typeof localStorage === "undefined")
            return;

        // Restore persisted files that are missing from the file system.
        for (const path of listStoredPaths()) {
            if (!isPersistedPath(path)) {
                localStorage.removeItem(prefix + path);
                continue;
            }
            const bytes = readStored(path);
            if (bytes !== undefined && readFileBytes(path) === null) {
                ensureDirectory(path);
                try {
                    FS.writeFile(path, bytes);
                } catch {
                    continue;
                }
            }
            cache.set(path, bytes);
        }

        // Persist files created or modified under /ModLoader.
        const files = [];
        listFiles(root, files);
        for (const path of files) {
            if (!isPersistedPath(path))
                continue;
            const bytes = readFileBytes(path);
            if (bytes === null)
                continue;
            if (bytesEqual(cache.get(path), bytes))
                continue;
            writeStored(path, bytes);
            cache.set(path, bytes);
        }
    }

    if (!initialized) {
        initialized = true;
        globalThis.addEventListener?.("beforeunload", () => {
            try {
                synchronize();
            } catch {
            }
        });
        globalThis.__ballanceStoreSync = synchronize;
    }

    try {
        synchronize();
    } catch (error) {
        console.warn("Ballance store synchronization failed:", error);
    }
});
}

void Synchronize() {
    RunStoreSynchronization();
}

bool Restore(const std::string &) {
    Synchronize();
    return true;
}

bool Persist(const std::string &) {
    Synchronize();
    return true;
}

void PersistIfChanged(const std::string &) {
    Synchronize();
}
}

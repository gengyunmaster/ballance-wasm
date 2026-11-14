(() => {
    const status = document.getElementById("status");
    const prefix = "ballance-wasm:file:";
    let deferredInstall = null;

    const setStatus = (text) => {
        status.textContent = text;
    };

    window.addEventListener("beforeinstallprompt", (event) => {
        event.preventDefault();
        deferredInstall = event;
        document.getElementById("install").hidden = false;
    });

    document.getElementById("install").addEventListener("click", async () => {
        if (!deferredInstall)
            return;
        deferredInstall.prompt();
        await deferredInstall.userChoice;
        deferredInstall = null;
        document.getElementById("install").hidden = true;
    });

    const collectSaveFiles = () => {
        const files = {};
        for (let index = 0; index < localStorage.length; ++index) {
            const key = localStorage.key(index);
            if (!key || !key.startsWith(prefix))
                continue;
            const stored = localStorage.getItem(key);
            if (stored === null)
                continue;
            if (stored.startsWith("text:"))
                files[key.slice(prefix.length)] = { encoding: "text", data: stored.slice(5) };
            else if (stored.startsWith("base64:"))
                files[key.slice(prefix.length)] = { encoding: "base64", data: stored.slice(7) };
            else
                files[key.slice(prefix.length)] = { encoding: "text", data: stored };
        }
        return files;
    };

    const download = (name, blob) => {
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = name;
        anchor.click();
        setTimeout(() => URL.revokeObjectURL(url), 5000);
    };

    document.getElementById("export").addEventListener("click", () => {
        const files = collectSaveFiles();
        const archive = {
            format: "ballance-wasm-save",
            version: 1,
            exportedAt: new Date().toISOString(),
            files,
        };
        download("ballance-save.json", new Blob([JSON.stringify(archive, null, 2)], { type: "application/json" }));
        setStatus(`Exported ${Object.keys(files).length} file(s).`);
    });

    const applyArchive = (archive) => {
        if (!archive || archive.format !== "ballance-wasm-save" || !archive.files)
            throw new Error("The selected file is not a Ballance save archive.");
        let count = 0;
        for (const [path, entry] of Object.entries(archive.files)) {
            const normalized = String(path).replaceAll("\\", "/").replace(/^\/+/, "/");
            if (entry.encoding === "text")
                localStorage.setItem(prefix + normalized, "text:" + entry.data);
            else if (entry.encoding === "base64")
                localStorage.setItem(prefix + normalized, "base64:" + entry.data);
            else
                continue;
            ++count;
        }
        return count;
    };

    const importFile = document.getElementById("import-file");
    document.getElementById("import").addEventListener("click", () => importFile.click());
    importFile.addEventListener("change", async () => {
        const file = importFile.files[0];
        importFile.value = "";
        if (!file)
            return;
        try {
            const count = applyArchive(JSON.parse(await file.text()));
            setStatus(`Imported ${count} file(s). Open the game to load them.`);
        } catch (error) {
            setStatus(`Import failed: ${error.message}`);
        }
    });

    // Custom maps live in IndexedDB: they are binary files that quickly exceed
    // the localStorage quota, while IndexedDB offers far larger capacity.
    // Both this page and game.html agree on the database layout below.
    const mapsDbName = "ballance-wasm-maps";
    const mapsStoreName = "maps";

    const openMapsDb = () => new Promise((resolve, reject) => {
        const request = indexedDB.open(mapsDbName, 1);
        request.onupgradeneeded = () => {
            if (!request.result.objectStoreNames.contains(mapsStoreName))
                request.result.createObjectStore(mapsStoreName, { keyPath: "name" });
        };
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error);
    });

    const listStoredMaps = async () => {
        const db = await openMapsDb();
        try {
            return await new Promise((resolve, reject) => {
                const request = db.transaction(mapsStoreName).objectStore(mapsStoreName).getAllKeys();
                request.onsuccess = () => resolve(request.result.map(String).sort((a, b) => a.localeCompare(b)));
                request.onerror = () => reject(request.error);
            });
        } finally {
            db.close();
        }
    };

    const putStoredMap = async (name, data) => {
        const db = await openMapsDb();
        try {
            await new Promise((resolve, reject) => {
                const request = db.transaction(mapsStoreName, "readwrite").objectStore(mapsStoreName).put({ name, data });
                request.onsuccess = () => resolve();
                request.onerror = () => reject(request.error);
            });
        } finally {
            db.close();
        }
    };

    const deleteStoredMap = async (name) => {
        const db = await openMapsDb();
        try {
            await new Promise((resolve, reject) => {
                const request = db.transaction(mapsStoreName, "readwrite").objectStore(mapsStoreName).delete(name);
                request.onsuccess = () => resolve();
                request.onerror = () => reject(request.error);
            });
        } finally {
            db.close();
        }
    };

    const mapList = document.getElementById("map-list");
    const mapStatus = document.getElementById("map-status");

    const renderMapList = async () => {
        const names = await listStoredMaps();
        mapList.textContent = "";
        if (names.length === 0) {
            const item = document.createElement("li");
            const empty = document.createElement("span");
            empty.className = "empty";
            empty.textContent = "No custom maps imported yet.";
            item.appendChild(empty);
            mapList.appendChild(item);
            return;
        }
        for (const name of names) {
            const item = document.createElement("li");
            const label = document.createElement("span");
            label.textContent = name;
            const remove = document.createElement("button");
            remove.type = "button";
            remove.textContent = "Delete";
            remove.addEventListener("click", async () => {
                await deleteStoredMap(name);
                mapStatus.textContent = `Deleted "${name}". It disappears from the game after restarting.`;
                await renderMapList();
            });
            item.appendChild(label);
            item.appendChild(remove);
            mapList.appendChild(item);
        }
    };

    // One-time migration of maps saved by the localStorage-based builds.
    (async () => {
        const legacyPrefix = prefix + "/ModLoader/Maps/";
        for (let index = localStorage.length - 1; index >= 0; --index) {
            const key = localStorage.key(index);
            if (!key || !key.startsWith(legacyPrefix))
                continue;
            const stored = localStorage.getItem(key);
            const name = key.slice(legacyPrefix.length);
            if (stored && stored.startsWith("base64:")) {
                const binary = atob(stored.slice(7));
                const bytes = new Uint8Array(binary.length);
                for (let i = 0; i < binary.length; ++i)
                    bytes[i] = binary.charCodeAt(i);
                await putStoredMap(name, bytes);
            }
            localStorage.removeItem(key);
        }
        await renderMapList();
    })();

    const importMapFile = document.getElementById("import-map-file");
    document.getElementById("import-map").addEventListener("click", () => importMapFile.click());
    importMapFile.addEventListener("change", async () => {
        const files = Array.from(importMapFile.files);
        importMapFile.value = "";
        if (!files.length)
            return;
        const existing = new Set(await listStoredMaps());
        existing.add("BML_Example_Level.nmo"); // bundled with the game
        const imported = [];
        const skipped = [];
        const failed = [];
        for (const file of files) {
            const name = file.name;
            if (existing.has(name) || !/\.(nmo|cmo)$/i.test(name)) {
                skipped.push(name);
                continue;
            }
            try {
                const bytes = new Uint8Array(await file.arrayBuffer());
                await putStoredMap(name, bytes);
                existing.add(name);
                imported.push(name);
            } catch (error) {
                failed.push(name);
            }
        }
        await renderMapList();
        const parts = [];
        if (imported.length)
            parts.push(`Imported ${imported.length} map(s): ${imported.join(", ")}`);
        if (skipped.length)
            parts.push(`Skipped ${skipped.length} duplicate or unsupported: ${skipped.join(", ")}`);
        if (failed.length)
            parts.push(`Failed ${failed.length} (storage may be full): ${failed.join(", ")}`);
        if (parts.length)
            mapStatus.textContent = parts.join(". ") + ". New maps appear in the game after restarting.";
    });
})();

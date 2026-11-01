import { createServer } from "node:http";
import { readFile, mkdir } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";

const projectRoot = resolve(import.meta.dirname, "../..");
const webRoot = resolve(projectRoot, "build/wasm-release/stage/Bin");
const outputRoot = resolve(projectRoot, "test-results");
process.env.PLAYWRIGHT_BROWSERS_PATH ??= resolve(projectRoot, ".tools/playwright-browsers");

const { chromium } = await import("playwright");
const mimeTypes = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".wasm": "application/wasm",
    ".data": "application/octet-stream",
};
const server = createServer(async (request, response) => {
    try {
        const pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
        const filePath = resolve(webRoot, `.${pathname === "/" ? "/index.html" : pathname}`);
        if (!filePath.startsWith(`${webRoot}${sep}`)) {
            response.writeHead(403).end();
            return;
        }
        const content = await readFile(filePath);
        response.writeHead(200, {
            "Content-Type": mimeTypes[extname(filePath)] ?? "application/octet-stream",
            "Cross-Origin-Opener-Policy": "same-origin",
            "Cross-Origin-Embedder-Policy": "require-corp",
        });
        response.end(content);
    } catch {
        response.writeHead(404).end();
    }
});
await new Promise((resolveListen) => server.listen(0, "127.0.0.1", resolveListen));
await mkdir(outputRoot, { recursive: true });

const address = server.address();
const browserUrl = process.env.BROWSER_URL ?? `http://127.0.0.1:${address.port}/`;
const browser = await chromium.launch({
    headless: true,
    args: ["--no-sandbox", "--enable-unsafe-swiftshader", "--use-angle=swiftshader"],
});
const page = await browser.newPage({ viewport: { width: 960, height: 720 } });
const messages = [];
const pageErrors = [];
page.on("console", (message) => {
    const text = message.text();
    if (!text.includes("getInternalformatParameter")) {
        messages.push(`${message.type()}: ${text}`);
    }
});
page.on("pageerror", (error) => pageErrors.push(error.message));

if (process.env.BROWSER_GL_DIAGNOSTICS === "1") {
    await page.addInitScript(() => {
        const prototype = WebGL2RenderingContext.prototype;
        const original = prototype.drawElementsInstanced;
        prototype.drawElementsInstanced = function (...args) {
            while (this.getError() !== this.NO_ERROR) {}
            original.apply(this, args);
            const error = this.getError();
            if (error !== this.NO_ERROR && !globalThis.__ballanceGlDiagnostic) {
                globalThis.__ballanceGlDiagnostic = true;
                console.warn(`WebGL draw error: ${error}`);
            }
        };
    });
}

await page.addInitScript(() => {
    globalThis.__ballanceLoadedFiles = [];
    window.addEventListener("ballance:object-loaded", (event) => {
        globalThis.__ballanceLoadedFiles.push(event.detail);
    });
});

if (process.env.BROWSER_MODS === "1") {
    await page.addInitScript(() => {
        globalThis.ballanceModule = {
            preRun: [() => {
                const fs = globalThis.Module.FS;
                if (!fs) {
                    throw new Error("Emscripten FS is unavailable for mod test configuration");
                }
                fs.mkdirTree("/ModLoader/Configs");
                fs.writeFile("/ModLoader/Configs/DeformedWB.cfg", "Misc {\n B Enable 1\n F Extent 0.35\n}\n");
                fs.writeFile("/ModLoader/Configs/CameraUtilities.cfg", "Camera {\n B Enable 1\n}\n");
            }],
        };
    });
}

const startupTimeout = Number(process.env.BROWSER_STARTUP_TIMEOUT ?? 120000);

const captureVisibleFrame = async (filename) => {
    const frame = await page.screenshot({ path: resolve(outputRoot, filename) });
    const visibleRatio = await page.evaluate(async (encoded) => {
        const image = new Image();
        image.src = `data:image/png;base64,${encoded}`;
        await image.decode();
        const surface = document.createElement("canvas");
        surface.width = image.naturalWidth;
        surface.height = image.naturalHeight;
        const context = surface.getContext("2d");
        context.drawImage(image, 0, 0);
        const pixels = context.getImageData(0, 0, surface.width, surface.height).data;
        let visiblePixels = 0;
        for (let index = 0; index < pixels.length; index += 4) {
            if (pixels[index] + pixels[index + 1] + pixels[index + 2] > 120) {
                ++visiblePixels;
            }
        }
        return visiblePixels / (pixels.length / 4);
    }, frame.toString("base64"));
    return visibleRatio > 0.1 ? frame : null;
};

const waitForVisibleFrame = async (filename) => {
    const deadline = Date.now() + Number(process.env.BROWSER_SCENE_TIMEOUT ?? 60000);
    while (Date.now() < deadline) {
        const frame = await captureVisibleFrame(filename);
        if (frame) {
            return frame;
        }
        await page.waitForTimeout(1000);
    }
    throw new Error(`The game scene did not become visible: ${filename}`);
};

try {
    await page.goto(browserUrl, { waitUntil: "load", timeout: startupTimeout });
    const startupOnly = process.env.BROWSER_STARTUP_ONLY === "1";
    if (startupOnly) {
        await page.waitForTimeout(Number(process.env.BROWSER_PROBE_DELAY ?? 5000));
    } else {
        await page.waitForFunction(() => document.getElementById("status").hidden, null, { timeout: startupTimeout });
        await page.waitForTimeout(30000);
    }
    if (startupOnly) {
        await page.screenshot({ path: resolve(outputRoot, "ballance-probe.png") });
    } else {
        const selectedLevel = Number(process.env.BROWSER_LEVEL ?? 1);
        if (!Number.isInteger(selectedLevel) || selectedLevel < 1 || selectedLevel > 13) {
            throw new Error("BROWSER_LEVEL must be an integer from 1 to 13");
        }
        if (process.env.BROWSER_FULLSCREEN === "1") {
            await page.setViewportSize({ width: 1440, height: 1080 });
            await page.locator("canvas").click({ position: { x: 10, y: 10 } });
            await page.evaluate(() => document.getElementById("canvas").requestFullscreen());
            await page.waitForFunction(() => document.fullscreenElement === document.getElementById("canvas"), null, { timeout: 10000 });
            await page.waitForTimeout(3000);
        }
        const clickGamePoint = async (x, y) => {
            const bounds = await page.locator("canvas").boundingBox();
            await page.locator("canvas").click({
                position: { x: x * bounds.width / 640, y: y * bounds.height / 480 },
                delay: 150,
            });
        };
        const menu = await page.screenshot({ path: resolve(outputRoot, "ballance-menu.png") });
        await clickGamePoint(320, 93.333);
        await page.waitForTimeout(2000);
        const levelButtonY = Number(process.env.BROWSER_LEVEL_Y ?? 38 + (selectedLevel - 1) * 28.6667);
        await clickGamePoint(320, levelButtonY);
        const selectedLevelPath = `/level/level_${String(selectedLevel).padStart(2, "0")}`;
        await page.waitForFunction((levelPath) => globalThis.__ballanceLoadedFiles.some((path) => path.toLowerCase().includes(levelPath)), selectedLevelPath, { timeout: 20000 });
        await page.waitForTimeout(Number(process.env.BROWSER_LEVEL_DELAY ?? 60000));
        const level = await waitForVisibleFrame("ballance-level.png");
        await page.keyboard.down("ArrowUp");
        await page.waitForTimeout(Number(process.env.BROWSER_KEY_HOLD_MS ?? 5000));
        await page.keyboard.up("ArrowUp");
        const movement = await waitForVisibleFrame("ballance-movement.png");
        if (level.equals(menu) || movement.equals(level)) {
            throw new Error("The rendered scene did not change after menu interaction and keyboard input");
        }
        if (process.env.BROWSER_MODS === "1") {
            const cameraBefore = await waitForVisibleFrame("ballance-mod-camera-before.png");
            await page.keyboard.down("q");
            await page.waitForTimeout(1000);
            await page.keyboard.up("q");
            const cameraAfter = await waitForVisibleFrame("ballance-mod-camera-after.png");
            if (cameraAfter.equals(cameraBefore)) {
                throw new Error("CameraUtilities did not change the rendered scene after the Q key was held");
            }
        }
    }
    if (!startupOnly) {
        const levelEvents = ["PreLoadLevel", "PostLoadLevel", "StartLevel"];
        let previousEventIndex = -1;
        for (const levelEvent of levelEvents) {
            const eventIndex = messages.findIndex((message) => message.includes(`Game event: ${levelEvent}`));
            if (eventIndex <= previousEventIndex) {
                throw new Error(`Game lifecycle events are missing or out of order: ${levelEvents.join(" -> ")}`);
            }
            previousEventIndex = eventIndex;
        }
    }
    if (process.env.BROWSER_MODS === "1") {
        const requiredLogs = [
            "BML+ WASM mod loaded: MapScripts 0.3.14",
            "BML+ WASM mod loaded: DeformedWB 0.3.14",
            "BML+ WASM mod loaded: CameraUtilities 0.3.14",
            "Game event: StartLevel",
        ];
        for (const requiredLog of requiredLogs) {
            if (!messages.some((message) => message.includes(requiredLog))) {
                throw new Error(`Missing static mod runtime log: ${requiredLog}`);
            }
        }
    }
    if (pageErrors.length > 0) {
        throw new Error(pageErrors.join("\n"));
    }
    console.log(messages.join("\n"));
    if (startupOnly) {
        console.log("Loaded objects:", await page.evaluate(() => globalThis.__ballanceLoadedFiles));
    }
    console.log(`Browser ${startupOnly ? "startup" : "menu interaction and rendering"} checks passed.`);
} catch (error) {
    console.error(messages.join("\n"));
    console.error(pageErrors.join("\n"));
    console.error("Loaded objects:", await page.evaluate(() => globalThis.__ballanceLoadedFiles).catch(() => []));
    await page.screenshot({ path: resolve(outputRoot, "ballance-failure.png") }).catch(() => {});
    throw error;
} finally {
    await browser.close();
    await new Promise((resolveClose) => server.close(resolveClose));
}

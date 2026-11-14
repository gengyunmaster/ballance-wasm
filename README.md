# Ballance WebAssembly Player

This project builds a browser-playable version of Ballance from the Ballanced reverse-engineering effort. It uses the original game scripts and assets, Emscripten, SDL3, and the bgfx WebGL2 renderer. The resulting image serves a self-contained static application through nginx; no application backend is required.

The repository contains only integration code, patches, tests, documentation, and external Git submodule references. It does not contain Ballance assets or vendored upstream source code.

## Current Status

Verified workflows include:

- Building SDL3 and the player from source in a clean Docker build environment.
- Rebuilding from a fresh recursive clone, then exporting and reloading an offline Docker archive.
- Starting the game through a Node-based WebAssembly smoke test during image construction.
- Opening the main menu, selecting levels 1 and 2 by their visible buttons, entering gameplay, and moving the ball in Chromium.
- Collecting score balls, activating checkpoints, and progressing through the sections of a level.
- Automatic render-resolution adaptation: the game renders at the largest 4:3 size that fits the browser viewport (capped at 2560x1920).
- Persisting mod configurations, segment records, and spirit-trail records in the page origin's `localStorage`, and restoring them after a browser reload.
- Exporting and importing the persisted save data as a JSON archive from the home page, including importing custom `.nmo` map files.
- Installing the application as a PWA with offline caching of the full game payload through the service worker.
- Serving the game with the required WebAssembly MIME type and cross-origin isolation headers.
- Running the complete set of 13 official in-tree BML+ mods through a statically linked WebAssembly runtime, including the command bar, HUD, mod menu, custom-map menu, DataShare, the native IVP bridge, and segment/spirit-trail records.

The current renderer is WebGL2 through bgfx. WebGPU selection has not been implemented. macOS Safari, comprehensive audio behavior, and a complete playthrough of level 13 have not been verified.

## Requirements

- Docker with network access to GitHub and the package repositories used by the build image.
- Enough disk space and memory for the Emscripten toolchain, SDL3, bgfx, and game assets.
- A legally obtained Windows Ballance installation. Its files are used only on your machine and must not be committed.

Docker is the only build requirement. You do not need to install Emscripten, CMake, Ninja, SDL3, or the font package on the host.

## Prepare the Game Assets

Place the Windows game installation in a directory named `Ballance` at the repository root. The directory must contain at least:

```text
Ballance/
├── base.cmo
├── Database.tdb
├── Textures/
├── Sounds/
├── Text/
└── 3D Entities/
```

`Ballance/` and `Ballance.7z` are ignored by Git. Do not move these assets into the Docker build context under another name, and do not publish the resulting image or an archive containing them.

The build packages `base.cmo`, `Database.tdb`, and the four asset directories above. Liberation Sans is supplied by the build image and is bundled with its license for browser text rendering.

## Obtain the Source

Clone the repository and all nested submodules:

```bash
git clone --recurse-submodules https://github.com/gengyunmaster/ballance-wasm.git ballance-wasm
cd ballance-wasm
```

For an existing checkout:

```bash
git submodule update --init --recursive
```

The `upstream/` directory is ignored to prevent accidental vendoring of other authors' code. The Ballanced and BML+ checkouts are nevertheless required: `.gitmodules` and the Git links pin their exact revisions. Nested submodules, including `Source/Managers/SdlInputManager`, must also be initialized.

After recursive submodule initialization, no additional source checkouts or integration files need to be downloaded manually. Place your legally obtained `Ballance` directory in this checkout before building.

## Build the Docker Image

Run the standard build:

```bash
docker build -t ballance-wasm:latest .
```

The builder performs all of the following steps:

1. Starts from `emscripten/emsdk:6.0.13`.
2. Installs Git, Ninja, Python, and Liberation Sans.
3. Downloads and builds SDL3 3.4.14 with pthread support.
4. Applies the local patches to the pinned upstream sources.
5. Configures and builds the player for WebAssembly with WebGL2 and pthreads.
6. Packages the legally supplied assets and font into `Player.data`.
7. Stages `Player.js`, `Player.wasm`, `Player.data`, configuration, and the browser pages.
8. Runs `scripts/test-wasm.sh` to confirm that the player reaches its playing state.
9. Copies the staged application into `nginx:1.29-alpine`, together with `BallanceSetup.exe` when that installer is present in the build context; the home page only offers the download when the file is served.

No host build products or cache-mounted dependency directories are used. For a clean rebuild that also ignores Docker's normal layer cache:

```bash
docker build --no-cache -t ballance-wasm:latest .
```

Network access is required because SDL3 and upstream dependency archives are downloaded during the build. A failed download or an uninitialized submodule must be fixed before retrying; it is not a reason to substitute different versions.

To reduce parallel compilation on a smaller machine:

```bash
docker build --build-arg BUILD_PARALLELISM=2 -t ballance-wasm:latest .
```

## Run the Game

Start the container:

```bash
docker run --rm --name ballance-wasm -p 8080:80 ballance-wasm:latest
```

Open <http://localhost:8080> in a WebGL2-capable browser. The home page offers two choices: **Play in Browser** opens the game, and **Download Windows Installer** serves the Windows installer (`BallanceSetup.exe`) when it was supplied at image build time. The home page also provides the save-data export/import controls and an **Install App** button while the browser reports the PWA as installable.

Wait for the menu to finish loading, click **START**, and select a level. Use the arrow keys to control the ball. The game renders at the largest 4:3 resolution that fits the browser window; resize the window and reload the page to re-fit.

Stop the container with Ctrl+C. The `--rm` option removes it after it stops. If another container already uses that name, stop and remove that container before starting a new one.

## Export for an Offline Machine

After building and testing the image on a networked machine, export it as a gzip-compressed Docker archive:

```bash
docker save ballance-wasm:latest | gzip -c > ballance-wasm.tar.gz
```

The archive contains nginx, the complete WebAssembly application, and your supplied game assets. It is ignored by Git and must not be published or shared without the necessary rights.

Copy `ballance-wasm.tar.gz` to the offline machine with a USB drive or another local transfer method. Docker must already be installed there; no network connection is required to load or run the image.

Import and run it:

```bash
gzip -dc ballance-wasm.tar.gz | docker load
docker run --rm --name ballance-wasm -p 8080:80 ballance-wasm:latest
```

Then open <http://localhost:8080>. The offline machine does not need the source repository, the original Windows game directory, Emscripten, or Node.js to run this image.

To verify the archive before transferring it:

```bash
gzip -t ballance-wasm.tar.gz
docker load -i ballance-wasm.tar.gz
```

## Browser Verification

The browser tests use Playwright 1.64.1 and real menu interactions rather than image-only startup checks. Node.js 24 is recommended for the versions used during development.

Install the test dependencies and Chromium:

```bash
npm ci
PLAYWRIGHT_BROWSERS_PATH="$PWD/.tools/playwright-browsers" npx playwright install chromium
```

Run the default menu, level 1, rendering, and keyboard test against a running container:

```bash
BROWSER_URL=http://127.0.0.1:8080 npm run test:browser
```

The test drives the game page directly (`game.html` is selected automatically when the URL points at the home page). The full mod runtime needs longer on software-rendered hosts; set `BROWSER_STARTUP_TIMEOUT=300000 BROWSER_SCENE_TIMEOUT=240000` when several minutes of loading are expected.

Run the same flow for level 2:

```bash
BROWSER_URL=http://127.0.0.1:8080 BROWSER_LEVEL=2 npm run test:browser
```

Useful optional settings include:

- `BROWSER_STARTUP_TIMEOUT`: menu startup timeout in milliseconds, default `120000`.
- `BROWSER_SCENE_TIMEOUT`: timeout for a visible gameplay frame, default `60000`.
- `BROWSER_LEVEL_DELAY`: additional delay before waiting for a visible gameplay frame, default `60000`. Set it to `0` on slower software-rendered test hosts to start checking immediately.
- `BROWSER_KEY_HOLD_MS`: arrow-key hold duration, default `5000`. A shorter value such as `1000` can keep the ball near the starting platform.
- `BROWSER_LEVEL`: level number from 1 to 13, default `1`.
- `BROWSER_FULLSCREEN=1`: resize to `1440x1080` and use the browser Fullscreen API before selecting a level.
- `BROWSER_LEVEL_Y`: explicit level-button Y coordinate in game-window units, for viewport-specific investigations.
- `BROWSER_STARTUP_ONLY=1`: perform only the startup probe.
- `BROWSER_GL_DIAGNOSTICS=1`: report the first WebGL draw error.

Screenshots and browser logs are written to the ignored `test-results/` directory. The tests wait for a frame with meaningful visible content so that loading and transition frames are not mistaken for a successful game scene.

To exercise the static BML+ mods with DeformedWB and CameraUtilities enabled for that browser session:

```bash
BROWSER_MODS=1 BROWSER_LEVEL_DELAY=0 BROWSER_KEY_HOLD_MS=1000 npm run test:browser
```

The test injects configuration through Emscripten's `preRun`; it does not change the official defaults in the packaged game. It checks each mod's startup log, the real `StartLevel` script event, gameplay rendering and movement, and CameraUtilities' Q-key camera path.

## BML+ Static Mods

BML+ is pinned at `a63000843f07b3812971b1b1e6c4150d770f0fc8`, the commit for tag `v0.3.14`. All 13 in-tree official mods are integrated: the browser build compiles their sources into `Player.wasm` and loads them through a static registry. It does not load Windows DLL or `.bmodp` packages.

The runtime reuses the upstream `IMod`/`IBML` interfaces and the upstream behavior-plan framework: the game lifecycle hooks (menu, level load/start/reset, checkpoints, extra points, lives) are installed by editing the game's behavior graphs at runtime, and the upstream command bar, HUD, mod menu, custom-map menu, DataShare, timers, and configuration stack drive the mods. WASM-specific services replace the Windows pieces: the browser overlay renders the ImGui interface, the persistent store mirrors mod files to `localStorage`, and the native IVP mod bridges the physics runtime for wasm32. AngelScript and user-supplied native packages remain unsupported.

Configurations are stored under `/ModLoader/Configs` and the log under `/ModLoader/ModLoader.log` in Emscripten's in-memory filesystem. Mod state — configurations under `/ModLoader/Configs`, records under `/ModLoader/Trails`, and the small shell/UI state files at the root of `/ModLoader` — is mirrored into the page origin's `localStorage` and restored when the page loads again, so mod settings and records survive browser reloads and the automatic restarts after exiting a level. The home page can export this data as a single JSON archive and import it again, which also accepts custom `.nmo` map files placed under `/ModLoader/Maps`. Static content such as bundled fonts is never written to `localStorage`.

This is not the complete upstream Windows `BML` target. Dynamic native mod loading (Windows DLLs and `.bmodp` packages) and AngelScript are not provided; the `script` command reports that limitation explicitly. Registration-only APIs that still require unavailable native work log an unsupported warning rather than silently claiming success.

## Static Deployment

The image contains only nginx and the static application. The runtime does not need Node.js, Emscripten, SDL, or any application server.

To deploy the files elsewhere, copy `/usr/share/nginx/html` from the image into your static web root. For example:

```bash
docker create --name ballance-export ballance-wasm:latest
docker cp ballance-export:/usr/share/nginx/html/. ./dist/
docker rm ballance-export
```

Configure the static server to provide:

```text
Content-Type: application/wasm for .wasm files
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

The isolation headers are required for the WebAssembly shared memory used by pthreads. Use HTTPS when deploying on a non-localhost origin. Opening `index.html` through `file://` is not supported.

The supplied nginx configuration is in `docker/nginx.conf`. The application is also an installable PWA: `web/sw.js` precaches the complete game payload on first load (including the packaged `Player.data`), serves responses with the cross-origin isolation headers when answering from the cache, and the home page surfaces the browser's install prompt. Once the first visit has completed, the game can be launched offline.

## Repository Layout

```text
Ballance/                  Local, ignored, legally supplied game assets
upstream/ballanced/       Ignored recursive Ballanced submodule checkout
upstream/ballancemodloaderplus/
                          Ignored recursive BML+ submodule checkout
wasm/bml/                 WASM-specific static mod runtime and integration
patches/                  Local compatibility patches, mirrored by upstream target
scripts/                  Patch, build, and smoke-test scripts
web/                      Home page, game page, service worker, PWA manifest, and icons
docker/                   nginx configuration
tests/browser/            Playwright gameplay and input tests
CMakeLists.txt            Asset packaging and WebAssembly link configuration
CMakePresets.json         Reproducible wasm-release configuration
Dockerfile                Complete build, smoke test, and runtime image
```

## Updating Upstream Changes

Do not rely on an edited submodule working tree as the only copy of a change. Every intentional upstream modification belongs in a patch under `patches/`, with paths relative to its actual upstream repository.

Run the patch step before building:

```bash
scripts/apply-patches.sh
```

The script is idempotent and accepts both pristine and already patched checkouts. It handles nested repositories independently; in particular, `Source/Managers/SdlInputManager` has its own Git root and patch directory.

Ballanced is pinned at `ececf940b3d195b3f95c9c55a6f9dd79e0ac1472`. This revision no longer includes the bgfx rasterizer required by the browser. For WebAssembly builds, CMake restores that component from CKRenderEngine commit `63863ff19e5578864f75b50b41c789372fc2e357` using `FetchContent`, verifies the archive checksum, and applies the compatibility patches from `patches/Source/RenderEngine`. The rasterizer source and generated shader data remain outside the root repository.

After changing an upstream file, regenerate its patch from that repository, for example:

```bash
git -C upstream/ballanced/Source/Managers/SdlInputManager diff -- Mouse.cpp Cursor.cpp \
  > patches/Source/Managers/SdlInputManager/0001-Use-canvas-coordinates-for-browser-mouse.patch
```

Keep the pinned submodule revision unless an upgrade is intentional. Review upstream changes and update affected patches rather than discarding compatibility work.

## Compatibility Notes and Lessons

### Camera initialization

Some scenes initially rendered only the HUD or a black screen because no camera was attached. The browser player restores the scene's starting camera when necessary, or selects the first loaded camera if no starting camera exists.

### Text appearing as white blocks

The font renderer's texture blending was being overwritten by a generic stage-blend state. The patch preserves the font texture's own blend state. Liberation Sans is bundled instead of relying on platform-specific Windows fonts.

### Missing or repeatedly restored textures

WebGL does not expose the native blit capability expected by one upload path. When blit is unavailable, textures are uploaded directly through bgfx rather than repeatedly queued through an unsupported staging path. Texture creation also preserves the source image format, avoiding an incompatible conversion of the game's 24-bit assets to 16-bit video textures.

### Blue wood and brown skies

The WebGL fallback changes some BGRA textures to RGBA. A packed-memory upload path skipped the required red/blue channel conversion, which made wood blue and the sky brown. Conversion now covers contiguous rows, padded rows, generated mipmaps, and the sampler-base texture. The unit tests also distinguish compatible opaque formats from formats that round-trip without conversion.

### Mouse positions not matching visible buttons

SDL reports browser mouse coordinates in CSS pixels, while game logic uses scaled canvas coordinates. SDL's virtual centered-window origin is not the canvas's real position in the page. The browser build uses SDL's already-scaled canvas coordinates, keeps window/client conversion in that coordinate space, and scales click picking when the render size differs from the window size.

The game also hides the native cursor and draws its own. Browser cursor visibility now represents whether the pointer is inside the canvas, so `TT PushButton2` remains interactive instead of rejecting all input.

### Testing fullscreen without opening a menu

`Alt+Enter` is not a neutral fullscreen setup for these tests: Enter can also activate the focused menu command. Browser tests use the Fullscreen API after focusing the canvas, then click the visible menu buttons. This prevents an unintended transition into the level list before the actual test click.

### Menu readiness and black transition frames

Loading `MenuLevel.nmo` does not mean that the menu is immediately interactive, and loading a level file does not guarantee a visible gameplay frame. Tests follow the real two-step menu flow and wait for meaningful rendered content instead of relying only on elapsed time or a black loading frame.

### Threaded WebAssembly in a static page

Pthreads require shared memory and cross-origin isolation. The nginx configuration supplies COOP and COEP on all responses, including the WebAssembly file, and declares the correct `application/wasm` MIME type. The service worker re-applies the isolation headers when it answers from the cache, so offline launches keep working.

### Render resolution and aspect ratio

The game page computes the largest 4:3 render size that fits the browser viewport, honours the device pixel ratio up to 2, caps the result at 2560x1920, and rewrites the packaged `Player.ini` before the engine reads it. The visible canvas is always letterboxed to 4:3 by the page stylesheet, so the picture never stretches.

### Exiting a level

The original title composition cannot be reloaded in-process in this runtime: the physics re-initialization that runs when the engine world resets crashes. `OnExitToTitle` therefore exits to the shell, and the host page restarts into the main menu. The restart reuses the cached game payload (and the service-worker cache after the first visit), so returning to the menu takes noticeably less time than the first load.

## Licensing and Redistribution

Ballance assets remain subject to their original rights and are not included in this repository. Do not commit them, publish a Docker image containing them, or distribute them without the necessary rights.

Ballanced and its nested dependencies remain the work of their respective authors and are covered by their own licenses. The root repository references them through Git submodules and keeps only local patches and integration work. Review those licenses before publishing this integration or its build artifacts.

This repository's original code is licensed under the MIT License.

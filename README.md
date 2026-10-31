# Ballance WebAssembly Player

This project builds a browser-playable version of Ballance from the Ballanced reverse-engineering effort. It uses the original game scripts and assets, Emscripten, SDL3, and the bgfx WebGL2 renderer. The resulting image serves a self-contained static application through nginx; no application backend is required.

The repository contains only integration code, patches, tests, documentation, and an external Git submodule reference. It does not contain Ballance assets or vendored upstream source code.

## Current Status

Verified workflows include:

- Building SDL3 and the player from source in a clean Docker build environment.
- Starting the game through a Node-based WebAssembly smoke test during image construction.
- Opening the main menu, selecting levels 1 and 2 by their visible buttons, entering gameplay, and moving the ball in Chromium.
- Selecting level 2 after resizing and entering fullscreen.
- Serving the game with the required WebAssembly MIME type and cross-origin isolation headers.

The current renderer is WebGL2 through bgfx. WebGPU selection and installable PWA support have not been implemented. macOS Safari, comprehensive audio behavior, and a complete playthrough of level 13 have not been verified.

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

The `upstream/` directory is ignored to prevent accidental vendoring of other authors' code. The Ballanced checkout is nevertheless required: `.gitmodules` and the Git link pin its exact revision. Nested submodules, including `Source/Managers/SdlInputManager`, must also be initialized.

Place your `Ballance` directory in this checkout before building.

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
7. Stages `Player.js`, `Player.wasm`, `Player.data`, configuration, and the browser page.
8. Runs `scripts/test-wasm.sh` to confirm that the player reaches its playing state.
9. Copies the staged application into `nginx:1.29-alpine`.

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

Open <http://localhost:8080> in a WebGL2-capable browser. Wait for the menu to finish loading, click **START**, and select a level. Use the arrow keys to control the ball.

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

The supplied nginx configuration is in `docker/nginx.conf`. The application is static, but it is not yet an installable PWA.

## Repository Layout

```text
Ballance/                  Local, ignored, legally supplied game assets
upstream/ballanced/       Ignored recursive submodule checkout
patches/                  Local compatibility patches, mirrored by upstream target
scripts/                  Patch, build, and smoke-test scripts
web/                      Browser page and runtime configuration
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

WebGL does not expose the native blit capability expected by one upload path. When blit is unavailable, textures are uploaded directly through bgfx rather than repeatedly queued through an unsupported staging path.

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

Pthreads require shared memory and cross-origin isolation. The nginx configuration supplies COOP and COEP on all responses, including the WebAssembly file, and declares the correct `application/wasm` MIME type.

## Licensing and Redistribution

Ballance assets remain subject to their original rights and are not included in this repository. Do not commit them, publish a Docker image containing them, or distribute them without the necessary rights.

Ballanced and its nested dependencies remain the work of their respective authors and are covered by their own licenses. The root repository references them through Git submodules and keeps only local patches and integration work. Review those licenses before publishing this integration or its build artifacts.

This repository's original integration code and documentations are licensed under the MIT License.

(() => {
    const state = {
        canvas: null,
        gl: null,
        program: null,
        vertexBuffer: null,
        indexBuffer: null,
        vao: null,
        textures: new Map(),
        displayLocation: null,
        textureLocation: null,
        swapLocation: null,
        homeParent: null,
        homeNext: null,
    };

    const vertexSource = `#version 300 es
    precision highp float;
    layout(location = 0) in vec2 a_position;
    layout(location = 1) in vec2 a_uv;
    layout(location = 2) in vec4 a_color;
    uniform vec2 u_display;
    out vec2 v_uv;
    out vec4 v_color;
    void main() {
        vec2 position = vec2(a_position.x / u_display.x * 2.0 - 1.0,
                             1.0 - a_position.y / u_display.y * 2.0);
        gl_Position = vec4(position, 0.0, 1.0);
        v_uv = a_uv;
        v_color = a_color.bgra;
    }`;

    const fragmentSource = `#version 300 es
    precision highp float;
    uniform sampler2D u_texture;
    uniform bool u_swapChannels;
    in vec2 v_uv;
    in vec4 v_color;
    out vec4 outputColor;
    void main() {
        vec4 texel = texture(u_texture, v_uv);
        if (u_swapChannels)
            texel = texel.bgra;
        outputColor = v_color * texel;
    }`;

    function compileShader(type, source) {
        const gl = state.gl;
        const shader = gl.createShader(type);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            throw new Error(gl.getShaderInfoLog(shader));
        }
        return shader;
    }

    function createProgram() {
        const gl = state.gl;
        const program = gl.createProgram();
        gl.attachShader(program, compileShader(gl.VERTEX_SHADER, vertexSource));
        gl.attachShader(program, compileShader(gl.FRAGMENT_SHADER, fragmentSource));
        gl.linkProgram(program);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            throw new Error(gl.getProgramInfoLog(program));
        }
        return program;
    }

    function bindInputs(canvas) {
        const forward = (event, wheelX = 0, wheelY = 0) => {
            if (typeof Module?._BMLWasmOverlaySetMouse !== "function")
                return;
            const bounds = canvas.getBoundingClientRect();
            const x = (event.clientX - bounds.left) / bounds.width;
            const y = (event.clientY - bounds.top) / bounds.height;
            Module._BMLWasmOverlaySetMouse(x, y, event.buttons, wheelX, wheelY);
        };
        for (const type of ["pointermove", "pointerdown", "pointerup", "pointercancel"]) {
            canvas.addEventListener(type, (event) => forward(event));
        }
        canvas.addEventListener("pointerleave", () => {
            if (typeof Module?._BMLWasmOverlaySetMouse === "function")
                Module._BMLWasmOverlaySetMouse(-1, -1, 0, 0, 0);
        });
        canvas.addEventListener("wheel", (event) => {
            forward(event, event.deltaX, event.deltaY);
            event.preventDefault();
        }, { passive: false });
        canvas.addEventListener("contextmenu", (event) => event.preventDefault());
    }

    function init() {
        if (state.gl)
            return true;
        const gameCanvas = document.getElementById("canvas");
        if (!gameCanvas)
            return false;
        bindInputs(gameCanvas);
        const overlay = document.createElement("canvas");
        overlay.id = "bml-overlay";
        overlay.setAttribute("aria-hidden", "true");
        Object.assign(overlay.style, {
            position: "fixed",
            pointerEvents: "none",
            zIndex: "1",
        });
        gameCanvas.after(overlay);
        state.homeParent = overlay.parentNode;
        state.homeNext = overlay.nextSibling;
        state.canvas = overlay;
        state.gl = overlay.getContext("webgl2", {
            alpha: true,
            antialias: false,
            depth: false,
            stencil: false,
            premultipliedAlpha: false,
            preserveDrawingBuffer: true,
        });
        if (!state.gl) {
            overlay.remove();
            state.canvas = null;
            return false;
        }

        const gl = state.gl;
        state.program = createProgram();
        state.displayLocation = gl.getUniformLocation(state.program, "u_display");
        state.textureLocation = gl.getUniformLocation(state.program, "u_texture");
        state.swapLocation = gl.getUniformLocation(state.program, "u_swapChannels");
        state.vertexBuffer = gl.createBuffer();
        state.indexBuffer = gl.createBuffer();
        state.vao = gl.createVertexArray();
        gl.bindVertexArray(state.vao);
        gl.bindBuffer(gl.ARRAY_BUFFER, state.vertexBuffer);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 20, 0);
        gl.enableVertexAttribArray(1);
        gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 20, 8);
        gl.enableVertexAttribArray(2);
        gl.vertexAttribPointer(2, 4, gl.UNSIGNED_BYTE, true, 20, 16);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, state.indexBuffer);
        gl.bindVertexArray(null);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.useProgram(state.program);
        gl.uniform1i(state.textureLocation, 0);
        uploadTexture(0, 1, 1, new Uint8Array([255, 255, 255, 255]).byteOffset, 4, 0,
                      new Uint8Array([255, 255, 255, 255]));
        resetRenderState();
        return true;
    }

    function shutdown() {
        if (!state.gl)
            return;
        const gl = state.gl;
        for (const entry of state.textures.values())
            gl.deleteTexture(entry.handle);
        state.textures.clear();
        gl.deleteBuffer(state.vertexBuffer);
        gl.deleteBuffer(state.indexBuffer);
        gl.deleteVertexArray(state.vao);
        gl.deleteProgram(state.program);
        state.canvas?.remove();
        state.canvas = null;
        state.gl = null;
        state.program = null;
        state.vertexBuffer = null;
        state.indexBuffer = null;
        state.vao = null;
    }

    function newFrame(width, height) {
        const gameCanvas = document.getElementById("canvas");
        if (document.fullscreenElement === gameCanvas) {
            if (state.canvas.parentNode !== document.body)
                document.body.appendChild(state.canvas);
        } else if (state.homeParent) {
            state.homeParent.insertBefore(state.canvas, state.homeNext);
        }
        const bounds = gameCanvas.getBoundingClientRect();
        Object.assign(state.canvas.style, {
            left: `${bounds.left}px`,
            top: `${bounds.top}px`,
            width: `${bounds.width}px`,
            height: `${bounds.height}px`,
        });
        state.canvas.width = width;
        state.canvas.height = height;
        state.gl.viewport(0, 0, width, height);
        state.gl.disable(state.gl.SCISSOR_TEST);
        state.gl.clearColor(0, 0, 0, 0);
        state.gl.clear(state.gl.COLOR_BUFFER_BIT);
        state.gl.useProgram(state.program);
        state.gl.uniform2f(state.displayLocation, width, height);
        state.gl.bindVertexArray(state.vao);
    }

    function uploadTexture(id, width, height, pixels, pitch, swapChannels, rawPixels) {
        const gl = state.gl;
        const bytes = rawPixels ?? new Uint8Array(HEAPU8.buffer, pixels, pitch * height);
        let handle = state.textures.get(id)?.handle;
        if (!handle) {
            handle = gl.createTexture();
            gl.bindTexture(gl.TEXTURE_2D, handle);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        } else {
            gl.bindTexture(gl.TEXTURE_2D, handle);
        }
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA,
                      gl.UNSIGNED_BYTE, bytes);
        state.textures.set(id, { handle, swapChannels: swapChannels !== 0 });
    }

    function destroyTexture(id) {
        const entry = state.textures.get(id);
        if (!entry)
            return;
        state.gl.deleteTexture(entry.handle);
        state.textures.delete(id);
    }

    function uploadVertices(data, count) {
        const gl = state.gl;
        gl.bindBuffer(gl.ARRAY_BUFFER, state.vertexBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(HEAPU8.buffer, data, count * 20),
                      gl.STREAM_DRAW);
    }

    function uploadIndices(data, count) {
        const gl = state.gl;
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, state.indexBuffer);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(HEAPU32.buffer, data, count),
                      gl.STREAM_DRAW);
    }

    function setTexture(id) {
        const entry = state.textures.get(id) ?? state.textures.get(0);
        state.gl.activeTexture(state.gl.TEXTURE0);
        state.gl.bindTexture(state.gl.TEXTURE_2D, entry.handle);
        state.gl.uniform1i(state.swapLocation, entry.swapChannels ? 1 : 0);
    }

    function setClipRect(left, top, right, bottom, height) {
        const gl = state.gl;
        const x = Math.max(0, Math.floor(left));
        const y = Math.max(0, Math.floor(height - bottom));
        const width = Math.max(0, Math.ceil(right - left));
        const clipHeight = Math.max(0, Math.ceil(bottom - top));
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(x, y, width, clipHeight);
    }

    function drawElements(count, offset) {
        state.gl.drawElements(state.gl.TRIANGLES, count, state.gl.UNSIGNED_INT, offset);
    }

    function resetRenderState() {
        const gl = state.gl;
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.enable(gl.BLEND);
        gl.blendEquation(gl.FUNC_ADD);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        setTexture(0);
    }

    globalThis.BallanceOverlay = {
        init,
        shutdown,
        newFrame,
        uploadTexture,
        destroyTexture,
        uploadVertices,
        uploadIndices,
        setTexture,
        setClipRect,
        drawElements,
        resetRenderState,
    };
})();

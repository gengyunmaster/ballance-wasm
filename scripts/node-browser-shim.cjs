// A falsy value keeps Node.js on non-browser paths and makes window.matchMedia evaluate to undefined.
globalThis.window = false;

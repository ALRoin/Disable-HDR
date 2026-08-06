/*
 * Vendored copy of the official `kernelsu` npm package (github.com/tiann/KernelSU,
 * published as "kernelsu" on npm), transcribed from the real published source of
 * kernelsu@3.0.2 (verified via https://app.unpkg.com/kernelsu@3.0.2/files/index.js
 * and index.d.ts) rather than reimplemented from memory.
 *
 * Vendored as a plain local ES module - loaded with a relative <script
 * type="module"> import, no bundler and no runtime CDN fetch - on purpose:
 * this page runs inside the Manager app's WebView with exec() access to a
 * root shell, so it should not be pulling script from the network at
 * runtime. Modern (Chromium-based) WebViews support native ES modules, so
 * no build step is needed either.
 *
 * Functions this module doesn't use (spawn, moduleInfo, enableEdgeToEdge,
 * exit) are kept for completeness/fidelity to the real package, not removed.
 *
 * listPackages()/getPackagesInfo() are a newer addition to KernelSU
 * (tiann/KernelSU PRs #2928/#2930); older Manager builds may not implement
 * them natively. index.html only relies on them as an optional enhancement
 * and falls back to exec("pm list packages ...") if they're unavailable.
 */

let callbackCounter = 0;

function getUniqueCallbackName(prefix) {
    return `${prefix}_callback_${Date.now()}_${callbackCounter++}`;
}

export function exec(command, options) {
    if (typeof options === "undefined") {
        options = {};
    }
    return new Promise((resolve, reject) => {
        const callbackFuncName = getUniqueCallbackName("exec");
        function cleanup(name) {
            delete window[name];
        }
        window[callbackFuncName] = (errno, stdout, stderr) => {
            resolve({ errno, stdout, stderr });
            cleanup(callbackFuncName);
        };
        try {
            ksu.exec(command, JSON.stringify(options), callbackFuncName);
        } catch (error) {
            reject(error);
            cleanup(callbackFuncName);
        }
    });
}

function Stdio() {
    this.listeners = {};
}
Stdio.prototype.on = function (event, listener) {
    (this.listeners[event] ??= []).push(listener);
};
Stdio.prototype.emit = function (event, ...args) {
    (this.listeners[event] || []).forEach((listener) => listener(...args));
};

function ChildProcess() {
    this.listeners = {};
    this.stdin = new Stdio();
    this.stdout = new Stdio();
    this.stderr = new Stdio();
}
ChildProcess.prototype.on = function (event, listener) {
    (this.listeners[event] ??= []).push(listener);
};
ChildProcess.prototype.emit = function (event, ...args) {
    (this.listeners[event] || []).forEach((listener) => listener(...args));
};

export function spawn(command, args, options) {
    if (typeof args === "undefined") {
        args = [];
    } else if (!(args instanceof Array)) {
        options = args;
        args = [];
    }
    if (typeof options === "undefined") {
        options = {};
    }

    const child = new ChildProcess();
    const childCallbackName = getUniqueCallbackName("spawn");
    window[childCallbackName] = child;
    function cleanup(name) {
        delete window[name];
    }
    child.on("exit", () => cleanup(childCallbackName));

    try {
        ksu.spawn(command, JSON.stringify(args), JSON.stringify(options), childCallbackName);
    } catch (error) {
        child.emit("error", error);
        cleanup(childCallbackName);
    }
    return child;
}

export function fullScreen(isFullScreen) {
    ksu.fullScreen(isFullScreen);
}

export function enableEdgeToEdge(enable) {
    ksu.enableEdgeToEdge(enable);
}

export function toast(message) {
    ksu.toast(message);
}

export function moduleInfo() {
    return ksu.moduleInfo();
}

/** @returns {string[]} package names. `type` is manager-version-defined (e.g. "user"); unrecognized values are expected to just come back empty rather than throw. */
export function listPackages(type) {
    try {
        return JSON.parse(ksu.listPackages(type));
    } catch (error) {
        return [];
    }
}

/** @returns {{packageName:string, versionName:string, versionCode:number, appLabel:string, isSystem:boolean, uid:number}[]} */
export function getPackagesInfo(packages) {
    try {
        if (typeof packages !== "string") {
            packages = JSON.stringify(packages);
        }
        return JSON.parse(ksu.getPackagesInfo(packages));
    } catch (error) {
        return [];
    }
}

export function exit() {
    ksu.exit();
}

// Local vendored, unminified copy of the official 'kernelsu' npm package
// (github.com/tiann/KernelSU), reconstructed from the verified published
// source (kernelsu@3.0.2) so this WebUI needs no Node/npm build step.

let callbackCounter = 0;
function getUniqueCallbackName(prefix) {
  return `${prefix}_callback_${Date.now()}_${callbackCounter++}`;
}

export function exec(command, options) {
  if (typeof options === 'undefined') options = {};
  return new Promise((resolve, reject) => {
    const callbackFuncName = getUniqueCallbackName('exec');
    function cleanup(name) { delete window[name]; }
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

function Stdio() { this.listeners = {}; }
Stdio.prototype.on = function (event, listener) {
  if (!this.listeners[event]) this.listeners[event] = [];
  this.listeners[event].push(listener);
};
Stdio.prototype.emit = function (event, ...args) {
  if (this.listeners[event]) this.listeners[event].forEach((l) => l(...args));
};

function ChildProcess() {
  this.listeners = {};
  this.stdin = new Stdio();
  this.stdout = new Stdio();
  this.stderr = new Stdio();
}
ChildProcess.prototype.on = Stdio.prototype.on;
ChildProcess.prototype.emit = Stdio.prototype.emit;

export function spawn(command, args, options) {
  if (typeof args === 'undefined') {
    args = [];
  } else if (!(args instanceof Array)) {
    options = args;
    args = [];
  }
  if (typeof options === 'undefined') options = {};

  const child = new ChildProcess();
  const childCallbackName = getUniqueCallbackName('spawn');
  function cleanup(name) { delete window[name]; }
  window[childCallbackName] = child;
  child.on('exit', () => cleanup(childCallbackName));
  try {
    ksu.spawn(command, JSON.stringify(args), JSON.stringify(options), childCallbackName);
  } catch (error) {
    child.emit('error', error);
    cleanup(childCallbackName);
  }
  return child;
}

export function fullScreen(isFullScreen) {
  ksu.fullScreen(isFullScreen);
}

export function enableEdgeToEdge(enabled) {
  ksu.enableEdgeToEdge(enabled);
}

export function toast(message) {
  try {
    ksu.toast(message);
  } catch (e) {
    // ksu bridge unavailable (e.g. previewing outside KernelSU Manager) -- ignore.
  }
}

export function moduleInfo() {
  return ksu.moduleInfo();
}

// type: 'user' | 'system' | undefined (all)
export function listPackages(type) {
  try {
    return JSON.parse(ksu.listPackages(type));
  } catch (e) {
    return [];
  }
}

export function getPackagesInfo(pkgs) {
  try {
    const arg = typeof pkgs === 'string' ? pkgs : JSON.stringify(pkgs);
    return JSON.parse(ksu.getPackagesInfo(arg));
  } catch (e) {
    return [];
  }
}

export function exit() {
  ksu.exit();
}

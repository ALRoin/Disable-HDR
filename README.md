# HDR Block — Zygisk module

Forces `Display.getHdrCapabilities()` / `Display.isHdr()` to report "no HDR
support" in target app processes, without requiring LSPosed. Pure Zygisk,
same architecture as HMA-OSS's Zygisk-only branch.

## How it works

1. **`native/`** — a Zygisk `.so` (C++, using Magisk's public `zygisk.hpp`
   API). In `preAppSpecialize` it checks the process name against your
   target list. In `postAppSpecialize` it asks its **companion process**
   (root-privileged, runs outside the app sandbox) for the hook payload.
2. The companion streams `classes.dex` (built from `hookdex/`) over a
   socket into the app process.
3. The native code loads that dex with `dalvik.system.InMemoryDexClassLoader`
   and calls `com.hdrblock.hook.HdrHook.install(appClassLoader)`.
4. `HdrHook` uses **YAHFA** (the same ART-hooking core LSPosed itself is
   built on) to replace `Display.getHdrCapabilities()` and `Display.isHdr()`
   with versions that always report no HDR support.

```
native/    -> zygisk/<abi>.so   (loaded into Zygote by Magisk/KernelSU)
hookdex/   -> classes.dex        (loaded into each target app by native/)
flashable_module/ -> module.prop, customize.sh, META-INF (Magisk/KernelSU zip skeleton)
package.sh -> builds both + zips them into dist/hdr_block.zip
```

## Choosing which apps get hooked

Edit `native/jni/hdr_block.cpp`:

```cpp
static const char *kTargetPackages[] = {
    "com.google.android.youtube",
    "com.netflix.mediaclient",
};
```

Leave the array empty (default) to hook every app process — i.e. HDR
reporting is disabled system-wide for all apps.

## Build requirements

- Android Studio (or standalone Android SDK + NDK, r21+)
- `ANDROID_HOME` / `ANDROID_NDK_HOME` set in your shell
- A KernelSU (or Magisk) device with Zygisk enabled, **Android 16 / API 36**

## Build & flash

```bash
git clone <this project>
cd hdr-block-zygisk
./package.sh
```

This produces `dist/hdr_block.zip`. Flash it via KernelSU Manager (or
Magisk Manager) → Modules → Install from storage → reboot.

## Verifying it worked

```bash
adb shell dumpsys SurfaceFlinger | grep -i hdr
```

Then open a target app (e.g. YouTube) and check whether it still offers
an HDR toggle / plays HDR streams. Framework-level API calls inside that
app's process should now report zero supported HDR types, regardless of
what the HWC/panel underneath actually reports.

## Building via GitHub Actions (no local SDK/NDK needed)

The project includes `.github/workflows/build.yml`, which installs the
Android SDK + NDK on GitHub's runners and produces `dist/hdr_block.zip`
as a downloadable artifact — you never need to install anything locally.

1. **Create a new GitHub repo** (can be private) and push this project:
   ```bash
   cd hdr-block-zygisk
   git init
   git add .
   git commit -m "Initial commit"
   git branch -M main
   git remote add origin https://github.com/<your-username>/<repo-name>.git
   git push -u origin main
   ```
2. **Go to the repo on GitHub → Actions tab.** The workflow runs
   automatically on every push to `main`. If it doesn't show up, click
   **"I understand my workflows, go ahead and enable them"** (GitHub
   disables Actions by default on some account types until you confirm).
3. **Wait for the run to finish** (a few minutes — first run is slower
   since it has to download the SDK/NDK components).
4. **Download the build**: open the finished workflow run → scroll to
   **Artifacts** at the bottom → download `hdr_block-module.zip`. Inside
   it is `hdr_block.zip` — that's the actual flashable file.
5. **Trigger a rebuild anytime** without pushing new code: Actions tab →
   "Build HDR Block Zygisk Module" → **Run workflow** button (this works
   because of the `workflow_dispatch` trigger in the yaml).

Copy `hdr_block.zip` to your phone (e.g. via `adb push`, a cloud drive,
or GitHub's mobile app) and flash it from KernelSU Manager → Modules →
Install from storage.

**If the build fails in Actions**, click into the failed step's log —
most likely culprits are: an NDK API-level mismatch (try changing
`ndk-version: r27c` in the workflow to whatever's current), or a YAHFA
Maven Central version that no longer resolves (check
[Maven Central](https://central.sonatype.com/artifact/io.github.pagalaxylab/yahfa)
for the latest version and update `hookdex/build.gradle.kts`).

## Known limitations — read before flashing

- **ART/ArtMethod internals are Android-version-specific.** YAHFA
  (`io.github.pagalaxylab:yahfa`) is a community-maintained hooking core.
  As of writing, its officially tested range tops out around Android 12;
  API 36 (Android 16) support depends on whichever fork/version you pin
  in `hookdex/build.gradle.kts`. **You should expect to test on your
  actual device and, if hooking fails, swap in a more current maintained
  fork** (search for forks that explicitly claim Android 14/15/16
  support — LSPosed's own bundled hooking core is the most actively
  updated one, since LSPosed's survival depends on staying current).
- If `HdrHook.install()` throws (e.g. because the `HdrCapabilities`
  constructor signature changed, or the reflective field layout doesn't
  match your OEM's framework build), it fails silently by design — the
  app keeps running normally with HDR untouched, it just won't be
  blocked. Check `adb logcat -s HdrBlockHook HdrBlockZygisk` after
  opening a target app to see whether the hook actually installed.
- This only affects what apps *see* via the public API. It does not
  touch the underlying HWC/panel capability (`HWC Support: hdr10=true...`
  in your `dumpsys` output) — that's compiled into the vendor display HAL
  and isn't reachable from userspace/Zygisk at all.
- OEM (MIUI/HyperOS, One UI, ColorOS, etc.) framework builds sometimes
  add extra HDR-related methods/fields beyond stock AOSP `Display`. If
  apps still detect HDR after this, `adb shell dumpsys SurfaceFlinger`
  and framework decompilation of your specific ROM's `framework.jar`
  will tell you what else to add to `HdrHook.java`.

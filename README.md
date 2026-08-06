# Disable HDR (KernelSU + Zygisk)

Hides HDR support from apps so they fall back to SDR playback, on Android 16.
A rewrite of an earlier version of this module, using [LSPlant](https://github.com/LSPosed/LSPlant)
instead of YAHFA for the ART hooking, since YAHFA's own upstream only
documents testing up to Android 12 and LSPlant is the actively-maintained
engine LSPosed itself is built on.

## What it actually hooks

Three framework methods, unconditionally:

- `android.view.Display$HdrCapabilities#getSupportedHdrTypes()` → `int[0]`
- `android.view.Display#isHdr()` → `false`
- `android.media.MediaCodecInfo#getCapabilitiesForType(String)` → the real
  `CodecCapabilities`, with every HDR-only profile (HDR10, HDR10Plus, every
  Dolby Vision profile, VP9/AV1/APV HDR variants, ...) removed from its
  `profileLevels`. Plain SDR profiles - including 10-bit SDR, a different
  thing from HDR - are left exactly as reported.

Almost everything that decides "should I request an HDR stream/surface"
ultimately calls through one of the first two, so spoofing them at the
source is more robust than trying to reconstruct a fake `HdrCapabilities`
object with a guessed constructor (see [What changed](#what-changed-from-the-original)
below for why that mattered). The third covers apps that skip `Display`
entirely and probe decoder profiles directly instead (some ExoPlayer-based
players do this) - see [Known limitations](#known-limitations) for what's
still genuinely out of reach of any client-side hook.

The HDR/non-HDR distinction for the third hook is derived at runtime by
reflecting over `MediaCodecInfo.CodecProfileLevel`'s own declared constants
and pattern-matching their *names*, rather than a hardcoded table of
integer values - Android keeps adding new profile constants (APV's HDR
variants are new as of API 36) and a fixed table would silently miss
whatever gets added next.

## Is this an LSPosed module? No.

LSPlant is the hooking *engine* LSPosed is built on, released separately as
a standalone library. Using it doesn't mean installing LSPosed - this module
links LSPlant and [Dobby](https://github.com/jmpews/Dobby) (its inline-hook
backend) directly into its own `.so`, the same way you'd link any native
library. LSPlant is built from source (see the top of
`native/src/main/cpp/CMakeLists.txt` for why - short version: its published
Maven/Prefab package is a shared library, which doesn't work for a Zygisk
module, and cost a full afternoon of bug-report spelunking to track down the
first time); Dobby still comes from its prebuilt Maven/Prefab package, which
*does* publish a genuine static archive. Either way, there's no LSPosed
manager, no Xposed module registration, nothing to install beyond this
module itself.

[HMA-OSS](https://github.com/frknkrc44/HMA-OSS), which was given to me as a
reference for "a Zygisk module, not an LSPosed module," makes the same
point a different way: it's written in Kotlin against a library called
[ZygoteLoader](https://github.com/Kr328/ZygoteLoader) that bundles its own
native Zygisk bootstrap + hook engine, so the app developer never writes
C++ at all. Same underlying idea (a self-contained Zygisk `.so`, no LSPosed
dependency) implemented differently - HMA-OSS's Gradle plugin comes from a
third-party Maven repo I can't fully audit or guarantee is still online
years from now, so I built this one directly against LSPlant + Dobby's own
official Maven Central packages instead. Both are legitimate ways to get to
the same place.

## Prerequisites

1. **KernelSU** itself. Note that KernelSU has **no built-in Zygisk
   support** - confirmed on [kernelsu.org's own FAQ](https://kernelsu.org).
2. **A Zygisk API provider** on top of it - install
   [ZygiskNext](https://github.com/Dr-TSNG/ZygiskNext) or
   [ReZygisk](https://github.com/PerformanC/ReZygisk) as its own KernelSU
   module and enable Zygisk in it. **Without this, nothing in this module
   runs at all** - it's the most common reason a Zygisk module silently does
   nothing on a KernelSU device, so check this first if HDR is still showing
   up after flashing.
3. Android 16, or really anything API 27+.

## Building

No local Android NDK setup needed - push to GitHub and let
`.github/workflows/build.yml` build it (Actions tab → run the workflow →
download the `disable_hdr` artifact). CMake fetches LSPlant's source
directly (see `native/src/main/cpp/CMakeLists.txt`) and builds Dobby from
its prebuilt Maven/Prefab package, so there's no NDK toolchain to
hand-configure beyond what `nttld/setup-ndk` sets up automatically - just
note the native build now needs network access during CMake's configure
step (to clone LSPlant) and takes noticeably longer the first time as a
result, since it's compiling LSPlant from source instead of pulling a
prebuilt binary.

By default that clone fetches LSPlant's **full history**, not a shallow
single-commit fetch, so CMake can resolve "the newest commit before a
given date" itself - see [Build fix: LSPlant upstream started shipping
C++20 modules](#build-fix-lsplant-upstream-started-shipping-c20-modules-2026-08-06)
for why. To pin an exact ref instead and skip all of that, add a second
entry to the `arguments` list already in `native/build.gradle.kts`:
`arguments += listOf("-DANDROID_STL=c++_static", "-DLSPLANT_GIT_TAG=6.4")`
(or pass `-DLSPLANT_GIT_TAG=...` straight to `cmake` if you're driving it
directly rather than through Gradle).

To build locally instead, you need JDK 17 and the Android NDK (r27c or
newer), then:

```
./gradlew :native:externalNativeBuildRelease
./gradlew :hookdex:extractHookDex
./package.sh          # or just run this - it does both of the above too
```

Output: `dist/disable_hdr.zip`.

This intentionally targets **AGP 8.7 / Gradle 8.9**, not the AGP 9.x line -
AGP 9.0 (Jan 2026) removed several old DSL classes, and that migration
happened after my reliable knowledge cutoff, so I'd rather hand you a build
I'm confident is correct than guess at syntax for a DSL I can't fully
verify. AGP 8.7 still builds `compileSdk 36` (Android 16) fine. Android
Studio's own Upgrade Assistant is the safer way to move this to AGP 9.x
later if you want to.

## Installing

KernelSU Manager → Modules → Install from storage → pick `disable_hdr.zip`
→ reboot (or at least force-stop/reopen the apps you care about - Zygisk
modules apply per-process at app start, no reboot strictly required).

There's no `update-binary`/`updater-script`/`META-INF` in this version -
[KernelSU explicitly doesn't support installing modules via custom
recovery](https://kernelsu.org/guide/module.html), only through the Manager
app, which doesn't use that mechanism either. The previous version carried
that scaffolding over from a Magisk module template; it was also carrying a
latent bug (`require_new_magisk` would refuse to install on a KernelSU-only
device with no Magisk present, if that code path were ever actually hit).
Dropping it is a correctness fix, not just cleanup.

## Choosing which apps get it

Open the module's page in KernelSU Manager for a WebUI app picker, or edit
`/data/adb/modules/disable_hdr/targets.txt` directly (one package name per
line, `#` for comments). **Empty file = applies to every app.** Add entries
to scope it down to specific apps instead.

The WebUI uses KernelSU's real, documented JS API
(`exec`/`toast`/`listPackages`/`getPackagesInfo`) rather than a CDN, since
the page runs with `exec()`-as-root access and shouldn't be pulling script
from the network at request time. `listPackages`/`getPackagesInfo` are a
fairly recent addition to KernelSU itself; if your Manager build predates
them the picker falls back to `pm list packages -3` automatically (real
packages, just without pretty app names).

## Known limitations

- **This is the standard Android application API surface, not a hardware-level
  guarantee.** The three hooks above cover how virtually every app - including
  Netflix, YouTube, Prime Video, Disney+, Jellyfin, Plex, and any
  ExoPlayer-based player - actually decides whether to request HDR. A
  sufficiently determined app could still find lower-level signals this
  doesn't touch (e.g. probing `Display.getMode()`'s bit depth, native
  `ANativeWindow`/`AHardwareBuffer` queries, or a hardcoded device-model
  allowlist that never calls any capability API at all). Nothing running
  inside the app's own process, at the Java framework layer, can make that
  category of workaround strictly impossible - this gets you as close as a
  client-side hook can.
- **LSPlant is well-maintained, but not infallible on brand-new Android
  point releases.** There are real reports of LSPlant-based hooks crashing
  the zygote on very recent Android 16 QPR builds (ART-internal layout
  drift between quarterly releases) before LSPlant catches up. If apps
  start crashing after flashing this, that's the first thing to suspect -
  check the module's GitHub issues, or `logcat | grep DisableHdrZygisk` for
  what specifically failed.
- **Which exact LSPlant snapshot gets built is resolved automatically, not
  frozen to one fixed version** - see [Build fix: LSPlant upstream started
  shipping C++20 modules](#build-fix-lsplant-upstream-started-shipping-c20-modules-2026-08-06).
  By design, so the build keeps working as LSPlant adds support for newer
  Android releases without needing a manual bump here - but it also means
  two builds run months apart can legitimately fetch different LSPlant
  code. Set `LSPLANT_GIT_TAG` explicitly (see [Building](#building)) if you
  need a fully reproducible, audit-pinned build instead.
- Every JNI call in `installHooks()` is wrapped to fail silently and log to
  logcat rather than crash the host app if a target method is missing on a
  particular OEM build - by design, "HDR stays on" is the safe failure mode
  here, not "the app crashes."
- `CMakeLists.txt` fails the build if `disable_hdr.so` ever ends up with a
  DT_NEEDED dependency on anything beyond the handful of libraries every
  Android process already has mapped - see
  [What changed](#what-changed-from-the-original) for why that check exists
  and what it would have caught here from the start.

## What changed from the original

- **Fixed the module never loading at all.** `find_package(lsplant REQUIRED
  CONFIG)` + `target_link_libraries(... lsplant::lsplant ...)` compiled and
  linked cleanly, but silently produced a `disable_hdr.so` with a
  `DT_NEEDED` dependency on `liblsplant.so` - a file that doesn't exist
  anywhere in a Zygisk module (one self-contained `.so` per ABI, no
  jniLibs-style secondary lookup the way a normal APK has). The Maven
  package's "-standalone" naming means libc++ is statically embedded in
  *its* `.so`, not that lsplant itself is static - two different things.
  Confirmed via ZygiskNext's own loader log on the reporting device:
  `Not found: 'liblsplant.so' needed by 'disable_hdr' while loading
  'disable_hdr'` → `Failed to link image` → `preload failed` → every hook
  silently never installed, in any process, ever (zero `DisableHdrZygisk`
  log lines anywhere in a multi-megabyte bug report is the tell). Fixed by
  building LSPlant from source and linking it as a real static library
  instead (`native/src/main/cpp/CMakeLists.txt`); Dobby was left as-is,
  since it does publish a genuine static archive. Added a build-time check
  that fails loudly if `disable_hdr.so` ever regains a non-system
  `DT_NEEDED` entry, so this exact failure mode can't silently ship again.
- **Added the `MediaCodecInfo#getCapabilitiesForType` hook** - see
  [What it actually hooks](#what-it-actually-hooks).
- **YAHFA → LSPlant + Dobby.** YAHFA's own upstream only documents testing
  through Android 12; LSPlant is what LSPosed itself currently ships.
- **`Display.getHdrCapabilities()` reflection → hooking
  `HdrCapabilities#getSupportedHdrTypes()` directly.** The original
  approach hooked the outer getter and reflectively constructed a
  replacement `HdrCapabilities` via a 4-argument constructor it assumed
  existed. I couldn't verify that constructor's exact signature holds on
  Android 16 across OEMs, and getting it wrong would throw and silently
  disable the whole hook. Hooking the inner getter instead needs no object
  construction and no guessed constructor at all - simpler and more robust,
  not just "different."
- **Removed the `ActivityThread`/app-`ClassLoader` lookup in
  `postAppSpecialize()`.** `postAppSpecialize` fires before
  `ActivityThread.main()` has run in the forked process, so
  `ActivityThread.currentActivityThread()` isn't reliably populated yet at
  that point - a real, likely source of the hook silently never installing,
  independent of which hooking library was in use. Since neither of our
  hook targets nor the tiny injected stub class depend on the app's own
  ClassLoader, the fix was to stop needing it at all rather than to chase
  a later, safer point to read it from.
- **`module.prop` cleaned up for KernelSU** - dropped `minMagisk` (a
  Magisk-only field with no KernelSU equivalent); current field set per
  [kernelsu.org's module guide](https://kernelsu.org/guide/module.html).
- **Dropped the recovery-flash `META-INF` scaffold** - see
  [Installing](#installing) above.
- **WebUI fixed to use the real, verified `kernelsu` npm package API**
  (checked directly against the published source on unpkg) instead of a
  hand-reconstructed approximation of it.

## Build fix: LSPlant upstream started shipping C++20 modules (2026-08-06)

The previous fix above (building LSPlant from source instead of consuming
its broken Prefab package) traded one failure mode for another. It pinned
`GIT_TAG` to `master`, and master moved: CI started failing during the
actual **compile** this time, not the link.

### What actually broke

The CI log from the bug report bundle (`build/6_Build + package.txt`)
shows the real failure:

```
FAILED: native/CMakeFiles/lsplant_static.dir/.../lsplant.cc.o
.../_deps/lsplant_src-src/lsplant/src/main/jni/lsplant.cc:25:8: fatal error: module 'lsplant' not found
   25 | module lsplant;
      | ~~~~~~~^~~~~~~
```

`module lsplant;` is real, standard C++20: it declares that file as the
*implementation unit* of a named module called `lsplant`
([cppreference: Modules](https://en.cppreference.com/w/cpp/language/modules)).
Building a file like that needs a build system that scans sources ahead of
time for module dependencies, compiles the corresponding *interface* unit
into a BMI (binary module interface) first, and passes every consumer
`-fmodule-file=lsplant=<path to that .pcm>` so the compiler can find it
([Clang's own docs on this](https://clang.llvm.org/docs/StandardCPlusPlusModules.html)).
For a minimal, from-scratch reproduction of exactly this failure shape -
a module implementation unit compiled without its interface being made
available - see [llvm/llvm-project#53661](https://github.com/llvm/llvm-project/issues/53661).

None of that scanning/ordering/BMI machinery runs here. AGP's external
native build drives the NDK's own bundled CMake - confirmed directly from
the log as **3.22.1** (`/usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja`)
- which just invokes `clang++ ... -c lsplant.cc -o lsplant.cc.o` per file,
with zero module awareness. CMake didn't support any of this until
**3.28** (Dec 2023, six minor releases past 3.22), and even then only for
Ninja/Visual-Studio generators paired with Clang 16+, GCC 14+, or MSVC
14.34+, per [CMake's own 3.28 release notes](https://cmake.org/cmake/help/latest/release/3.28.html)
and Kitware's post announcing this was the point it left experimental
status: ["import CMake; the Experiment is Over!"](https://www.kitware.com/import-cmake-the-experiment-is-over/)
There is no flag that fixes this. The only real fix is making sure the
LSPlant commit we fetch predates whatever commit introduced that line.

### Why "just pin to the latest release tag" isn't the whole fix

That's the obvious next move, and it's literally what the old comment in
`CMakeLists.txt` said to do "once CI is green." It's incomplete on its
own: the last LSPlant version actually **published to Maven Central is
6.4, from April 18, 2024** - confirmed independently via
[mvnrepository.com](https://mvnrepository.com/artifact/org.lsposed.lsplant/lsplant)
and [central.sonatype.com](https://central.sonatype.com/artifact/org.lsposed.lsplant/lsplant/versions),
whose 6.4 changelog entry ("Try support for 16k page size", "Fix riscv64
support") confirms the vintage. LSPlant's own GitHub
[Discussion #161](https://github.com/LSPosed/LSPlant/discussions/161),
titled "when supports android 16.", was opened **November 17, 2025** and
is still unanswered - so Android 16 support didn't exist in LSPlant as of
that date, let alone back in April 2024. This module targets Android 16.
Pinning to 6.4 would fix the build and silently reintroduce "the hooks
don't work on the OS version this module exists for" - worse than a build
failure, since a build failure is loud and a wrong ART offset inside a
working hook is not.

### The actual fix: two cache variables instead of one hardcoded tag

`native/src/main/cpp/CMakeLists.txt` now exposes:

- **`LSPLANT_GIT_TAG`** (empty by default) - set this to an exact
  tag/branch/commit you've picked and verified yourself, and the build
  falls back to the same simple shallow-clone `FetchContent` flow it
  always used, just parameterized instead of hardcoded to `master`.
- **`LSPLANT_PIN_BEFORE`** (default `2026-07-01T00:00:00Z`; only consulted
  when `LSPLANT_GIT_TAG` is empty) - the build clones LSPlant's full
  history itself and resolves `git rev-list -n 1 --before=<this>
  origin/master`, then checks that exact commit out. "Newest commit
  strictly before a date" instead of a hand-picked tag name, because I
  had no way to browse GitHub's own tag/commit list to hand-pick a good
  one from where I was working (see below) - a date estimate degrades far
  more gracefully than a guessed tag name if it turns out to be wrong.

Either way, a new **build-time guard** runs right after LSPlant's sources
are collected and before anything is compiled: it scans every fetched
`.cc`/`.cpp` file for a line that looks like a C++20 module declaration
and fails the *configure* step immediately, by filename, with remediation
instructions, if it finds one - instead of letting it reach the compiler
and produce another cryptic `module 'lsplant' not found` several minutes
into the build. If `LSPLANT_PIN_BEFORE`'s value ever turns out to be
wrong, this is what tells you, in plain English, the moment you
reconfigure.

### Why a date, and how "2026-07-01" was chosen

I could not browse `github.com/LSPosed/LSPlant/tags` or `/commits/master`
directly from this sandbox - both are blocked by that site's
`robots.txt`, as were the two read-only mirrors I tried instead
(`git.haisto.cn`, `rat.dev`). What I could establish, from sources that
*were* reachable:

- LSPlant's GitHub org activity page shows commits continuing through
  **August 3, 2026** - three days before the CI failure above.
- A Gitea mirror's search-indexed (though not directly fetchable) commit
  log shows a commit titled **"Update supported Android versions in
  README", dated 2026-04-25**.
- Discussion #161 above puts "no Android 16 support yet" at November 2025.

So Android 16 support most likely landed sometime in the roughly
five-month window ending around late April 2026, and the C++20-modules
regression is recent enough that I found no tagged release built from it.
**`2026-07-01`** sits about nine weeks after that README update and about
five weeks before the observed break - a deliberately rough midpoint, not
a verified boundary. Treat it as a documented starting guess, not a
confirmed-safe date; if the guard above fires, move it earlier and
reconfigure.

### What I verified locally, and what I could not

This sandbox's command line has no outbound network access at all (only
the separate web-search/web-fetch tools used for the research above could
reach the internet), so I could not run this build against the real
LSPlant repository. What I *did* verify, against a throwaway local git
repo built to mirror the same timeline (five commits dated across
Apr-Jul 2026, one of them standing in for the modules regression):

- `git rev-list -n 1 --before="2026-07-01T00:00:00Z" master` correctly
  resolves to the commit immediately before the simulated regression, and
  correctly skips both it and a later one.
- `git clone --no-checkout`, then `rev-list` against `origin/master`, then
  `checkout --force <resolved sha>` - the exact sequence the CMake logic
  above runs - reproduces it end-to-end and leaves the working tree at
  exactly the expected commit, with a second "already cloned" pass
  correctly reusing it via `fetch` instead of re-cloning.
- The module-declaration `REGEX` correctly flags `module lsplant;` (both
  indented and not) and `export module lsplant;`, and correctly leaves
  alone near-miss lines that merely contain the word "module"
  (`int module = 5;`, `ModuleLoader module_lsplant;`, `import_settings();`).

What I could **not** verify: that `2026-07-01` genuinely lands before
LSPlant's real modules-refactor commit, or that CMake 3.22.1 itself
accepts every construct in the new `CMakeLists.txt` block - there's no
local `cmake` binary in this sandbox either, only `git`, which is what the
simulation above actually exercises. **Your CI run is the real test of
both.** Watch for these two new lines near the top of the build log:

```
-- LSPlant: cloning full history (auto-resolving newest commit before 2026-07-01T00:00:00Z)
-- LSPlant: resolved <sha> as the newest commit before 2026-07-01T00:00:00Z
```

If the build instead fails at the module-declaration guard, that's the
new mechanism doing its job correctly - follow the message it prints
(move `LSPLANT_PIN_BEFORE` earlier, or set `LSPLANT_GIT_TAG` explicitly;
`6.4` is a confirmed-real, confirmed-old fallback guaranteed to at least
build, per the two Maven sources above, if you want everything else
verified while you sort out a better LSPlant pin).

### Sources consulted for this fix

| Claim | Source |
| --- | --- |
| Exact compiler error, file, and line that's actually failing | `logs_84314619862.zip` → `build/6_Build + package.txt` (this project's own CI run) |
| `module x;` = a C++20 module implementation unit declaration | [cppreference: Modules](https://en.cppreference.com/w/cpp/language/modules) |
| What a module implementation unit needs from the build system | [Clang: Standard C++ Modules](https://clang.llvm.org/docs/StandardCPlusPlusModules.html) |
| Identical failure shape, minimal repro | [llvm/llvm-project#53661](https://github.com/llvm/llvm-project/issues/53661) |
| CMake 3.28 is the first version with real (non-experimental) C++20 modules support, and its compiler/generator requirements | [CMake 3.28 release notes](https://cmake.org/cmake/help/latest/release/3.28.html); [Kitware: "import CMake; the Experiment is Over!"](https://www.kitware.com/import-cmake-the-experiment-is-over/) |
| NDK-bundled CMake version actually in use in this project's CI | This project's own CI log, `/usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja` |
| LSPlant repo identity, activity, current claimed Android version support | [github.com/LSPosed/LSPlant](https://github.com/LSPosed/LSPlant) |
| Last Maven-published LSPlant version is 6.4, April 18 2024 | [mvnrepository.com](https://mvnrepository.com/artifact/org.lsposed.lsplant/lsplant); [central.sonatype.com](https://central.sonatype.com/artifact/org.lsposed.lsplant/lsplant/versions) |
| Android 16 not supported by LSPlant as of Nov 17, 2025 | [github.com/LSPosed/LSPlant/discussions/161](https://github.com/LSPosed/LSPlant/discussions/161) |
| LSPlant README's "supported Android versions" updated 2026-04-25 | Gitea mirror of the repo - search-indexed content only, see caveat below |
| LSPlant repo activity continuing through Aug 3, 2026 | LSPosed org GitHub page - search-indexed content only, see caveat below |
| `github.com/LSPosed/LSPlant`'s `/tags` and `/commits` views are not directly browsable from this sandbox | Direct fetch attempts against both, and against two mirrors, all returned `robots.txt`-disallowed |
| `git rev-list --before=`, `--no-checkout` clone + `checkout --force`, and the module-declaration `REGEX` all behave as this fix assumes | Local simulation in this sandbox (real `git`, throwaway repo) - not a substitute for testing against the real LSPlant repository |

The two "search-indexed content only" rows are flagged deliberately: I
could not open those pages myself and I'm relying on how their content
was surfaced through search results, which is weaker sourcing than
everything else in this list. If you can browse GitHub directly, five
minutes at `https://github.com/LSPosed/LSPlant/commits/master` to find the
exact commit that adds `module lsplant;` and setting `LSPLANT_GIT_TAG` to
its immediate parent is strictly better than trusting my date estimate -
the guard above will confirm either way, loudly, if it isn't.

## Project layout

```
native/       Zygisk .so - CMake + LSPlant (source) + Dobby (native/src/main/cpp/disable_hdr.cpp)
              native/src/main/cpp/check_self_contained.cmake - build-time DT_NEEDED guard
hookdex/      Bridge.java - three `native` method stubs, no logic, no dependencies
flashable_module/   module.prop, customize.sh, sepolicy.rule, webroot/ (WebUI)
package.sh    Builds both Gradle modules and assembles dist/disable_hdr.zip
```

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
single-commit fetch, so CMake can search backwards from `master`'s tip
itself for the newest commit this toolchain can actually compile - see
[Build fix](#build-fix-lsplant-upstream-started-shipping-c20-modules-2026-08-06)
and [part 2](#build-fix-part-2-from-date-guessing-to-actually-searching-history-2026-08-07)
for why it works that way instead of just pinning a tag. To pin an exact
ref instead and skip all of that, add a second
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
  frozen to one fixed version** - see [Build fix, part
  2](#build-fix-part-2-from-date-guessing-to-actually-searching-history-2026-08-07)
  and [part 3](#build-fix-part-3-the-guard-only-checked-half-the-syntax-2026-08-07).
  By design, so the build keeps working as LSPlant adds support for newer
  Android releases without needing a manual bump here - but it also means
  two builds run months apart can legitimately fetch different LSPlant
  code, and if the build ever prints a `LSPlant: the resolved commit ...`
  `WARNING` during configure, it means the newest buildable commit it
  could find predates LSPlant's Android 16 support - check part 3 above
  before trusting hooks on Android 16 specifically. Set `LSPLANT_GIT_TAG`
  explicitly (see [Building](#building)) if you need a fully reproducible,
  audit-pinned build instead.
- **On crDroid for the Mi 9 (cepheus) specifically:** this ROM's own
  release thread has multiple independent reports of KernelSU-Next and its
  bundled Zygisk implementation getting out of sync after a ROM or
  KSU-Next update - modules that depend on Zygisk (this one included) stop
  loading until the Zygisk implementation (ReZygisk/Zygisk Next, whichever
  the manager app installed) is reinstalled, and at least one report of
  KSU-Next itself crashing on launch above v1.07 on this ROM ([XDA: ROM
  crDroid v12.10, cepheus](https://xdaforums.com/t/rom-16-0-official-cepheus-retrofit-crdroid-v12-10-26-05-2026.4758108/);
  [XDA: ROM crDroid v12.7, cepheus, page
  5](https://xdaforums.com/t/rom-16-0-official-cepheus-retrofit-crdroid-v12-7-14-02-2026.4758108/page-5)).
  Neither is anything this project can fix - they're KernelSU-Next/ROM
  interactions, not `disable-hdr-zygisk` bugs - but if the module still
  doesn't load after a build succeeds and installs cleanly, checking that
  Zygisk itself is actually active (KernelSU Next manager app → Zygisk
  status) before digging further will save time.
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

> **Superseded 2026-08-07.** This date was wrong, CI proved it wrong, and
> the mechanism described in this subsection (`LSPLANT_PIN_BEFORE`) no
> longer exists in `CMakeLists.txt` - it's been replaced with something
> that doesn't require guessing a date at all. See [Build fix, part 2:
> from date-guessing to actually searching history
> (2026-08-07)](#build-fix-part-2-from-date-guessing-to-actually-searching-history-2026-08-07)
> below. Kept here, unedited, for the record.

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

> **Note (2026-08-07):** the specific mechanism this subsection verifies
> (date-based pinning) has been replaced - see [Build fix, part
> 2](#build-fix-part-2-from-date-guessing-to-actually-searching-history-2026-08-07).
> The underlying primitives tested here (`clone --no-checkout`, `checkout
> --force`, the module-declaration `REGEX`) are still exactly what the
> current mechanism uses, just driven by a search loop instead of a single
> date lookup - so this verification isn't obsolete, just incomplete; the
> new section adds a second round covering the loop itself.

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

## Build fix, part 2: from date-guessing to actually searching history (2026-08-07)

### The date was wrong, and CI proved it

The `2026-07-01` guess above got tested for real. The next CI log
(`logs_84508657624.zip`) shows the auto-resolve mechanism ran exactly as
designed - clone, resolve, checkout all worked - and then the new guard
fired, also exactly as designed:

```
-- LSPlant: cloning full history (auto-resolving newest commit before 2026-07-01T00:00:00Z)
-- LSPlant: resolved a96c7978a9fe578e0e265cc8e45cb117fa8250a3 as the newest commit before 2026-07-01T00:00:00Z
CMake Error at CMakeLists.txt:233 (message):
  lsplant.cc declares a C++20 module ("module lsplant\;") - this toolchain
  ...has drifted onto a version of LSPlant that uses C++20 modules.
```

Two separate things to take from that. First, the **mechanism worked** -
this is the difference between a build that fails with a five-line CMake
error naming the exact file and telling you what to do, versus one that
fails with a three-hundred-line clang diagnostic three layers deep in a
Gradle stack trace. Second, the **guess was wrong** - `a96c7978a9...`
("Move FD closing to callers of `CreateDualMapping`") already had the
`module lsplant;` line, meaning the actual regression landed *before*
July 1, not after some point past it. Getting a hand-picked date wrong
once is a normal part of debugging something you can't directly observe.
Shipping a *second* hand-picked date and hoping it's right would not be -
there's no reason a second guess is inherently more trustworthy than the
first, and this project doesn't need a third round to find that out.

### Better research this time: /actions and individual commits load, /tags and /commits don't

The first attempt only tried `github.com/LSPosed/LSPlant/tags` and
`/commits/master`, both blocked by `robots.txt`. This time, two other
routes turned out to work from this sandbox:

- **`https://github.com/LSPosed/LSPlant/actions`** - GitHub's workflow-run
  history. It's paginated, but each page lists real commits (message,
  short SHA, author, a link to the commit itself) for every push that
  triggered a workflow - which is most pushes, since this repo runs CI on
  nearly everything.
- **`https://github.com/LSPosed/LSPlant/commit/<full-sha>`** - individual
  commit pages. These load full diffs, and critically, their page
  metadata includes a real Unix timestamp (`meta-og:updated_time`),
  giving an exact date for any commit whose SHA is already known - such
  as one a build just resolved.

Neither of those was tried in the first attempt; both turned out to be
open doors. Walking `/actions` back through its recent pages surfaced the
real commit sequence around the failure - most recently, in order:
`b25ea23` ("Fix 32-bit symbol name for EncodeGenericId"), `3eafa29`
("Add null checks for dual mapping results in lsplant.cc"), `57df30f`
("Pass std::string_view by value in symbol resolvers"), `aa8e099`
("Disable static-accessed-through-instance clang-tidy check"), and
`5185687` ("Replace RoundUpTo with `__builtin_align_up`") - all clustered
together, all plausibly part of the same recent cleanup pass that
`a96c797` (not itself individually listed on `/actions`, since `git log`
shows it sits right in the middle of that cluster) belongs to too.

Further back, past a long run of routine Dependabot gradle-version bumps,
sits `16205e7` - **"Hook ReinitializeMethodsCode on A16+ (#158)"**,
fixing [issue #157](https://github.com/LSPosed/LSPlant/issues/157). Its
commit page's `meta-og:updated_time` is Unix timestamp `1752394576`,
which is **2025-07-13** - over a year before this investigation, and
about four months *before* [Discussion
#161](https://github.com/LSPosed/LSPlant/discussions/161) ("when
supports android 16.", opened Nov 17 2025) went unanswered. Read
together, the honest picture is: LSPlant landed a first, partial Android
16 hook in July 2025; the person asking in November 2025 either hadn't
seen it or didn't consider one hook "support" yet; and a Gitea mirror's
directory listing (search-indexed only, not directly fetchable, same as
before) independently shows both `.github/` and `lsplant/`'s most recent
commit *as of that mirror's 2026-05-31 sync* was a same-evening pair from
**2026-04-25** - `"Update build and test workflows"` at 19:39:18+08:00,
then `"[skip ci] Fix typos in lsplant.hpp header comments"` at
19:58:11+08:00 - which reads like a finishing-up-and-polishing session,
not a modules migration. That mirror snapshot means *nothing* under
`lsplant/` changed again for over a month after that (until at least
2026-05-31) - so whatever introduced `module lsplant;` almost certainly
lives in the June-July cluster above, not anywhere near April 25.

This time I'm not turning that into a third hand-picked date, for the
same reason the second one wasn't good enough: I still can't fully
enumerate the commits between April 25 and the June-July cluster (`/tags`
and `/commits` are still blocked), so I still can't *prove* April 25 (or
any specific point after it) is clean, only argue it's likely. The fix
below doesn't need that proof.

### The actual fix: stop guessing, search instead

`LSPLANT_PIN_BEFORE` and the date-based `rev-list --before=` lookup are
gone from `CMakeLists.txt`. In their place, auto-resolve mode now does
this at configure time:

1. Clone LSPlant's full history (unchanged from before).
2. List the newest `LSPLANT_MAX_SEARCH_COMMITS` commits on `origin/master`
   (new cache variable, default `400` - comfortably more than LSPlant's
   entire history, currently a few hundred commits).
3. Starting from the newest, check each one out and scan its
   `lsplant/*.cc`/`*.cpp` for the same module-declaration pattern the
   guard already used. The **first** (i.e. newest) commit that comes back
   clean is what gets built.
4. Only if every single commit in that search comes back dirty does this
   fail - with a message pointing at `/actions` instead of the blocked
   `/tags`/`/commits`, and at `LSPLANT_MAX_SEARCH_COMMITS` in case history
   has just gotten longer than the default covers.

This is strictly more work per configure than one `rev-list --before=`
call - it's cloning the same amount of history either way, but now
potentially checking out a handful of commits instead of one - and that's
the right trade. It doesn't need a date at all, so it can't be wrong the
way `2026-07-01` was wrong: whatever the real boundary is, on any given
day, this finds it, because it's actually looking rather than predicting.
If LSPlant reintroduces modules again next month, or removes them again
next year, or the June-July cluster above turns out to have a clean
commit *in the middle* of it that a linear "just find any older tag"
approach would've walked straight past - this handles all three the same
way, automatically, because it checks every candidate rather than
assuming the history is neatly split into one bad range and one good one.

`LSPLANT_GIT_TAG` (manual override) is untouched and still the right tool
if you'd rather pin an exact ref yourself - `6.4` remains a confirmed-real
fallback that just predates Android 16 support, as before.

### What I verified locally this time

Same constraint as before: no outbound network from this sandbox's
command line, so no run against the real repository. What's different is
the shape of what needed testing - not "does one date resolve correctly"
but "does the search loop find the right commit when history *isn't*
neatly split into one clean range and one broken one." I built a
throwaway local repo with ten commits mirroring the real
timeline - clean through mid-2025, clean again through the April 2026
cluster, then a **deliberately non-monotonic** stretch: one experimental
modules commit, a revert back to clean, a routine bump, and *then* four
more modules commits in a row (mirroring the real RoundUpTo → clang-tidy
→ string_view → dual-mapping → FD-closing → 32-bit-symbol sequence) with
no more reverts after that - and ran a direct shell port of the CMake
search loop (same `git checkout --force` + `grep -E` pattern, same
newest-first order, same stop-at-first-clean logic) against it.

It walked past all four commits in the final dirty stretch, correctly
stopped at the routine bump just past them without needing to walk as far
back as the earlier revert-then-clean stretch, and printed exactly the
kind of `#N <sha> -> HAS modules, trying older` / `#N <sha> -> clean,
STOP` trail the real `message(STATUS ...)` calls will produce. What this
does *not* prove: that CMake 3.22.1's actual parser accepts every
construct in the real `CMakeLists.txt` (`math(EXPR ...)`, nested
`foreach()`/`break()`, `continue()` inside `foreach()`) - there's still no
local `cmake` binary here to check that against, only `git` and `grep`,
which is what the simulation above exercises. **Your CI run is still the
real test.** Watch the build log for a run of lines like:

```
-- LSPlant: searching back from origin/master HEAD (up to 400 commits) for the newest one this toolchain can compile
-- LSPlant: commit <sha> (#1 back from HEAD) uses C++20 modules, trying an older commit
-- LSPlant: commit <sha> (#2 back from HEAD) uses C++20 modules, trying an older commit
...
-- LSPlant: resolved <sha> (#N commit(s) back from HEAD, counting HEAD as #1) as the newest commit this toolchain can build
```

If it instead fails at `CMakeLists.txt:233` again, something about the
loop itself is broken (not just a wrong date this time, since there's no
date to get wrong) - and that's a real bug report, not a one-line fix.

### Sources consulted for this fix

| Claim | Source |
| --- | --- |
| The auto-resolve mechanism ran correctly and resolved a real commit, which then failed the module guard | `logs_84508657624.zip` → `0_build.txt` (this project's own CI run, 2026-08-07) |
| `github.com/LSPosed/LSPlant/actions` lists real commits with messages, short SHAs, and author, across multiple pages | Direct fetch of [`.../actions`](https://github.com/LSPosed/LSPlant/actions) and [`.../actions?page=2`](https://github.com/LSPosed/LSPlant/actions?page=2) |
| Individual commit pages (`.../commit/<sha>`) load full diffs and a real Unix timestamp in page metadata | Direct fetch of [`.../commit/16205e71aa3f028a729b00ed4aafd86793ea94f7`](https://github.com/LSPosed/LSPlant/commit/16205e71aa3f028a729b00ed4aafd86793ea94f7) |
| That commit ("Hook ReinitializeMethodsCode on A16+ (#158)") is dated 2025-07-13 | `date -u -d @1752394576` against the `meta-og:updated_time` found on that commit page (pure local computation, no network) |
| `.github/` and `lsplant/`'s most recent commits, as of a 2026-05-31 mirror sync, were both from the evening of 2026-04-25 | Gitea mirror of the repo - search-indexed content only, same caveat as the first attempt: I could not open this page directly, only see how it was surfaced through search results |
| `github.com/LSPosed/LSPlant`'s `/tags` and `/commits` (and two mirrors) are still not directly browsable from this sandbox | Repeat direct fetch attempts, all still `robots.txt`-disallowed |
| The search loop correctly finds the newest clean commit even against non-monotonic history (dirty → clean → dirty-again patterns) | Local simulation in this sandbox (real `git` + `grep`, throwaway repo built to mirror the real timeline) - not a substitute for testing against the real LSPlant repository or the real CMake parser |

## Build fix, part 3: the guard only checked half the syntax (2026-08-07)

### The search loop worked. The definition of "clean" was incomplete.

The `logs_84687441446.zip` CI run shows the search loop from part 2 doing
exactly what it was built to do - the log is one long chain of `HEAD is
now at <sha> <message>` / `Previous HEAD position was <sha> <message>`
pairs, git's own output from 51 consecutive `checkout --force` calls,
walking backward from `origin/master`'s tip. It correctly rejected 50
commits in a row and stopped at the 51st, `e2a35a4` ("Setup Android SDK"),
because that one has no `module lsplant;` self-declaration. Then the
*build* step failed anyway, on a different line:

```
lsplant.cc:15:8: fatal error: module 'dex_builder' not found
   15 | import dex_builder;
```

Column 8 lands exactly on the `d` of `dex_builder` in `import
dex_builder;` - confirmed by counting characters, not just eyeballing it.
This is a **named-module import**, a second, distinct piece of C++20
modules grammar from the self-declaration the guard already checked for.
The guard was checking for one half of the syntax that makes a file
unbuildable here and had never been told about the other half - there
was no reason to know about it until a real build hit it.

### `dex_builder` is not part of LSPlant. It's a separate repository.

This took the investigation somewhere the first two rounds didn't go:
[`LSPosed/DexBuilder`](https://github.com/LSPosed/DexBuilder) is its own
repo - "Generate dex file by c++", built via a plain `Android.mk`
(old-style `ndk-build`, not CMake, and its `LOCAL_MODULE := dex_builder`
is just an ndk-build target name, unrelated to C++20 "named modules"
despite the identical word). LSPlant's own official build evidently wraps
that dependency as a real C++20 module named `dex_builder` and imports it
from `lsplant.cc` - which means this isn't "one file in LSPlant's own
tree got converted," it's LSPlant importing a *second*, independently
versioned project through the same modules mechanism this toolchain can't
support. Re-checked whether LSPlant had published anything past the `6.4`
Maven release in the meantime (it hadn't - `6.4`, April 2024, is still
the newest version listed on both
[mvnrepository.com](https://mvnrepository.com/artifact/org.lsposed.lsplant/lsplant)
and [central.sonatype.com](https://central.sonatype.com/artifact/org.lsposed.lsplant/lsplant)),
so there's no newer prebuilt to fall back to either.

What this means for how deep the modules adoption actually goes: still
genuinely unclear from outside the repo. `36e3f80` ("Update dex builder"),
visible in the same walk, is far too vague a commit message to tell
whether `dex_builder` was already an *imported module* at that point or
just an updated vendored copy of the old kind. This round's fix doesn't
try to resolve that uncertainty by more archaeology - it makes the build
resilient to not knowing.

### The fix: check for both syntax shapes, and stop being silent about Android 16 coverage

Two changes to `native/src/main/cpp/CMakeLists.txt`, both in the same
regex-based checking logic added in part 2:

1. **The check now matches `import x;` and `export import x;`, as well as
   `module x;` / `export module x;`.** Same spot (the search loop) and
   same spot again (the manual-pin defense-in-depth guard). `export
   import` (re-exporting an import from within a module interface) hasn't
   actually been seen in LSPlant's history yet - it's the same statement
   family as the other three and cheap to cover now rather than after a
   fourth CI round finds it first.
2. **A new, non-fatal check runs after the search resolves a commit:**
   `git merge-base --is-ancestor <resolved> 16205e71aa...` (the "Hook
   ReinitializeMethodsCode on A16+" commit from parts 1-2, 2025-07-13).
   If the resolved commit is at or before that point, the build prints a
   `message(WARNING ...)` - loud, named, impossible to miss in a CI log -
   and **still finishes the build**. A commit that predates Android 16
   support still compiles and installs; failing the build over that would
   be strictly worse than a clearly labeled warning, since a build that
   works everywhere except Android 16 is still useful and still better
   than no build. The point of the warning is to stop that gap from being
   silent, not to block it.

Both checks were verified locally the same way as before - not against
the real repository (still no outbound network from this sandbox's
command line), but against real `git`, with cases constructed specifically
to test the new logic rather than re-testing what part 2 already covered:

- The combined regex (`module`/`export module` OR `import`) correctly
  matches `import dex_builder;`, indented or not, and dotted names like
  `import foo.bar;` - while still correctly ignoring `int module = 5;`,
  `ModuleLoader module_lsplant;`, `import_settings();`, and, new this
  round, `import(dex_builder);` and `importer.run();` (both plausible
  near-misses specifically because the real bug involves a function-like
  use of the word "import" being one character away from the real thing).
- `git merge-base --is-ancestor` was tested directly (not simulated) for
  all three relevant orderings against a throwaway repo with a marked
  "android16-hook" commit: a later commit correctly reports "not an
  ancestor" (exit 1, no warning), an earlier one correctly reports "is an
  ancestor" (exit 0, warning fires), and the boundary commit itself
  correctly counts as an ancestor of itself (exit 0, warning fires) -
  matching the CMake side's `EQUAL 0` check exactly.

What still hasn't been verified against the real repository: whether the
search, with the fixed check, lands on a commit *after* the Android 16
floor or before it. That's exactly what the new warning is for - the next
CI run's log will say so explicitly, either by staying silent (good) or
by printing the warning above (meaning the modules adoption goes back
further than 2025-07-13, and this project has a real decision to make -
see below).

### Where this goes if the next run still isn't clean

Two build-time diagnostics now exist for two different failure shapes,
and it's worth being explicit about what each one *means* for next steps,
not just what it *prints*:

- **The `FATAL_ERROR` module/import check fires again** → the regex
  itself has a gap (a third syntax shape, e.g. `import <name>:<partition>;`
  or a header-unit import) - same class of fix as this round, extend the
  pattern.
- **The Android-16-floor `WARNING` fires** → this is the more serious
  case. It would mean every commit between LSPlant's tip and its very
  first Android 16 hook uses C++20 modules somewhere - i.e. modules
  adoption and Android 16 support arrived close enough together, or in
  the wrong order, that "find an older clean commit" and "keep Android 16
  support" are mutually exclusive on LSPlant's current `master`. If that
  turns out to be true, patching the regex further stops being the right
  move, and the real options become: (a) accept an LSPlant snapshot
  without full Android 16 support for now, by pinning `LSPLANT_GIT_TAG`
  explicitly to something before `16205e7`; (b) make this toolchain
  genuinely modules-capable - a real CMake 3.28+/Ninja upgrade path
  inside AGP's external native build, which is a materially bigger change
  than anything done across these three rounds and not something to
  attempt without being able to test it; or (c) go back to consuming
  LSPlant as a prebuilt Maven artifact and solve the static-linking
  problem from [What changed](#what-changed-from-the-original) a
  different way. None of these are needed *yet* - only if the warning
  actually fires - but it's worth knowing the decision tree in advance
  rather than improvising it under a fourth CI failure.

### Sources consulted for this fix

| Claim | Source |
| --- | --- |
| Exact new failure: file, line, column, and the fact that the search loop itself ran and resolved a commit before failing | `logs_84687441446.zip` → `0_build.txt` (this project's own CI run, 2026-08-07) |
| Column 8 is exactly where `dex_builder` starts in `import dex_builder;` | Direct character count in this sandbox (`"import dex_builder;".index('d')` → 7, so column 8) - pure local computation, no network |
| `dex_builder` is a separate repository (`LSPosed/DexBuilder`), built via `Android.mk`/ndk-build, not part of LSPlant's own source tree | [github.com/LSPosed/DexBuilder](https://github.com/LSPosed/DexBuilder) - `Android.mk` and `dex_builder.cc` both directly fetched |
| LSPlant has still not published anything past version `6.4` (April 2024) to Maven Central | Re-checked [mvnrepository.com](https://mvnrepository.com/artifact/org.lsposed.lsplant/lsplant) and [central.sonatype.com](https://central.sonatype.com/artifact/org.lsposed.lsplant/lsplant) |
| The combined module/import `REGEX` correctly matches both syntax shapes and correctly ignores near-miss lines, including two new ones specific to `import` | Local test in this sandbox: 13 cases run directly against the regex with `grep -E`, including `import(dex_builder);` and `importer.run();` |
| `git merge-base --is-ancestor` behaves as the new CMake check assumes for all three relevant orderings (strictly before / strictly after / equal to the Android-16-floor commit) | Local test in this sandbox: a throwaway repo with a marked commit, `git merge-base --is-ancestor` run directly (not simulated) for all three cases |

## Is there a fundamentally different way to do this?

Asked directly after the third CI failure in a row from the same
underlying dependency, and worth answering directly rather than just
patching the regex again and moving on. Two separate questions live
inside it, and they have different answers.

### "Is per-app native hooking the right approach at all, or is there a simpler lever?"

The appealing alternative is a single system-wide switch - one property,
flipped once at boot, instead of a native library injected into every app
process. Two independent findings this round argue against it existing in
a form that would actually work:

- **Every real prior-art project doing something in this neighborhood does
  it with app-level hooking, not a global switch.** An Xposed module that
  makes "video enhancement work for every video application" had to
  bypass a per-package whitelist check inside a specific system
  component, one package at a time
  ([XDA thread](https://xdaforums.com/t/xposed-a-xposed-module-to-make-video-enhancement-work-for-every-video-application.4158509/)).
  Unlocking HDR in Netflix on unsupported devices is done by spoofing
  device identity (`Build.MODEL`/`Build.MANUFACTURER`) through an Xposed
  module so the app's own capability check believes it's a Pixel
  ([Xiaomiui.net walkthrough](https://xiaomiui.net/how-to-enable-hdr-in-netflix-for-unsupported-devices-34106/)).
  Neither reaches for a system property, because HDR eligibility on
  Android isn't decided in one place - individual apps call
  `Display.getHdrCapabilities()`, query `MediaCodecInfo`, or check `Build`
  fields directly, and some (as the first thread found) apply their own
  server-controlled whitelists on top. That's exactly why this project
  hooks three separate call sites (`Display`, `MediaCodecInfo`, and direct
  decoder-profile probing) instead of one - see [What it actually
  hooks](#what-it-actually-hooks) - and prior art elsewhere hitting the
  same wall independently is reasonable evidence that's not
  over-engineering.
- **A genuine, documented SurfaceFlinger property mechanism exists
  (`SurfaceFlingerProperties`,
  [source.android.com](https://source.android.com/docs/core/graphics/surfaceflinger-props)),
  but testing a closely related one didn't work.** Android 13's "SDR
  dimming" feature is gated by a pair of system properties, and someone
  who actually flipped both of them on a rooted Pixel 6 Pro
  [reported no observable change](https://www.esper.io/blog/android-sdr-dimming)
  - the feature needed native SurfaceFlinger changes the properties alone
  didn't trigger. That's a different feature, not proof that *no*
  HDR-related property would work, but it's a concrete example of the
  failure mode: these properties are read by native compositor code at
  specific points, and getting the timing and the actual gate right from
  outside AOSP source access is not something to assume works without
  testing on the real device - which this sandbox, again, has no way to
  do (see "What I verified locally, and what I could not" in the sections
  above).

Bottom line on this question: no evidence turned up for a simpler,
reliable, single-point alternative, and real prior art solving adjacent
problems converged on the same app-level hooking this project already
does. That part of the architecture is very likely correct as-is.

### "Is fighting LSPlant's build requirements forever the only option?"

This is the question the last three sections actually bear on, and
there's more honest daylight here than the question above:

- **Keep pinning older commits (current approach).** Costs nothing extra
  today, and the search loop makes it self-updating rather than a
  hand-guessed date - but it has a shelf life. `LSPosed/LSPlant@0110cf7`
  ("upgrade to ndk 29 an use module partition") reads as a deliberate,
  permanent toolchain decision on LSPlant's side, not a transient
  regression that might revert. Every month that passes without this
  toolchain gaining real modules support, the newest buildable commit
  gets relatively older next to LSPlant's actual tip.
- **Make the toolchain itself modules-capable.** AGP genuinely supports
  pointing at a CMake other than the NDK-bundled 3.22.1 - `cmake.dir=` in
  `local.properties`, confirmed directly from
  [Android's own Cmake DSL reference](https://developer.android.com/reference/tools/gradle-api/8.3/null/com/android/build/api/dsl/Cmake)
  - which opens the door to CMake 3.28+ (the version that made C++20
    modules support non-experimental, requiring Clang 16+/GCC 14+, per
  [CMake's 3.28 release notes](https://cmake.org/cmake/help/latest/release/3.28.html))
  paired with a Ninja new enough for its dyndep-based module scanning.
  This is the option that actually ends the chase instead of postponing
  it. It's also a materially bigger change than anything done across
  three rounds so far - it needs a CI step installing a newer CMake and
  likely `-DCMAKE_MAKE_PROGRAM=` pointed at a matching Ninja, and there's
  no way to know from this sandbox whether AGP's external native build
  integration (its own incremental-build and IDE-sync metadata generation
  layered on top of raw CMake/Ninja) actually tolerates a `CXX_MODULES`
  file set cleanly, since that combination is unusual enough that it
  isn't something to assume works without a real test build. Worth
  attempting deliberately, in its own change, with room to fall back -
  not as a rushed reaction to a fourth CI failure.
- **Fall back to the last Maven release, `6.4`.** Still real, still
  confirmed-buildable (`LSPLANT_GIT_TAG=6.4`), still predates Android 16
  ART support entirely. A legitimate choice if a build that's certain to
  work matters more than Android 16 coverage for a while.
- **Replace LSPlant with a different ART-hooking library.** Deliberately
  listed last. This project already moved off YAHFA once, specifically
  because of an Android-version-support gap (see [What
  changed](#what-changed-from-the-original)) - trading one hooking
  library's problem for a different library's different problem, without
  first exhausting the cheaper options above, would be repeating that
  same mistake in the opposite direction.

Nothing here needs deciding today - the search loop keeps working right
now, and the Android-16-floor warning added this round means it'll say so
loudly, in the build log, the moment it stops being enough.

## Project layout

```
native/       Zygisk .so - CMake + LSPlant (source) + Dobby (native/src/main/cpp/disable_hdr.cpp)
              native/src/main/cpp/check_self_contained.cmake - build-time DT_NEEDED guard
hookdex/      Bridge.java - three `native` method stubs, no logic, no dependencies
flashable_module/   module.prop, customize.sh, sepolicy.rule, webroot/ (WebUI)
package.sh    Builds both Gradle modules and assembles dist/disable_hdr.zip
```

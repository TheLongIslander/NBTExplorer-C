# Building C-NBT Explorer

C-NBT Explorer supports Windows, macOS, and Linux through CMake. You need
CMake 3.21.1 or newer and a C11 compiler. CMake uses an installed zlib when it
can find one; otherwise it downloads the pinned zlib 1.3.2 source release and
builds it with the application.

## macOS and Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/bin/nbt_explorer level.dat
```

## Windows

Run these commands in PowerShell from a Visual Studio Developer PowerShell,
or use another CMake-supported compiler such as MinGW-w64:

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\bin\nbt_explorer.exe level.dat
```

To require a system or package-manager copy of zlib instead of allowing a
download, configure with `-DNBT_EXPLORER_FETCH_ZLIB=OFF`. Package managers such
as Homebrew, apt, vcpkg, and MSYS2 can provide zlib.

On macOS and Linux, the Makefile remains available for a lightweight build:

```sh
make
make test
```

## Native desktop application

The Qt 6 Widgets application is optional so the CLI does not require Qt. To
build it, install Qt 6.5 or newer and enable the desktop target:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DNBT_EXPLORER_BUILD_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/compiler
cmake --build build --config Release --parallel
```

If `Qt6Config.cmake` is already on CMake's search path, omit
`CMAKE_PREFIX_PATH`. The desktop target is named `cnbt_explorer`; its output is
`C-NBT Explorer.app` on macOS and `cnbt-explorer` on Windows and Linux.

Desktop builds fetch and statically link the pinned Bedrock-compatible Amulet
LevelDB backend and its pinned compression dependencies by default, so packaged
users do not have to install a LevelDB library separately. The first desktop
configure therefore needs network access; CMake reuses the downloaded sources
from that build directory on later runs. CLI-only builds do not fetch or link
this backend. Configure a desktop build with
`-DNBT_EXPLORER_BUNDLE_BEDROCK_LEVELDB=OFF` to skip the fetch and use the
runtime-loaded external backend instead; `BEDROCK_LEVELDB.md` describes the
external-library setup and its safety constraints.

The Qt installation and selected compiler architecture must match. On Apple
silicon, use an arm64 Qt installation for a native build. An Intel Qt under
`/usr/local` can instead produce an Intel build under Rosetta by configuring
with `-DCMAKE_OSX_ARCHITECTURES=x86_64`. A universal macOS build requires a Qt
installation containing both arm64 and x86_64 slices.

Qt's deployment tooling is run during installation and packaging, so the
result includes the Qt libraries and plugins it needs:

```sh
cmake --install build --config Release --prefix stage
```

Platform packages can be created as follows:

```sh
# macOS: creates a DMG
cpack --config build/CPackConfig.cmake -G DragNDrop -C Release -B dist

# Linux x86_64: creates an AppImage (downloads linuxdeploy tools on first run)
./packaging/build-appimage.sh ./build ./dist
```

On Windows, run these in PowerShell. The NSIS command requires NSIS to be
installed; ZIP packaging only needs CMake:

```powershell
cpack --config build/CPackConfig.cmake -G ZIP -C Release -B dist
cpack --config build/CPackConfig.cmake -G NSIS -C Release -B dist
```

Tagged builds and manually dispatched runs use the native desktop packaging
workflow to build a DMG, a Windows portable ZIP and installer, and an x86_64
AppImage. Manual runs retain the packages as GitHub Actions artifacts for
testing. A pushed `vX.Y.Z` tag must match the version declared by the CMake
`project()` command; after every platform succeeds, the workflow attaches the
packages and `SHA256SUMS` to a draft GitHub Release.

For a release, first update the version in the top-level CMake `project()`
command and merge that change to `main`. CMake and Make both inject that value
into the CLI and desktop application at build time. Run the packaging workflow
manually and test its artifacts. Then create and push only the intended
annotated tag:

```sh
git tag -a v0.2.0 -m "C-NBT Explorer 0.2.0"
git push origin v0.2.0
```

Review and test the draft release assets before publishing the release from
GitHub. Do not create the release in the GitHub UI before pushing the tag; the
workflow owns draft creation and asset upload.

The automated packages are unsigned. Public distribution without operating
system trust warnings requires an Apple Developer ID plus notarization for the
DMG and a code-signing certificate for the Windows executable and installer.

# C-NBT Explorer

C-NBT Explorer is a native, offline Minecraft NBT tree editor for macOS,
Windows, and Linux. It is a Qt desktop application—not a web app, local server,
or browser frontend—and its release packages include the runtime libraries they
need.

Use it to inspect and edit Java and Bedrock NBT, SNBT, region chunks, and
selected Bedrock world-database records. The source tree also contains a
scriptable command-line editor.

## Install and run the desktop app

Release packages are produced for all three desktop platforms. They are
currently unsigned, so only run a package obtained from a source you trust.

### macOS

1. Open the `C-NBT-Explorer-*.dmg` file.
2. Drag **C-NBT Explorer** into **Applications**.
3. Open it from Applications, or open a supported NBT file with it.

The macOS package is a universal application for Apple silicon and Intel Macs.
If Gatekeeper reports that the unsigned app cannot be opened, Control-click the
app and choose **Open**, or approve it under **System Settings → Privacy &
Security**. Do not disable Gatekeeper globally.

### Windows

- Installer: run the generated `.exe`, then launch **C-NBT Explorer** from the
  Start menu. The installer adds the application to Windows' **Open with** list
  without taking over generic extensions such as `.dat`.
- Portable: extract the entire Windows `.zip` and run
  `bin\cnbt-explorer.exe`. Keep the bundled DLL and plugin directories beside
  the executable.

Windows SmartScreen may warn about the unsigned package. Use **More info → Run
anyway** only after verifying where the file came from.

### Linux

```sh
chmod +x C-NBT-Explorer-*.AppImage
./C-NBT-Explorer-*.AppImage
```

The release AppImage targets x86_64 Linux with glibc 2.35 or newer (for
example, Ubuntu 22.04 or newer) and does not require installation. If the
system does not provide FUSE, AppImage's extraction fallback can be used:

```sh
./C-NBT-Explorer-*.AppImage --appimage-extract-and-run
```

## Desktop features

- Typed tree view for every standard NBT tag type, with recursive search,
  expandable nodes, multiple document tabs, and drag-and-drop file opening.
- Create Java NBT, Bedrock little-endian NBT, Bedrock `level.dat`, and SNBT
  documents.
- Edit values and compounds; add, rename, delete, duplicate, cut, copy, paste,
  and drag tags while enforcing compound-name and list-type rules.
- Unlimited undo/redo history for tag mutations, contextual right-click menus,
  and familiar platform shortcuts.
- Choose and switch populated chunks in `.mca` and `.mcr` region files while
  preserving the selected chunk's encoding and compression.
- Export any open tree as typed JSON or formatted SNBT.
- Browse a Bedrock world's LevelDB keys, decoded chunk coordinates where
  recognizable, value sizes, and likely value kinds. Standalone NBT-valued
  records open in the normal tree editor.
- Native file dialogs, file-open events, Windows **Open with** integration,
  Linux MIME metadata, and Unicode paths for normal file operations.

To browse a Bedrock world, use **File → Open Bedrock World Database…** and
select either the world folder or its `db` folder. Minecraft must be closed
first.

## Supported data

| Data | Support |
| --- | --- |
| Java NBT | Big-endian raw, gzip, and zlib documents; read and write |
| Bedrock NBT | Little-endian standalone documents and the eight-byte Bedrock `level.dat` envelope; read and write |
| SNBT | Parse, edit, create, serialize, and export |
| Anvil / McRegion | `.mca` and `.mcr` chunks using gzip, zlib, uncompressed, or Minecraft LZ4 streams; read and write |
| External region chunks | Modern `0x80` region stubs with `.mcc` sidecars; read, write, and back up |
| Legacy Cubic Chunks | `r2.<x>.<y>.<z>.mca` and `.mcr` layouts; read and write |
| NBT-based structures | `.schematic`, `.schem`, `.litematic`, and `.mcstructure` are editable as generic NBT when their contents use a supported encoding |
| Bedrock LevelDB | Browse binary-safe records; edit values that are exactly one complete little-endian NBT root |

Auto-detection examines the contents; the extension does not make a file NBT.
This means many Minecraft files named `.dat`, `.dat_old`, or `.nbt` work, but
not every `.dat` file is an NBT document.

## Safety and backups

Editing world data can make a world unusable even when the resulting NBT is
structurally valid. Keep a separate copy of the world before making important
changes.

- **Tools → Create Backup Before Saving** is enabled by default. Existing
  standalone and region files are copied to `.bak` before replacement; later
  collisions receive a UTC timestamp. Referenced `.mcc` sidecars are backed up
  with the region.
- Normal document and region saves use atomic replacement.
- With backups enabled, before a Bedrock database record is changed its
  previous raw value is saved under `<world>/cnbt-record-backups`. The update
  is a synchronous, WAL-backed LevelDB write batch.
- Close Minecraft before opening its Bedrock database. LevelDB takes a lock,
  and even a logically read-only open can perform recovery housekeeping. For
  forensic inspection, use a copied world.

## Current limitations

- Newer OpenCubicChunks `.3dr` and `.2dr` region formats are not supported.
- Bedrock LevelDB values do not share one universal schema. Only values that
  consist of exactly one complete little-endian NBT root can be opened and
  written as NBT. Custom records, concatenated roots, chunk/subchunk encodings,
  palettes, and binary suffixes remain visible in the record browser but are
  not interpreted or edited.
- The application does not coordinate related Bedrock keys such as actor data
  and chunk membership records. It deliberately avoids guessing versioned
  schemas that could corrupt a world.
- Structure and mod formats are edited as tag trees; there is no
  schema-specific block, entity, or world preview and no guarantee that an
  arbitrary semantic edit is valid for Minecraft or a particular mod.
- Documents are parsed into memory rather than streamed.

See [Bedrock LevelDB support](BEDROCK_LEVELDB.md) for backend compatibility,
database behavior, and additional safety details.

## Command-line interface

The optional `nbt_explorer` CLI supports automatic or explicit Java, Bedrock,
Bedrock `level.dat`, and SNBT input; tree printing; validation; typed JSON,
SNBT, and text dumps; path-based editing; and region chunk listing/editing.
Region coordinates passed to `--chunk` are local coordinates from `0` to `31`.

```sh
# Inspect or validate
./build/bin/nbt_explorer level.dat
./build/bin/nbt_explorer level.dat --validate

# Export
./build/bin/nbt_explorer player.dat --json player.json
./build/bin/nbt_explorer player.dat --snbt player.snbt

# Regions
./build/bin/nbt_explorer r.0.0.mca --list-chunks
./build/bin/nbt_explorer r.0.0.mca --chunk 4 7 --dump chunk.txt

# Edit an existing tag and atomically replace the source with a .bak copy
./build/bin/nbt_explorer level.dat \
  --edit Data/SpawnX 100 --in-place --backup

# Add or replace a path and write a new file
./build/bin/nbt_explorer input.snbt --format snbt \
  --set Data/GameType 1 --output changed.snbt
```

Mutation values use JSON expressions. Use `--delete <path>` to remove a tag,
`--rename <path> <new-name>` to rename one, `--output <path>` to keep the input
unchanged, or `--in-place --backup[=suffix]` for a backed-up replacement. Run
`nbt_explorer --help` for the complete syntax. Bedrock LevelDB browsing is a
desktop-app feature, not a CLI command.

## Build and test

The project uses CMake 3.21.1 or newer and a C11 compiler. The desktop target
also needs Qt 6.5 or newer. A typical desktop build is:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DNBT_EXPLORER_BUILD_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/compiler
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Desktop builds statically bundle a pinned Bedrock-compatible Amulet LevelDB
backend by default; no separate LevelDB install or server is required. CLI-only
builds do not require Qt.

See [BUILDING.md](BUILDING.md) for platform-specific builds and packaging,
[the test suite](tests/) for coverage, and
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) for bundled dependency
licenses. CI builds and tests the project on macOS, Windows, and Linux; the
desktop packaging workflow produces a universal macOS DMG, Windows installer
and portable ZIP, and Linux x86_64 AppImage.

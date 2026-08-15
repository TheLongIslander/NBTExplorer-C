# C-NBT Explorer: NBTExplorer Parity Roadmap

Last audited: 2026-08-14

Status: **The application is a usable NBT editor, but exact original-NBTExplorer feature parity is not complete.**

This document is the implementation handoff for maintainers and future AI agents. Update its checkboxes and notes as work lands. Do not mark a feature complete merely because a menu item or API stub exists; every item includes acceptance criteria that must pass first.

## 1. Goal and parity target

The goal is a native, offline, cross-platform replacement for the original NBTExplorer that:

- provides the user-facing capabilities of the original Windows application;
- runs as a normal desktop application on Windows, macOS, and Linux;
- keeps the C11 parsing, storage, and editing core reusable by the CLI;
- uses the C++17/Qt Widgets shell only for platform UI behavior;
- preserves the newer format and safety features already implemented here;
- never requires a browser, local server, hosted service, Java, .NET, or Mono.

The functional parity reference is the original NBTExplorer Windows 2.8.0 application and the behavior documented in its earlier 2.7.x releases. The Windows UI is the target because it was the most complete original interface. Parity means equivalent capability and safe behavior, not a pixel-identical copy of the old UI.

The original was not strictly Windows-only: its README documented Linux through Mono and a separate Mac interface. Its Windows interface nevertheless contained features that were missing or reduced on other platforms. C-NBT Explorer must expose the completed parity features consistently on all three supported operating systems.

### Primary original-NBTExplorer references

Use primary sources when resolving ambiguous behavior:

- [Original README and supported formats](https://github.com/jaquadro/NBTExplorer/blob/master/README.md)
- [Original release history](https://github.com/jaquadro/NBTExplorer/releases)
- [Windows menus, commands, shortcuts, recents, and refresh behavior](https://github.com/jaquadro/NBTExplorer/blob/master/NBTExplorer/Windows/MainForm.Designer.cs)
- [Directory/world data node](https://github.com/jaquadro/NBTExplorer/blob/master/NBTModel/Data/Nodes/DirectoryDataNode.cs)
- [Typed Find/Replace interface](https://github.com/jaquadro/NBTExplorer/blob/master/NBTExplorer/Windows/FindReplace.cs)
- [Search rule model](https://github.com/jaquadro/NBTExplorer/blob/master/NBTModel/Search/SearchRule.cs)
- [Coordinate-based Chunk Finder](https://github.com/jaquadro/NBTExplorer/blob/master/NBTExplorer/Windows/FindBlock.cs)
- [Region chunk deletion behavior](https://github.com/jaquadro/NBTExplorer/blob/master/NBTModel/Data/Nodes/RegionChunkDataNode.cs)
- [Windows NBT clipboard format](https://github.com/jaquadro/NBTExplorer/blob/master/NBTExplorer/Windows/NbtClipboardControllerWin.cs)
- [Nonstandard short-array node](https://github.com/jaquadro/NBTExplorer/blob/master/NBTModel/Data/Nodes/TagShortArrayDataNode.cs)

Do not silently expand the parity target to NBT Studio, NBT Workbench, or unrelated modern editors. Ideas from those applications belong in the post-parity section unless the user explicitly changes scope.

## 2. Non-negotiable engineering rules

Every remaining feature must preserve these rules:

1. **No server architecture.** The GUI remains a native Qt application bundle/executable.
2. **C core, Qt shell.** Put reusable parsing, mutation, search traversal, and region behavior in `h/` and `src/` where practical. Keep dialogs, models, clipboard integration, and file-manager integration in `gui/`.
3. **No silent data loss.** Preserve unknown tags, tag order on disk unless the user explicitly changes it, compression, endianness, region timestamps, and external chunk sidecars.
4. **Back up before destructive writes.** Existing backup preferences apply to standalone files, regions, sidecars, and Bedrock database records.
5. **Atomic writes where the platform permits them.** Never overwrite a source file directly while serialization is still in progress.
6. **Detect external changes.** A document must not overwrite a newer on-disk version without warning and an explicit user decision.
7. **Treat paths as Unicode.** Core-facing path strings are UTF-8; Windows filesystem operations use the existing wide-character platform layer.
8. **Do not infer format only from an extension.** Extensions help discovery and save dialogs; content validation remains authoritative.
9. **Never test destructive database or region operations on a real world.** Use generated disposable fixtures or copied worlds.
10. **Keep all current tests passing.** A parity feature may extend formats, but it must not regress Java, Bedrock, SNBT, LevelDB, `.mca`, `.mcr`, LZ4, `.mcc`, or Cubic Chunks r2 support.
11. **Be honest about unsupported schemas.** Browsing a Bedrock database does not imply that every custom binary record is safe to edit.

## 3. Completed baseline — do not reimplement

The following capabilities are already implemented and tested. Future work should reuse them.

- [x] C11 NBT/parser/storage/editing core and CLI.
- [x] Native C++17/Qt Widgets desktop application.
- [x] Standard tag types: End, Byte, Short, Int, Long, Float, Double, Byte Array, String, List, Compound, Int Array, and Long Array.
- [x] Tree value editing, add, rename for normal compound children, delete, duplicate, cut, copy, paste, and internal drag/move.
- [x] Compound-name uniqueness and list-element type enforcement.
- [x] Per-document undo/redo through snapshots.
- [x] Multiple tabs for different files.
- [x] Recursive, case-insensitive fixed-string filtering across displayed names, types, and value summaries.
- [x] Expand/collapse controls and file drag-and-drop.
- [x] New Java NBT, raw Bedrock NBT, Bedrock `level.dat`, and SNBT documents.
- [x] Java big-endian raw/gzip/zlib NBT.
- [x] Bedrock little-endian NBT and the Bedrock `level.dat` envelope.
- [x] SNBT parsing, creation, saving, and export.
- [x] Typed JSON and formatted SNBT export.
- [x] `.mca` and `.mcr` region read/write.
- [x] Region gzip, zlib, raw, and Minecraft LZ4 compression.
- [x] Modern external `0x80` chunk stubs and `.mcc` sidecars.
- [x] Legacy Cubic Chunks `r2.<x>.<y>.<z>.mca/.mcr` layout.
- [x] Filterable Bedrock LevelDB record browser.
- [x] Safe editing of LevelDB values that are exactly one complete little-endian NBT root.
- [x] Atomic standalone/region saves and backup-on-save preference.
- [x] Bedrock record raw-value backups.
- [x] Cross-platform path layer, CMake, CI, file associations, and desktop packaging.
- [x] Universal Intel/Apple-silicon macOS application bundle.

See `README.md`, `BUILDING.md`, and `BEDROCK_LEVELDB.md` for the current behavior and known non-parity limitations.

## 4. Remaining parity matrix

| ID | Original capability | Current state | Target milestone |
| --- | --- | --- | --- |
| PARITY-01 | Open an entire folder/world as a hierarchical source tree | Missing | M2 |
| PARITY-02 | Open the default Minecraft save folder | Missing | M2 |
| PARITY-03 | Recent files and recent folders | Missing | M3 |
| PARITY-04 | Reveal a source in Explorer/Finder/file manager | Missing | M3 |
| PARITY-05 | Refresh/reload content from disk safely | Missing | M3 |
| PARITY-06 | Save all modified sources | Missing | M3 |
| PARITY-07 | Find and Find Next with persistent results | Partial: live substring filter only | M4 |
| PARITY-08 | Typed, rule-based Find/Replace and delete matches | Missing | M4 |
| PARITY-09 | Coordinate-based Chunk Finder | Missing | M5 |
| PARITY-10 | Show region chunk world coordinates | Missing | M5 |
| PARITY-11 | Delete an entire region chunk safely | Missing | M5 |
| PARITY-12 | Open multiple chunks from the same region concurrently | Missing because tabs are deduplicated by path | M1/M5 |
| PARITY-13 | Multi-selection and bulk operations | Missing | M6 |
| PARITY-14 | System clipboard interoperability for NBT tags | Partial: internal process-only clipboard | M6 |
| PARITY-15 | Rename the root tag | Missing in GUI | M1 |
| PARITY-16 | Dedicated numeric/array and hexadecimal editors | Missing | M7 |
| PARITY-17 | Limited nonstandard `TAG_Short_Array` compatibility | Missing | M7 |
| PARITY-18 | Explicit Move Up/Move Down commands | Partial: single-node drag/move only | M1/M8 |
| PARITY-19 | Compound-first/alphabetical presentation option | Missing; disk order is preserved | M8 |
| PARITY-20 | Original shortcut/menu workflow and useful error logging | Partial | M8 |

## 5. Current architecture and constraints

Understanding these constraints will prevent unsafe rewrites.

### 5.1 Document model

`gui/Document.h` and `gui/Document.cpp` currently model one parsed root per `NbtDocument`. A region document represents one selected chunk and stores the region path plus local X/Z coordinates. A Bedrock database document represents one exact NBT-valued key.

Consequences:

- a path alone is not a unique tab identity for regions;
- raw `NBTTag*` pointers become stale after undo/redo or a model reset;
- the current whole-tree snapshot undo system can become expensive for bulk edits;
- a future workspace tree must not take ownership of document roots;
- a workspace node and an open editor tab need separate lifetimes.

### 5.2 Tree model

`gui/NbtTreeModel.*` rebuilds its internal nodes from `NBTTag*` after mutations. It currently allows one selected row and encodes in-process pointer addresses for internal drag/drop. Pointer payloads must never be reused for the system clipboard or retained by asynchronous search results.

### 5.3 Main window

`gui/MainWindow.*` owns tabs, actions, a single internal clipboard tag, and file dialogs. Any opened directory is currently interpreted specifically as a Bedrock `db` directory. There is no general workspace model or file-system dock.

### 5.4 Region core

`src/region_file.c`, `src/region_read.c`, and `src/region_write.c` own region layout, codecs, and atomic rewriting. Modern external chunks and legacy r2 layouts have different constraints. Region deletion and compaction must be implemented here rather than by manually editing header bytes in the GUI.

### 5.5 Mutation core

`src/edit_path.c`, `src/edit_value.c`, `src/edit_save.c`, and `src/nbt_tree.c` contain reusable path and tree mutations. Extend these APIs instead of duplicating mutation rules in Qt code when a feature is also useful to search/replace or the CLI.

## 6. Implementation roadmap

Milestones are ordered by dependency. M1 establishes stable identities needed by workspace search, region tabs, multi-selection, and asynchronous operations.

## M0 — Freeze the parity contract and test baseline

Purpose: make later work measurable and prevent accidental regressions.

### Tasks

- [ ] Add a concise machine-checkable parity checklist under `tests/parity/` or `docs/` that maps PARITY IDs to tests.
- [ ] Record representative original behavior from the primary sources linked above.
- [ ] Create only redistributable/generated fixtures; do not commit copyrighted real-world saves.
- [ ] Add fixtures for standalone Java NBT, raw NBT, Bedrock NBT, SNBT, `.mca`, `.mcr`, external `.mcc`, r2, malformed inputs, and non-ASCII paths.
- [ ] Establish performance baselines for a large compound, a large array, a full region header, and a directory containing many region files.
- [ ] Document which behaviors intentionally improve on the original, such as preserving on-disk order and always offering backups.

### Acceptance criteria

- Existing `make test` passes.
- Desktop CMake build and all CTest tests pass.
- The parity checklist names every PARITY ID in this document.
- Tests never write into a real Minecraft save directory.

## M1 — Stable source/tag identity and quick editing parity

Purpose: remove assumptions that block multiple region chunks, search results, root rename, and safe model resets.

### Proposed design

Introduce a generalized `DocumentSource` value/variant plus two explicit identities:

1. `DocumentSource`: standalone file, region chunk, or Bedrock record metadata without parallel source booleans in `NbtDocument`.
2. `DocumentIdentity`: source kind plus canonical path and, when applicable, region local coordinates or a binary-safe Bedrock database key.
3. `TagLocator`: a re-resolvable structural path from the root rather than a retained `NBTTag*`.

A `TagLocator` should store child positions and expected container/type/name metadata. Compound names alone are insufficient because malformed or noncanonical files can contain duplicate names. Re-resolution must fail safely if the tree changed incompatibly.

Suggested files:

- new `gui/DocumentSource.h/.cpp` and `gui/DocumentIdentity.h` or an equivalent combined value type;
- new `gui/DocumentManager.h/.cpp` to own open-source deduplication, tab routing, reload, and shared-container coordination;
- new `h/nbt_locator.h` and `src/nbt_locator.c` if locators are useful to the C search/mutation layer;
- updates to `gui/Document.*`, `gui/MainWindow.*`, and `gui/NbtTreeModel.*`;
- focused tests in `tests/gui_document_test.cpp` plus new C locator tests.

### Tasks

- [ ] Replace path-only tab deduplication with `DocumentIdentity` comparison.
- [ ] Replace the current source-specific booleans/parallel fields with `DocumentSource` without changing serialized behavior.
- [ ] Move open-document ownership/deduplication out of the growing `MainWindow` class and into `DocumentManager`.
- [ ] Permit separate tabs for `(region path, chunk X, chunk Z)`.
- [ ] Preserve normal deduplication for the same standalone file or same database record.
- [ ] Add safe tag-locator construction and resolution.
- [ ] Stop exposing raw pointer addresses anywhere that may outlive one synchronous drag operation.
- [ ] Allow renaming the root tag through the GUI, including an empty name where the selected format permits it.
- [ ] Add explicit Move Up and Move Down actions with original-style shortcuts where they do not conflict with platform conventions.
- [ ] Ensure moving list entries preserves list homogeneity and compound moves preserve name uniqueness.
- [ ] Define canonicalization behavior for case-insensitive filesystems, symlinks, UNC paths, and Unicode normalization.

### Acceptance criteria

- Two different chunks from one region can remain open simultaneously.
- Reopening the same exact source focuses its existing tab.
- Undo/redo invalidates no retained search/selection locator silently.
- Root names round-trip for Java, Bedrock, raw, gzip, zlib, and SNBT saves.
- Move Up/Down is one undoable operation and cannot move a node outside its valid container.

## M2 — Folder and Minecraft world explorer

Purpose: restore the original application's central whole-world navigation workflow.

### Proposed UI

Add a dockable workspace/source tree beside the existing document tabs. Keep open documents in tabs; the workspace tree discovers sources and opens them lazily.

Suggested files:

- new `gui/WorkspaceModel.h/.cpp`;
- new `gui/WorkspaceController.h/.cpp` if model orchestration outgrows `MainWindow`;
- new `gui/WorkspaceNode.h` or a private node structure;
- new `h/region_index.h` and `src/region_index.c` if a lightweight header-only region index cannot reuse existing APIs safely;
- updates to `gui/MainWindow.*` and the Qt resource file;
- optional C header-only format-sniff helpers if existing loaders cannot inspect cheaply.

### Required node types

- workspace root;
- directory;
- recognized standalone NBT/SNBT file;
- region file;
- populated region chunk;
- Java world and dimension grouping where recognizable;
- player data, data, entities, POI, and region directories as normal folders rather than hard-coded schemas;
- Bedrock world/database entry that invokes the existing database browser without locking it during passive scanning.

### Tasks

- [ ] Add **File → Open Folder…**.
- [ ] Add **File → Open Minecraft Save Folder**.
- [ ] Discover the conventional Java saves directory per platform, while allowing a user override:
  - Windows: `%APPDATA%/.minecraft/saves`
  - macOS: `~/Library/Application Support/minecraft/saves`
  - Linux: `~/.minecraft/saves`
- [ ] Scan directories lazily; never parse every NBT payload just to expand a folder.
- [ ] Read only region headers to enumerate chunks, then decompress a chunk on open.
- [ ] Recognize supported extensions but retain an **All files / try as NBT** path for unusual names.
- [ ] Prevent symlink/junction loops and handle permission errors as visible child errors rather than crashes.
- [ ] Add context actions: Open, Open in New Tab, Refresh, Reveal in File Manager, and Close Workspace.
- [ ] Show modified/open state without transferring `NbtDocument` ownership to the workspace model.
- [ ] Route a dropped normal directory to the workspace explorer and a detected Bedrock `db` directory to the Bedrock database browser.
- [ ] Keep scanning off the UI thread for large worlds; deliver results incrementally and support cancellation.
- [ ] Attach generation IDs to asynchronous scans so results from a cancelled/replaced workspace cannot update the new model.
- [ ] Do not open or lock LevelDB during ordinary directory expansion.

### Acceptance criteria

- A normal Java world can be opened once and browsed from `level.dat` through dimensions, region files, and populated chunks.
- Expanding a large region directory does not freeze the UI while parsing every chunk.
- Inaccessible, malformed, and unsupported files do not prevent sibling files from appearing.
- Negative region filenames and r2 filenames are parsed correctly.
- Closing a workspace does not close modified tabs without the standard save prompt.
- Windows junctions, Unix symlinks, and non-ASCII paths have automated coverage.

## M3 — Source lifecycle: recents, refresh, reveal, and Save All

Purpose: match the original source-management workflow without risking external-change data loss.

### Tasks

- [ ] Add bounded Recent Files and Recent Folders menus using `QSettings`.
- [ ] Store canonical paths, remove missing entries gracefully, and offer **Clear Recent Items**.
- [ ] Add **Reveal in Explorer/Finder/File Manager** through platform-appropriate Qt/native APIs.
- [ ] Add F5 **Refresh from Disk** for the selected source/workspace branch.
- [ ] Capture a `SourceStamp` at load: canonical path, size, modification time, and a stronger content identity where required.
- [ ] Before save, detect whether the source changed externally.
- [ ] For a clean document, refresh may reload immediately after confirmation.
- [ ] For a modified document, offer Cancel, Discard and Reload, Save As, or an explicit overwrite path. Never default to overwrite.
- [ ] Add **Save All Modified Sources**.
- [ ] Preflight all destinations, backup requirements, locks, and external-change conflicts before Save All starts.
- [ ] Explain that cross-file saving cannot be globally atomic; save each source atomically and return a per-source success/failure summary.
- [ ] Keep failed documents dirty and focus the first failure.
- [ ] Update workspace badges and recent menus after moves, Save As, or deleted sources.

### Acceptance criteria

- Recent items persist across launches on Windows, macOS, and Linux.
- Reveal selects the file where supported or opens the containing directory otherwise.
- An externally modified file is never overwritten without a warning.
- Save All never marks a failed document clean.
- Backups are created consistently for every eligible source in Save All.
- Refreshing one branch does not discard unrelated open documents.

## M4 — Original-style Find, Find Next, and rule-based Replace

Purpose: replace the current display filter with a complete search tool while retaining the fast filter for simple navigation.

### Search scopes

- selected tag subtree;
- current document;
- all open documents;
- selected workspace folder/world;
- selected region file or dimension.

### Rule capabilities

- tag name: exists, exact, contains, starts with, ends with, optional regular expression;
- tag type: one type, a set of types, numeric scalar, array, container;
- scalar value: exact and type-aware comparison;
- numeric value: `=`, `!=`, `<`, `<=`, `>`, `>=`, and range;
- string value: exact/contains/prefix/suffix/regular expression with case option;
- array/list length comparisons;
- path matching;
- AND/OR rule groups;
- optional inclusion of array elements rather than only array length summaries.

### Proposed design

Create a reusable traversal/result layer. Results must hold `DocumentIdentity` plus `TagLocator`, never `NBTTag*` from a worker thread.

Suggested files:

- new `h/nbt_search.h` and `src/nbt_search.c` for typed tree traversal and non-UI predicates;
- new `gui/SearchDialog.h/.cpp`;
- new `gui/SearchResultsModel.h/.cpp`;
- new `gui/SearchDock.h/.cpp` or a combined results panel;
- new `gui/ReplacePreviewDialog.h/.cpp`;
- CLI integration only after the core API is stable.

### Tasks

- [ ] Keep the current live filter as **Quick Filter**.
- [ ] Add **Find…** and original-style **Find Next** (F3).
- [ ] Add **Find Previous** (Shift+F3) as a small cross-platform usability extension, while keeping it separate from the strict original parity claim.
- [ ] Show persistent result rows containing source, path, type, and value summary.
- [ ] Double-clicking a result opens/focuses its source, resolves the locator, and selects the tag.
- [ ] Run workspace searches in cancellable workers over immutable data or synchronized snapshots.
- [ ] Place configurable limits on results and total work; surface truncation.
- [ ] Add Replace Current, Replace Selected Results, Replace All, and Delete Matches.
- [ ] Preview every batch mutation with source/path/old/new values before committing.
- [ ] Validate the entire operation before mutating a document where possible.
- [ ] Group all replacements in one document into one undo command.
- [ ] Re-resolve locators before commit and reject stale results rather than editing the wrong tag.
- [ ] For workspace results in unopened files, use the normal loader, backup, atomic save, and error pipeline.
- [ ] Guard regular-expression execution and cancellation so a pathological query cannot hang the application indefinitely.

### Acceptance criteria

- Find Next/Previous cycles deterministically and wraps only after informing the user.
- Numeric comparisons do not compare formatted strings lexicographically.
- Case-sensitive and case-insensitive name/string rules are tested.
- AND/OR groups reproduce representative original Find/Replace behavior.
- Batch replacement is undoable per document and does not leave half-mutated trees after validation failure.
- Search remains responsive on a generated large world and can be cancelled.
- Stale results after edit/undo/refresh are detected safely.

## M5 — Chunk Finder and complete region chunk management

Purpose: match the original coordinate workflow and whole-chunk deletion while preserving modern codecs and sidecars.

Before adding multi-chunk mutations, introduce a shared `RegionSession` (or equivalent container coordinator) per canonical region path. It should own the file fingerprint, dirty chunk overlays, pending deletions, and the single atomic commit. Independent chunk tabs must never race by each rewriting a stale copy of the same region.

Suggested GUI files include `gui/RegionSession.h/.cpp` and, if useful, `gui/RegionBrowser.h/.cpp` for a 32×32 grid/list view with coordinates, timestamp, compression, size, external status, and parse errors.

### Coordinate utilities

Add reusable, tested conversion helpers for:

- block coordinate → chunk coordinate;
- chunk coordinate → region coordinate;
- chunk coordinate → local region coordinate `0..31`;
- region filename + local coordinate → world chunk coordinate;
- negative coordinates using mathematical floor division, not C/C++ truncation toward zero;
- legacy r2 filenames, including their Y region coordinate where relevant.

Suggested files:

- new `h/minecraft_coords.h` and `src/minecraft_coords.c`;
- new `gui/ChunkFinderDialog.h/.cpp`;
- updates to `gui/BedrockDatabaseDialog.cpp` only if shared coordinate formatting is beneficial;
- tests in a new `tests/test_minecraft_coords.c`.

### Chunk Finder tasks

- [ ] Add **Search/Tools → Chunk Finder…**.
- [ ] Accept block, chunk, or region/local coordinates.
- [ ] Let the user choose an opened world/dimension or browse to a region directory.
- [ ] Resolve the expected `.mca`/`.mcr` region and local X/Z.
- [ ] Open and select the target chunk if present.
- [ ] Clearly distinguish missing region file, empty chunk slot, corrupt chunk, and unsupported layout.
- [ ] Display both local and world coordinates in the normal region chooser and workspace tree.
- [ ] Coordinate all tabs for one physical region through the shared `RegionSession`.
- [ ] Commit several dirty chunk tabs and pending deletions in one region rewrite rather than last-writer-wins saves.

### Region deletion API

Add a core operation similar to:

```c
int region_file_delete_chunk(
    RegionFile* region,
    int chunk_x,
    int chunk_z,
    char* err,
    size_t err_sz
);
```

The exact API may differ, but deletion must clear the location/timestamp, release sectors during rewrite, and track whether an external sidecar becomes orphaned.

### Region deletion tasks

- [ ] Implement deletion in the region core rather than editing bytes from the GUI.
- [ ] Add a confirmation dialog showing local/world coordinates, compression, timestamp, and external-sidecar state.
- [ ] Make deletion undoable before save by retaining the complete compressed slot/metadata or an equivalent safe snapshot.
- [ ] Rewrite and compact the region atomically through the existing writer.
- [ ] Back up the region and referenced sidecar before committing.
- [ ] For an external chunk, stage sidecar retirement so a crash cannot leave the only payload deleted before the new region header is durable.
- [ ] Do not apply modern `.mcc` assumptions to legacy r2 files.
- [ ] Respect the r2 inline payload limit and header-sector rules.
- [ ] Decide and document whether empty external sidecars are retained, moved into the backup set, or removed after a successful commit.

### Acceptance criteria

- Coordinate tests cover `-33`, `-32`, `-1`, `0`, `31`, `32`, and region boundaries in both axes.
- World coordinates displayed for `r.-1.-1.mca` are correct.
- Deleting gzip, zlib, raw, LZ4, external, `.mca`, `.mcr`, and r2 chunks is tested.
- Other chunk payloads and timestamps remain byte-for-byte or semantically unchanged as appropriate.
- A failed write leaves the original region and sidecar usable.
- Backup restoration recovers an externally stored deleted chunk.
- Multiple chunks from one region can stay open; a delete invalidates or closes only the deleted chunk after confirmation.
- Two independently edited chunks from the same region survive one coordinated save and reopen with both changes.

## M6 — Multi-selection, bulk operations, and system clipboard

Purpose: match the original Windows tree workflow and make copy/paste safe across tabs, processes, and platforms.

### Multi-selection tasks

- [ ] Change the tree to extended row selection.
- [ ] Normalize a selection so selecting a parent and its descendant does not mutate the descendant twice.
- [ ] Add bulk delete, copy, cut, duplicate, and move where container rules permit.
- [ ] Reject mixed operations that would violate list element types or duplicate compound names before mutating anything.
- [ ] Make each bulk action one undo command per document.
- [ ] Preserve a sensible selection after delete/move.
- [ ] Add keyboard behavior consistent with each desktop platform.

### Clipboard design

Do not put pointers in the system clipboard. Define a versioned, size-bounded format such as:

- custom MIME: `application/x-cnbt-tag-list`;
- magic/version/flags header;
- count and length-prefixed serialized tags;
- explicit encoding and validation;
- `text/plain` SNBT fallback for interoperability and debugging.

Suggested files:

- new `gui/NbtClipboardCodec.h/.cpp` or reusable `h/nbt_clipboard.h` and `src/nbt_clipboard.c`;
- updates to `gui/MainWindow.*` and `gui/NbtTreeModel.*`;
- Qt clipboard tests guarded for headless CI.

### Tasks

- [ ] Copy selected tags into `QClipboard` using the custom MIME format.
- [ ] Paste custom NBT data between tabs and between two running app instances.
- [ ] Accept valid SNBT text when custom data is absent, after showing the inferred type/name behavior.
- [ ] Treat clipboard data as untrusted: validate counts, lengths, tag types, nesting depth, and total allocation.
- [ ] Resolve naming collisions predictably and preview renamed copies when more than one tag is pasted.
- [ ] Keep an in-memory fallback only if the platform clipboard cannot retain custom data.
- [ ] Clear or replace stale internal clipboard ownership correctly on application exit.

### Acceptance criteria

- Multi-delete and multi-paste are atomic from the user's perspective and undo in one step.
- List-type violations make no partial changes.
- Clipboard round-trips every standard tag type and nested containers.
- Malformed and oversized clipboard payloads fail without excessive allocation or crashes.
- Cross-process clipboard tests pass on Windows, macOS, and Linux runners where GUI sessions are available.
- Plain SNBT copied from a text editor can be pasted through an explicit, validated path.

## M7 — Dedicated editors and nonstandard short-array compatibility

Purpose: restore the original specialized editing workflow without converting huge arrays into one fragile text field.

### Type-specific editors

- [ ] Provide validators and range-aware dialogs for Byte, Short, Int, Long, Float, and Double.
- [ ] Keep a multiline editor for strings and preserve embedded control characters correctly.
- [ ] Add a virtualized array table model for Byte Array, Int Array, and Long Array.
- [ ] Support signed decimal and hexadecimal presentation.
- [ ] Add offset/address, index, value, and optional ASCII columns where meaningful.
- [ ] Support copy/paste of selected array ranges with strict parsing and overflow reporting.
- [ ] Do not duplicate a multi-gigabyte array merely to render it.
- [ ] Preserve generic JSON/SNBT editing as an advanced alternative, not the only editor.
- [ ] Consider a list bulk editor for primitive lists, while using the normal tree for nested containers.

Suggested files:

- new `gui/ScalarEditorDialog.h/.cpp`;
- new `gui/ArrayEditorDialog.h/.cpp`;
- new `gui/ArrayTableModel.h/.cpp`;
- focused Qt tests for boundaries, hex parsing, cancellation, and large-array paging.

### `TAG_Short_Array` research gate

The short-array tag is nonstandard and used by some historical mods. The original 2.7.6 release described only read/update/delete support. Do not assign a wire ID or enable creation based on guesswork.

- [ ] Confirm the exact wire tag ID, endianness, list behavior, and format scope from the original source and generated fixtures.
- [ ] Audit every range assumption such as `type <= TAG_Long_Array` before adding another enum value.
- [ ] Extend `NBTTag`, parser, serializer, clone/free, JSON/SNBT representation, edit logic, summaries, and array editor.
- [ ] Initially expose read/update/delete only, matching the original's conservative behavior.
- [ ] Reject the extension in formats where its ID conflicts or where no authoritative encoding exists.
- [ ] Preserve unsupported short arrays losslessly when the user edits unrelated sibling tags.

### Acceptance criteria

- Every scalar min/max boundary has a passing and failing test.
- Hex editing round-trips negative signed values without changing bit patterns.
- Large arrays scroll/edit without constructing one giant display string.
- Cancel leaves the document and undo stack unchanged.
- Short-array fixtures parse, update, delete, save, and reopen without affecting sibling tags.
- Standard NBT and Bedrock parsing remain unchanged for tag IDs 0 through 12.

## M8 — Remaining original workflow polish and parity release

Purpose: close smaller behavioral gaps and ship a verifiable parity release.

### Tasks

- [ ] Add an optional presentation sort matching the original's compound-first/alphabetical view.
- [ ] Keep serialized/on-disk order unchanged unless the user explicitly performs a reorder.
- [ ] Complete shortcut parity where it does not conflict with native conventions:
  - Ctrl/Cmd+O: Open
  - Ctrl/Cmd+Shift+O: Open Folder
  - Ctrl/Cmd+S: Save
  - F5: Refresh
  - Ctrl/Cmd+F: Find
  - F3 / Shift+F3: Find Next/Previous
  - Ctrl/Cmd+H: Replace
  - Delete: Delete selection
  - F2: Rename
- [ ] Add menu help/tooltips for potentially destructive whole-chunk and batch operations.
- [ ] Add a user-visible error log location and copyable diagnostic details.
- [ ] Log source type, format, operation, and error without logging private NBT payloads by default.
- [ ] Ensure all long-running scans and searches have progress and cancellation.
- [ ] Audit accessibility names, keyboard navigation, high-DPI behavior, dark/light themes, and screen-reader labels.
- [ ] Update README feature/limitation tables only after acceptance tests pass.
- [ ] Add a migration note for users coming from original NBTExplorer.
- [ ] Build and smoke-test the macOS DMG, Windows NSIS/ZIP, and Linux AppImage.
- [ ] Re-run malformed-input, sanitizer, static-analysis, and cross-compilation checks.

### Acceptance criteria

- Every PARITY ID is checked off and linked to at least one automated or documented manual test.
- The three desktop packages launch without a server or separately installed Qt/LevelDB runtime.
- File associations open supported files correctly.
- A representative original-NBTExplorer workflow can be completed on every platform:
  1. open a Minecraft world folder;
  2. locate a chunk by coordinates;
  3. find and replace a typed tag;
  4. copy multiple tags through the system clipboard;
  5. delete a whole chunk with a backup;
  6. refresh after an external change;
  7. save all modified sources;
  8. close and reopen recent sources.

## 7. Required test strategy

Every milestone must add tests at the lowest practical layer.

### 7.1 C unit/integration tests

Use these for:

- tag locator resolution;
- coordinate conversion;
- typed search predicates and traversal;
- bulk mutation validation;
- region deletion, compaction, timestamp, and sidecar behavior;
- short-array parsing/serialization;
- malformed/oversized input handling.

Prefer generated fixtures and byte-level assertions where format preservation matters.

### 7.2 Qt tests

Use these for:

- document identity and tab deduplication;
- root rename and specialized editors;
- workspace lazy loading;
- multi-selection behavior;
- search-result navigation and stale-result handling;
- refresh/external-change prompts;
- Save All status handling;
- clipboard codecs and supported cross-process behavior.

Keep core assertions independent of a visible display when possible. Use an offscreen platform plugin in CI where supported.

### 7.3 End-to-end tests

Add disposable workflows covering:

- folder → region → chunk → tag → edit → backup → save → reopen;
- search/replace across several standalone and region sources;
- coordinate lookup across negative and positive regions;
- whole-chunk deletion and backup restoration;
- external `.mcc` deletion/recovery;
- simultaneous tabs for several chunks from one region;
- external file modification while a tab is dirty;
- non-ASCII and long paths;
- application launch through file associations.

### 7.4 Safety/failure injection

Where practical, simulate:

- allocation failure;
- serialization failure;
- permission denial;
- disk-full/short write;
- failed atomic replacement;
- sidecar copy/delete failure;
- stale search result;
- LevelDB lock contention;
- application interruption between staging and commit.

The invariant is that the original data and its backup remain recoverable.

## 8. Risk register and design warnings

### R1 — Stale tag pointers

Undo/redo rebuilds roots and invalidates raw pointers. Search results, clipboard items, selections, and asynchronous work must use locators or serialized copies.

### R2 — Region tab identity

Using only the region path as a tab key prevents multiple chunks and can focus/edit the wrong chunk. Include local coordinates in document identity.

Distinct tab identity does not by itself solve concurrent writes. All chunk documents for the same physical region need a shared session or commit coordinator so one save cannot discard another tab's dirty overlay.

### R3 — External `.mcc` lifecycle

The region header and sidecar are two filesystem objects. Deletion is not automatically atomic across both. Stage backups and commit order explicitly.

### R4 — Negative coordinate math

C/C++ integer division truncates toward zero. Minecraft region math needs floor division. Centralize and test it.

### R5 — Bulk undo memory

The current whole-tree snapshot strategy is simple but expensive. Measure it before workspace-wide replace. If necessary, introduce command deltas without weakening rollback guarantees.

### R6 — Threading

`NBTTag` trees are not thread-safe. A worker must operate on immutable snapshots or own its parsed tree. Never mutate a live document from a search thread.

### R7 — Nonstandard tag IDs

`TAG_Short_Array` can conflict with later extensions or format-specific tags. Confirm the authoritative encoding before changing enum ranges or auto-detection.

### R8 — Cross-file Save All

Atomic replacement is per file, not global. Preflight first, then report partial success accurately. Never imply a transactional whole-world commit.

### R9 — File-system traversal

Worlds can contain symlink loops, inaccessible directories, huge files, and non-NBT `.dat` files. Scan lazily, cap work, and isolate errors.

### R10 — Bedrock database scope

LevelDB keys and values have many versioned schemas. The current exact-one-NBT rule remains in force unless a separate schema-aware design is implemented and tested.

## 9. Explicitly deferred post-parity work

These would be useful, but they are not required to claim original NBTExplorer parity:

- schema-aware Bedrock chunk, subchunk, palette, actor, and `digp` editing;
- coordinated multi-key Bedrock transactions at the Minecraft schema level;
- newer OpenCubicChunks `.3dr` and `.2dr` formats;
- visual block, entity, structure, inventory, or world previews;
- JSON import as a first-class document format;
- regex bookmarks, saved searches, favorites, and session/tab restoration;
- autosave/crash-recovery journals beyond normal backups;
- plugin or scripting APIs;
- remote/cloud world access;
- a web UI or server mode, which is intentionally outside the product design.

Keep these in separate roadmap sections or issues so they do not obscure remaining original parity work.

## 10. Definition of complete parity

Do not describe the application as having all original NBTExplorer functions until all of the following are true:

- [ ] PARITY-01 through PARITY-20 are implemented or explicitly waived by the user with a documented reason.
- [ ] Every completed item has automated coverage or a repeatable manual test where automation is impractical.
- [ ] Existing Java, Bedrock, SNBT, region, LZ4, external chunk, Cubic r2, and LevelDB tests still pass.
- [ ] Backups and external-change checks cover every new destructive operation.
- [ ] Windows, macOS, and Linux desktop packages pass the representative workflow in M8.
- [ ] The README accurately distinguishes full NBT editing from intentionally limited Bedrock custom-record support.
- [ ] No desktop feature requires a local server, browser, .NET/Mono runtime, or separately installed LevelDB.
- [ ] The final package dependency audits contain no developer-machine paths.

At that point, the accurate description becomes:

> C-NBT Explorer is a native cross-platform reimplementation of the original NBTExplorer feature set, with additional modern Java/Bedrock formats, safety features, exports, tabs, undo/redo, and packaging.

Until then, use:

> C-NBT Explorer is an NBTExplorer-style native editor with most core tag-editing functions and several newer formats; exact feature parity is still in progress.

## 11. Future-agent handoff checklist

Before starting a parity item:

1. Read `plan.md`, `README.md`, `BUILDING.md`, and `BEDROCK_LEVELDB.md`.
2. Identify the PARITY ID and milestone being implemented.
3. Inspect the current implementation; do not assume this plan reflects uncommitted later changes.
4. Consult the linked original source for behavioral details.
5. Run the existing baseline tests.
6. Write or update the acceptance test before declaring the item complete.
7. Reuse existing platform, backup, serialization, and atomic-save APIs.
8. Test only against disposable/generated data.
9. Update the relevant checkbox and add a short implementation note below it.
10. Update the README only when user-visible behavior is actually available.

Recommended baseline commands:

```sh
make test

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DNBT_EXPLORER_BUILD_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/compiler
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For release work, also follow the platform packaging and signing notes in `BUILDING.md`.

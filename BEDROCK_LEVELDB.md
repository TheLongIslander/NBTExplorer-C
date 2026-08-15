# Bedrock LevelDB support

Bedrock Edition stores most world data in the `db` directory of a world using
LevelDB, while Java Edition uses Anvil region files. Microsoft documents that
distinction here:

- <https://learn.microsoft.com/minecraft/creator/documents/differencesbetweenbedrockandjava>

This is not stock Google LevelDB. Bedrock databases contain raw-zlib-compressed
table blocks. C-NBT Explorer therefore refuses to guess at or automatically
load a generic system `libleveldb`; doing so can make existing records
unreadable and can produce incompatible new records.

## Compatible backend

The adapter in `src/bedrock_db.c` targets the maintained Amulet-Team fork of
LevelDB. The tested core revision is:

```text
https://github.com/Amulet-Team/leveldb
1352243bb27d287b27be94f7591218dddb3ef900
```

That is also the core revision pinned by Amulet LevelDB revision
`736228e375e9f535b6522bd28168fc306a5354b9`. Its public options define
`kZlibRawCompression = 0x4`, and its table reader and writer implement that
compression type:

- <https://github.com/Amulet-Team/leveldb/blob/1352243bb27d287b27be94f7591218dddb3ef900/include/leveldb/options.h>
- <https://github.com/Amulet-Team/leveldb/blob/1352243bb27d287b27be94f7591218dddb3ef900/table/format.cc>
- <https://github.com/Amulet-Team/leveldb/blob/1352243bb27d287b27be94f7591218dddb3ef900/table/table_builder.cc>

Desktop builds bundle this revision statically by default. Configure with
`-DNBT_EXPLORER_BUNDLE_BEDROCK_LEVELDB=OFF` to opt out. The pinned project
provides CMake target `leveldb`; its static transitive targets are `crc32c`,
`snappy`, `libzstd_static`, and `zlibstatic`. C-NBT Explorer calls the public
`include/leveldb/c.h` C ABI and sets compression type 4
(`leveldb_zlib_raw_compression`). No LevelDB dynamic library is required by a
default desktop package.

CLI-only and opt-out builds retain the runtime bridge. For those builds, build
the compatible project as a shared library, then supply its absolute path to
`bedrock_db_open`, or set:

```sh
export NBT_EXPLORER_LEVELDB_LIBRARY=/absolute/path/to/libleveldb.so
```

Use the corresponding `.dylib` on macOS or `.dll` on Windows. This fallback
keeps lightweight CLI-only builds free of a mandatory LevelDB dependency.

The bundle uses immutable revisions for LevelDB (`1352243...`), CRC32C
(`2bbb3be...`), Snappy (`6af9287...`), and zlib (`51b7f2a...`), plus a
SHA-256-verified Zstandard 1.5.7 archive. License notices required for binary
redistribution are installed from `THIRD_PARTY_NOTICES.txt` (inside macOS app
resources, or under the installed documentation directory on Windows/Linux).

## Safety model

- Close Minecraft before opening a world and make a backup of the whole world.
- The database is opened with `create_if_missing` disabled, paranoid checks
  enabled, checksum verification enabled, and raw-zlib selected for writes.
- All edits use one synchronous, WAL-backed LevelDB write batch. A batch is
  atomic at the LevelDB layer.
- Logical read-only mode rejects all writes through this API. LevelDB itself
  has no truly read-only `DB::Open`: it still acquires the database lock and may
  perform recovery housekeeping. For forensic use, work on a copied world.
- LevelDB permits only one process to open a database for writing. A lock error
  is surfaced instead of bypassed.
- The wrapper never rewrites SST, manifest, or log files itself.

## Data scope

`bedrock_db_get`, `bedrock_db_iterate`, and
`bedrock_db_apply_mutations` provide binary-safe key/value access. Keys and
values may contain NUL bytes.

`bedrock_db_get_nbt` and `bedrock_db_put_nbt` are intentionally narrower: they
operate only on a value that is exactly one complete little-endian NBT root.
They reject trailing data rather than risk discarding an adjacent NBT root or a
record-specific binary suffix.

Bedrock's database values do not all share one schema. Microsoft documents, for
example, that modern actor records use `actorprefix<ActorUniqueID>` keys and
that `digp<Chunk Key>` records map chunks to actors. It also documents the
non-actor chunk key IDs and the older actor layout:

- <https://learn.microsoft.com/minecraft/creator/documents/actorstorage>

Because those layouts vary by game version, this low-level layer deliberately
does not invent a universal chunk or actor schema. Higher-level editors must
update related keys together (for example, an actor and its `digp` membership)
and preserve unknown fields. Raw records, concatenated NBT sequences, subchunk
palettes, biome/height data, and version-specific chunk encodings remain
available through the binary APIs but are not interpreted by the exact-one-NBT
convenience helpers.

## Integration test

The opt-in integration test modifies a disposable database and needs a built
Amulet shared library:

```sh
tests/run_bedrock_db_integration.sh \
  /absolute/path/to/libleveldb \
  /absolute/path/to/disposable/world/db
```

The test covers binary get/write/delete, iteration, an atomic batch, exact
little-endian NBT round trips, reopen behavior, logical read-only enforcement,
and a five-megabyte value forced into a raw-zlib SST block and read back
byte-for-byte. Never point this integration test at a real world.

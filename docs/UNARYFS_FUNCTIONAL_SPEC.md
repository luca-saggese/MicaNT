# UnaryFS — Functional Specification

**Project:** MicaNT  
**Document:** `docs/UNARYFS_FUNCTIONAL_SPEC.md`  
**Status:** Proposed  
**Version:** 0.1  
**Scope:** Filesystem-agnostic virtual layered filesystem  
**Architecture:** AMD64 / NT-compatible  
**Primary integration:** Io, FsRtl, Mm, Ob, Se, Ps

---

## 1. Purpose

UnaryFS is a lightweight virtual filesystem layering mechanism for MicaNT.

It provides a unified filesystem view composed of an existing base filesystem and an ordered collection of sparse layers.

Each layer stores only changes relative to the visible filesystem beneath it.

UnaryFS does not replace FAT32, ext2, ext4, NTFS or any other physical filesystem.

It operates above the canonical MicaNT filesystem interfaces and is independent of the underlying on-disk filesystem format.

The primary goals are:

- Transparent filesystem access.
- Minimal runtime and storage overhead.
- Copy-on-write modification isolation.
- Persistent filesystem layers.
- Layer activation and deactivation.
- File content and metadata versioning.
- Scoped operation on an individual directory tree or entire volume.
- Immutable object protection.
- Compatibility with NT file semantics.

UnaryFS is not intended to become a new general-purpose physical filesystem.

**Simplicity and performance are fundamental design requirements.**

## 2. Core principles

UnaryFS follows five rules.

### Rule 1 — Unmodified base

The base filesystem is never modified through UnaryFS.

It remains the lowest layer of the composed view.

### Rule 2 — Transparent reads

Reads resolve against the highest enabled layer containing the requested object.

If no enabled layer overrides the object, the request resolves to the base filesystem.

### Rule 3 — Top-layer writes

All permitted modifications within the configured scope are directed to the active writable layer.

Lower layers are never modified.

### Rule 4 — Independent layers

Layers may be enabled, disabled and reordered, provided the resulting composition is valid.

Higher layers shadow lower layers.

Disabling a layer does not delete its contents.

### Rule 5 — Immutable objects

UnaryFS supports objects protected against modification across all layers.

An immutable object cannot be modified, replaced, hidden, deleted or shadowed by an unauthorized layer.

Immutable protection is enforced separately from ordinary file attributes.

---

## 3. Filesystem independence

UnaryFS MUST NOT depend on:

- FAT tables.
- ext filesystem inodes.
- NTFS MFT records.
- Filesystem-specific journals.
- Physical disk sectors.
- Device-specific allocation structures.

UnaryFS interacts with the canonical filesystem layer through abstract file and directory operations.

A UnaryFS layer may be stored on any writable filesystem offering the required backing operations and persistence guarantees.

The base filesystem and the layer storage filesystem may be different.

Example:

```text
BaseFS: FAT32
Layer storage: ext4
UnaryFS view: C:\Windows\System32
```

The application must not observe this distinction.

Filesystem independence does not imply that all backends provide identical durability, metadata or maximum file-size capabilities. UnaryFS must detect unsupported requirements and fail explicitly.

## 4. Scoped operation

A UnaryFS instance is associated with a filesystem scope.

A scope may represent:

- An individual directory.
- An entire directory tree.
- A mounted volume.

Example:

```text
Scope = C:\Windows\System32
```

All operations within that tree are resolved through UnaryFS.

Operations outside the scope bypass UnaryFS and use the ordinary MicaNT filesystem path.

Multiple scopes may exist simultaneously.

Overlapping scopes must have deterministic resolution and ownership rules; the first implementation may reject overlapping writable scopes.

The scope is identified through canonical filesystem objects, not exclusively through path-string comparison.

Aliases, alternate paths and filesystem redirections must not bypass scope enforcement.

## 5. Layer model

A UnaryFS view consists of:

```text
Writable Layer
      |
Read-only Layer N
      |
Read-only Layer N-1
      |
...
      |
BaseFS
```

Each layer is sparse and stores only objects or metadata that differ from lower layers.

A newly created layer is empty.

An empty layer does not copy files or directories from the base filesystem.

A UnaryFS view may contain any supported number of layers, subject to explicit resource limits.

Only one layer is writable within a view at a time.

Sealed layers are immutable.

The base layer is never writable through UnaryFS.

## 6. Read resolution and shadowing

A filesystem read uses the highest-priority enabled definition of the requested object.

Example:

```text
BaseFS:  config.ini = A
Layer 1: config.ini = B
Layer 2: config.ini = C
```

With all layers enabled, the visible content is `C`.

Disabling Layer 2 reveals `B`.

Disabling Layer 1 and Layer 2 reveals `A`.

Re-enabling Layer 2 restores `C`.

No file copying is required when changing the visible layer composition.

Layer ordering and enablement are view properties, not modifications to the stored layers.

Object data and metadata may resolve from different layers when permitted by the override model.

## 7. Write behavior

Every permitted write within the UnaryFS scope targets the active writable layer.

When a file exists only in a lower layer, the first content modification creates a private version in the writable layer.

This operation is called *copy-up*.

Subsequent modifications operate on the private version.

The initial implementation uses file-level copy-up.

Block-level or extent-level copy-on-write is a possible future optimization, not a requirement of UnaryFS v1.

Creating a new file creates an entry in the writable layer.

Operations that do not modify files must not create unnecessary layer data.

A file that is only read must not be copied.

## 8. Metadata versioning

UnaryFS versions file metadata independently from file contents.

Supported metadata includes, according to the capabilities of the NT filesystem abstraction:

- File attributes.
- File size and allocation-related metadata.
- Timestamps.
- Security descriptors.
- Ownership information.
- Directory attributes.
- Other supported NT file metadata.

A change to an attribute must not require copying the file data.

Example:

```text
BaseFS:
  example.dll
  ReadOnly = false

Layer 1:
  ReadOnly = true
  No data override

Layer 2:
  Hidden = true
  No data override
```

The visible result combines the file content from BaseFS with the effective metadata overrides.

An override must distinguish between an unchanged property and a property explicitly assigned a new value.

Security-sensitive metadata changes remain subject to Se access checks and UnaryFS immutable policy.

## 9. Read-only and immutable semantics

UnaryFS distinguishes three protection concepts.

### 9.1 Ordinary file read-only attribute

The standard NT file attribute is versionable.

It follows the normal applicable file-operation and access-check semantics.

### 9.2 Read-only layer

A sealed layer cannot receive modifications.

Its stored objects remain unchanged.

A higher writable layer may shadow its objects unless an immutable policy forbids this.

### 9.3 Immutable object

An immutable object is protected across the entire UnaryFS view.

Unauthorized operations must not:

- Modify its contents.
- Override its metadata.
- Rename it.
- Delete it.
- Hide it through a whiteout.
- Create a conflicting object that shadows it.
- Replace it through a writable section or alternate path.

This restriction is enforced independently of layer order and normal read-only file attributes.

Immutable policy is anchored to trusted object identity and namespace policy rather than only a filename string.

An application must not be able to remove its own immutable restrictions through a metadata override.

## 10. Deletion and whiteouts

UnaryFS implements virtual deletion using whiteouts.

A whiteout hides an object located in a lower layer without deleting the underlying object.

Example:

```text
BaseFS:
  old.dll

Layer 1:
  WHITEOUT old.dll
```

The composed view does not contain `old.dll`.

Disabling Layer 1 makes the original file visible again.

Directory deletion must also account for descendants, including lower-layer entries.

Directory enumeration must return the effective composed namespace without duplicates or whiteouted entries.

Rename, replacement and delete-on-close must maintain correct NT filesystem behavior.

## 11. Layer activation and versioning

A layer may be enabled or disabled without modifying its stored contents.

A sealed layer represents a persistent version.

Creating a new version consists of creating a new writable layer above the selected composition.

UnaryFS supports the following logical operations:

- CreateLayer.
- SealLayer.
- EnableLayer.
- DisableLayer.
- ReorderLayers.
- SelectWritableLayer.
- CreateView.

These names describe internal functionality, not new public NT APIs.

Changes to the active layer composition must be atomic from the perspective of new filesystem operations.

Existing open handles and memory mappings must retain a valid reference to their resolved backing objects.

A view change must not cause existing I/O to access freed or unrelated backing data.

When a layer depends on lower-layer content, UnaryFS must validate that the selected composition remains compatible.

Invalid compositions must be rejected rather than silently producing corrupted file contents.

## 12. Persistence

UnaryFS layers are persistent objects.

A layer may be represented by a self-contained backing file.

The proposed extension is:

`.unary`

A layer contains the information necessary to reconstruct its contents and metadata overrides.

The first implementation should use a minimal format consisting conceptually of:

```text
Header
Layer metadata
Object index
Data records
```

The implementation must detect malformed records, invalid lengths, unsupported versions and corrupt references.

Crash consistency is mandatory for persistent committed state.

A crash must not expose partially published filesystem entries as valid committed data.

The implementation may use a minimal transaction log, generation-based index publication or another simple crash-consistent mechanism supported by the backing filesystem.

UnaryFS must not require the journal of the underlying filesystem.

No compression, deduplication or block-level delta format is required for v1.

## 13. Performance requirements

UnaryFS must minimize overhead when a filesystem object is not overridden.

### 13.1 Fast path

For an object without an active override:

- No file data copying.
- No layer data allocation.
- No persistent layer write.
- No full layer scan on every I/O operation.
- No unnecessary journal operation.

After name resolution and view lookup, reads should use the underlying backing object directly.

### 13.2 Indexing

UnaryFS should maintain an efficient index for the active view.

The index maps canonical object/path keys to effective layer entries.

It must support positive and negative lookup caching with correct invalidation.

Metadata-only overrides must not require reading or copying file contents.

### 13.3 Layer switching

Enabling or disabling a layer must not copy the contents of the affected directory tree.

An implementation may rebuild or incrementally update a view index before atomically publishing the new view generation.

### 13.4 Memory usage

An empty UnaryFS must have a small bounded metadata footprint.

Inactive layers should not require loading all file data into RAM.

Indexes and caches must remain subject to resource limits.

### 13.5 Benchmarking

Performance must be measured against direct filesystem access.

Required configurations:

- UnaryFS disabled.
- UnaryFS enabled with no overrides.
- One active layer.
- Four active layers.
- Eight active layers.
- Sixteen active layers.

Measure:

- File open latency.
- Cached read latency.
- Uncached read latency.
- Directory enumeration.
- Metadata query.
- Metadata update.
- First-write copy-up.
- Subsequent write.
- Layer activation time.
- Peak memory consumption.

Do not claim zero overhead. Define acceptable regression thresholds from a measured baseline before declaring performance acceptance.

## 14. NT filesystem compatibility

UnaryFS must preserve the applicable semantics of the canonical NT filesystem interface.

Important cases include:

- File handles.
- Desired access and share access.
- Object lifetime.
- File identity.
- Security descriptors.
- Directory enumeration.
- Rename.
- Delete-on-close.
- File attributes.
- File timestamps.
- Hard links.
- Reparse points, where supported.
- Alternate data streams, where supported.
- File locking.
- Flush semantics.
- File-backed sections.
- `SEC_IMAGE`.
- Writable memory mappings.
- Cache coherency.

Operations involving unsupported backend semantics must fail explicitly.

A writable memory mapping must not modify a lower read-only layer.

A file opened before a layer switch must retain coherent backing ownership.

UnaryFS must cooperate with Mm and the canonical Cache Manager rather than implementing an independent memory or cache subsystem.

## 15. Security requirements

UnaryFS is a filesystem virtualization mechanism, not a complete process sandbox.

Normal Se/Ob access checks remain applicable.

UnaryFS must not grant access to files that the caller is not authorized to access.

Its virtualization layer must not permit bypass through:

- Alternate paths.
- Relative paths.
- Hard links.
- Reparse points.
- Inherited handles.
- Duplicated handles.
- Writable sections.
- Raw device access.
- Privileged filesystem control operations.

Unauthorized modification of an immutable object must fail explicitly.

Management of protected layers and immutable policies requires a trusted authority outside ordinary application control.

## 16. Integration into MicaNT

UnaryFS must be integrated into the existing canonical filesystem path.

Primary responsibilities:

**Io / FsRtl**

- Scoped namespace resolution.
- Composed file operations.
- Layer backing access.
- Directory enumeration.
- Copy-up and whiteouts.
- Persistence.

**Mm / Cc**

- File-backed mappings.
- Section consistency.
- Page-cache coherency.
- Writable-view isolation.
- Lifetime of mapped backing objects.

**Ob / Se**

- Access control.
- Object identity.
- Handle lifetime.
- Immutable policy enforcement.

**Ps**

- Association of a view with a process when required by a later integration phase.

UnaryFS must not introduce competing implementations of these subsystems.

No application identity, PE hashing, MSI installation or registry virtualization is required for the initial filesystem implementation.

Those capabilities may consume UnaryFS later, but do not belong to the UnaryFS v1 core.

## 17. Initial scope — UnaryFS v1

The first version must implement:

1. Directory-tree scoping.
2. Base filesystem passthrough.
3. Sparse layers.
4. One active writable layer.
5. Read-only sealed layers.
6. Top-down shadowing.
7. File creation.
8. File-level copy-up.
9. File read and write.
10. Metadata-only overrides.
11. Virtual deletion/whiteouts.
12. Directory enumeration.
13. Immutable object policy.
14. Layer activation/deactivation.
15. Persistent storage.
16. Crash-consistent committed state.
17. Correct interaction with file-backed mappings.
18. Explicit failure for unsupported operations.

The first version does not require:

- Block-level COW.
- Compression.
- Deduplication.
- Remote layers.
- Network distribution.
- Content-addressed storage.
- Automatic application identity.
- MSI installation.
- Registry virtualization.
- General process sandboxing.
- An entirely new physical filesystem.
- A graphical layer manager.

## 18. Acceptance tests

### U0 — Empty overlay

Mount a UnaryFS view over a directory tree.

Verify that all base files remain visible.

Verify that an empty layer does not copy base file contents.

Verify that operations outside the scope bypass UnaryFS.

### U1 — Basic shadowing

Create two layers overriding the same file.

Confirm that the highest enabled layer wins.

Disable the upper layer and confirm that the lower version becomes visible.

Re-enable it and confirm that the higher version returns.

### U2 — Metadata-only override

Create a large file in BaseFS.

Change only its attributes through UnaryFS.

Verify that no file-content copy-up occurs.

Disable the modifying layer and verify restoration of the effective original attributes.

### U3 — Virtual deletion

Delete a file through UnaryFS.

Verify that the file is hidden in the active view but remains present in BaseFS.

Disable the whiteout layer and verify that the file becomes visible.

### U4 — Immutability

Mark an object immutable through trusted policy.

Attempt content writes, metadata changes, rename, deletion, replacement and writable section mapping.

All unauthorized modifications must fail.

Verify the base object remains unchanged.

### U5 — Persistence

Create and modify files in multiple layers.

Restart MicaNT.

Reopen the same UnaryFS view.

Verify the same effective namespace, contents and metadata.

### U6 — Crash consistency

Interrupt execution during copy-up and metadata commit.

After restart, committed state must be consistent.

Uncommitted operations must not become partially visible as valid completed changes.

### U7 — Handle and section lifetime

Open and map a file.

Change layer visibility.

Verify the existing handle and mapping remain coherent.

Verify new opens use the new view.

Verify no leaked PFNs, handles or backing references.

### U8 — Filesystem independence

Run the same logical UnaryFS tests over at least two different supported filesystem backends.

The observable virtual filesystem behavior must be equivalent for the capabilities common to both backends.

### U9 — Performance

Measure the configurations defined in Section 13.

Compare empty-overlay and populated-overlay behavior against direct filesystem access.

Identify and correct unnecessary allocations, locking, copying and persistent I/O in the fast path.

## 19. Implementation sequence

Recommended order:

**Phase F0 — Scoped passthrough**

Implement a view over one directory tree without persistent modifications.

**Phase F1 — Writable sparse layer**

Implement create, read, write and file-level copy-up.

**Phase F2 — Metadata and namespace**

Implement metadata overrides, whiteouts, rename and directory enumeration.

**Phase F3 — Persistent versions**

Implement layer storage, sealing, activation/deactivation and crash recovery.

**Phase F4 — NT integration**

Complete handle lifetime, sharing semantics, Mm/Cc integration, immutable policy and mapped-file behavior.

**Phase F5 — Verification and performance**

Perform QEMU, cross-filesystem, fault-injection and benchmark acceptance.

A phase is complete only when its actual runtime behavior is verified.

## 20. Non-negotiable design constraints

UnaryFS must remain:

- Small.
- Predictable.
- Filesystem-independent.
- Lazy.
- Sparse.
- Transparent.
- Versioned.
- Fast.
- Compatible with canonical MicaNT ownership and security.

Avoid new features unless they solve a concrete functional requirement.

Do not duplicate the physical filesystem.

Do not duplicate the Cache Manager.

Do not create an independent handle manager.

Do not implement application packaging inside the UnaryFS core.

Do not claim a layer is durable without real persistence guarantees.

Do not claim an immutable object is protected if it can be modified through an alternate I/O path.

## 21. Definition of done

UnaryFS v1 is complete when a real MicaNT process can access a scoped filesystem tree through a composed view, modify files and metadata exclusively in the writable layer, switch between persistent versions and observe correct shadowing without modifying BaseFS.

The implementation must preserve required NT file semantics, enforce immutable objects, survive restart, handle failed writes consistently and remain lightweight when no override exists.

Verification must include the canonical AMD64 freestanding kernel path and real QEMU execution.

A hosted test or successful compilation alone does not establish completion.

---

**Design principle:**

*UnaryFS is not another filesystem. It is the smallest possible mechanism for composing multiple filesystem realities into one coherent view.*
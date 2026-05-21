# PatchTrack Build Guide

Transactional code editing, session history, rollback, GUI inspection, and MCP integration

Version baseline
- Target environment: multi-agent coding workflows for local repositories and workspace folders.
- Intended consumers: internal CLI users, Codex/OpenCode-style agents, future editor integrations, and human reviewers using a small GUI inspector.
- Primary objective: deterministic code edits with strong history, rollback, hygiene checks, and handoff semantics.
- Dependency baseline: prefer U++ Core plus local project code. Do not require SQLite or other non-U++ storage libraries in the first implementation.

## 1. Purpose

This guide defines PatchTrack, a single tool product for AI-driven code editing. The tool must provide safe edit application, transaction preview, session history, rollback, machine-readable integration through CLI, JSON, and MCP, and a GUI mode for inspection and maintenance.

The central design principle is simple:

**The AI may decide what should change. The engine must decide whether it is safe to change it.**

The system is one product with one source of truth, but it is not a giant all-in-one code path. It consists of one shared transaction engine and storage model exposed through multiple commands and adapters.

## 2. Goals

PatchTrack must:
- apply deterministic edits to source files;
- preserve file encoding, BOM, and newline style where possible;
- support exact, contextual, code-aware, and guarded line-based edit operations;
- preview edits before commit;
- record every successful apply as a transaction;
- support transaction-level rollback owned by the application, not improvised by the AI;
- keep session history and rolling summaries for handoff to later agents;
- support multiple agents working in the same workspace with optimistic concurrency checks;
- expose a stable machine interface suitable for MCP and other agent frameworks;
- provide a GUI inspector for human review, cleanup, and maintenance of session state.

## 3. Non-goals

The first implementation does not attempt:
- arbitrary semantic merge resolution across heavily diverged files;
- perfect full-language parsing for every supported programming language;
- unrestricted line-level undo across interleaved multi-agent edits;
- replacing Git or existing VCS history;
- acting as a custom IDE;
- using raw shell escaping as the canonical edit transport.

## 4. Product shape

This is one tool product. Tool name:
- `patchtrack`

Recommended executable forms:
- `patchtrack`            (CLI)
- `uedit --json`     (machine mode over stdin/stdout)
- `uedit mcp serve`  (MCP adapter mode)
- `uedit gui`        (human inspector / maintenance mode)

Recommended command surface:
- `uedit apply`
- `uedit preview`
- `uedit diff`
- `uedit history`
- `uedit rollback`
- `uedit session`
- `uedit validate`
- `uedit compact`
- `uedit gui`

Preview, apply, history, and rollback are separate operations over the same transaction/session model and shared state store.

## 5. Core design principles

### 5.1 Determinism first
The engine should prefer exact matching and explicit preconditions. If a requested edit cannot be applied safely, the engine must fail clearly.

### 5.2 Application-owned rollback
Rollback must be computed and executed by the tool itself using stored transaction metadata and rollback payloads. The AI should not be trusted to improvise reverse patches.

### 5.3 Explicit ambiguity handling
If a match is ambiguous, the engine must report `AMBIGUOUS_MATCH` instead of choosing one candidate silently.

### 5.4 Transactional writes
Every edit batch is a transaction. Either all validated file changes commit together, or nothing commits.

### 5.5 Handoff-friendly history
Session history and rolling summaries are first-class. A later agent should be able to understand what was attempted, what succeeded, what failed, and what remains to do.

### 5.6 Storage-backed truth
Preview, apply, diff, history, rollback, GUI inspection, and MCP tool calls must all read and write through the same state model and persistent store.

### 5.7 Text hygiene is mandatory
The engine must protect the repository from malformed or transport-corrupted AI output. Suspicious escaped newline artifacts, merge markers, broken encoding, or similar corruption should be rejected before write.

## 6. System architecture

### 6.1 Layers

**Layer 1: Core engine**

Responsible for:
- workspace discovery;
- file access;
- edit planning;
- match resolution;
- diff creation;
- validation;
- rollback payload generation;
- transaction persistence;
- session summary updates.

**Layer 2: Product front doors**

Responsible for:
- CLI command handling;
- JSON request/response mode;
- MCP server method mapping;
- GUI inspector actions.

**Layer 3: Optional adapters**

Responsible for:
- IDE integration;
- test harness wrappers;
- editor plugins.

### 6.2 Shared core state

These components must be shared across all front doors:
- workspace registry;
- session registry;
- transaction log;
- rollback snapshot store;
- file hash verification;
- diff generator;
- validator;
- naming and path rules.

## 7. Storage model

### 7.1 Storage backend choice

The first implementation should use a **file-based journal store** built only on U++ Core and local project code. Avoid SQLite in the first version.

Preferred U++ facilities:
- `FileIn`, `FileOut`, `SaveFile`, `LoadFile`;
- `FindFile`;
- `RealizeDirectory`;
- `String`, `Vector`, `ValueMap`, `ValueArray`;
- `Jsonize` / JSON support where appropriate;
- `Serialize(Stream&)` only if a compact binary format is later needed.

### 7.2 On-disk layout

Keep the journal layout shallow and human-readable:

```text
.patchtrack/
    workspace.json
    recent.json
    sess-cfe543gd9k-myproject/
        session.json
        tran-ab63gd53qx-uiosfiledialog.json
        tran-91aa72fe4m-linuxgtkfix.json
        snap/
            tran-ab63gd53qx-UiOsFileDialogWin.cpp.before
            tran-91aa72fe4m-UiOsFileDialogLinux.cpp.before
```

### 7.3 Naming rules

Use a simple uniform naming convention:
- `sess-<10id>-<slug>`
- `tran-<10id>-<slug>.json`

Rules:
- id is fixed length: 10 lowercase alphanumeric characters;
- slug is optional and capped at 16 characters;
- slug is lowercase, sanitized to `[a-z0-9-]`;
- repeated separators collapse to one `-`;
- leading and trailing `-` are removed;
- full descriptive titles stay inside JSON metadata;
- filenames are for readability only; canonical identity still lives inside metadata.

Example:
- `sess-cfe543gd9k-myproject`
- `tran-ab63gd53qx-uiosfiledialog.json`

## 8. Data model

### 8.1 Workspace metadata

`workspace.json` should contain:
- workspace id;
- workspace root path;
- created timestamp;
- last active timestamp;
- format version;
- current retention settings;
- optional GUI preferences.

### 8.2 Session metadata

Each `session.json` should contain:
- `session_id`;
- rendered folder name;
- workspace id;
- goal;
- rolling summary;
- start time;
- last active time;
- recent transaction ids;
- touched files;
- open issues;
- next suggested action;
- tags;
- archived flag.

### 8.3 Transaction metadata

Each transaction file should contain:
- `transaction_id`;
- `session_id`;
- workspace id;
- timestamp;
- actor/tool name;
- status;
- summary;
- touched files;
- hashes before;
- hashes after;
- validation results;
- rollback payload references;
- optional next-step note.

Example:

```json
{
  "transaction_id": "tran-ab63gd53qx",
  "session_id": "sess-cfe543gd9k",
  "workspace_id": "work-4dce75a1b2",
  "status": "applied",
  "summary": "Added Windows backend for UiOsFileDialog.",
  "files": [
    {
      "path": "Ui/UiOsFileDialogWin.cpp",
      "hash_before": "â€¦",
      "hash_after": "â€¦",
      "snapshot_before": "snap/tran-ab63gd53qx-UiOsFileDialogWin.cpp.before"
    }
  ],
  "validation": {
    "ok": true,
    "checks": ["hash_precondition", "write_atomic", "must_contain"]
  }
}
```

## 9. Transaction flow

### 9.1 Preview
Preview must:
- load current file state;
- verify preconditions where possible;
- compute the proposed change;
- produce a diff;
- perform lightweight validation;
- avoid mutating workspace files;
- optionally create an internal preview record, but not a committed transaction.

### 9.2 Apply
Apply must:
1. verify file preconditions;
2. save rollback snapshots of all files to be changed;
3. write updated content atomically;
4. validate the written content;
5. persist transaction metadata;
6. update session summary and recent history;
7. return machine-readable results.

### 9.3 Rollback
Rollback must:
1. load the original transaction;
2. verify current files still match the rollback preconditions, normally the stored `hash_after`;
3. restore stored `before` snapshots atomically;
4. validate restored content;
5. record the rollback as a **new transaction**;
6. update session summary and recent history.

Rollback should fail cleanly if current file state has diverged beyond the accepted rollback policy.

## 10. Edit primitives

### 10.1 Preferred operations
The first implementation should support:
- `replace_exact`
- `replace_all_exact`
- `insert_before_exact`
- `insert_after_exact`
- `delete_exact`
- `rewrite_file`
- `replace_between`
- `insert_before_line_matching`
- `insert_after_line_matching`
- `replace_lines`
- `ensure_include`
- `replace_method`
- `ensure_upp_entry`

### 10.2 Matching strategy order
Preferred order:
1. exact hash + exact text replacement;
2. exact anchor/context replacement;
3. code-aware symbol replacement;
4. guarded line-range editing;
5. fail explicitly.

The engine must not silently fall through to unsafe fuzzy editing.

## 11. Line-number editing policy

Line-number editing is supported, but it is **not the preferred primary mode**.

It is allowed when:
- a diagnostic or upstream tool provides stable line ranges;
- the file has been freshly read;
- additional guards are present.

Every line-based edit should include at least one guard beyond bare line numbers:
- expected file hash;
- expected text at the start or end line;
- surrounding context checks;
- optional symbol/section checks.

Example request shape:

```json
{
  "op": "replace_lines",
  "file": "UiOsFileDialog.cpp",
  "expected_sha256": "â€¦",
  "start_line": 120,
  "end_line": 138,
  "expected_start": "bool UiOsFileDialog::Execute(Ctrl* owner)",
  "new_lines": [
    "bool UiOsFileDialog::Execute(Ctrl* owner)",
    "{",
    "    ClearResult();",
    "    return ExecuteCore(owner);",
    "}"
  ]
}
```

Line-based edits must be committed as normal transactions with diff, validation, and rollback data.

## 12. Text transport and newline hygiene

### 12.1 Canonical edit payloads
Canonical edit payloads must use one of:
- raw UTF-8 text blocks;
- structured arrays of lines such as `new_lines`;
- exact binary content only when intentionally editing binary files.

Do **not** use shell-escaped patch text as the canonical representation.

### 12.2 Newline policy
The engine must:
- detect file newline style on read;
- preserve dominant newline style on write where possible;
- preserve BOM if present;
- preserve end-of-file newline policy where practical;
- optionally reject mixed-newline files by policy or normalize them in a controlled way.

### 12.3 Suspicious text detection
Before write, the engine must scan for likely corruption such as:
- literal `` `r`n `` sequences in normal source text where line breaks were expected;
- literal `\r\n` sequences that appear to be transport artifacts;
- merge markers like `<<<<<<<`, `=======`, `>>>>>>>`;
- NUL bytes in text files;
- obvious malformed encoding;
- duplicated or garbled quote escaping caused by transport errors.

If suspicious text is found, the engine should fail with a validation error rather than writing broken source.

### 12.4 Engine-owned byte construction
The AI may describe the desired lines or text. The engine should construct final file bytes itself:
- join line arrays using the chosen newline policy;
- preserve encoding and BOM rules;
- perform final hygiene checks before atomic write.

## 13. Validation and hygiene

### 13.1 Minimum validation
Every write should validate:
- file hash preconditions;
- successful snapshot creation;
- atomic write success;
- basic text decoding for text files;
- required strings present;
- forbidden strings absent.

### 13.2 Optional source hygiene checks
Recommended lightweight checks:
- balanced braces and parentheses for code-like text;
- no merge markers;
- no suspicious escaped newline artifacts;
- no duplicate inserted blocks where uniqueness was expected;
- expected symbol still present after method/class replacement.

### 13.3 Error taxonomy
Use machine-readable error codes such as:
- `FILE_NOT_FOUND`
- `HASH_MISMATCH`
- `NO_MATCH`
- `AMBIGUOUS_MATCH`
- `VALIDATION_FAILED`
- `WRITE_FAILED`
- `OVERLAPPING_OPERATIONS`
- `ROLLBACK_BLOCKED`

## 14. Multi-agent concurrency

### 14.1 Optimistic concurrency
Use optimistic concurrency by default:
1. read file;
2. record current hash;
3. plan edit;
4. re-check current hash before write;
5. abort on mismatch.

### 14.2 Session continuity
A later agent should be able to:
- resume a session;
- inspect recent history;
- read rolling summary;
- see open issues and next-step notes;
- optionally fork a new session for a different approach.

### 14.3 Recent history
`recent.json` should keep a compact set of recently active sessions and transactions for quick handoff without scanning the entire journal tree.

## 15. GUI inspector mode

The GUI is a review and maintenance console over the same engine-owned store. It should not be a free-form state editor.

### 15.1 Primary GUI functions
The GUI should allow users to:
- browse sessions;
- browse transactions within a session;
- review summaries and handoff notes;
- review changed files;
- preview diffs;
- see rollback availability;
- perform rollback through the engine;
- archive sessions;
- purge old snapshots;
- reset workspace journal data when explicitly requested;
- open the backing folder for inspection.

### 15.2 Editable vs read-only fields
Safe GUI-editable fields:
- summary;
- tags;
- user notes;
- next-step note;
- archived flag.

Engine-owned read-only fields:
- hashes;
- timestamps generated by the engine;
- transaction ordering;
- rollback metadata;
- validation outcomes;
- snapshot references.

## 16. MCP integration

### 16.1 Integration model
MCP is an adapter over the same core engine. Do not build a separate edit engine for MCP. The same storage, validation, rollback, and session logic must be used for CLI, JSON, GUI, and MCP.

### 16.2 Suggested MCP tool surface
Expose small, structured tools such as:
- `session_start`
- `session_resume`
- `session_history`
- `session_summary`
- `edit_preview_transaction`
- `edit_apply_transaction`
- `edit_show_diff`
- `edit_rollback_transaction`
- `workspace_compact`
- `workspace_reset_journal`

### 16.3 MCP approval guidance
High-impact actions should be easy to gate in MCP-aware hosts:
- apply transaction;
- rollback transaction;
- purge snapshots;
- reset journal data.

Lower-risk actions:
- preview;
- history query;
- diff query;
- session summary.

### 16.4 One engine, multiple front doors
The same operation semantics should be available through:
- CLI verbs;
- JSON machine mode;
- MCP tools;
- GUI actions.

This keeps behavior consistent across Codex, OpenCode-style tools, and human operation.

## 17. Command and API shape

### 17.1 CLI examples
- `uedit preview request.json`
- `uedit apply request.json`
- `uedit history --session sess-cfe543gd9k`
- `uedit diff --transaction tran-ab63gd53qx`
- `uedit rollback --transaction tran-ab63gd53qx`
- `uedit gui`

### 17.2 JSON request model
Requests should include:
- workspace root;
- session id or session-start request;
- list of edits;
- expected hashes where available;
- validation requirements;
- human summary;
- optional next-step note.

## 18. Retention and cleanup

The tool should support:
- compacting old snapshots;
- archiving old sessions;
- keeping recent history small and fast;
- preserving metadata longer than rollback payloads when space matters.

Recommended policy:
- keep recent sessions fully reversible;
- allow archived sessions to keep metadata and diff history even if snapshots are removed;
- expose this clearly in GUI and CLI.

## 19. Recommended implementation phases

### Phase 1
- file-based journal store;
- exact edit primitives;
- atomic writes;
- rollback snapshots;
- transaction logging;
- CLI.

### Phase 2
- session summaries;
- recent history indexes;
- guarded line-based editing;
- text/newline hygiene and suspicious-text validation;
- rollback command.

### Phase 3
- code-aware helpers such as `replace_method`, `ensure_include`, and `.upp` helpers;
- GUI inspector;
- archive and compact actions.

### Phase 4
- MCP adapter;
- richer policy controls for approvals;
- IDE/editor integration.

## 20. Practical summary

Build one tool with:
- one shared transaction engine;
- one file-based journal store;
- one rollback mechanism owned by the application;
- one consistent naming convention;
- multiple front doors: CLI, JSON, GUI, and MCP.

Do not let the AI improvise rollback or final file bytes. Let the AI describe intent. Let the engine enforce safety, hygiene, history, and reversibility.


## 18. Agent contract and tool-directed behavior

This section is normative. The system must not rely on a prompt alone to convince an AI to edit safely. Safe behavior must be encoded in the tool contract, request schema, operation set, validation rules, and failure responses.

### 18.1 Principle

The AI should not improvise patch strategy at runtime. The engine must present a small set of explicit edit primitives and clearly state:
- which primitive is being used;
- what guards are required;
- what fallback order is allowed;
- what suspicious inputs are rejected;
- what diagnostics are returned on failure.

### 18.2 Required agent behavior

Agents using the tool must:
- read current file content before planning edits;
- supply expected file hash or equivalent freshness guard for mutating operations;
- choose from declared edit primitives instead of synthesizing raw shell patch commands;
- provide replacement text as raw UTF-8 text or structured `new_lines`;
- prefer exact, anchor, or code-aware operations before line-range fallback;
- rely on the engine for final byte construction, diff generation, transaction recording, and rollback.

Agents must not:
- treat PowerShell or shell escape sequences as canonical source text;
- assume line numbers are stable without hash/context checks;
- rely on whitespace-collapsed blobs as anchors for class member blocks;
- attempt ad hoc reverse patches when a rollback transaction can be requested from the engine;
- silently continue after a hash mismatch or ambiguous match result.

### 18.3 Required engine behavior

The engine must:
- expose a fixed operation vocabulary;
- reject unsupported or suspicious edit payloads;
- return typed, machine-readable errors;
- recommend a safer primitive when a brittle edit request fails;
- preserve file newline/BOM/encoding policy unless configured otherwise;
- own final write construction rather than trusting transport-escaped text;
- record preview/apply/rollback outcomes in the same shared history model.

### 18.4 Allowed primitive families

Preferred order:
1. exact/hash-based edit;
2. anchor/context edit;
3. symbol-aware or code-aware edit;
4. guarded line-range edit;
5. fail with diagnostics.

The engine should refuse to silently jump from exact matching to broad fuzzy editing.

### 18.5 Diagnostics must teach the caller

When an operation fails, the tool should tell the caller exactly what to do next. Example:
- `NO_MATCH`: expected anchor line not found. Consider `ensure_class_member_declaration`.
- `SUSPICIOUS_MATCH_INPUT`: expected text appears whitespace-collapsed. Use line-array or class-member insertion.
- `HASH_MISMATCH`: file changed since read. Re-read file and retry.

## 19. Context matching hygiene and declaration-block policy

A recurring failure mode in patch systems is attempting to match large declaration lists or header regions as one flattened blob of text.

Bad example pattern:
- multiple declarations collapsed into a single line of expected text;
- large whitespace runs where real source lines were lost;
- semicolon-dense class member lists used as one exact-match blob.

This must be treated as suspicious input.

### 19.1 Declaration-block policy

For header declaration edits, prefer:
- `insert_after_exact_line`;
- `insert_before_exact_line`;
- `ensure_class_member_declaration`;
- `replace_lines` with hash plus surrounding context;
- class-scope symbol-aware insertion.

Avoid:
- matching a whole declaration run as one flattened exact string;
- broad fuzzy match over an entire class when only one declaration is needed.

### 19.2 Suspicious match payload detection

The engine should warn or fail when the expected match input:
- contains several semicolons but no real newlines;
- appears to be whitespace-collapsed source;
- mixes shell escapes and source text;
- contains many declarations compressed into one fragment.

Suggested error:
- `SUSPICIOUS_MATCH_INPUT`: expected source block appears whitespace-collapsed; use line-array, anchor-line, or class-member edit.

### 19.3 Fallback ladder for declaration edits

If exact verification fails, the engine may try:
1. exact line-array match;
2. normalized whitespace match within the already resolved class scope only;
3. anchor-line insertion;
4. symbol-aware class member insertion;
5. guarded line-range replacement;
6. fail with diagnostics.

The engine must not jump to repository-wide fuzzy replacement.

## 20. Text transport, newline safety, and malformed AI output hygiene

Many edit failures blamed on line-based editing are actually text transport failures. Common examples include literal `` `r`n `` inserted into source, literal `\r\n` sequences where real newlines were intended, or merge markers accidentally written into files.

### 20.1 Canonical transport

Canonical edit payloads must be one of:
- raw UTF-8 text;
- structured line arrays such as `new_lines`;
- exact file content for full rewrite.

Canonical payloads must not depend on:
- PowerShell escape syntax;
- shell here-strings as the primary representation;
- ad hoc quoting rules from a host shell.

### 20.2 Engine-owned final byte construction

The engine, not the AI, should:
- join `new_lines` using the file newline policy;
- preserve or normalize final newline by configuration;
- preserve BOM if present;
- write the final bytes atomically.

### 20.3 Suspicious content validation

Before write, the engine should scan candidate output for:
- literal `` `r`n `` sequences likely meant to be line breaks;
- literal `\r\n` in ordinary source regions where real line breaks were expected;
- merge markers `<<<<<<<`, `=======`, `>>>>>>>`;
- NUL bytes in text files;
- mixed escaped/newline representations inside source declarations or include blocks.

If detected, the engine should fail unless the request explicitly allows those literals.

### 20.4 Mixed-newline policy

The engine should:
- detect current file newline style;
- preserve dominant style on write;
- optionally normalize mixed newline files by explicit policy only;
- never silently reinterpret escaped newline characters as physical line breaks.

## 21. Line-number editing policy

Line-number editing is supported, but only as a guarded fallback or when diagnostics and file freshness make it reliable.

### 21.1 When line-based editing is acceptable

Use guarded line editing when:
- the file was freshly read;
- the operation includes expected file hash;
- exact or symbol-aware matching is unavailable or unsuitable;
- diagnostics point to a precise line region;
- surrounding context is checked before commit.

### 21.2 Required guards

Line-based mutating operations should include at least:
- expected file hash;
- `start_line` and `end_line`;
- expected start/end or surrounding context;
- replacement as `new_lines` or raw UTF-8 text.

### 21.3 Not preferred as the primary mode

Line-number editing should not be the default first choice for class member edits, include updates, or function replacements when exact anchors or code-aware helpers exist.

### 21.4 Example

```json
{
  "op": "replace_lines",
  "file": "DirLister/MainWindow.h",
  "expected_sha256": "abc123...",
  "start_line": 42,
  "end_line": 45,
  "expected_contains": [
    "void HandleApplyTransfer();",
    "DirectoryScanSettings ReadSettings() const;"
  ],
  "new_lines": [
    "    void HandleApplyTransfer();",
    "    void HandleSlashModeChanged();",
    "    DirectoryScanSettings ReadSettings() const;"
  ]
}
```

## 22. MCP surface and one-tool implementation model

This product is one tool with one shared state store and one transaction engine. MCP is an adapter over that engine, not a separate implementation.

### 22.1 Product shape

Recommended executable modes:
- `patchtrack` for CLI;
- `uedit --json` for machine stdin/stdout mode;
- `uedit mcp serve` for MCP tool mode;
- `uedit gui` for human inspector mode.

### 22.2 MCP tool surface

The MCP adapter should expose small explicit tools over the same engine, for example:
- `session_start`
- `session_resume`
- `session_history`
- `session_summary`
- `edit_preview_transaction`
- `edit_apply_transaction`
- `edit_show_diff`
- `edit_rollback_transaction`
- `edit_validate_request`

These are not separate systems. They are front doors into the same workspace/session/transaction model.

### 22.3 Approval guidance

Low-risk MCP actions:
- history lookup;
- session summary;
- diff preview;
- validation.

Higher-risk MCP actions:
- apply transaction;
- rollback transaction;
- purge/archive/delete session data.

### 22.4 One engine, many commands

The same engine must back:
- CLI;
- JSON request/response mode;
- MCP;
- GUI.

This ensures preview, apply, diff, rollback, and history all refer to the same truth.

## 23. File-based journal store and naming rules

The first implementation should use a shallow file-based journal store built with U++ Core facilities only.

### 23.1 Layout

```text
.patchtrack/
    workspace.json
    recent.json
    sess-cfe543gd9k-myproject/
        session.json
        tran-ab63gd53qx-uiosfiledialog.json
        tran-91aa72fe4m-linuxgtkfix.json
        snap/
            tran-ab63gd53qx-UiOsFileDialogWin.cpp.before
```

### 23.2 Naming rules

Use:
- `sess-<10id>-<slug>`
- `tran-<10id>-<slug>.json`

Rules:
- fixed 10-character id;
- slug max 16 chars;
- lowercase only;
- sanitize to `[a-z0-9-]`;
- collapse repeated dashes;
- trim leading/trailing dashes;
- omit slug if empty;
- keep full descriptive title in JSON metadata.

### 23.3 Identity source

The filename is for readability. Canonical identity must still live in metadata, for example:
- `session_id: "sess-cfe543gd9k"`
- `transaction_id: "tran-ab63gd53qx"`

## 24. GUI inspector policy

The GUI is a review and maintenance console over engine-owned state, not a free-form JSON editor.

### 24.1 GUI should support

- browse sessions;
- browse transactions;
- inspect changed files and diffs;
- view summaries, notes, and next steps;
- preview rollback availability;
- execute rollback through the engine;
- archive, compact, purge, or reset journal data;
- open raw files/folders for manual inspection.

### 24.2 Editable versus engine-owned fields

GUI-editable fields:
- summary;
- notes;
- tags;
- next step;
- archive flag.

Read-only engine-owned fields:
- hashes;
- validation status;
- transaction ordering;
- rollback snapshot metadata;
- timestamps created by the engine.

## 25. Recommended implementation phases

### Phase 1
- file journal store;
- exact and anchor-based edits;
- transaction recording;
- rollback snapshots;
- diff generation;
- CLI.

### Phase 2
- session summaries;
- history queries;
- guarded line-based edits;
- suspicious input hygiene checks;
- JSON machine mode.

### Phase 3
- symbol-aware C++ helpers;
- MCP adapter;
- approval-aware action classification;
- GUI inspector.

### Phase 4
- richer project-specific helpers such as `.upp` editing;
- compaction/archive tools;
- optional portal/native editor integrations;
- optional later alternate storage backend if ever justified.

## 26. Final engineering rules

- The AI may choose intent; the engine must enforce safety.
- The tool contract must encode safe behavior; prompts alone are insufficient.
- Rollback should be executed by the application, not improvised by the AI.
- Match large declaration lists structurally, not as flattened blobs.
- Treat shell-escaped text as transport, never as canonical source representation.
- Keep the store shallow, readable, and dependency-light.
- Keep MCP as an adapter over one engine, not a parallel subsystem.

## 27. Current Implementation Notes (Living Status)

This section tracks the implemented behavior of the current repository so a later agent does not have to rediscover it from scratch.

### 27.1 Current code shape

Current packages:
- `patchtrack/` - core CLI engine
- `patchtrack_tests/` - end-to-end protocol harness

Current binaries:
- `build/patchtrack.exe`
- `build/patchtrack_tests.exe`

The code is still a single translation unit for the engine. The test harness is a separate black-box console package that drives the compiled CLI binary.

### 27.2 Verified implemented guarantees

The current implementation now verifies or enforces:
- `preview` does not mutate workspace files;
- malformed or non-object JSON requests return `BAD_JSON` as structured JSON on stdout;
- newline style, BOM, and EOF newline are preserved across current text edit paths;
- apply re-checks file hashes before write;
- if a write-stage failure happens after one or more files were already written, earlier writes are restored from the original in-memory snapshot;
- if transaction-log persistence fails after file writes, changed files are restored and no committed transaction file is left behind;
- rollback is blocked when current file state no longer matches transaction `hash_after`.

### 27.3 Explicit fault injection for harness use

The current engine accepts a test-only request object:

```json
{
  "testing": {
    "inject_fault": "write_failed_after_first_write"
  }
}
```

Current injected values:
- `write_failed_snapshot`
- `write_failed_before_first_write`
- `write_failed_after_first_write`
- `write_failed_transaction_log`

These are for automated protocol tests only. They should not be used by normal editing agents.

### 27.4 Current test layers

Current test layers are:
- internal `patchtrack selftest`
- external `patchtrack_tests`

`selftest` covers core engine invariants directly in-process.

`patchtrack_tests` covers the real CLI protocol by:
- generating disposable workspaces;
- writing request JSON files;
- invoking the built `patchtrack.exe` process;
- parsing machine-readable JSON output;
- validating file content, journal state, rollback behavior, transport errors, injected write failures, and stress behavior.

### 27.5 Current protocol harness coverage

The end-to-end harness currently covers:
- command surface: `hash`, `history`, `preview`, `apply`, `rollback`;
- all implemented edit primitives and exact-line alias forms;
- preview no-write guarantees;
- successful journaling and rollback;
- deterministic engine failures: `FILE_NOT_FOUND`, `HASH_MISMATCH`, `NO_MATCH`, `AMBIGUOUS_MATCH`, `VALIDATION_FAILED`, `UNSUPPORTED_OP`, `RANGE_ERROR`, `UNSAFE_PATH`, `TRANSACTION_NOT_FOUND`, `ROLLBACK_BLOCKED`;
- transport failures: missing request file and malformed JSON;
- injected `WRITE_FAILED` cases after partial file mutation and before transaction-log commit;
- large-batch stress with 1000 sequential exact replacements in a single transaction.

### 27.6 Important remaining gaps

The following classes still deserve further work if the tool is pushed harder:
- true OS-level permission-denied and disk-full failures outside the explicit fault hooks;
- crash consistency if the process is terminated mid-apply rather than returning a normal error;
- concurrent multi-process apply races beyond optimistic hash checks;
- very large multi-file stress suites that simulate thousands of touched files, not just thousands of edit operations in one file;
- binary-file policy, if binary patching is ever allowed;
- richer semantic edit helpers such as declaration-aware or method-aware transforms;
- richer session summaries and handoff notes for long-running MCP usage.

### 27.7 Recommended continuation path

If another agent continues this work, the next recommended order is:
1. preserve the black-box harness as the source of truth for protocol behavior;
2. add new engine capabilities only with a matching harness case;
3. expand fault injection before trusting new transactional guarantees;
4. keep the build guide updated whenever a guarantee becomes real, not merely planned;
5. separate engine code from CLI plumbing once feature growth starts making `main.cpp` hard to reason about.
### 2026-04-27 Test/Journaling Update

Current implementation now records staged transaction states in the journal: `pending`, `applied`, `failed_restored`, and `failed_recovery_required`.

Current harness coverage now explicitly includes:
- preview no-write
- exact and anchor edits
- rollback success and rollback blocked on divergence
- validation failures
- injected partial-write failure
- injected transaction-log failure after file writes
- rollback failure after first restore with recovery
- 1000-edit single-transaction batch stress
- 1000 sequential transaction stress

Current test-only fault hooks exposed through `testing.inject_fault`:
- `write_failed_snapshot`
- `write_failed_before_first_write`
- `write_failed_after_first_write`
- `write_failed_transaction_log`
- `rollback_failed_before_first_write`
- `rollback_failed_after_first_write`
- `rollback_failed_transaction_log`

One practical lesson from stress testing: IDs generated inside many short-lived CLI processes must not depend only on per-process RNG seeding. The engine now incorporates process-specific entropy so repeated invocations do not overwrite transaction logs.

### 2026-04-28 Documentation/Hardening Direction Update

Public-facing positioning should now be considered part of the product surface, not an afterthought.

Current README direction should make the following explicit to outside users:
- PatchTrack exists to reduce brittle AI editing and partial text-replacement damage.
- Structured edits are preferred because raw/mechanical replacements can hit the wrong region or leave malformed code behind.
- The engine distinguishes transport failures from engine failures.
- Current guarantees include staged `pending` journal records, before-snapshots, commit precondition re-checks, rollback preflight, and rollback recovery.
- Current gaps still include true OS-level permission/disk/full/host-crash handling outside explicit test hooks.

Additional lessons now worth keeping visible in the living guide:
- “mechanical replacement left a malformed block” is exactly the class of defect the product is trying to minimize.
- Recovery metadata is part of usability, not just engineering hygiene.
- Diagnostics should answer `what failed`, `where`, `why`, and `who likely needs to fix it`.
- Any future suspicious-character validation should default to detection/warning or opt-in strict rejection, not silent filtering.

Recommended next design step after docs:
1. add a structured filesystem/permission diagnostic layer;
2. expose permission-denied, sharing-violation, and disk-full classes with actionable hints;
3. define how stricter suspicious-character validation is enabled;
4. add harness coverage for those diagnostics and enablement modes.

Recommended enablement model for future validators/diagnostics:
- safe defaults always on for clearly dangerous cases;
- warning-only checks available by default for suspicious-but-not-illegal content;
- strict rejection only when enabled explicitly by config or request policy;
- no silent content rewriting unless the caller asks for it.

### 2026-04-28 Filesystem Diagnostics Implementation Update

The structured filesystem/permission diagnostic layer is now implemented in the current engine, not merely planned.

Current behavior:
- filesystem reads, writes, directory creation, transaction-log writes, and rollback writes are classified before returning a generic failure;
- structured error payloads now carry `error`, `message`, `stage`, `path`, `operation`, `owner_hint`, `resolution_hint`, and `os_code` fields when available;
- current classified families include `PERMISSION_DENIED_READ`, `PERMISSION_DENIED_WRITE`, `PERMISSION_DENIED_CREATE_DIR`, `PERMISSION_DENIED_TRANSACTION_LOG`, `PERMISSION_DENIED_ROLLBACK`, `NO_SPACE_LEFT`, plus fallback `READ_FAILED` and `WRITE_FAILED`;
- abrupt host termination is still modeled through explicit harness fault injection rather than a normal returned error, and the contract there is pending journal state plus workspace inspection.

Current harness expansion now also covers:
- structured permission-denied classification;
- structured disk-full classification;
- abrupt host termination after first apply write;
- pending journal inspection after abrupt host termination.

Current explicit `testing.inject_fault` values now include, in addition to the earlier write-failure hooks:
- `permission_denied_create_journal_dir`
- `permission_denied_create_session_dir`
- `permission_denied_create_snapshot_dir`
- `permission_denied_snapshot_write`
- `permission_denied_read_target`
- `permission_denied_write_target`
- `permission_denied_transaction_log`
- `permission_denied_rollback_write`
- `no_space_create_dir`
- `no_space_snapshot_write`
- `no_space_target_write`
- `no_space_transaction_log`
- `host_crash_after_first_write`
- `host_crash_during_rollback_after_first_write`

What still remains open:
1. Distinguish more detailed real-world permission causes such as read-only attributes, sandbox policy, and lock/share violations with stronger user-facing remediation.
2. Add reproducible external OS-failure runs where possible, rather than relying only on explicit injected hooks.
3. Keep the public README and this guide synchronized whenever a planned diagnostic becomes an implemented one.

### 2026-04-28 MCP Positioning Clarification

The intended product should now be described explicitly as an MCP system patching tool for AI agents across Linux, macOS, and Windows. The first intended MCP hosts are Codex, OpenCode, and Hermes.

Important clarification:
- the current U++ console app is the implementation vehicle, not the final product framing;
- the real objective is an MCP-callable transaction engine that closes a practical gap in current AI patch/apply workflows;
- the value proposition is not merely "apply text edits", but "apply structured edits with validation, journaling, rollback, and actionable diagnostics";
- documentation should stop implying that MCP integration is a distant future add-on, because that is already the primary reason the project exists.

When describing PatchTrack publicly, lead with:
1. MCP tool for AI editing.
2. Cross-platform target: Linux, macOS, Windows.
3. Structured edit contract instead of brittle freeform patch injection.
4. Transaction and recovery semantics as the differentiator.

### 2026-04-28 Platform Layer Split

Because the product target is Linux, macOS, and Windows, OS-specific code should not remain embedded in the transaction engine.

Current implementation change:
- filesystem access, directory creation, low-level OS error capture, and abrupt test-process termination are now routed through a dedicated platform layer;
- `platform_fs.h` defines the engine-facing contract;
- `platform_fs_win.cpp` contains the Win32 implementation;
- `platform_fs_posix.cpp` contains the POSIX implementation used for Linux and macOS builds.

This is the correct direction for the MCP tool as well, because the MCP-facing behavior should stay stable while each OS supplies its own low-level file semantics.

### 2026-04-28 Soft Claim Coordination Update

The engine now has a first concurrency layer built around soft session claims rather than OS-held file locks.

Current behavior:
- `apply` and `rollback` write short-lived claim files under `.patchtrack/claims`;
- claims record session id, summary, intent, file list, heartbeat, and expiry;
- active overlapping claims return structured `FILE_BUSY` with blocking session metadata;
- expired claims are removed during startup recovery scan;
- startup recovery scan also records pending and `failed_recovery_required` transactions into `recovery_scan.json`.

This is the right direction for MCP use because it coordinates multiple AI editors without leaving the source file itself locked open after a crash or power loss.

### 2026-04-29 README / Usage / Verification Update

The public README should now be treated as a real product-facing entry point, not a placeholder.

It now needs to stay current on:
- MCP-first positioning;
- package layout (`patchtrack_core`, `patchtrack`, `patchtrack_mcp`);
- CLI usage and MCP tool usage;
- minimal request examples for preview/apply, rollback, hash, and recovery scan;
- failure model and transaction guarantees;
- current diagnostics and current gaps;
- build/run flow;
- current verification status and transport benchmark observations.

Current repo status worth keeping visible to the next agent:
- `patchtrack selftest` is passing;
- `patchtrack_mcp --selftest` is passing;
- the protocol harness is currently at `21 / 21` passing;
- the dedicated MCP round-trip harness case now checks framed MCP responses, preview/apply/rollback state transitions, transaction id propagation, and recovery scan success;
- observed preview timing on this machine is roughly `0.20 ms` in core, `24.7 ms` via CLI, and `24.3 ms` via MCP, which points to transport/process/filesystem overhead rather than engine cost.

Documentation rule going forward:
- if the public behavior changes, update `README.md` first;
- if the engineering direction or known gaps change, update this guide as the living handoff document;
- if verification status meaningfully changes, record it in both the README and changelog so another agent can quickly see what is stable and what is still in motion.



### 2026-04-29 Repository Cleanup Note

Repository hygiene update:
- `UPP_GUIDES/` is now treated as living documentation and should not be ignored.
- generated scratch paths such as `_protocol_tests/`, `_selftest/`, `_mcp_selftest/`, `_mcp_init.bin`, `out/`, `build/`, and `OLD/` should stay out of source control.
- the first MCP host targets to keep in mind are Codex, OpenCode, and Hermes.

Cleanup caveat from this session:
- destructive deletion of generated scratch directories was blocked by the current sandbox policy, so the cleanup was completed through ignore rules and documentation rather than removing local generated output.


### 2026-04-30 Host Setup And Verification Direction

Current near-term priority is first use as an MCP system tool in Codex, OpenCode, and Hermes on Windows.

New repo artifacts:
- `verify.ps1` is the canonical Windows verification script. It builds `patchtrack`, `patchtrack_mcp`, and `patchtrack_tests`, then runs the engine selftest, MCP selftest, and protocol harness. Use `-SkipProtocolTests` for a faster build plus smoke check.
- `integration/MCP_HOSTS.md` records the initial stdio MCP server setup notes for Codex, OpenCode, and Hermes.

Clarification:
- portability builds for Linux/macOS are deferred until those environments are available;
- performance work should focus on MCP/transport overhead after the host configs are usable;
- the CLI exists for verification and fallback, but host integration should prefer `patchtrack_mcp` directly.

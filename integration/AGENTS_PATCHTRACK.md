# Agent rule: use PatchTrack for source edits

When editing this repository:

1. Read the target file first.
2. Run `patchtrack hash <file>` and include `expected_sha256` in mutating requests.
3. Write a PatchTrack request JSON with one or more explicit operations.
4. Run `patchtrack preview request.json`.
5. Only run `patchtrack apply request.json` when preview succeeds and the diff is correct.
6. Never improvise reverse patches; use `patchtrack rollback rollback.json`.

Prefer these operations, in order:

- `replace_exact`
- `insert_before_exact`
- `insert_after_exact`
- `replace_between`
- `replace_lines` only with hash and context guards
- `rewrite_file` only for generated files or deliberate full rewrites

## Failure Model

Treat failures in two buckets:

- Front-door failure:
  the request never reached PatchTrack correctly. Examples include sandbox/tool errors, request file read failures, or malformed JSON. Assume no PatchTrack transaction exists and no engine-managed rollback is needed.
- Engine failure:
  PatchTrack received the request and rejected or blocked it with a machine-readable error like `HASH_MISMATCH`, `NO_MATCH`, `VALIDATION_FAILED`, `WRITE_FAILED`, or `ROLLBACK_BLOCKED`.

Caller behavior:

- On front-door failure: fix transport, re-read files if needed, and retry from `preview`.
- On engine failure: inspect the returned error, re-read the target file if hashes or matches are stale, and do not assume any write happened unless `apply` returned success.
- Never invent a reverse patch after either failure mode; if an apply succeeded and needs to be undone, use `patchtrack rollback`.
## Fault Injection

For protocol or engine tests only, PatchTrack accepts a test-only request field:

- `testing.inject_fault`

Current injected values:

- `write_failed_snapshot`
- `write_failed_before_first_write`
- `write_failed_after_first_write`
- `write_failed_transaction_log`

Do not use these during normal repository edits. They exist so the harness can verify that failed apply paths restore already-written files and do not leave committed transactions behind.
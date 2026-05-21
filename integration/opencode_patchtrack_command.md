---
description: Use PatchTrack safe transactional edits
---

Use PatchTrack for this edit.

Rules:
- Read the target files first.
- Use `patchtrack hash <file>` for each file to get `expected_sha256`.
- Create a request JSON under `.patchtrack-requests/`.
- Run `patchtrack preview <request.json>` and inspect the returned diff.
- Run `patchtrack apply <request.json>` only after preview succeeds.
- For rollback, create rollback JSON and run `patchtrack rollback <rollback.json>`.
- Distinguish transport failure from engine failure:
  if the host tool or sandbox fails before PatchTrack runs, no transaction exists yet;
  if PatchTrack returns `HASH_MISMATCH`, `NO_MATCH`, `VALIDATION_FAILED`, `WRITE_FAILED`, or `ROLLBACK_BLOCKED`, treat that as an engine refusal and re-read or repair inputs before retrying.

User request:
$ARGUMENTS
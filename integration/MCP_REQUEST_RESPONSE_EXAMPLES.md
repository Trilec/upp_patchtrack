# MCP Request And Response Examples

These examples show the raw JSON-RPC payloads PatchTrack expects over MCP stdio.

MCP stdio uses newline-delimited UTF-8 JSON-RPC messages. Each request and response is one JSON object on one line; messages must not contain embedded newlines.

Natural-language mapping:
- `Replace beta with BETA in one file` maps to `op: replace_exact`, `find: beta`, `text: BETA`.
- `expected_sha256` carries the guard hash from `hash`.
- `replace` and `replace_text` are not canonical MCP operation names here.

## Initialize

Request body:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
```

Request on stdin:

```text
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
```

Response body shape:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "serverInfo": {
      "name": "patchtrack_mcp",
      "version": "1.1.0"
    },
    "capabilities": {
      "tools": {
        "listChanged": false
      }
    }
  }
}
```

## Tools List

Request body:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
```

Expected response includes these tool names:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tools": [
      {"name": "version"},
      {"name": "preview"},
      {"name": "apply"},
      {"name": "rollback"},
      {"name": "hash"},
      {"name": "history"},
      {"name": "recovery_scan"}
    ]
  }
}
```

## Version

`version` takes an empty object and returns the active PatchTrack release,
MCP protocol version, and supported feature flags. Hosts also receive the same
release in `initialize.serverInfo.version`.

```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"version","arguments":{}}}
```

The real response includes full input schemas for each tool.

## Hash

Request body:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "hash",
    "arguments": {
      "path": "E:\\apps\\github\\upp_patchtrack\\examples\\sample.txt"
    }
  }
}
```

Response shape:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\n  \"ok\": true,\n  \"sha256\": \"...\",\n  \"path\": \"...\"\n}\n"
      }
    ],
    "structuredContent": {
      "ok": true,
      "sha256": "...",
      "normalized_sha256": "...",
      "newline": "lf",
      "path": "E:\\apps\\github\\upp_patchtrack\\examples\\sample.txt"
    },
    "isError": false
  }
}
```

## Preview

Request body:

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "tools/call",
  "params": {
    "name": "preview",
    "arguments": {
      "workspace_root": "E:\\apps\\github\\myrepo",
      "summary": "replace beta with gamma",
      "actor": "codex",
      "session": {"id": "sess-codex-session-1", "goal": "safe agent edit"},
      "edits": [
        {
          "op": "replace_exact",
          "file": "src\\sample.txt",
          "find": "beta\n",
          "text": "gamma\n",
          "expected_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        }
      ],
      "validation": {
        "must_contain": ["gamma"],
        "forbid": ["<<<<<<<", ">>>>>>>"]
      }
    }
  }
}
```

Response checks agents should make:

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "result": {
    "structuredContent": {
      "ok": true
    },
    "isError": false
  }
}
```

Preview must not mutate workspace files.

## Creating A File

Use `create_file` when the target must not already exist. It requires `text`
and returns `FILE_ALREADY_EXISTS` if another writer created the target first.
Use `rewrite_file` for a deliberate complete-file replacement; it can create a
missing target. Both are journaled as creations and rollback removes the file.

```json
{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"apply","arguments":{"workspace_root":"E:\\apps\\github\\myrepo","summary":"add generated file","actor":"agent","edits":[{"op":"create_file","file":"generated\\config.txt","text":"enabled=true\n"}]}}}
```

## Apply

Apply uses the same arguments as preview, but the tool name changes:

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "tools/call",
  "params": {
    "name": "apply",
    "arguments": {
      "workspace_root": "E:\\apps\\github\\myrepo",
      "summary": "replace beta with gamma",
      "actor": "codex",
      "session": {"id": "sess-codex-session-1", "goal": "safe agent edit"},
      "edits": [
        {
          "op": "replace_exact",
          "file": "src\\sample.txt",
          "find": "beta\n",
          "text": "gamma\n"
        }
      ]
    }
  }
}
```

Successful apply responses include a `transaction_id` in `structuredContent`.

## Rollback

Request body:

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "tools/call",
  "params": {
    "name": "rollback",
    "arguments": {
      "workspace_root": "E:\\apps\\github\\myrepo",
      "transaction_id": "tran-xxxxxxxxxx",
      "actor": "codex",
      "session": {"id": "sess-codex-session-1", "goal": "safe agent edit"}
    }
  }
}
```

Successful rollback responses report `ok: true`. If the workspace diverged after apply, rollback returns a structured error instead of guessing.

## Recovery Scan

Request body:

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "method": "tools/call",
  "params": {
    "name": "recovery_scan",
    "arguments": {
      "workspace_root": "E:\\apps\\github\\myrepo"
    }
  }
}
```

Agents should call this on startup or after a failed edit session when `.patchtrack` may contain pending or recovery-required records.
The structured result also reports `temporary_artifacts`, so hosts can confirm whether any `.patchtrack-tmp-*` files remain without doing an extra directory walk.

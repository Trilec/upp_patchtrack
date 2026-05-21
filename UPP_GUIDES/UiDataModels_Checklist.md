# UiDataModels Checklist

## Goals
- Build lean, shared data-model primitives for list/tree/graph.
- Keep APIs compact and deterministic.
- Avoid dual code paths in controls by enabling model-first integration later.
- Provide a console stress test bed with clear pass/fail output.

## Phase 1: Core Model Files
- [x] Add `Ui/UiDataModels.h`.
- [x] Add `Ui/UiDataModels.cpp`.
- [x] Export through `Ui/Ui.h`.
- [x] Add files to `Ui/Ui.upp`.

## Phase 2: Base + Flat Model
- [x] Add shared model change contract (`UiModelChangeKind`, `UiModelChange`).
- [x] Add base class notification/revision support (`UiDataModelBase`).
- [x] Implement `UiListModel` operations:
  - [x] add/insert/set/remove
  - [x] move/swap
  - [x] clear/reserve/getall

## Phase 3: Tree Model
- [x] Implement `UiTreeNodeRef`.
- [x] Implement `UiTreeModel` with:
  - [x] add/insert child
  - [x] get parent/children/count
  - [x] set/get item
  - [x] remove subtree
  - [x] move subtree
  - [x] clone subtree
  - [x] clear/root reset
  - [x] count alive nodes

## Phase 4: Graph Model
- [x] Implement `UiGraphModel` with:
  - [x] add/remove nodes
  - [x] add/remove edges
  - [x] incoming/outgoing queries
  - [x] clear
  - [x] tree-to-graph conversion helper

## Phase 5: Interop Surface
- [x] Tree -> List export.
- [x] List -> Tree import.
- [x] Tree -> Graph conversion.

## Phase 6: Console Test Bed
- [x] Create console package `examples/UiDataModelsTest`.
- [x] Add tests for list operations (hundreds of entries + move/copy/erase/rebuild).
- [x] Add tests for tree operations (insert/move/clone/remove/clear/rebuild).
- [x] Add tests for graph operations (nodes/edges remove/query/clear).
- [x] Add interop tests (list<->tree + tree->graph).
- [x] Add assertion summary and non-zero exit on failures.

## Phase 7: Hardening
- [x] Validate with `umk` build.
- [x] Run test bed and capture output summary.
- [x] Address edge-case bugs (invalid refs, cycle prevention, range guards).
- [x] Final API pass for simplicity/no bloat.

## Deferred (after test bed)
- [ ] Integrate model binding into `UiDropdown`.
- [ ] Integrate model binding into `UiTree`.

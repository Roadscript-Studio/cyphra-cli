# DSL Reference

Roadscript includes a small workflow DSL for scripted CLI execution:

Run the full language reference with:
```sh
rse run examples/language_reference.rsx --dry-run
```

```sh
./cmake-build-debug/rse run workflow.rsx
./cmake-build-debug/rse run workflow.rsx --dry-run
```

The v0.1 runtime is intentionally conservative:

- it reuses the same internal command model as the normal CLI
- it supports `version`, `doctor`, `config show`, `info`, `embed`, `extract`, and `verify`
- it supports `let` bindings, explicit-list loops, `glob(...)` loops, and
  `if exists(...)` / `if not exists(...)`
- it stops on the first failing workflow step
- it reports planned/executed/skipped/failure counts in a workflow summary

Example:

```rsx
let input = "tests/fixtures/input/input.jpg"
let output = "/tmp/rse_classic.png"

embed {
  in: input
  out: output
  msg_block: "hello classic"
  protocol: classic
}

extract {
  in: output
  protocol: classic
}
```

Use `--dry-run` before a real run when a workflow writes files or expands
`glob(...)` loops.

Current runtime behavior:

- reports runtime failures with source location and step number
- includes elapsed time and failed-step details in the workflow summary
- expands `glob(...)` locally in deterministic lexicographic order
- reports empty glob loops explicitly as skipped
- never prints key contents in dry-run previews or workflow summaries

See [CLI](cli.md) for the command entry points that host workflow execution.

Repository examples:

- `examples/classic_roundtrip.rsx`
- `examples/mosaic_debug.rsx`
- `examples/batch_info.rsx`

Current v0.1 limitations:

- no workflow JSON summary mode
- no command result capture
- no string interpolation
- no user-defined functions
- no parallel execution
- command blocks with `json: true` are accepted for parsing and dry-run
  inspection, but real workflow execution rejects them for now to avoid mixing
  per-command JSON with workflow-level terminal summaries

# Classic Verification Edge Cases

This directory holds optional reference artifacts for a small set of Classic
verification edge cases used during manual regression checks:

- `000000020333.jpg`
- `000000035279.jpg`

These cases are useful because they let the CLI and tooling be exercised
against repeatable non-success outcomes without relying on private datasets.

If local preserved artifacts are available, the filenames in this directory are
intended to follow:

- `000000020333_original.jpg`
- `000000020333_embedded.png`
- `000000035279_original.jpg`
- `000000035279_embedded.png`

These assets are optional debugging fixtures rather than a required part of the
automated test suite.

## Reproduction

Example commands:

```sh
./build/rse embed \
  --protocol classic \
  --in datasets/coco/val2017/000000020333.jpg \
  --out /tmp/000000020333_classic.png \
  --msg-block "roadscript-corpus-eval" \
  --step 30 \
  --json

./build/rse verify \
  --protocol classic \
  --in /tmp/000000020333_classic.png \
  --step 30 \
  --json
```

Use these cases when comparing CLI behavior across builds or validating
reporting paths in the surrounding tooling.

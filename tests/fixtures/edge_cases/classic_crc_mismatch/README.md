# Classic CRC Mismatch Edge Cases

This directory documents two known deterministic Classic payload recovery
failures observed during corpus evaluation:

- `000000020333.jpg`
- `000000035279.jpg`

These cases are useful because:

- embedding succeeds
- payload recovery fails with a CRC mismatch
- the failure is deterministic
- the result is a payload recovery failure, not an engine crash

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

Expected interpretation:

- embedding: succeeds
- payload recovery: fails
- failure category: `payload_crc_mismatch`

Use these cases for future regression/debug work when you need to separate:

- protocol support-envelope rejections
- payload recovery failures
- runtime/tool execution failures

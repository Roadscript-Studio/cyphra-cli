#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
import urllib.request
import zipfile
from pathlib import Path

from corpus.evaluation_support import ensure_dir

COCO_VAL_URL = "http://images.cocodataset.org/zips/val2017.zip"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download and prepare explicit Roadscript evaluation datasets.",
    )
    parser.add_argument(
        "--dataset",
        required=True,
        choices=["coco-val"],
        help="Dataset bootstrap to prepare.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path("datasets") / "coco",
        help="Root directory for the downloaded dataset (default: datasets/coco).",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Extract only the first N images in deterministic order.",
    )
    return parser.parse_args()


def download_file(url: str, destination: Path) -> None:
    ensure_dir(destination.parent)
    if destination.exists():
        print(f"[reuse] archive already exists: {destination}")
        return

    print(f"[download] {url}")
    print(f"[target]   {destination}")
    urllib.request.urlretrieve(url, destination)


def extract_coco_val(archive_path: Path, output_root: Path, limit: int | None) -> tuple[Path, int]:
    extract_dir = output_root / "val2017"
    ensure_dir(extract_dir)

    with zipfile.ZipFile(archive_path) as archive:
        image_members = sorted(
            (
                member for member in archive.namelist()
                if member.startswith("val2017/") and member.lower().endswith(".jpg")
            ),
            key=str.lower,
        )
        if limit is not None:
            image_members = image_members[:limit]

        extracted = 0
        for member in image_members:
            destination = output_root / member
            if destination.exists():
                continue
            archive.extract(member, output_root)
            extracted += 1

    return extract_dir, len(image_members)


def main() -> int:
    args = parse_args()

    if args.limit is not None and args.limit <= 0:
        print("[FAIL] --limit must be greater than 0", file=sys.stderr)
        return 1

    if args.dataset != "coco-val":
        print(f"[FAIL] unsupported dataset: {args.dataset}", file=sys.stderr)
        return 1

    output_root = args.output_root
    downloads_dir = output_root / "downloads"
    archive_path = downloads_dir / "val2017.zip"

    download_file(COCO_VAL_URL, archive_path)
    extract_dir, prepared_count = extract_coco_val(archive_path, output_root, args.limit)

    print("Roadscript dataset bootstrap")
    print(f"  dataset        : {args.dataset}")
    print(f"  archive        : {archive_path}")
    print(f"  extract_dir    : {extract_dir}")
    print(f"  requested_limit: {args.limit if args.limit is not None else 'all'}")
    print(f"  prepared_files : {prepared_count}")
    print("  note           : downloads are explicit only and never run during normal tests or CI")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

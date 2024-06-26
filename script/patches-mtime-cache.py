#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import posixpath
import sys
import traceback

from lib.patches import patch_from_dir


def patched_file_paths(patches_config):
    for target in patches_config:
        patch_dir = target.get('patch_dir')
        repo = target.get('repo')
        for line in patch_from_dir(patch_dir).split("\n"):
            if line.startswith("+++"):
                yield posixpath.join(repo, line[6:])


def generate_cache(patches_config):
    mtime_cache = {}

    for file_path in patched_file_paths(patches_config):
        if file_path in mtime_cache:
            # File may be patched multiple times, we don't need to
            # rehash it since we are looking at the final result
            continue

        if not os.path.exists(file_path):
            print("Skipping non-existent file:", file_path)
            continue

        with open(file_path, "rb") as f:
            mtime_cache[file_path] = {
                "sha256": hashlib.sha256(f.read()).hexdigest(),
                "atime": os.path.getatime(file_path),
                "mtime": os.path.getmtime(file_path),
            }

    return mtime_cache


def apply_mtimes(mtime_cache):
    updates = []

    for file_path, metadata in mtime_cache.items():
        if not os.path.exists(file_path):
            print("Skipping non-existent file:", file_path)
            continue

        with open(file_path, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() == metadata["sha256"]:
                updates.append(
                    [file_path, metadata["atime"], metadata["mtime"]]
                )

    # We can't atomically set the times for all files at once, but by waiting
    # to update until we've checked all the files we at least have less chance
    # of only updating some files due to an error on one of the files
    for [file_path, atime, mtime] in updates:
        os.utime(file_path, (atime, mtime))


def set_mtimes(patches_config, mtime):
    mtime_cache = {}

    for file_path in patched_file_paths(patches_config):
        if file_path in mtime_cache:
            continue

        if not os.path.exists(file_path):
            print("Skipping non-existent file:", file_path)
            continue

        mtime_cache[file_path] = mtime

    for file_path, file_mtime in mtime_cache.items():
        os.utime(file_path, (file_mtime, file_mtime))


def main():
    parser = argparse.ArgumentParser(
        description="Make mtime cache for patched files"
    )
    subparsers = parser.add_subparsers(
        dest="operation", help="sub-command help"
    )

    apply_subparser = subparsers.add_parser(
        "apply", help="apply the mtimes from the cache"
    )
    apply_subparser.add_argument(
        "--cache-file", required=True, help="mtime cache file"
    )
    apply_subparser.add_argument(
        "--preserve-cache",
        action="store_true",
        help="don't delete cache after applying",
    )

    generate_subparser = subparsers.add_parser(
        "generate", help="generate the mtime cache"
    )
    generate_subparser.add_argument(
        "--cache-file", required=True, help="mtime cache file"
    )

    set_subparser = subparsers.add_parser(
        "set", help="set all mtimes to a specific date"
    )
    set_subparser.add_argument(
        "--mtime",
        type=int,
        required=True,
        help="mtime to use for all patched files",
    )

    for subparser in [generate_subparser, set_subparser]:
        subparser.add_argument(
            "--patches-config",
            type=argparse.FileType("r"),
            required=True,
            help="patches' config in the JSON format",
        )

    args = parser.parse_args()

    if args.operation == "generate":
        try:
            # Cache file may exist from a previously aborted sync. Reuse it.
            with open(args.cache_file, mode='r', encoding='utf-8') as fin:
                json.load(fin)  # Make sure it's not an empty file
                print("Using existing mtime cache for patches")
                return 0
        except Exception:
            pass

        try:
            with open(args.cache_file, mode="w", encoding='utf-8') as fin:
                mtime_cache = generate_cache(json.load(args.patches_config))
                json.dump(mtime_cache, fin, indent=2)
        except Exception:
            print(
                "ERROR: failed to generate mtime cache for patches",
                file=sys.stderr,
            )
            traceback.print_exc(file=sys.stderr)
            return 0
    elif args.operation == "apply":
        if not os.path.exists(args.cache_file):
            print("ERROR: --cache-file does not exist", file=sys.stderr)
            return 0  # Cache file may not exist, fail more gracefully

        try:
            with open(args.cache_file, mode='r', encoding='utf-8') as file_in:
                apply_mtimes(json.load(file_in))

            if not args.preserve_cache:
                os.remove(args.cache_file)
        except Exception:
            print(
                "ERROR: failed to apply mtime cache for patches",
                file=sys.stderr,
            )
            traceback.print_exc(file=sys.stderr)
            return 0
    elif args.operation == "set":
        answer = input(
            "WARNING: Manually setting mtimes could mess up your build. "
            "If you're sure, type yes: "
        )

        if answer.lower() != "yes":
            print("Aborting")
            return 0

        set_mtimes(json.load(args.patches_config), args.mtime)

    return 0


if __name__ == "__main__":
    sys.exit(main())

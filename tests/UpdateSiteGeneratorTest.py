#!/usr/bin/env python3
"""Exercise the release manifest generator without requiring real installers."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    generator = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as raw_directory:
        root = Path(raw_directory)
        artifacts = {
            "windows-x64": root / "DannyDee-Windows-x64-Setup.exe",
            "macos-arm64": root / "DannyDee-macOS-arm64.dmg",
            "macos-x64": root / "DannyDee-macOS-x64.dmg",
        }
        for index, artifact in enumerate(artifacts.values(), start=1):
            artifact.write_bytes((artifact.name.encode("utf-8") + b"\n") * index)

        output = root / "site"
        command = [
            sys.executable,
            str(generator),
            "--output",
            str(output),
            "--base-url",
            "https://example.test/releases",
            "--version",
            "1.2.3",
            "--notes",
            "Test release",
        ]
        for platform, artifact in artifacts.items():
            command.extend(("--artifact", f"{platform}={artifact}"))
        subprocess.run(command, check=True, capture_output=True, text=True)

        manifest = json.loads((output / "updates.json").read_text(encoding="utf-8"))
        if manifest["schema"] != 1 or manifest["version"] != "1.2.3":
            raise AssertionError("Manifest metadata is incorrect")
        if set(manifest["platforms"]) != set(artifacts):
            raise AssertionError("Manifest does not contain all release platforms")

        for platform, artifact in artifacts.items():
            entry = manifest["platforms"][platform]
            expected_hash = hashlib.sha256(artifact.read_bytes()).hexdigest()
            if entry["sha256"] != expected_hash or entry["size"] != artifact.stat().st_size:
                raise AssertionError(f"Integrity metadata is incorrect for {platform}")
            if not entry["url"].endswith(artifact.name):
                raise AssertionError(f"Download URL is incorrect for {platform}")

        page = (output / "index.html").read_text(encoding="utf-8")
        for expected_label in ("Windows x64", "macOS – Apple Silicon", "macOS – Intel"):
            if expected_label not in page:
                raise AssertionError(f"Download page is missing {expected_label}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

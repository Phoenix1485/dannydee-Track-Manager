#!/usr/bin/env python3
"""Create a multi-platform GitHub Pages update site and hash-and-size manifest."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import shutil
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote, urljoin, urlparse


PLATFORM_EXTENSIONS = {
    "windows-x64": ".exe",
    "macos-arm64": ".dmg",
    "macos-x64": ".dmg",
}
PLATFORM_LABELS = {
    "windows-x64": "Windows x64",
    "macos-arm64": "macOS – Apple Silicon",
    "macos-x64": "macOS – Intel",
}


def parse_artifact(value: str) -> tuple[str, Path]:
    platform, separator, raw_path = value.partition("=")
    if not separator or platform not in PLATFORM_EXTENSIONS:
        raise argparse.ArgumentTypeError(
            "artifact must use platform=path with a supported platform key"
        )
    path = Path(raw_path).resolve()
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"artifact does not exist: {path}")
    expected = PLATFORM_EXTENSIONS[platform]
    if path.suffix.lower() != expected:
        raise argparse.ArgumentTypeError(f"{platform} requires a {expected} artifact")
    return platform, path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--notes", default="")
    parser.add_argument("--artifact", action="append", required=True, type=parse_artifact)
    args = parser.parse_args()

    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("version must use numeric major.minor.patch format")
    parsed_base = urlparse(args.base_url)
    if parsed_base.scheme != "https" or not parsed_base.netloc:
        parser.error("base URL must be an absolute HTTPS URL")

    artifacts = dict(args.artifact)
    if len(artifacts) != len(args.artifact):
        parser.error("each platform may only be supplied once")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    base_url = args.base_url.rstrip("/") + "/"

    platforms: dict[str, dict[str, object]] = {}
    buttons: list[str] = []
    for platform, source in artifacts.items():
        destination = output / source.name
        shutil.copy2(source, destination)
        artifact_url = urljoin(base_url, quote(source.name))
        platforms[platform] = {
            "url": artifact_url,
            "sha256": sha256(destination),
            "size": destination.stat().st_size,
        }
        buttons.append(
            f'<a href="{html.escape(source.name)}">'
            f'{html.escape(PLATFORM_LABELS[platform])} herunterladen</a>'
        )

    manifest = {
        "schema": 1,
        "version": args.version,
        "published_at": datetime.now(timezone.utc).isoformat(),
        "notes": args.notes,
        "platforms": platforms,
    }
    (output / "updates.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (output / ".nojekyll").touch()
    page = f"""<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DannyDee Track Manager {html.escape(args.version)}</title>
<style>
:root{{color-scheme:dark;font-family:system-ui,sans-serif;background:#0b0d13;color:#f4f6fa}}
body{{margin:0;min-height:100vh;display:grid;place-items:center}}
main{{width:min(680px,calc(100% - 48px));background:#131722;border:1px solid #2b3241;
border-radius:18px;padding:34px;box-shadow:0 24px 70px #0008}}
p{{color:#aab1c1;line-height:1.6}}.downloads{{display:flex;flex-wrap:wrap;gap:10px;margin-top:18px}}
a{{display:inline-block;padding:12px 18px;border-radius:10px;background:#7c4dff;color:white;
font-weight:700;text-decoration:none}}small{{display:block;margin-top:22px;color:#778096}}
</style></head><body><main>
<h1>DannyDee Track Manager {html.escape(args.version)}</h1>
<p>{html.escape(args.notes)}</p><div class="downloads">{''.join(buttons)}</div>
<small>Downloads werden von der App per SHA-256 und Dateigröße geprüft.</small>
</main></body></html>"""
    (output / "index.html").write_text(page, encoding="utf-8")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

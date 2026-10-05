import json
import re
import subprocess
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(message)


def main() -> None:
    if len(sys.argv) != 2:
        fail("Usage: python scripts/update_luau_version.py <luau-tag>")

    luau_tag = sys.argv[1]
    if not re.fullmatch(r"[0-9]+\.[0-9]+", luau_tag):
        fail(f"Unexpected Luau tag format: {luau_tag}")

    release_tag = subprocess.check_output(
        ["git", "describe", "--tags", "--abbrev=0", "--match", "v[0-9]*"],
        text=True,
    ).strip()
    release_sha = subprocess.check_output(
        ["git", "rev-list", "-n", "1", release_tag], text=True
    ).strip()

    props_path = Path("Directory.Build.props")
    props_text = props_path.read_text(encoding="utf-8")
    version_match = re.search(r"<Version>([^<]+)</Version>", props_text)
    if version_match is None:
        fail("Could not find <Version> in Directory.Build.props")
    assert version_match is not None

    current_version = version_match.group(1)
    base_version = re.sub(r"\+luau\.[0-9]+(?:\.[0-9]+)?$", "", current_version)
    next_version = f"{base_version}+luau.{luau_tag}"

    updated_props = (
        props_text[: version_match.start(1)]
        + next_version
        + props_text[version_match.end(1) :]
    )
    props_path.write_text(updated_props, encoding="utf-8")

    manifest_path = Path(".release-please-manifest.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["."] = next_version
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    # Changing Luau metadata breaks the exact tag match; retain the release cutoff.
    config_path = Path(".release-please-config.json")
    config = json.loads(config_path.read_text(encoding="utf-8"))
    config["bootstrap-sha"] = release_sha
    config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

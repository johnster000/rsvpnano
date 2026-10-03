Import("env")

import json
import os
import re
import subprocess
from pathlib import Path


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SHORT_SHA_LENGTH = 12
UPSTREAM_OWNER = "ionutdecebal"
OWNER_PATTERN = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})$")


def run_git(*args: str) -> str:
    return subprocess.check_output(
        ["git", *args],
        cwd=PROJECT_DIR,
        text=True,
        stderr=subprocess.DEVNULL,
    ).strip()


def detect_version() -> str:
    selected_tag = os.environ.get("RSVP_FIRMWARE_TAG", "").strip()

    try:
        head_sha = run_git("rev-parse", "HEAD")
        short_sha = run_git("rev-parse", f"--short={SHORT_SHA_LENGTH}", "HEAD")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "dev"

    if selected_tag:
        try:
            tag_sha = run_git("rev-list", "-n", "1", selected_tag)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(f"Firmware tag does not exist: {selected_tag}") from error

        if tag_sha != head_sha:
            raise RuntimeError(
                f"Firmware tag {selected_tag} points at {tag_sha}, "
                f"but the build is using {head_sha}"
            )

        tag = selected_tag
    else:
        try:
            tag = run_git("describe", "--tags", "--abbrev=0")
        except subprocess.CalledProcessError:
            tag = "dev"

    version = f"{tag}+{short_sha}"

    try:
        if run_git("status", "--porcelain", "--untracked-files=no"):
            version += ".dirty"
    except subprocess.CalledProcessError:
        pass

    return version


def detect_repository_owner() -> str:
    """Owner whose releases, catalogs and GitHub Pages site this build trusts by default."""
    owner = os.environ.get("RSVP_REPOSITORY_OWNER", "").strip() or os.environ.get("GITHUB_REPOSITORY_OWNER", "").strip()
    if not owner:
        try:
            remote = run_git("remote", "get-url", "origin")
        except (subprocess.CalledProcessError, FileNotFoundError):
            remote = ""
        match = re.search(r"github\.com[:/]([^/]+)/[^/]+?(?:\.git)?/?$", remote)
        owner = match.group(1) if match else ""
    owner = owner.lower()
    return owner if OWNER_PATTERN.match(owner) else UPSTREAM_OWNER


version = detect_version()
repository_owner = detect_repository_owner()
generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_header = generated_dir / "FirmwareVersion.generated.h"
contents = f"#pragma once\n\ninline constexpr char kFirmwareVersion[] = {json.dumps(version)};\n"

generated_dir.mkdir(parents=True, exist_ok=True)
if not generated_header.exists() or generated_header.read_text(encoding="utf-8") != contents:
    generated_header.write_text(contents, encoding="utf-8")

env.AppendUnique(CPPPATH=[str(generated_dir)])
env.Append(CPPDEFINES=[("RSVP_REPOSITORY_OWNER", env.StringifyMacro(repository_owner))])

print(f"Firmware version: {version}")
print(f"Firmware repository owner: {repository_owner}")

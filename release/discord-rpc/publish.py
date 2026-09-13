"""Publish the existing user-tested artifact only to this fork; never rebuild it."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile


REPO = "NargaFRZ/Stremio-Kai"
SOURCE = "ee6af542941f4821e74176cceafb70d312a62942"
RUN = 34756954880
ARTIFACT = 10316913425
ARTIFACT_SHA256 = "699c2367f2f24ba7336422ec3c85a0e92278a459b81beb774b73394305f39f52"
TAG = "v4.8.0-rpc1"
TITLE = "Stremio-Kai 4.8.0 RPC1 - Windows portable"
FILES = {"Stremio-Kai-4.8.0-RPC-Portable-x64.zip", "Stremio-Kai-RPC-Sources.zip", "SHA256SUMS.txt"}
NOTES = Path(__file__).with_name("notes.md")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def api(path):
    return json.loads(subprocess.check_output(["gh", "api", f"repos/{REPO}/{path}"]))


def release_command(*args):
    subprocess.run(["gh", "release", *args, "--repo", REPO], check=True)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_assets(release, expected, complete):
    assets = {asset["name"]: asset for asset in release["assets"]}
    require(len(assets) == len(release["assets"]), "Duplicate release asset names")
    require(set(assets) <= FILES, "Unexpected existing release assets; refusing to change them")
    if complete:
        require(set(assets) == FILES, "Release is missing required assets")
    for name, asset in assets.items():
        require(asset["state"] == "uploaded", f"Incomplete asset: {name}")
        require(asset["size"] == expected[name]["size"], f"Asset size mismatch: {name}")
        require(asset.get("digest") == "sha256:" + expected[name]["sha256"], f"Asset digest mismatch: {name}")
    return assets


def main():
    require(os.environ.get("GITHUB_REPOSITORY") == REPO, "Publishing is limited to the user's fork")
    require(os.environ.get("GITHUB_REF") == "refs/heads/codex/discord-media-title", "Unexpected branch")
    run = api(f"actions/runs/{RUN}")
    require(run["conclusion"] == "success" and run["head_sha"] == SOURCE, "Build identity/result mismatch")
    artifact = api(f"actions/artifacts/{ARTIFACT}")
    require(not artifact["expired"], "The tested build artifact has expired")
    require(artifact["name"] == "Stremio-Kai-4.8.0-RPC-Windows-x64", "Unexpected artifact name")
    require(artifact["workflow_run"]["id"] == RUN and artifact["workflow_run"]["head_sha"] == SOURCE, "Artifact source mismatch")
    require(artifact["digest"] == "sha256:" + ARTIFACT_SHA256, "Artifact digest changed")

    directory = Path(os.environ["RUNNER_TEMP"]) / "tested-discord-release"
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / "artifact.zip"
    print("Downloading the exact user-tested Windows build", flush=True)
    with archive.open("wb") as output:
        subprocess.run(["gh", "api", f"repos/{REPO}/actions/artifacts/{ARTIFACT}/zip"], stdout=output, check=True)
    require(archive.stat().st_size == artifact["size_in_bytes"], "Artifact size mismatch")
    require(sha256(archive) == ARTIFACT_SHA256, "Downloaded artifact checksum mismatch")
    assets_dir = directory / "assets"
    assets_dir.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        require(set(package.namelist()) == FILES and len(package.infolist()) == len(FILES), "Unexpected artifact contents")
        package.extractall(assets_dir)
    subprocess.run(["sha256sum", "--check", "SHA256SUMS.txt"], cwd=assets_dir, check=True)
    expected = {name: {"size": (assets_dir / name).stat().st_size, "sha256": sha256(assets_dir / name)} for name in sorted(FILES)}
    print(json.dumps(expected, indent=2), flush=True)

    # Never move an existing tag or overwrite an existing asset, including on retry.
    for ref in api(f"git/matching-refs/tags/{TAG}"):
        if ref["ref"] == "refs/tags/" + TAG:
            require(ref["object"]["type"] == "commit" and ref["object"]["sha"] == SOURCE, "Existing release tag points elsewhere")
    releases = api("releases?per_page=100")
    release = next((item for item in releases if item["tag_name"] == TAG), None)
    if release is None:
        release_command("create", TAG, "--target", SOURCE, "--title", TITLE, "--notes-file", str(NOTES), "--draft")
        # GitHub's tag endpoint returns published releases, so find this draft by ID.
        release = next(item for item in api("releases?per_page=100") if item["tag_name"] == TAG)
    release_path = f"releases/{release['id']}"
    require(release["target_commitish"] == SOURCE, "Existing release target differs")
    require(release["name"] == TITLE and release["body"].strip() == NOTES.read_text().strip(), "Existing release notes differ")
    present = verify_assets(release, expected, complete=not release["draft"])
    if release["draft"]:
        for name in sorted(FILES - set(present)):
            print(f"Uploading {name}", flush=True)
            release_command("upload", TAG, str(assets_dir / name))
        release = api(release_path)
        verify_assets(release, expected, complete=True)
        release_command("edit", TAG, "--draft=false", "--latest")
    release = api(release_path)
    require(not release["draft"] and release["published_at"], "Release was not published")
    verify_assets(release, expected, complete=True)
    tag = api(f"git/ref/tags/{TAG}")
    require(tag["object"]["type"] == "commit" and tag["object"]["sha"] == SOURCE, "Published tag source mismatch")
    print("Published and verified: " + release["html_url"], flush=True)
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
        summary.write(f"Published [{TAG}]({release['html_url']}) from the original tested artifact.\n\n")
        summary.write("All three uploaded asset sizes and SHA-256 digests verified. No rebuild performed.\n")


if __name__ == "__main__":
    main()

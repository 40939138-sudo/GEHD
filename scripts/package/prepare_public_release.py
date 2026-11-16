"""Prepare a publication directory from a sealed candidate and a new source export.

Does not upload, change repository visibility, or alter the sealed candidate.
Native payloads retain their prior qualification; the manager gains the MIT text.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import zipfile


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def pin(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(16 * 1024 * 1024), b""):
            digest.update(block)
    return {"name": path.name, "bytes": path.stat().st_size, "sha256": digest.hexdigest().upper()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    old = read(args.candidate / "release-manifest.json")
    export = read(args.export)
    assert old["localCandidateReady"] and export["passed"] and export["nativeSourceMatchesCleanBuild"]
    assert args.candidate.resolve() != args.out.resolve()
    assert not (args.out / "release-manifest.json").exists(), "Already prepared"
    version = "v" + (root / "VERSION").read_text().strip()
    assert old["version"] == version
    expected = {item["name"]: item for item in old["attachments"]}
    verified = []
    media = read(args.candidate / "media-manifest.json")
    assert pin(args.candidate / "media-manifest.json") == expected["media-manifest.json"]
    for game in media["games"]:
        for item in game["volumes"]:
            source, target = args.candidate / item["name"], args.out / item["name"]
            assert item == expected[item["name"]]
            assert pin(source) == item, "Changed volume: " + item["name"]
            assert not target.exists()
            # Same-volume hard links avoid rewriting unchanged multi-GB payloads.
            try:
                os.link(source, target)
            except OSError:
                shutil.copyfile(source, target)
            verified.append(item)
            print("Verified unchanged volume:", item["name"], flush=True)
    manager = media["manager"]["name"]
    assert pin(args.candidate / manager) == expected[manager]
    with zipfile.ZipFile(args.candidate / manager) as original:
        with zipfile.ZipFile(args.out / manager, "x") as target:
            for entry in original.infolist():
                target.writestr(entry, original.read(entry.filename))
            target.writestr("GEHD_Common_Manager/LICENSE.txt", (root / "LICENSE.md").read_bytes())
        with zipfile.ZipFile(args.out / manager) as target:
            assert target.testzip() is None
            assert all(original.read(n) == target.read(n) for n in original.namelist())
    media["manager"] = pin(args.out / manager)
    write(args.out / "media-manifest.json", media)
    for name, source in [("RELEASE_NOTES.md", "scripts/package/release-notes.md"),
                         ("LICENSE.md", "LICENSE.md"),
                         ("verify_release.py", "scripts/package/verify_release.py"),
                         ("split_zip.py", "scripts/package/split_zip.py")]:
        shutil.copyfile(root / source, args.out / name)
    notices = (root / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8")
    notices = re.sub(r"\[([^\]]+)\]\((?!https?://)([^)]+)\)",
                     lambda m: m[1] + "（來源ZIP內：`" + m[2] + "`）", notices)
    (args.out / "THIRD_PARTY_NOTICES.md").write_text(notices, encoding="utf-8")
    for item in export["artifacts"]:
        assert pin(args.out / item["name"]) == item
    verification = read(args.candidate / "VERIFICATION.json")
    assert pin(args.candidate / "VERIFICATION.json") == expected["VERIFICATION.json"]
    verification["priorSourceCommit"] = verification["sourceCommit"]
    verification["sourceCommit"] = export["sourceCommit"]
    verification["limits"] = [x for x in verification["limits"] if x != "No public-release license clearance"]
    verification["publicationPackaging"] = {
        "nativeSourceMatchesQualifiedBuild": True,
        "gameVolumesRehashedUnchanged": True,
        "managerExistingEntriesUnchanged": True,
        "managerAddedEntry": "GEHD_Common_Manager/LICENSE.txt",
        "priorMediaManifestSha256": expected["media-manifest.json"]["sha256"],
        "mediaManifestSha256": pin(args.out / "media-manifest.json")["sha256"],
        "newGameRun": False,
    }
    write(args.out / "VERIFICATION.json", verification)
    manifest = dict(old)
    for key in ("published", "publicReleaseEligible", "publicBlockers", "proposedTag"):
        manifest.pop(key, None)
    manifest.update(sourceCommit=export["sourceCommit"], tag=version,
                    createdUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    preparedForPublication=True, sourceLicense="MIT",
                    releaseUrl="https://github.com/40939138-sudo/GEHD/releases/tag/" + version)
    known = {item["name"]: item for item in verified}
    manifest["attachments"] = [known[p.name] if p.name in known else pin(p) for p in sorted(args.out.iterdir())]
    write(args.out / "release-manifest.json", manifest)
    sums = manifest["attachments"] + [pin(args.out / "release-manifest.json")]
    (args.out / "SHA256SUMS.txt").write_text("".join(
        item["sha256"] + "  " + item["name"] + "\n" for item in sorted(sums, key=lambda x: x["name"])), encoding="utf-8")
    write(args.receipt, {"passed": True, "sourceCommit": export["sourceCommit"],
                         "releaseManifest": pin(args.out / "release-manifest.json"),
                         "attachments": sums + [pin(args.out / "SHA256SUMS.txt")],
                         "gameVolumesUnchanged": verified})
    print("Prepared publication directory:", args.out, flush=True)


if __name__ == "__main__":
    main()

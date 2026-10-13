#!/usr/bin/env python3
"""Build a self-contained universal Mac app and a forwardable ZIP.

This is a developer packaging tool. Recipients only run the compiled .app.
"""
import argparse
import hashlib
import json
import pathlib
import plistlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "helper" / "macos"
BUILD = ROOT / "helper" / "build" / "distribution"


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def compiler_options():
    compiler = pathlib.Path(subprocess.check_output(["xcrun", "--find", "swiftc"], text=True).strip())
    include = compiler.parent.parent / "include" / "swift"
    old = include / "module.modulemap"
    new = include / "bridging.modulemap"
    sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
    options = ["-O", "-warnings-as-errors", "-swift-version", "5", "-sdk", sdk]
    if old.exists() and new.exists() and all("module SwiftBridging" in item.read_text() for item in (old, new)):
        empty = BUILD / "empty.modulemap"
        empty.write_text("")
        overlay = BUILD / "modulemap-overlay.json"
        overlay.write_text(json.dumps({"version": 0, "case-sensitive": False, "roots": [
            {"type": "file", "name": str(old), "external-contents": str(empty)}
        ]}))
        options.extend(["-vfsoverlay", str(overlay), "-module-cache-path", str(BUILD / "module-cache")])
    return compiler, options


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="CatDesk-Mac-即用包-2026-10-07")
    parser.add_argument("--destination", type=pathlib.Path, default=ROOT / "exports")
    args = parser.parse_args()
    if pathlib.Path(args.name).name != args.name or args.name in (".", ".."):
        parser.error("name must be a plain directory name")
    destination = args.destination.resolve()
    folder = destination / args.name
    archive = destination / (args.name + ".zip")
    if folder.exists() or archive.exists():
        parser.error("output already exists; choose a new name rather than overwrite it")
    BUILD.mkdir(parents=True, exist_ok=True)
    destination.mkdir(parents=True, exist_ok=True)
    compiler, options = compiler_options()
    for architecture in ("arm64", "x86_64"):
        target = ["-target", f"{architecture}-apple-macos15.0"]
        run(compiler, *options, *target, "-framework", "Foundation", "-framework", "IOKit",
            ROOT / "helper" / "CatDeskHelper.swift", "-o", BUILD / f"helper-{architecture}")
        # Swift requires the top-level entry point of a multi-file app to be main.swift.
        entry = BUILD / "main.swift"
        shutil.copyfile(SOURCE / "CatDeskApp.swift", entry)
        run(compiler, *options, *target, "-framework", "AppKit", "-framework", "ServiceManagement",
            SOURCE / "ConnectionState.swift", entry, "-o", BUILD / f"app-{architecture}")
    checker = BUILD / "test-connection-state"
    check_options = [option for option in options if option != "-O"]
    run(compiler, *check_options, "-Onone", SOURCE / "ConnectionState.swift", SOURCE / "test_connection_state.swift",
        "-o", checker)
    run(checker)
    app = folder / "CatDesk连接器.app"
    macos = app / "Contents" / "MacOS"
    resources = app / "Contents" / "Resources"
    macos.mkdir(parents=True)
    resources.mkdir(parents=True)
    run("lipo", "-create", BUILD / "app-arm64", BUILD / "app-x86_64", "-output", macos / "CatDesk")
    run("lipo", "-create", BUILD / "helper-arm64", BUILD / "helper-x86_64", "-output", macos / "CatDeskHelper")
    for executable in macos.iterdir():
        executable.chmod(0o755)
    info = {
        "CFBundleName": "CatDesk连接器", "CFBundleDisplayName": "CatDesk连接器",
        "CFBundleExecutable": "CatDesk", "CFBundleIdentifier": "com.catdesk.connector",
        "CFBundlePackageType": "APPL", "CFBundleShortVersionString": "1.0.0", "CFBundleVersion": "1",
        "CFBundleIconFile": "CatDesk.icns", "LSMinimumSystemVersion": "15.0",
        "NSHighResolutionCapable": True, "NSPrincipalClass": "NSApplication",
    }
    (app / "Contents" / "Info.plist").write_bytes(plistlib.dumps(info, sort_keys=False))
    shutil.copyfile(SOURCE / "使用说明.txt", resources / "使用说明.txt")
    shutil.copyfile(SOURCE / "使用说明.txt", folder / "先读我.txt")
    icon_builder = BUILD / "make-icon"
    run(compiler, *options, "-framework", "AppKit", SOURCE / "MakeIcon.swift", "-o", icon_builder)
    iconset = BUILD / "CatDesk.iconset"
    run(icon_builder, iconset)
    run("iconutil", "-c", "icns", iconset, "-o", resources / "CatDesk.icns")
    # Ad hoc signatures verify integrity; they do not imply Developer ID signing or notarization.
    run("codesign", "--force", "--sign", "-", macos / "CatDeskHelper")
    run("codesign", "--force", "--sign", "-", app)
    run("codesign", "--verify", "--deep", "--strict", "--verbose=2", app)
    hashes = []
    for item in sorted(folder.rglob("*")):
        if item.is_file():
            hashes.append(f"{hashlib.sha256(item.read_bytes()).hexdigest()}  {item.relative_to(folder).as_posix()}")
    (folder / "SHA256SUMS.txt").write_text("\n".join(hashes) + "\n", encoding="utf-8")
    run("ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent", folder, archive)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256.txt").write_text(f"{checksum}  {archive.name}\n", encoding="utf-8")
    print(f"Folder: {folder}\nZIP: {archive}\nZIP SHA-256: {checksum}")


if __name__ == "__main__":
    main()

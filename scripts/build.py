#!/usr/bin/env python3
"""Build with Apple's standalone Command Line Tools; full Xcode is optional."""
import argparse
import datetime
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
from embed_metal import embed_metal

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
NAME = "OrderedPatterns"


def run(args):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], check=True)


def sdk_examples(given):
    candidates = [Path(given).expanduser()] if given else []
    configured = ROOT / ".local/sdk-path.txt"
    if not given and configured.exists():
        candidates.append(Path(configured.read_text().strip()).expanduser())
    if not given and os.environ.get("AE_SDK_PATH"):
        candidates.append(Path(os.environ["AE_SDK_PATH"]).expanduser())
    candidates.append(ROOT / ".local/sdk")
    for base in candidates:
        if not base.exists():
            continue
        for folder in [base, base / "Examples"]:
            if (folder / "Headers/AE_Effect.h").is_file():
                return folder.resolve()
        for header in base.rglob("AE_Effect.h"):
            if header.parent.name == "Headers":
                return header.parent.parent.resolve()
    raise RuntimeError("AE SDK not found. Download Adobe's Plug-in SDK, then run: python3 scripts/build.py --sdk /absolute/path/to/SDK")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="SDK root or its Examples folder; remembered locally after success")
    parser.add_argument("--test", action="store_true", help="Test pure rendering core without the Adobe SDK")
    parser.add_argument("--benchmark", action="store_true", help="Benchmark reference, tiled CPU and real Metal rendering")
    parser.add_argument("--cpu-only", action="store_true", help="Use portable CPU fallback instead of Metal")
    parser.add_argument("--host-test", action="store_true", help="Exercise the built bundle with simulated AE SDK callbacks")
    parser.add_argument("--trace", type=Path, help="Development only: log render call intervals to this JSONL file")
    parser.add_argument("--install", action="store_true", help="Build and install into the current user's Adobe MediaCore folder")
    parser.add_argument("--install-dir", type=Path, default=Path.home() / "Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore")
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    embed_metal(ROOT, BUILD)
    raster_sources = [ROOT / "src/PatternCore.cpp", ROOT / "src/RasterCore.cpp"]
    raster_flags = []
    if args.cpu_only:
        raster_sources.append(ROOT / "src/RasterGPUStub.cpp")
    else:
        raster_sources.append(ROOT / "src/RasterMetal.mm")
        raster_flags = ["-fobjc-arc", "-I", BUILD, "-framework", "Foundation", "-framework", "Metal"]
    if args.test:
        binary = BUILD / "core_test"
        run(["xcrun", "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", ROOT / "src/PatternCore.cpp", ROOT / "tests/core_test.cpp", "-o", binary])
        run([binary])
        raster_binary = BUILD / "raster_test"
        run(["xcrun", "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", *raster_sources,
             ROOT / "tests/raster_test.cpp", *raster_flags, "-o", raster_binary])
        run([raster_binary] + ([] if args.cpu_only else ["--metal"]))
        return
    if args.benchmark:
        binary = BUILD / "raster_benchmark"
        run(["xcrun", "clang++", "-std=c++17", "-O2", *raster_sources,
             ROOT / "tools/raster_benchmark.cpp", *raster_flags, "-o", binary])
        run([binary])
        return
    sdk = sdk_examples(args.sdk)
    embed_metal(ROOT, BUILD)
    bundle = ROOT / "dist" / (NAME + ".plugin")
    contents = bundle / "Contents"
    (contents / "MacOS").mkdir(parents=True, exist_ok=True)
    (contents / "Resources").mkdir(exist_ok=True)
    includes = [sdk / "Headers", sdk / "Headers/SP", sdk / "Util", sdk / "Resources", ROOT / "src"]
    cmd = ["xcrun", "clang++", "-std=c++17", "-arch", "arm64", "-mmacosx-version-min=14.0", "-bundle", "-O2", "-g", "-fvisibility=hidden"]
    if args.trace:
        import json
        cmd.append("-DPATTERN_MFR_TRACE_PATH=" + json.dumps(str(args.trace.resolve())))
    for folder in includes:
        cmd.extend(["-I", folder])
    cmd.extend([ROOT / "src/OrderedPatterns.cpp", *raster_sources, *raster_flags, "-o", contents / "MacOS" / NAME])
    run(cmd)
    rez = ["xcrun", "Rez", "-d", "SystemSevenOrLater=1", "-d", "__MACH__=1", "-d", "AE_OS_MAC=1", "-arch", "arm64", "-useDF", "-script", "Roman"]
    for folder in includes:
        rez.extend(["-i", folder])
    rez.extend([ROOT / "resources/OrderedPatternsPiPL.r", "-o", contents / "Resources" / (NAME + ".rsrc")])
    run(rez)
    info = {
        "CFBundleDevelopmentRegion": "zh-Hans", "CFBundleExecutable": NAME,
        "CFBundleIdentifier": "com.mashao.orderedpatterns", "CFBundleName": "有序图案",
        "CFBundleDisplayName": "有序图案",
        "CFBundlePackageType": "eFKT", "CFBundleSignature": "FXTC",
        "CFBundleVersion": "2.0.0", "CFBundleShortVersionString": "2.0.0",
    }
    with (contents / "Info.plist").open("wb") as f:
        plistlib.dump(info, f)
    run(["codesign", "--force", "--sign", "-", bundle])
    run(["codesign", "--verify", "--strict", bundle])
    run(["file", contents / "MacOS" / NAME])
    run(["plutil", "-lint", contents / "Info.plist"])
    (ROOT / ".local").mkdir(exist_ok=True)
    (ROOT / ".local/sdk-path.txt").write_text(str(sdk) + "\n")
    print("Built:", bundle)
    if args.host_test:
        binary = BUILD / "macos_host_test"
        test_cmd = ["xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror"]
        for folder in includes:
            test_cmd.extend(["-I", folder])
        test_cmd.extend([ROOT / "tests/macos_host_test.cpp", ROOT / "src/PatternCore.cpp", "-o", binary])
        run(test_cmd)
        run([binary, contents / "MacOS" / NAME])
    if args.install:
        args.install_dir.mkdir(parents=True, exist_ok=True)
        destination = args.install_dir / bundle.name
        if not args.install_dir.is_dir() or not os.access(args.install_dir, os.W_OK):
            raise RuntimeError("Install folder is unavailable or not writable: " + str(args.install_dir))
        if destination.exists():
            stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
            backup = ROOT / ".local/backups" / stamp / bundle.name
            backup.parent.mkdir(parents=True)
            shutil.copytree(destination, backup)
            shutil.rmtree(destination)
            print("Previous build backed up:", backup)
        shutil.copytree(bundle, destination)
        print("Installed:", destination)
        print("Restart After Effects before testing the new binary.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print("Build failed:", error, file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""Build the public Linux x86-64 executables without local source paths."""

import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def main():
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise SystemExit("Run this script on x86-64 Linux with GCC.")
    root = Path(__file__).resolve().parent.parent
    if not os.environ.get("VCPKG_ROOT"):
        raise SystemExit("Set VCPKG_ROOT to your bootstrapped vcpkg checkout.")
    vcpkg = Path(os.environ["VCPKG_ROOT"]).resolve()
    build = root / "build/linux-public"
    overlay = build / "triplets"
    overlay.mkdir(parents=True, exist_ok=True)
    # Apply these flags to dependencies as well as RaceVideo: stripping cannot
    # remove paths stored in logging strings or Abseil flag metadata.
    mappings = [(Path.home(), "/build/home"),
                (Path(tempfile.gettempdir()), "/build/tmp"),
                (vcpkg, "/build/vcpkg"), (root, "/src")]
    flags = " ".join(f"-ffile-prefix-map={path}={replacement}"
                     for path, replacement in mappings)
    if any(any(c.isspace() or c in '\";\\' for c in str(path))
           for path, _ in mappings):
        raise SystemExit("Build paths must not contain whitespace or CMake delimiters.")
    triplet = "x64-linux-public"
    (overlay / f"{triplet}.cmake").write_text(
        'set(VCPKG_TARGET_ARCHITECTURE x64)\n'
        'set(VCPKG_CRT_LINKAGE dynamic)\n'
        'set(VCPKG_LIBRARY_LINKAGE static)\n'
        'set(VCPKG_CMAKE_SYSTEM_NAME Linux)\n'
        'set(VCPKG_BUILD_TYPE release)\n'
        f'set(VCPKG_C_FLAGS "{flags}")\n'
        f'set(VCPKG_CXX_FLAGS "{flags}")\n')
    subprocess.run([
        "cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
        f"-DVCPKG_OVERLAY_TRIPLETS={overlay}",
        f"-DVCPKG_TARGET_TRIPLET={triplet}",
        "-DCMAKE_BUILD_TYPE=Release", "-DRACEVIDEO_BUILD_TESTS=OFF",
        f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_CXX_FLAGS={flags}"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
    candidates = []
    for name in ("racevideo", "racevideo_commands"):
        candidate = build / f"{name}-stripped"
        subprocess.run(["strip", "--strip-unneeded", "-o", str(candidate),
                        str(build / name)], check=True)
        strings = subprocess.check_output(["strings", str(candidate)], text=True)
        if (any(str(path) + "/" in strings for path, _ in mappings)
                or any(prefix in strings for prefix in ("/home/", "/tmp/", "/root/"))):
            raise SystemExit(f"Local paths remain in {name}; binaries were not replaced.")
        candidates.append((candidate, root / "bin/linux-x86_64" / name))
    for candidate, destination in candidates:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(candidate, destination)
        destination.chmod(0o755)
        print(f"Built and verified {destination}")


if __name__ == "__main__":
    main()

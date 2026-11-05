#!/usr/bin/env python3
"""Build and run DSLogic boundary tests without hardware or a Unix shell."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deps", type=Path, default=root.parent / "build-dsl/deps/root/mingw64")
    parser.add_argument("--cc", default=os.environ.get("GCC"))
    parser.add_argument("--out", type=Path, default=root.parent / "build-dsl/dslogic-native-build")
    args = parser.parse_args()
    deps, out = args.deps.resolve(), args.out.resolve()
    cc = args.cc or str(deps / "bin/gcc.exe")
    if not Path(cc).is_file():
        cc = shutil.which(cc) or shutil.which("gcc")
    if not cc:
        parser.error("GCC not found; pass --cc and --deps")
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(deps / "bin") + os.pathsep + env.get("PATH", "")
    flags = ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra",
             "-Werror=implicit-function-declaration", "-ffunction-sections", "-fdata-sections"]
    flags += ["-D" + name for name in ("HAVE_LIBUSB_1_0", "HAVE_LIBUSB_OS_HANDLE",
              "HAVE_SERIAL_COMM", "HAVE_ZLIB", "HAVE_LIBZIP", "HAVE_DRIVERS", "_GNU_SOURCE")]
    flags += ["-I" + str(path) for path in (root / "src", root / "include",
              root.parent / "libusb/libusb", root.parent / "common", deps / "include",
              deps / "include/glib-2.0", deps / "lib/glib-2.0/include")]
    for name in ("api", "command", "protocol"):
        source = root / "src/hardware/dreamsourcelab-dslogic" / (name + ".c")
        subprocess.run([cc, *flags, "-c", str(source), "-o", str(out / (name + ".o"))],
                       cwd=root, env=env, check=True)
    exe = out / ("dslogic-native.exe" if os.name == "nt" else "dslogic-native")
    glib = deps / "lib/libglib-2.0.dll.a"
    libs = [str(glib)] if glib.exists() else ["-L" + str(deps / "lib"), "-lglib-2.0"]
    subprocess.run([cc, *flags, str(root / "tests/dslogic-native.c"),
                    "-Wl,--gc-sections", *libs, "-lm", "-o", str(exe)],
                   cwd=root, env=env, check=True)
    subprocess.run([str(exe)], cwd=root, env=env, check=True)


if __name__ == "__main__":
    main()

"""Build CDMaterialBridge.asi with MSVC and the Windows SDK, without game files.

Run on Windows with Python 3.9+: py scripts/build_windows.py
Requires Visual Studio 2022 (or Build Tools), Desktop development with C++,
and a Windows SDK. Use --vcvars64 to select a nonstandard installation.
Outputs: build/ (temporary files and log), output/CDMaterialBridge.asi.
This script does not install, package, or load the plugin.
"""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BATCH = r'''@echo off
setlocal
if defined CMB_VCVARS (
    call "%CMB_VCVARS%" >nul
    if errorlevel 1 exit /b 1
)
cd /d "%CMB_BUILD%"
if errorlevel 1 exit /b 1
rc /nologo /fo version.res "%CMB_ROOT%\src\version.rc"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /MT /DUNICODE /D_UNICODE /LD "%CMB_ROOT%\src\bridge.cpp" /Fe:CDMaterialBridge.asi /link version.res bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
exit /b %errorlevel%
'''


def find_vcvars(override):
    if override:
        result = override.expanduser().resolve()
        if not result.is_file():
            raise RuntimeError(f"vcvars64.bat not found: {result}")
        return result
    if (os.environ.get("VSCMD_ARG_TGT_ARCH", "").lower() == "x64"
            and shutil.which("cl.exe") and shutil.which("rc.exe")):
        return None
    installer = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    vswhere = installer / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        raise RuntimeError("Install Visual Studio C++ Build Tools, use an x64 Native Tools "
                           "Command Prompt, or pass --vcvars64 PATH.")
    result = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
         "installationPath", "-utf8"],
        check=True, capture_output=True, encoding="utf-8")
    install = result.stdout.strip()
    vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    if not install or not vcvars.is_file():
        raise RuntimeError("No MSVC x64 installation found; install the C++ workload "
                           "or pass --vcvars64 PATH.")
    return vcvars


def check_x64_dll(data):
    if len(data) < 64 or data[:2] != b"MZ":
        raise RuntimeError("Compiler output is not a PE binary")
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if pe + 26 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise RuntimeError("Invalid PE header")
    machine = struct.unpack_from("<H", data, pe + 4)[0]
    flags = struct.unpack_from("<H", data, pe + 22)[0]
    magic = struct.unpack_from("<H", data, pe + 24)[0]
    if machine != 0x8664 or magic != 0x20b or not flags & 0x2000:
        raise RuntimeError("Expected an AMD64 PE32+ DLL; use the x64 toolchain")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vcvars64", type=Path, help="Path to vcvars64.bat")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Run this script with Windows Python, not WSL/Linux Python")
    vcvars = find_vcvars(args.vcvars64)
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    batch = build / "build.cmd"
    batch.write_text(BATCH, encoding="ascii")
    env = os.environ.copy()
    env.update(CMB_ROOT=str(ROOT), CMB_BUILD=str(build))
    env.pop("CMB_VCVARS", None)
    if vcvars:
        env["CMB_VCVARS"] = str(vcvars)
    for variable in ("CL", "_CL_", "LINK", "_LINK_"):
        env.pop(variable, None)
    shell = os.environ.get("COMSPEC", "cmd.exe")
    result = subprocess.run(
        f'"{shell}" /d /s /c ""{batch}""',
        cwd=build, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (build / "build.log").write_bytes(result.stdout)
    print(result.stdout.decode("mbcs", errors="replace"), end="", flush=True)
    if result.returncode:
        return result.returncode
    binary = build / "CDMaterialBridge.asi"
    payload = binary.read_bytes()
    check_x64_dll(payload)
    output = ROOT / "output/CDMaterialBridge.asi"
    output.parent.mkdir(exist_ok=True)
    temporary = output.with_suffix(".asi.tmp")
    shutil.copyfile(binary, temporary)
    temporary.replace(output)
    print(f"Built: {output}")
    print(f"SHA256: {hashlib.sha256(payload).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        raise SystemExit(1)

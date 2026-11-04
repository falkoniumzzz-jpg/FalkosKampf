"""Build FalkosKampf.dll on Windows (GitHub Actions) and package the MO2 zip.

Needs Python 3.12+, Git, CMake and VS 2022/2026 C++ tools.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "_work"
OUT = ROOT / "out"
VCPKG_COMMIT = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))["builtin-baseline"]


def run(name, cmd, **kw):
    OUT.mkdir(parents=True, exist_ok=True)
    log = OUT / "logs" / f"{name}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    print(f"== {name}: {' '.join(map(str, cmd))}", flush=True)
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        proc = subprocess.run(list(map(str, cmd)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace", **kw)
        f.write(proc.stdout)
    if proc.returncode:
        print(proc.stdout[-20000:], flush=True)
        raise SystemExit(f"{name} failed ({proc.returncode}), see {log}")
    return proc.stdout


def vcpkg():
    path = WORK / "vcpkg"
    if not (path / ".git").exists():
        run("vcpkg-clone", ["git", "clone", "https://github.com/microsoft/vcpkg.git", path])
    run("vcpkg-checkout", ["git", "-C", path, "checkout", "--force", VCPKG_COMMIT])
    run("vcpkg-bootstrap", ["cmd", "/c", path / "bootstrap-vcpkg.bat", "-disableMetrics"])
    return path


def generator():
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    found = json.loads(subprocess.check_output([str(vswhere), "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json"], text=True))
    found = [v for v in found if int(v["installationVersion"].split(".")[0]) in (17, 18)]
    if not found:
        raise SystemExit("VS 2022/2026 C++ build tools missing")
    newest = sorted(found, key=lambda v: list(map(int, v["installationVersion"].split("."))))[-1]
    return "Visual Studio 18 2026" if newest["installationVersion"].startswith("18.") else "Visual Studio 17 2022"


def main():
    vc = vcpkg()
    build = WORK / "build"
    env = os.environ.copy()
    env["VCPKG_ROOT"] = str(vc)
    run("configure", ["cmake", "-S", ROOT, "-B", build, "-G", generator(), "-A", "x64",
        f"-DCMAKE_TOOLCHAIN_FILE={vc.as_posix()}/scripts/buildsystems/vcpkg.cmake",
        "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md"], env=env)
    run("build", ["cmake", "--build", build, "--config", "Release", "--target", "FalkosKampf",
        "--parallel", "4"], env=env)
    dll = build / "Release" / "FalkosKampf.dll"
    if not dll.exists():
        raise SystemExit(f"{dll} missing")
    sys.path.insert(0, str(ROOT / "scripts"))
    from package import package
    package(dll, build / "Release" / "FalkosKampf.pdb", None, OUT / "FalkosKampf-MO2.zip")


if __name__ == "__main__":
    main()

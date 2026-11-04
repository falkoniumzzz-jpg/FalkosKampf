"""Package FalkosKampf for MO2.

Everything under data/ is copied as is, including the finisher animation
fk_finisher.hkx (a copy of vanilla 2hm_attackleft.hkx). --hkx overrides it.

  python scripts/package.py --dll FalkosKampf.dll --hkx 2hm_attackleft.hkx --out FalkosKampf-MO2.zip
"""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ANIM_DIR = "meshes/actors/character/animations/FalkosKampf"


def package(dll, pdb, hkx, out):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(dll, "SKSE/Plugins/FalkosKampf.dll")
        if pdb and Path(pdb).exists():
            z.write(pdb, "SKSE/Plugins/FalkosKampf.pdb")
        for f in sorted((ROOT / "data").rglob("*")):
            if f.is_file() and not (hkx and f.name == "fk_finisher.hkx"):
                z.write(f, f.relative_to(ROOT / "data").as_posix())
        if hkx:
            z.write(hkx, f"{ANIM_DIR}/fk_finisher.hkx")
    print(f"wrote {out}")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--dll", required=True)
    p.add_argument("--pdb")
    p.add_argument("--hkx")
    p.add_argument("--out", required=True)
    a = p.parse_args()
    package(a.dll, a.pdb, a.hkx, a.out)

import os
import subprocess
from pathlib import Path

# PlatformIO extra script: increments build number, computes FW_VERSION strings,
# injects -D macros, and sets PROGNAME with the short version.

def _read_text(path: Path, default: str = "") -> str:
    try:
        return path.read_text(encoding="utf-8").strip()
    except Exception:
        return default


def _write_text(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _get_git_branch(project_dir: Path) -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=str(project_dir),
            stderr=subprocess.STDOUT,
        ).decode("utf-8").strip()
        # detached HEAD handling
        if out and out != "HEAD":
            return out.replace("/", "-")
    except Exception:
        pass
    return "local"


def _sanitize_version_base(text: str) -> str:
    # Expect format like "01.02"; fallback to "01.00"
    if not text:
        return "01.00"
    parts = text.split(".")
    if len(parts) < 2:
        return "01.00"
    try:
        major = int(parts[0])
        minor = int(parts[1])
        return f"{major:02d}.{minor:02d}"
    except Exception:
        return "01.00"


def before_build(target, source, env):
    project_dir = Path(env["PROJECT_DIR"])  # type: ignore

    version_file = project_dir / "VERSION"
    buildnum_file = project_dir / ".buildnumber"

    base_version = _sanitize_version_base(_read_text(version_file, "01.00"))

    try:
        buildnum = int(_read_text(buildnum_file, "0"))
    except Exception:
        buildnum = 0
    buildnum += 1
    _write_text(buildnum_file, str(buildnum))

    branch = _get_git_branch(project_dir)

    fw_version = f"{base_version}.{branch}:{buildnum}"
    fw_version_short = f"{base_version}.{buildnum}"

    # Inject into compile flags so code can use FW_VERSION and FW_VERSION_SHORT
    env.Append(BUILD_FLAGS=[
        f'-DFW_VERSION="{fw_version}"',
        f'-DFW_VERSION_SHORT="{fw_version_short}"',
    ])

    # Name artifact with env and short version
    # Example: esp01_1m-01.02.125
    env.Replace(PROGNAME=f"${{PIOENV}}-{fw_version_short}")

# Hook (solo cuando se ejecuta dentro de PlatformIO/SCons)
try:
    from SCons.Script import Import as _SConsImport  # type: ignore
    _SConsImport("env")
    env.AddPreAction("buildprog", before_build)  # type: ignore # noqa
except Exception:
    # Si se ejecuta este archivo con `python scripts/versioning.py`, no hay entorno SCons.
    if __name__ == "__main__":
        print("versioning.py: este script está diseñado para ejecutarse desde PlatformIO (extra_scripts).")

import os
import subprocess
from pathlib import Path

# PlatformIO post-build script: optionally publish firmware via PowerShell script.
# Enable by setting environment variable FTP_AUTO_PUBLISH=1 (recommended for release only).


def _sanitize_version_base(text: str) -> str:
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


def _load_env_files(project_dir: Path):
    """Load .env.local then .env into os.environ if keys are not already set."""
    def _parse_env_file(p: Path):
        try:
            for line in p.read_text(encoding="utf-8").splitlines():
                s = line.strip()
                if not s or s.startswith("#") or "=" not in s:
                    continue
                k, v = s.split("=", 1)
                k = k.strip()
                v = v.strip()
                if k and (k not in os.environ or os.environ[k] == ""):
                    os.environ[k] = v
        except Exception:
            pass

    env_local = project_dir / ".env.local"
    env_file = project_dir / ".env"
    if env_local.exists():
        _parse_env_file(env_local)
    if env_file.exists():
        _parse_env_file(env_file)


def after_build(target, source, env):
    try:
        pioenv = env["PIOENV"]
        project_dir = Path(env["PROJECT_DIR"])  # type: ignore
    except Exception:
        return

    # Publicar solo si está habilitado explícitamente o si es un entorno *release*
    auto_env = str(os.environ.get("FTP_AUTO_PUBLISH", "0")) == "1"
    is_release_env = str(pioenv).endswith("_release")
    if not (auto_env or is_release_env):
        return

    # Load env vars from .env files for convenience
    _load_env_files(project_dir)

    # Determine publish mode
    mode = str(os.environ.get("PUBLISH_MODE", "")).lower()

    # Compute short version (e.g., 01.02.125) to pass to publish script
    version_file = project_dir / "VERSION"
    buildnum_file = project_dir / ".buildnumber"
    try:
        base_version = _sanitize_version_base(version_file.read_text(encoding="utf-8").strip())
    except Exception:
        base_version = "01.00"
    try:
        buildnum = int(buildnum_file.read_text(encoding="utf-8").strip())
    except Exception:
        buildnum = 0
    fw_version_short = f"{base_version}.{buildnum}"

    # HTTP mode when PUBLISH_MODE=http or HTTP_PUBLISH_URL is set
    http_url = os.environ.get("HTTP_PUBLISH_URL", "").strip()
    if mode == "http" or http_url:
        ps1 = project_dir / "tools" / "publish-http.ps1"
        if not ps1.exists():
            print("[publish] HTTP publish script not found:", ps1)
            return
        if not http_url:
            print("[publish] HTTP_PUBLISH_URL not set; skipping HTTP publish.")
            return
        cmd = [
            "pwsh","-NoProfile","-ExecutionPolicy","Bypass","-File",str(ps1),
            "-EnvName", pioenv,
            "-Url", http_url,
            "-Version", fw_version_short,
        ]
        api_key = os.environ.get("HTTP_PUBLISH_TOKEN", "").strip()
        if api_key:
            cmd += ["-ApiKey", api_key]
        print("[publish] Running HTTP:", " ".join(cmd))
        try:
            subprocess.check_call(cmd)
            print("[publish] Firmware published successfully (HTTP).")
        except subprocess.CalledProcessError as e:
            print("[publish] HTTP publish failed:", e)
            return
    else:
        # FTP/FTPS mode (default)
        ps1 = project_dir / "tools" / "publish-firmware.ps1"
        if not ps1.exists():
            print("[publish] PowerShell script not found:", ps1)
            return
        cmd = [
            "pwsh","-NoProfile","-ExecutionPolicy","Bypass","-File",str(ps1),
            "-EnvName", pioenv,
            "-Version", fw_version_short,
            "-StableOnly"
        ]
        print("[publish] Running FTP:", " ".join(cmd))
        try:
            subprocess.check_call(cmd)
            print("[publish] Firmware published successfully (FTP/FTPS).")
        except subprocess.CalledProcessError as e:
            print("[publish] Publish failed:", e)
            return

    # Opcional: crear tag git con la versión detectada si se solicita
    try:
        if os.environ.get("GIT_TAG_ON_RELEASE", "0") == "1":
            # Buscar el binario generado para extraer la versión corta
            build_dir = project_dir / ".pio" / "build" / pioenv
            bins = sorted(build_dir.glob(f"{pioenv}-*.bin"), key=lambda p: p.stat().st_mtime, reverse=True)
            if bins:
                name = bins[0].name  # <env>-<version>.bin
                ver = name.split("-", 1)[-1].rsplit(".", 1)[0]
                tag = f"v{ver}"
                print(f"[publish] Tagging git: {tag}")
                subprocess.check_call(["git", "tag", "-a", tag, "-m", f"Release {tag}"])
                subprocess.check_call(["git", "push", "--tags"])
    except Exception as e:
        print("[publish] Tagging skipped or failed:", e)


# Hook
try:
    from Scons.Script import Import as _SConsImport  # type: ignore
except Exception:
    # Correct module name
    from SCons.Script import Import as _SConsImport  # type: ignore

_SConsImport("env")
env.AddPostAction("buildprog", after_build)  # type: ignore # noqa

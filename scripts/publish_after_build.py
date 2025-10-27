import os
import subprocess
from pathlib import Path

# PlatformIO post-build script: optionally publish firmware via PowerShell script.
# Enable by setting environment variable FTP_AUTO_PUBLISH=1 (recommended for release only).


def after_build(target, source, env):
    try:
        pioenv = env["PIOENV"]
        project_dir = Path(env["PROJECT_DIR"])  # type: ignore
    except Exception:
        return

    if os.environ.get("FTP_AUTO_PUBLISH", "0") != "1":
        # Not enabled
        return

    ps1 = project_dir / "tools" / "publish-firmware.ps1"
    if not ps1.exists():
        print("[publish] PowerShell script not found:", ps1)
        return

    # Run PowerShell publish script; it reads .env/.env.local itself
    cmd = [
        "pwsh",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ps1),
        "-EnvName",
        pioenv,
    ]
    print("[publish] Running:", " ".join(cmd))
    try:
        subprocess.check_call(cmd)
        print("[publish] Firmware published successfully.")
    except subprocess.CalledProcessError as e:
        print("[publish] Publish failed:", e)


# Hook
try:
    from Scons.Script import Import as _SConsImport  # type: ignore
except Exception:
    # Correct module name
    from SCons.Script import Import as _SConsImport  # type: ignore

_SConsImport("env")
env.AddPostAction("buildprog", after_build)  # type: ignore # noqa

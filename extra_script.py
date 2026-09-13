# Stamps the current git tag/commit into the firmware as FIRMWARE_VERSION,
# shown on the Diary screen (see src/ui_diary.cpp). Falls back to "unknown"
# if git isn't available or this isn't a git checkout at all -- never
# breaks the build over a missing version string.
Import("env")
import subprocess


def get_version():
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
        return v if v else "unknown"
    except Exception:
        return "unknown"


env.Append(BUILD_FLAGS=['-DFIRMWARE_VERSION=\\"%s\\"' % get_version()])

# The environment name, as SQW_ENV. A Bluetooth update is signed for exactly one
# build and the board checks the signature against its own name, so an image
# for a different board -- a different display driver, say -- is refused
# rather than installed as a white screen. See include/ota_ble.h.
env.Append(BUILD_FLAGS=['-DSQW_ENV=\\"%s\\"' % env["PIOENV"]])

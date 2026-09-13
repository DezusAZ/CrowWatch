"""Sign a firmware image for Bluetooth updates.

The board hashes exactly this message while the image streams in, and checks
the signature against the public key compiled into include/ota_pubkey.h:

    SHA-256( b"SQWOTA1\\n" + env + b"\\n" + image )

The build name is part of what is signed, so an image signed for one board is
refused by every other one -- a white screen cannot be navigated to switch back.

Usage:
    python tools/sign_firmware.py --key key.pem --env cyd-fast \\
        --bin cyd-fast-firmware.bin --out cyd-fast-firmware.sig

Needs only the openssl command line tool. The output is a DER ECDSA signature,
which is what the board's mbedtls_pk_verify() reads.
"""
import argparse
import os
import subprocess
import sys
import tempfile

PREFIX = b"SQWOTA1\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", required=True, help="P-256 private key, PEM")
    ap.add_argument("--env", required=True, help="PlatformIO environment the image was built for")
    ap.add_argument("--bin", required=True, help="the app image (firmware.bin)")
    ap.add_argument("--out", required=True, help="where to write the DER signature")
    ap.add_argument("--pub", help="optional public key to verify the result against")
    a = ap.parse_args()

    with open(a.bin, "rb") as f:
        image = f.read()
    if not image or image[0] != 0xE9:
        sys.exit("%s does not start with the ESP32 image magic byte" % a.bin)

    fd, msg_path = tempfile.mkstemp(suffix=".msg")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(PREFIX + a.env.encode("ascii") + b"\n" + image)
        subprocess.run(["openssl", "dgst", "-sha256", "-sign", a.key, "-out", a.out, msg_path],
                       check=True)
        if a.pub:
            subprocess.run(["openssl", "dgst", "-sha256", "-verify", a.pub,
                            "-signature", a.out, msg_path], check=True)
    finally:
        os.remove(msg_path)
    print("signed %s for %s (%d bytes) -> %s" % (a.bin, a.env, len(image), a.out))


if __name__ == "__main__":
    main()

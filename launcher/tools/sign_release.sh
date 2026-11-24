#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Run by the owner in WSL for every official release (asks the passphrase):
#   bash launcher/tools/sign_release.sh APP_ROOT
# Signs APP_ROOT/jk4speed-files/release.txt with ~/.jk4speed-keys/release.key
# into release.sig (raw 64-byte Ed25519) and checks it with the public key.
set -euo pipefail
root="$1"
key="$HOME/.jk4speed-keys/release.key"
txt="$root/jk4speed-files/release.txt"
sig="$root/jk4speed-files/release.sig"
[ -f "$key" ] || { echo "sign_release: no key at $key (run release_keygen.sh once)" >&2; exit 1; }
[ -f "$txt" ] || { echo "sign_release: no $txt (run make_release.py first)" >&2; exit 1; }
openssl pkeyutl -sign -inkey "$key" -rawin -in "$txt" -out "$sig"
[ "$(stat -c %s "$sig")" = 64 ] || { echo "sign_release: signature is not 64 bytes" >&2; exit 1; }
echo "signed: $sig"

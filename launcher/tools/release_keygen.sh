#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# ONE TIME, run by the owner in WSL: makes the JK4SPEED release key.
#   bash launcher/tools/release_keygen.sh
# The private key goes to ~/.jk4speed-keys/release.key, encrypted with a
# passphrase you choose (asked twice, then once more to read the public key).
# It never goes in the repo, in OneDrive or to anyone. Keep a backup of that
# file and the passphrase offline (USB stick): without them no new release can
# show "Official Build". Writes the public key to launcher/src/jk_release_key.h.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
key="$HOME/.jk4speed-keys/release.key"
out="$here/../src/jk_release_key.h"
[ -e "$key" ] && { echo "release_keygen: $key exists already; not overwriting it" >&2; exit 1; }
mkdir -p "$(dirname "$key")"; chmod 700 "$(dirname "$key")"
umask 077
openssl genpkey -algorithm ed25519 -aes256 -out "$key"
pub=$(openssl pkey -in "$key" -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -s ' \n' ' ' | sed 's/^ //; s/ $//')
[ "$(echo "$pub" | wc -w)" = 32 ] || { echo "release_keygen: could not read the public key" >&2; exit 1; }
{
  echo "/* SPDX-License-Identifier: LGPL-2.1-or-later */"
  echo "/* JK4SPEED release public key (Ed25519), made by tools/release_keygen.sh. */"
  echo "#ifndef JK_RELEASE_KEY_H"
  echo "#define JK_RELEASE_KEY_H"
  printf 'static const unsigned char JK_RELEASE_PK[32] = {'
  for b in $pub; do printf ' 0x%s,' "$b"; done
  echo ' };'
  echo "#endif"
} > "$out"
echo "release key made: $key (keep it secret, back it up offline)"
echo "public key written: $out"

# TweetNaCl

Source: https://tweetnacl.cr.yp.to/20140427/ (tweetnacl.c sha256 02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4,
tweetnacl.h 43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287).
Authors: Daniel J. Bernstein, Wesley Janssen, Tanja Lange, Peter Schwabe, Matthew Dempsky, Sjaak Smetsers. **Public domain.**

JK4SPEED uses only `crypto_sign_open` (Ed25519 verify) for the "Official Build" badge (`src/jk_ed25519.c`, `src/jk_release.c`).
Local change: two left shifts of possibly negative signed values (undefined behaviour in C) are written as
multiplications with the same result: `o[i]-=c<<16` -> `o[i]-=c*65536` (car25519) and `x[j] -= carry << 8` ->
`x[j] -= carry * 256` (modL).

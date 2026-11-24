/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The load-segment digest of a console module (docs/launcher/
 * 2026-11-23-console-prechecks.md): SHA-256 over the file bytes of every
 * program header of type PT_LOAD (1) or 0x61000010 with p_filesz > 0, in
 * program-header order. It is the same for the signed file (SELF, as the
 * title reads it), the ELF before signing (.oelf) and the ELF that GoldHEN's
 * FTP serves for a *.prx (it unwraps the SELF), so one value recognises our
 * installed server in every form.
 *
 * SELF: segment table at 0x20 (count: u16 at 24), 32-byte entries
 * {flags, offset, compressed size, decompressed size}; the embedded ELF header
 * follows the table, its program headers follow it. An entry with flag 0x800
 * holds program header (flags >> 20) & 0xFFF. Encrypted (0x2) or compressed
 * (0x8) segments cannot be hashed here and give an error.
 */
#ifndef JK_ELFDIGEST_H
#define JK_ELFDIGEST_H

/* 0 ok (out: 64 hex digits); -1 not an ELF/SELF, damaged, or unhashable. */
int jk_load_digest(const char *path, char out[65]);

#endif

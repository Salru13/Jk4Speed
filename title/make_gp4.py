# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Write a GP4 project for PkgTool.Core pkg_build from a list of files.

    python3 -I make_gp4.py OUT.gp4 CONTENT_ID FILES.tsv

FILES.tsv: one "<package path>\t<absolute source path>" per line. The format is
the one of the bench packages (mwbench002.gp4, package292.gp4): one chunk, one
scenario, digital50, passcode zeros. Deterministic: files sorted by path, a
fixed volume time (GP4_TIME, default 2026-11-23 00:00:00).
"""
import os
import sys
from xml.sax.saxutils import quoteattr


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    out, content_id, listing = argv
    files = []
    with open(listing, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue
            targ, orig = line.split('\t')
            if targ.startswith('/') or '..' in targ.split('/') or not os.path.isfile(orig):
                print('make_gp4: bad entry %r -> %r' % (targ, orig), file=sys.stderr)
                return 1
            files.append((targ, orig))
    files.sort()
    if len({t for t, _ in files}) != len(files):
        print('make_gp4: duplicate package path', file=sys.stderr)
        return 1
    tree = {}
    for targ, _ in files:
        node = tree
        for part in targ.split('/')[:-1]:
            node = node.setdefault(part, {})

    def dirs(node):
        return ''.join('<dir targ_name=%s>%s</dir>' % (quoteattr(k), dirs(v)) if v else '<dir targ_name=%s />' % quoteattr(k)
                       for k, v in sorted(node.items()))

    when = os.environ.get('GP4_TIME', '2026-11-23 00:00:00')
    lines = [
        "<?xml version='1.0' encoding='utf-8'?>",
        '<psproject fmt="gp4" version="1000">',
        '\t<volume>',
        '\t\t<volume_type>pkg_ps4_app</volume_type>',
        '\t\t<volume_id>PS4VOLUME</volume_id>',
        '\t\t<volume_ts>%s</volume_ts>' % when,
        '\t\t<package content_id=%s passcode="00000000000000000000000000000000" storage_type="digital50" app_type="full" />'
        % quoteattr(content_id),
        '\t\t<chunk_info chunk_count="1" scenario_count="1">',
        '\t\t\t<chunks>',
        '\t\t\t\t<chunk id="0" layer_no="0" label="Chunk #0" />',
        '\t\t\t</chunks>',
        '\t\t\t<scenarios default_id="0">',
        '\t\t\t\t<scenario id="0" type="sp" initial_chunk_count="1" label="Scenario #0">0</scenario>',
        '\t\t\t</scenarios>',
        '\t\t</chunk_info>',
        '\t</volume>',
        '\t<files img_no="0">',
    ]
    lines += ['\t\t<file targ_path=%s orig_path=%s />' % (quoteattr(t), quoteattr(o)) for t, o in files]
    lines += ['\t</files>', '\t<rootdir>', '\t\t' + dirs(tree), '\t</rootdir>', '</psproject>']
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('make_gp4: %d files -> %s' % (len(files), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

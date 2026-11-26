#!/usr/bin/env python3
"""Build the AmigaGuide files of VagaFileManager.

Sources (UTF-8, easy to edit on any system):
    guides/<language>.guide   user guide, one per language
    guides/Developer.guide    plugin and ARexx reference (English)

Output (the 8-bit codeset AmigaOS uses for each language, LF line ends):
    Docs/<language>/VagaFileManager.guide
    Docs/Developer.guide

Source format: AmigaGuide with @wordwrap, plus three conveniences
    - a paragraph may be wrapped on several lines: they are joined;
    - a line starting with "- " starts a new list item;
    - a line starting with "| " is kept as it is (tables, addresses);
    - lines starting with "@" are AmigaGuide commands, headings or
      links and are kept on their own line.

Checks: every link points to an existing node, every language has the
same nodes as English, and every character exists in the codeset.

Run from the project directory:  python3 Tools/build_guides.py
"""
import os
import re
import sys

CODESETS = {
    'english': 'iso-8859-1',
    'italian': 'iso-8859-1',
    'french': 'iso-8859-1',
    'german': 'iso-8859-1',
    'spanish': 'iso-8859-1',
    'polish': 'iso-8859-2',
    'greek': 'iso-8859-7',
}

NODE_RE = re.compile(r'^@node\s+(\S+)', re.I)
LINK_RE = re.compile(r'@\{"[^"]*"\s+link\s+([^\s}]+)', re.I)


def convert(text):
    """Join wrapped paragraphs; return the list of output lines."""
    out = []
    para = []

    def flush():
        if para:
            out.append(' '.join(para))
            del para[:]

    for raw in text.splitlines():
        line = raw.rstrip()
        if not line:
            flush()
            out.append('')
        elif line.startswith('| ') or line == '|':
            flush()
            out.append(line[2:])
        elif line.startswith('@'):
            flush()
            out.append(line)
        elif line.startswith('- '):
            flush()
            para.append(line)
        else:
            para.append(line.strip() if para else line)
    flush()
    return out


def check(name, lines):
    nodes = []
    errors = []
    for l in lines:
        m = NODE_RE.match(l)
        if m:
            nodes.append(m.group(1).lower())
    for l in lines:
        for target in LINK_RE.findall(l):
            if '/' not in target and target.lower() not in nodes:
                errors.append('%s: link to missing node "%s"' % (name, target))
    return nodes, errors


def write(src, dst, codeset):
    text = open(src, encoding='utf-8').read()
    lines = convert(text)
    nodes, errors = check(src, lines)
    data = '\n'.join(lines).rstrip('\n') + '\n'
    try:
        raw = data.encode(codeset)
    except UnicodeEncodeError as e:
        bad = data[e.start:e.end]
        lineno = data.count('\n', 0, e.start) + 1
        errors.append('%s: "%s" (U+%04X) not in %s, output line %d'
                      % (src, bad, ord(bad[0]), codeset, lineno))
        raw = None
    if raw is not None:
        d = os.path.dirname(dst)
        if d and not os.path.isdir(d):
            os.makedirs(d)
        open(dst, 'wb').write(raw)
    return nodes, errors


def copy_icons():
    """Give every guide and every language drawer an icon.

    guides/guide.info (a copy of ENV:Sys/def_guide.info) goes next to each
    .guide; guides/drawer.info (ENV:Sys/def_drawer.info) becomes the icon
    of each Docs/<language> drawer.  Missing source icons are skipped."""
    import shutil
    guide_icon = os.path.join('guides', 'guide.info')
    drawer_icon = os.path.join('guides', 'drawer.info')
    if os.path.exists(guide_icon):
        for lang in CODESETS:
            shutil.copyfile(guide_icon,
                            os.path.join('Docs', lang, 'VagaFileManager.guide.info'))
        if os.path.exists('Docs/Developer.guide'):
            shutil.copyfile(guide_icon, 'Docs/Developer.guide.info')
        print('icons    guide icons copied')
    if os.path.exists(drawer_icon):
        for lang in CODESETS:
            shutil.copyfile(drawer_icon, os.path.join('Docs', lang + '.info'))
        # icons of the Docs and Plugins drawers, copied by make dist
        shutil.copyfile(drawer_icon, 'Docs.info')
        shutil.copyfile(drawer_icon, 'Plugins.info')
        print('icons    drawer icons copied')


def main():
    errors = []
    english_nodes = None
    for lang in ['english'] + sorted(l for l in CODESETS if l != 'english'):
        src = os.path.join('guides', lang + '.guide')
        if not os.path.exists(src):
            errors.append('missing %s' % src)
            continue
        dst = os.path.join('Docs', lang, 'VagaFileManager.guide')
        nodes, errs = write(src, dst, CODESETS[lang])
        errors += errs
        if lang == 'english':
            english_nodes = nodes
        elif english_nodes is not None and nodes != english_nodes:
            errors.append('%s: nodes differ from English: %s'
                          % (src, ' '.join(sorted(set(nodes) ^ set(english_nodes)))))
        print('%-8s %s' % (lang, dst))
    if os.path.exists('guides/Developer.guide'):
        nodes, errs = write('guides/Developer.guide', 'Docs/Developer.guide', 'ascii')
        errors += errs
        print('%-8s %s' % ('dev', 'Docs/Developer.guide'))
    copy_icons()
    for e in errors:
        print('ERROR ' + e)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())

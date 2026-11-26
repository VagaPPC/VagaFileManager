#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: Find
# VFM-Icon: find   (AISS image in tbimages:, shown in the Scripts menu)
# ---------------------------------------------------------------------------
#
# Recursive search by file name, done entirely by the plugin:
#
#   1. asks for the name or pattern (VFM Reaction DIALOG), with an
#      "Include subfolders" checkbox, ticked by default;
#   2. searches from the drawer open in the pane the plugin was started
#      from (PATH, or argv[2] as a fallback), in the subfolders too if
#      the box is ticked;
#   3. one match: VFM goes straight to it;
#      more matches: they are listed in the same result window as Grep.py
#      (ProAction listbrowser, or the ASL fallback);
#   4. the chosen entry is shown in the calling VFM window with REVEAL:
#      the pane moves to its drawer and the entry is selected.
#
# The search field accepts AmigaDOS wildcards (#?, ?, #x, [a-z], [~x],
# (a|b), ~pattern, ' to escape) and the common *.  Without wildcards the
# name is matched as a substring.  Case is ignored.  Drawers match too.
# .info files are skipped, unless the pattern itself contains ".info"
# (for example #?.info): then they are searched like any other file.
#
# The channel, progress window and result window come from Grep.py, which
# must be in the same drawer.  The plugin receives:
#
#   sys.argv[1] = ARexx port (kept for compatibility)
#   sys.argv[2] = active pane path at launch
#   sys.argv[3] = VagaFileManager executable
#   sys.argv[4] = public drop port of this instance
#   sys.argv[5] = plugin session directory (T:VFM_<task>/s<n>)
#
# Diagnostics are appended to T:Find.log.
# ---------------------------------------------------------------------------

import os
import re
import sys
import time

import Grep as G

G.LOG = 'T:Find.log'
G.TITLE = 'VFM Find'
G.QUERY_LABEL = 'Pattern'
G.NOUN = 'entries'

log = G.log
message = G.message

WILDCARDS = '#?*[]()|~%\''


# ===== AmigaDOS pattern -> regular expression =====

def _atom(pat, i):
    """Translate one atom starting at pat[i]; return (regex, next index)."""
    c = pat[i]
    if c == '?':
        return ('.', i + 1)
    if c == '*':
        return ('.*', i + 1)
    if c == '%':
        return ('', i + 1)
    if c == "'" and i + 1 < len(pat):
        return (re.escape(pat[i + 1]), i + 2)
    if c == '[':
        j = pat.find(']', i + 1)
        if j < 0:
            return (re.escape(c), i + 1)
        body = pat[i + 1:j]
        if body.startswith('~'):
            body = '^' + body[1:]
        body = body.replace('\\', '\\\\')
        return ('[' + body + ']', j + 1)
    if c == '(':
        inner, j = _sequence(pat, i + 1, ')')
        return ('(?:' + inner + ')', j + 1)
    if c == '#' and i + 1 < len(pat):
        inner, j = _atom(pat, i + 1)
        return ('(?:' + inner + ')*', j)
    return (re.escape(c), i + 1)


def _sequence(pat, i, stop):
    """Translate atoms and '|' alternatives until 'stop' or the end."""
    parts = []
    current = ''
    while i < len(pat) and pat[i] != stop:
        if pat[i] == '|':
            parts.append(current)
            current = ''
            i += 1
            continue
        regex, i = _atom(pat, i)
        current += regex
    parts.append(current)
    return ('|'.join(parts), i)


def make_matcher(pattern):
    """Return a function name -> bool, or None if the pattern is invalid."""
    text = pattern.strip()
    if not text:
        return None

    if not [c for c in text if c in WILDCARDS]:
        needle = text.lower()
        return lambda name: needle in name.lower()

    negate = False
    if text.startswith('~') and not text.startswith('~['):
        negate = True
        text = text[1:]

    try:
        regex, end = _sequence(text, 0, None)
        compiled = re.compile('^(?:' + regex + ')$', re.IGNORECASE)
    except re.error:
        return None

    if negate:
        return lambda name: compiled.match(name) is None
    return lambda name: compiled.match(name) is not None


# ===== Search =====

def find_names(root, matches_name, progress=None, with_info=False, recursive=True):
    """Recursive search by name.  Returns (paths, truncated, canceled).

    .info files are skipped unless with_info is true.
    """
    found = []
    seen = {}
    stack = [(root, 0)]
    scanned = 0
    last_progress = 0.0

    while stack:
        directory, depth = stack.pop()
        if depth > G.MAX_DEPTH:
            continue

        key = directory.lower()
        if key in seen:
            continue
        seen[key] = 1

        try:
            names = os.listdir(directory)
        except (IOError, OSError):
            continue
        names.sort()

        for name in names:
            scanned += 1
            if progress is not None:
                now = time.time()
                if (now - last_progress) >= 0.5:
                    last_progress = now
                    if not progress(scanned, 0):
                        return (found, False, True)

            if not with_info and name.lower().endswith('.info'):
                continue

            path = os.path.join(directory, name)
            try:
                is_dir = os.path.isdir(path)
            except (IOError, OSError):
                continue

            if matches_name(name):
                found.append(path)
                if len(found) >= G.MAX_RESULTS:
                    return (found, True, False)

            if recursive and is_dir and not G.is_link(path):
                stack.append((path, depth + 1))

    return (found, False, False)


# ===== VFM interaction =====

def ask_pattern():
    """Pattern and 'Include subfolders' choice (VFM DIALOG with checkbox)."""
    return G.ask_search('VFM Find', 'Name or pattern (#?, *, ?):')


def search_root():
    """The drawer open in the plugin's pane, or the launch path.

    PATH is always the drawer the user is looking at, even when a drawer
    is selected in it.
    """
    result = G.send_channel('PATH', 600)
    if result is not None and result[0] == 0 and result[1].strip():
        return result[1].strip()
    return G.START_PATH


# ===== Main =====

def main():
    log('start path=%s vfm=%s channel=%s drop=%s' %
        (G.START_PATH, G.VFM, G.PLUGIN_DIR, G.DROP_PORT))

    if not G.PLUGIN_DIR or not G.DROP_PORT:
        message('The plugin command channel is not available.')
        return

    root = search_root()
    if not root or not os.path.isdir(root):
        message('The active pane path is not a directory:\n' + str(root))
        return

    pattern, recursive = ask_pattern()
    if not pattern:
        log('no pattern')
        return

    matches_name = make_matcher(pattern)
    if matches_name is None:
        message('Invalid pattern:\n' + pattern, False)
        return

    log('search %r from %s (subfolders: %s)' % (pattern, root, recursive))
    if not G.progress_open(False):
        G.progress_close()
        log('search canceled before start')
        return

    # Searching for icons on purpose: include the .info files.
    with_info = '.info' in pattern.lower()
    found, truncated, canceled = find_names(root, matches_name,
                                            G.progress_update, with_info,
                                            recursive)
    G.progress_close()

    if canceled:
        log('search canceled')
        return

    found.sort()
    log('matches=%d truncated=%d' % (len(found), 1 if truncated else 0))

    if not found:
        message('Nothing matches "%s".' % pattern, False)
        return

    chosen = None
    if len(found) == 1:
        # A single match needs no list: show it in VFM right away.
        chosen = found[0]
    elif G.arexx is not None:
        status, chosen = G.choose_result_proaction(found, pattern, truncated)
        if status == 'cancel':
            log('ProAction: cancelled')
            return
        if status == 'fail':
            log('ProAction unavailable, falling back to ASL')

    if chosen is None and len(found) > 1:
        chosen = G.choose_result_asl_full(found, root, truncated)

    if not chosen:
        log('no result selected')
        return

    log('selected %r' % chosen)
    result = G.send_channel('REVEAL ' + chosen)
    if result is None or result[0] != 0:
        message('Cannot reveal the selected entry in VagaFileManager.\n'
                'See T:Find.log for details.')
        return

    log('revealed %r' % chosen)


if __name__ == '__main__':
    main()
    sys.exit(0)

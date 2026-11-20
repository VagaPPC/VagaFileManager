#!python
# VagaFileManager - recursive file finder (Python + ARexx)
#
# Uses the "arexx" module included in AmigaOS 4.1 to:
#  - get the window path (passed as argv[2], or PATH as fallback)
#  - ask for the file name with a requester (ASK)
#  - move to the result and select it (REVEAL)

import os
import sys
import fnmatch

try:
    import arexx
except ImportError:
    arexx = None

try:
    import asl
except ImportError:
    asl = None

try:
    import amiga
except ImportError:
    amiga = None

PORT = sys.argv[1] if len(sys.argv) > 1 else 'VAGAFILEMANAGER'
LAUNCH_PATH = sys.argv[2] if len(sys.argv) > 2 else ''


def vfm(command):
    """Send an ARexx command to VagaFileManager; None on failure."""
    if arexx is None:
        return None
    try:
        rc, rc2, result = arexx.dorexx(PORT, command)
    except Exception:
        return None
    if rc != 0:
        return None
    if result is None:
        return ''
    return result


def message(text):
    if asl is not None:
        try:
            asl.MessageBox('Find file', text, 'OK')
        except Exception:
            pass


def join(base, name):
    """Join an AmigaOS path (volume:...) and a name."""
    if base.endswith(':') or base.endswith('/'):
        return base + name
    return base + '/' + name


def basename(path):
    i = path.rfind('/')
    j = path.rfind(':')
    if j > i:
        i = j
    return path[i + 1:]


def listdir(path):
    """os.listdir with a fallback to the amiga module."""
    try:
        return os.listdir(path)
    except Exception:
        if amiga is not None:
            try:
                return amiga.listdir(path)
            except Exception:
                pass
    return None


def isdir(path):
    return listdir(path) is not None


def walk(path, depth, out):
    if depth > 64:
        return
    names = listdir(path)
    if names is None:
        return
    for name in names:
        full = join(path, name)
        try:
            if isdir(full):
                walk(full, depth + 1, out)
            else:
                out.append(full)
        except Exception:
            continue


def main():
    if arexx is None:
        return

    # The file manager passes the window path when it starts the plugin.
    # Fall back to the ARexx PATH command for older installations.
    root = LAUNCH_PATH or vfm('PATH')
    if not root:
        message('Open a drawer in the file manager first.')
        return

    needle = vfm('ASK File name to find')
    if needle is None:
        return
    needle = needle.strip()
    if not needle:
        return

    pattern = needle
    if '*' not in pattern and '?' not in pattern:
        pattern = '*' + pattern + '*'
    pattern = pattern.lower()
    skip_info = '.info' not in pattern

    files = []
    walk(root, 0, files)
    files.sort()

    found = ''
    for full in files:
        name = basename(full)
        if skip_info and name.lower().endswith('.info'):
            continue
        if fnmatch.fnmatch(name.lower(), pattern):
            found = full
            break

    if not found:
        message('No files found.')
        return

    if vfm('REVEAL ' + found) is None:
        message('File found, but I cannot reach it:\n' + found)


if __name__ == '__main__':
    main()
    sys.exit(0)

#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: CompressLHA
# ---------------------------------------------------------------------------
#
# Creates an LHA archive from the files selected in the active pane.
#
#   - no selection          -> a message asks to select one or more files;
#   - single pane           -> an ASL save requester asks for the archive;
#   - split view            -> asks only for the archive name and stores it
#                              in the other (not active) pane;
#   - compression           -> uses the LHA executable installed on the
#                              machine (C:LHA, or LHA from the path).
#
# The addon uses the ADDONINFO command of the plugin channel (VFM build 35):
#
#   split\n
#   active-pane\n
#   pane0-path\n
#   pane1-path\n
#   selected-count\n
#   selected-list-file
#
# The selected list file contains one full path per line.  This is needed
# because SELECTED joins names with spaces and cannot represent file names
# containing spaces reliably.
#
# The plugin receives:
#
#   sys.argv[1] = ARexx port (kept for compatibility)
#   sys.argv[2] = active pane path at launch
#   sys.argv[3] = VagaFileManager executable
#   sys.argv[4] = public drop port of this instance
#   sys.argv[5] = plugin channel directory (T:VFM_<task>)
#
# Channel protocol: write req_<id>.txt, run VFM --notify, read rep_<id>.txt.
# Diagnostics are appended to T:CompressLHA.log.
# ---------------------------------------------------------------------------

import os
import sys
import time

try:
    import amiga
except ImportError:
    amiga = None

try:
    import asl
except ImportError:
    asl = None

PORT = sys.argv[1] if len(sys.argv) > 1 else 'VAGAFILEMANAGER'
START_PATH = sys.argv[2] if len(sys.argv) > 2 else ''
VFM = sys.argv[3] if len(sys.argv) > 3 else ''
DROP_PORT = sys.argv[4] if len(sys.argv) > 4 else ''
PLUGIN_DIR = sys.argv[5] if len(sys.argv) > 5 else ''
# argv[6] = DEBUG when the DEBUG tooltype of VagaFileManager is on:
# then every step is logged, otherwise only errors.
DEBUG = len(sys.argv) > 6 and sys.argv[6].upper() == 'DEBUG'

LOG = 'T:CompressLHA.log'

try:
    _PID = os.getpid()
except Exception:
    _PID = int(time.time()) & 0xffff

REQ_OUT = 'T:VFM_CompressLHA_req_%d.txt' % _PID
LHA_LOG = 'T:VFM_CompressLHA_lha_%d.log' % _PID
FILE_LIST = 'T:VFM_CompressLHA_files_%d.lst' % _PID
RUN_SCRIPT = 'T:VFM_CompressLHA_run_%d.script' % _PID
DONE_FILE = 'T:VFM_CompressLHA_done_%d.txt' % _PID

_seq = 0


# ===== Utilities =====

def log(text, error=False):
    if not (DEBUG or error):
        return
    try:
        f = open(LOG, 'a')
        f.write(('ERROR: ' if error else '') + text + '\n')
        f.close()
    except Exception:
        pass


def message(text, error=True):
    log('message: ' + text, error)
    if asl is not None:
        try:
            asl.MessageBox('CompressLHA', text, 'OK')
        except Exception:
            pass


def run_command(cmd):
    if amiga is not None:
        try:
            return amiga.system(cmd)
        except Exception:
            log('amiga.system exception')
    try:
        return os.system(cmd)
    except Exception:
        return None


def quote_arg(text):
    """Quote one AmigaDOS command-line argument."""
    text = text.replace('*', '**').replace('"', '*"')
    return '"' + text + '"'


# ===== VFM plugin channel =====

def send_channel(command, timeout=50):
    global _seq

    if not PLUGIN_DIR or not DROP_PORT or not VFM:
        return None

    command = command.replace('\n', ' ').strip()
    if not command:
        return (None, '')

    _seq += 1
    ident = '%d_%d_%d' % (_PID, int(time.time() * 1000) % 100000000, _seq)
    req = os.path.join(PLUGIN_DIR, 'req_' + ident + '.txt')
    rep = os.path.join(PLUGIN_DIR, 'rep_' + ident + '.txt')

    try:
        f = open(req, 'w')
        f.write(command + '\n')
        f.close()
    except IOError:
        log('channel: cannot write ' + req)
        return None

    rc = run_command('"%s" --notify "%s"' % (VFM, DROP_PORT))
    log('channel notify: rc=%s' % rc)

    answer = None
    for attempt in range(timeout):
        try:
            f = open(rep, 'r')
            answer = f.read()
            f.close()
            if '|' in answer:
                break
        except IOError:
            answer = None
        time.sleep(0.1)

    try:
        os.remove(req)
    except OSError:
        pass
    try:
        os.remove(rep)
    except OSError:
        pass

    if answer is None or '|' not in answer:
        log('channel: no reply for ' + command)
        return None

    rc, sep, result = answer.partition('|')
    try:
        log('channel: rc=%s result=%r' % (rc, result))
        return (int(rc), result)
    except ValueError:
        log('channel: bad reply %r' % answer)
        return None


# ===== Progress bar =====

PROGRESS_OK = None


def progress_open(nocancel=True, total=0):
    global PROGRESS_OK

    prefix = 'NOCANCEL ' if nocancel else ''
    result = send_channel('PROGRESS %sINDET 0 %d' % (prefix, total), 6000)
    if result is None or result[0] != 0:
        PROGRESS_OK = False
        return True

    PROGRESS_OK = True
    return result[1].strip() != '1'


def progress_update(done=0, total=0, nocancel=True):
    if not PROGRESS_OK:
        return True

    prefix = 'NOCANCEL ' if nocancel else ''
    result = send_channel('PROGRESS %sINDET %d %d' % (prefix, done, total), 6000)
    if result is None or result[0] != 0:
        return True
    return result[1].strip() != '1'


def progress_close():
    global PROGRESS_OK

    if PROGRESS_OK:
        send_channel('PROGRESS CLOSE', 600)
    PROGRESS_OK = None


# ===== Selection and panes =====

def parse_addoninfo(text):
    """Parse the ADDONINFO reply (6 lines)."""
    lines = text.replace('\r', '').split('\n')
    if len(lines) < 6:
        return None
    try:
        split = int(lines[0].strip())
        active = int(lines[1].strip())
        count = int(lines[4].strip())
    except ValueError:
        return None

    return {
        'split': split,
        'active': active,
        'pane0': lines[2],
        'pane1': lines[3],
        'count': count,
        'listfile': lines[5].strip(),
    }


def read_selected(listfile, expected):
    paths = []
    try:
        f = open(listfile, 'r')
        while True:
            line = f.readline()
            if not line:
                break
            line = line.rstrip('\r\n')
            if line:
                paths.append(line)
        f.close()
    except IOError:
        log('cannot read selected list ' + listfile)
        return None

    try:
        os.remove(listfile)
    except OSError:
        pass

    if expected > 0 and len(paths) < expected:
        log('selected list truncated: %d of %d' % (len(paths), expected))

    return paths


def active_path(info):
    return info['pane1'] if info['active'] == 1 else info['pane0']


def other_path(info):
    return info['pane0'] if info['active'] == 1 else info['pane1']


# ===== Archive name =====

def ensure_lha_extension(path):
    lower = path.lower()
    if lower.endswith('.lha') or lower.endswith('.lzh'):
        return path
    return path + '.lha'


def path_leaf(path):
    cut = max(path.rfind('/'), path.rfind(':'))
    return path[cut + 1:] if cut >= 0 else path


def default_archive_name(files):
    """Default archive name from the first selected file or drawer."""
    if not files:
        return 'archive.lha'

    first = files[0]
    name = path_leaf(first)

    try:
        if not os.path.isdir(first):
            base, ext = os.path.splitext(name)
            if base:
                name = base
    except Exception:
        pass

    if not name:
        name = 'archive'
    return ensure_lha_extension(name)


def ask_archive_mode(path):
    """If the archive exists, ask Add / Overwrite / Cancel."""
    if not os.path.exists(path):
        return 'add'
    if asl is None:
        return 'add'

    try:
        result = asl.MessageBox(
            'CompressLHA',
            'The archive already exists:\n%s\n\n'
            'Add to it or overwrite it?' % path,
            'Add|Overwrite|Cancel')
    except Exception:
        return 'add'

    if result == 1:
        return 'add'
    if result == 2:
        return 'overwrite'
    return None


def validate_destination(archive, files):
    for path in files:
        if path.lower() == archive.lower():
            message('The archive cannot be one of the selected files.', False)
            return False
        if os.path.isdir(path) and path_is_inside(archive, path):
            message('The archive cannot be created inside a selected drawer.', False)
            return False
    return True


def join_amiga(directory, name):
    if not directory:
        return name
    if directory.endswith(':') or directory.endswith('/'):
        return directory + name
    return directory + '/' + name


def ask_archive_name(default):
    """Ask only for an archive file name (split view)."""
    default = default.replace('|', '_')
    result = send_channel(
        'DIALOG CompressLHA|Archive name:|%s|Create|Cancel' % default, 600)
    if result is None:
        return None

    rc, answer = result
    if rc != 0 or '|' not in answer:
        return None

    idx, sep, text = answer.partition('|')
    if idx.strip() != '1':
        return None

    name = text.strip()
    return name if name else None


def parse_request_output(text):
    """Extract the first quoted path from C:RequestFile output."""
    text = text.strip()
    if not text:
        return None

    if text[0] != '"':
        return text.split()[0]

    out = []
    i = 1
    while i < len(text):
        ch = text[i]
        if ch == '*' and i + 1 < len(text):
            nxt = text[i + 1]
            if nxt == '"':
                out.append('"')
                i += 2
                continue
            if nxt == '*':
                out.append('*')
                i += 2
                continue
        if ch == '"':
            break
        out.append(ch)
        i += 1
    return ''.join(out)


def ask_archive_file(drawer, default):
    """ASL save requester for the single-pane case."""
    if not os.path.exists('C:RequestFile'):
        # Very old systems: fall back to a text prompt in the active drawer.
        name = ask_archive_name(default)
        if not name:
            return None
        return join_amiga(drawer, name)

    try:
        os.remove(REQ_OUT)
    except OSError:
        pass

    cmd = 'C:RequestFile %s FILE %s TITLE %s POSITIVE %s NEGATIVE %s SAVEMODE NOICONS >%s' % (
        quote_arg(drawer),
        quote_arg(default),
        quote_arg('Save LHA archive'),
        quote_arg('Save'),
        quote_arg('Cancel'),
        REQ_OUT)

    rc = run_command(cmd)
    log('RequestFile rc=%s' % rc)

    try:
        f = open(REQ_OUT, 'r')
        text = f.read()
        f.close()
    except IOError:
        text = ''

    try:
        os.remove(REQ_OUT)
    except OSError:
        pass

    path = parse_request_output(text)
    if not path:
        return None
    # RequestFile normally returns an absolute Amiga path.  If it returns
    # only a relative name, anchor it to the active drawer.
    if (':' not in path) and (not path.startswith('/')):
        path = join_amiga(drawer, path)
    return path


# ===== LHA =====

def find_lha():
    for candidate in ('C:LHA', 'C:Lha', 'C:lha'):
        try:
            if os.path.exists(candidate):
                return candidate
        except Exception:
            pass
    return 'LHA'


def path_is_inside(path, container):
    p = path.lower().rstrip('/:')
    c = container.lower().rstrip('/:')
    if not c or not p.startswith(c):
        return False
    if len(p) == len(c):
        return True
    return p[len(c)] in '/:'


def read_log_tail(path, size=700):
    try:
        f = open(path, 'r')
        text = f.read()
        f.close()
    except IOError:
        return ''
    text = text.replace('\r', '\n')
    if len(text) > size:
        text = text[-size:]
    return text.strip()


def count_files(paths):
    """Count files, recursively for selected drawers."""
    total = 0
    for path in paths:
        try:
            if os.path.isdir(path):
                for root, dirs, names in os.walk(path):
                    # Do not follow drawer links (avoid loops).
                    dirs[:] = [d for d in dirs
                               if not os.path.islink(os.path.join(root, d))]
                    total += len(names)
            else:
                total += 1
        except Exception:
            total += 1
    return total


def run_lha(archive, files):
    lha = find_lha()
    log('LHA=%s archive=%s files=%d' % (lha, archive, len(files)))

    if not files:
        message('No files selected.', False)
        return False

    for path in files:
        if path.lower() == archive.lower():
            message('The archive cannot be one of the selected files.', False)
            return False
        if os.path.isdir(path) and path_is_inside(archive, path):
            message('The archive cannot be created inside a selected drawer.', False)
            return False

    # LHA @file: one quoted path per line, so spaces are safe and the
    # command line does not grow with the selection.
    try:
        f = open(FILE_LIST, 'w')
        for path in files:
            f.write(quote_arg(path) + '\n')
        f.close()
    except IOError:
        message('Cannot write the LHA file list:\n' + FILE_LIST)
        return False

    try:
        os.remove(LHA_LOG)
    except OSError:
        pass

    args = [lha, '-m']
    for path in files:
        try:
            if os.path.isdir(path):
                args.append('-r')
                break
        except Exception:
            pass
    args.append('a')
    args.append(quote_arg(archive))
    args.append('@' + FILE_LIST)

    cmd = ' '.join(args) + ' >' + LHA_LOG
    log('LHA command: ' + cmd)

    # Run LHA asynchronously from a script that creates DONE_FILE at the
    # end, so the add-on can keep the VFM progress bar animated while LHA
    # works.  The progress window has no Cancel/Pause: stopping LHA would
    # leave a partial archive.
    try:
        os.remove(DONE_FILE)
    except OSError:
        pass
    try:
        f = open(RUN_SCRIPT, 'w')
        f.write('FailAt 21\n')
        # Give LHA a large stack: the default stack of a RUN shell is
        # small, and old command-line tools can overflow it.
        f.write('Stack 262144\n')
        f.write(cmd + '\n')
        f.write('Echo >' + DONE_FILE + '\n')
        f.close()
    except IOError:
        try:
            os.remove(FILE_LIST)
        except OSError:
            pass
        message('Cannot write the LHA launcher script:\n' + RUN_SCRIPT)
        return False

    progress_open(True, 0)
    total_files = count_files(files)
    progress_update(0, total_files, True)
    rc = run_command('RUN >NIL: C:Execute ' + RUN_SCRIPT)
    if rc is None or rc != 0:
        progress_close()
        try:
            os.remove(RUN_SCRIPT)
        except OSError:
            pass
        try:
            os.remove(FILE_LIST)
        except OSError:
            pass
        message('Cannot start LHA.\nSee T:CompressLHA.log.')
        return False

    timed_out = False
    start = time.time()
    while not os.path.exists(DONE_FILE):
        time.sleep(0.6)
        progress_update(0, total_files, True)
        if (time.time() - start) > 3600:
            timed_out = True
            break

    progress_close()

    try:
        os.remove(DONE_FILE)
    except OSError:
        pass
    try:
        os.remove(RUN_SCRIPT)
    except OSError:
        pass
    try:
        os.remove(FILE_LIST)
    except OSError:
        pass

    if timed_out:
        message('LHA did not finish within the timeout.\n'
                'Check T:VFM_CompressLHA_lha_%d.log.' % _PID)
        return False

    tail = read_log_tail(LHA_LOG)
    lower = tail.lower()
    success = 'operation successful' in lower

    if not success:
        try:
            success = (os.path.exists(archive) and
                       os.path.getsize(archive) > 0 and
                       ('error' not in lower) and
                       ('cannot' not in lower) and
                       ('failed' not in lower))
        except Exception:
            success = False

    if not success:
        log('LHA failed')
        message('LHA failed.\n\n%s' % (tail if tail else
                'See T:CompressLHA.log.'))
        return False

    log('LHA ok')
    message('Archive created:\n' + archive, False)
    send_channel('REFRESH')
    return True


# ===== Main =====

def main():
    log('start path=%s vfm=%s channel=%s drop=%s' %
        (START_PATH, VFM, PLUGIN_DIR, DROP_PORT))

    if not PLUGIN_DIR or not DROP_PORT:
        message('The plugin command channel is not available.\n'
                'Use VFM build 11 or newer.')
        return

    result = send_channel('ADDONINFO')
    if result is None:
        message('ADDONINFO is not available.\n'
                'Install VFM build 35 or newer.')
        return

    rc, info_text = result
    info = parse_addoninfo(info_text) if rc == 0 else None
    if not info:
        message('ADDONINFO failed.\n'
                'Install VFM build 35 or newer and check T:CompressLHA.log.')
        return

    if info['count'] <= 0:
        message('Select one or more files in the active pane first.', False)
        return

    files = read_selected(info['listfile'], info['count'])
    if files is None:
        message('Cannot read the selected paths.\n'
                'See T:CompressLHA.log.')
        return
    if not files:
        message('Select one or more files in the active pane first.', False)
        return

    src = active_path(info)
    if not src:
        message('Select files inside a drawer, not volumes/assigns.', False)
        return

    default_name = default_archive_name(files)

    if info['split']:
        dst = other_path(info)
        if not dst:
            message('The other pane is in the root view.\n'
                    'Open a drawer in the other pane first.', False)
            return

        name = ask_archive_name(default_name)
        if not name:
            return
        if ('/' in name) or (':' in name):
            message('Enter only a file name, not a path.', False)
            return
        name = ensure_lha_extension(name)
        archive = join_amiga(dst, name)
    else:
        archive = ask_archive_file(src, default_name)
        if not archive:
            return
        archive = ensure_lha_extension(archive)

    if not archive:
        return
    if os.path.isdir(archive):
        message('The archive path is a drawer:\n' + archive)
        return

    if not validate_destination(archive, files):
        return

    mode = ask_archive_mode(archive)
    if mode is None:
        return

    if mode == 'overwrite':
        try:
            os.remove(archive)
        except OSError:
            if os.path.exists(archive):
                message('Cannot remove the existing archive:\n' + archive)
                return

    log('archive=%s source=%s split=%d mode=%s' %
        (archive, src, info['split'], mode))
    run_lha(archive, files)


if __name__ == '__main__':
    main()
    sys.exit(0)

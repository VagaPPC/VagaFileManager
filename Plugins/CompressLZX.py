#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: CompressLZX
# ---------------------------------------------------------------------------
#
# Creates an LZX archive from the files selected in the active pane.
#
#   - no selection          -> a message asks to select one or more files;
#   - single pane           -> an ASL save requester asks for the archive;
#   - split view            -> asks only for the archive name and stores it
#                              in the other (not active) pane;
#   - compression           -> uses the LZX executable installed on the
#                              machine (C:LZX, or LZX from the path).
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
# Diagnostics are appended to T:CompressLZX.log.
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

LOG = 'T:CompressLZX.log'

try:
    _PID = os.getpid()
except Exception:
    _PID = int(time.time()) & 0xffff

REQ_OUT = 'T:VFM_CompressLZX_req_%d.txt' % _PID
LZX_LOG = 'T:VFM_CompressLZX_lzx_%d.log' % _PID
RUN_SCRIPT = 'T:VFM_CompressLZX_run_%d.script' % _PID
DONE_FILE = 'T:VFM_CompressLZX_done_%d.txt' % _PID
CLI_FILE = 'T:VFM_CompressLZX_cli_%d.txt' % _PID
ARCHIVER = 'LZX'
TITLE = 'CompressLZX'

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
            asl.MessageBox('CompressLZX', text, 'OK')
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


def path_leaf(path):
    path = path.rstrip('/')
    cut = max(path.rfind('/'), path.rfind(':'))
    return path[cut + 1:] if cut >= 0 else path


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

    prefix = 'NOBUTTONS ' if nocancel else ''
    result = send_channel('PROGRESS %sINDET 0 %d' % (prefix, total), 6000)
    if result is None or result[0] != 0:
        PROGRESS_OK = False
        return True

    PROGRESS_OK = True
    return result[1].strip() != '1'


def progress_update(done=0, total=0, nocancel=True):
    if not PROGRESS_OK:
        return True

    prefix = 'NOBUTTONS ' if nocancel else ''
    result = send_channel('PROGRESS %sINDET %d %d' % (prefix, done, total), 6000)
    if result is None or result[0] != 0:
        return True
    return result[1].strip() != '1'


def progress_close():
    global PROGRESS_OK

    if PROGRESS_OK:
        send_channel('PROGRESS CLOSE', 600)
    PROGRESS_OK = None


# ===== Interruption (close button of the progress window) =====
#
# The progress window has no buttons (PROGRESS NOBUTTONS): its close button
# asks to stop.  The launcher script writes its CLI number to CLI_FILE and
# the packer gets CTRL-C (C:Break).  If it has not stopped after 15 s, the
# plugin waits for it to finish (window without close button, bar still
# moving) instead of removing files that are still open.  Then the result
# is thrown away: a new archive is removed, an archive that was being added
# to is restored from the safety copy.

def remove_quiet(path):
    try:
        os.remove(path)
    except OSError:
        pass


def remove_retry(path):
    """Remove a file that the stopped packer may still hold open."""
    for attempt in range(10):
        try:
            if os.path.exists(path):
                os.remove(path)
            return True
        except OSError:
            time.sleep(0.5)
    return not os.path.exists(path)


def confirm_interrupt():
    if asl is None:
        return True
    try:
        return asl.MessageBox(TITLE, 'Interrupt the compression?',
                              'Interrupt|Continue') == 1
    except Exception:
        return True


def wait_job(total_files):
    """Wait for DONE_FILE keeping the progress bar alive.

    Returns 'done', 'timeout' or 'cancel'.
    """
    start = time.time()
    while not os.path.exists(DONE_FILE):
        time.sleep(0.6)
        if not progress_update(0, total_files, True):
            if confirm_interrupt():
                return 'cancel'
            # Continue: a new window clears the close request.
            progress_close()
            progress_open(True, 0)
            progress_update(0, total_files, True)
        if (time.time() - start) > 3600:
            return 'timeout'
    return 'done'


def job_finished():
    """True when the launcher script has ended.

    The script writes DONE_FILE as its last line, but CTRL-C also stops
    the script itself, so that line is not reached after an interruption.
    While the script runs, the shell keeps RUN_SCRIPT open and AmigaOS
    refuses to delete it: once it can be deleted, the job is over.
    """
    if os.path.exists(DONE_FILE):
        return True
    try:
        os.remove(RUN_SCRIPT)
        return True
    except OSError:
        return not os.path.exists(RUN_SCRIPT)


WAIT_TEXT = 'Interrupting: please wait while the files are removed...'


def stop_job(total_files):
    """CTRL-C to the shell running the packer, then wait for the script to
    end.  Meanwhile a window without close button shows WAIT_TEXT; the
    caller closes it (progress_close) after the clean-up."""
    global PROGRESS_OK
    num = ''
    try:
        f = open(CLI_FILE, 'r')
        num = f.read().strip()
        f.close()
    except IOError:
        pass
    log('interrupt: CLI %r' % num)

    progress_close()
    cmd = 'PROGRESS NOCANCEL NOBUTTONS INDET 0 %d' % total_files
    result = send_channel(cmd, 6000)
    # progress_close() above cleared PROGRESS_OK: the wait window is open
    # again, so the caller's progress_close() must send PROGRESS CLOSE.
    PROGRESS_OK = (result is not None and result[0] == 0)
    send_channel('PROGRESS TEXT ' + WAIT_TEXT, 6000)

    if num.isdigit():
        run_command('C:Break %s C' % num)
    start = time.time()
    warned = False
    while not job_finished() and (time.time() - start) < 3600:
        time.sleep(0.5)
        send_channel(cmd, 6000)          # keeps the bar moving
        if not warned and (time.time() - start) > 15:
            log('interrupt: %s did not stop, waiting for it to finish'
                % ARCHIVER, True)
            warned = True
    log('interrupt: %s stopped' % ARCHIVER)


def make_backup(archive):
    """Copy an existing archive before adding to it.

    Returns the backup path, None if there is no archive, False on error.
    """
    if not os.path.exists(archive):
        return None
    backup = archive + '.vfmbak'
    remove_quiet(backup)
    try:
        import shutil
        shutil.copyfile(archive, backup)
    except Exception:
        remove_quiet(backup)
        message('Cannot make a safety copy of the archive:\n' + archive)
        return False
    return backup


def restore_backup(archive, backup):
    """Put the archive back as it was before."""
    if not backup:
        return
    remove_retry(archive)
    try:
        os.rename(backup, archive)
    except OSError:
        log('cannot restore %s from %s' % (archive, backup), True)


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

def ensure_lzx_extension(path):
    if path.lower().endswith('.lzx'):
        return path
    return path + '.lzx'


def default_archive_name(files):
    """Default archive name from the first selected file or drawer."""
    if not files:
        return 'archive.lzx'

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
    return ensure_lzx_extension(name)


def ask_archive_mode(path):
    """If the archive exists, ask Add / Overwrite / Cancel."""
    if not os.path.exists(path):
        return 'add'
    if asl is None:
        return 'add'

    try:
        result = asl.MessageBox(
            'CompressLZX',
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


def path_is_inside(path, container):
    p = path.lower().rstrip('/:')
    c = container.lower().rstrip('/:')
    if not c or not p.startswith(c):
        return False
    if len(p) == len(c):
        return True
    return p[len(c)] in '/:'


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
        'DIALOG CompressLZX|Archive name:|%s|Create|Cancel' % default, 600)
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
        quote_arg('Save LZX archive'),
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


# ===== LZX =====

def find_lzx():
    for candidate in ('C:LZX', 'C:Lzx', 'C:lzx'):
        try:
            if os.path.exists(candidate):
                return candidate
        except Exception:
            pass
    return 'LZX'


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


def make_lzx_commands(lzx, archive, names, recurse, limit=1500):
    """Build one or more LZX command lines, each below the DOS limit."""
    exe = quote_arg(lzx) if ' ' in lzx else lzx
    # -X0: the add-on already ensures the .lzx suffix; do not let LZX
    # append another one (and do not depend on its case handling).
    prefix = exe + ' -m -X0'
    if recurse:
        prefix += ' -r'
    prefix += ' a ' + quote_arg(archive)
    lines = []
    current = []
    length = len(prefix)

    for name in names:
        arg = quote_arg(name)
        if current and (length + 1 + len(arg) > limit):
            lines.append(prefix + ' ' + ' '.join(current))
            current = []
            length = len(prefix)
        current.append(arg)
        length += 1 + len(arg)

    if current:
        lines.append(prefix + ' ' + ' '.join(current))
    return lines


def run_lzx(archive, files, src):
    lzx = find_lzx()
    log('LZX=%s archive=%s files=%d src=%s' %
        (lzx, archive, len(files), src))

    if not files:
        message('No files selected.', False)
        return False

    if not validate_destination(archive, files):
        return False

    names = []
    recurse = False
    for path in files:
        name = path_leaf(path)
        if not name:
            continue
        try:
            if os.path.isdir(path):
                recurse = True
                # LZX documents directory names with a trailing slash.
                name += '/'
        except Exception:
            pass
        names.append(name)
    if not names:
        message('Cannot determine the selected file names.')
        return False

    commands = make_lzx_commands(lzx, archive, names, recurse)
    if not commands:
        message('Cannot build the LZX command line.')
        return False
    for command in commands:
        log('LZX command: ' + command)

    try:
        os.remove(LZX_LOG)
    except OSError:
        pass
    try:
        os.remove(DONE_FILE)
    except OSError:
        pass

    # Run LZX asynchronously from a script that creates DONE_FILE at the
    # end, so the add-on can keep the VFM progress bar animated while LZX
    # works.  The progress window has no Cancel/Pause: stopping LZX would
    # leave a partial archive.
    cd_dir = src.rstrip('/')
    if not cd_dir:
        cd_dir = src
    try:
        f = open(RUN_SCRIPT, 'w')
        f.write('FailAt 21\n')
        # LZX is an old 68k program and needs a large stack, especially
        # with -r; the default stack of a RUN shell is far too small.
        f.write('Stack 262144\n')
        # CLI number of this shell, to stop the packer (C:Break)
        f.write('Echo >%s "$Process"\n' % CLI_FILE)
        f.write('CD %s\n' % quote_arg(cd_dir))
        for command in commands:
            f.write('%s >>%s\n' % (command, LZX_LOG))
        f.write('Echo >' + DONE_FILE + '\n')
        f.close()
    except IOError:
        message('Cannot write the LZX launcher script:\n' + RUN_SCRIPT)
        return False

    backup = make_backup(archive)
    if backup is False:
        return False

    total_files = count_files(files)
    progress_open(True, 0)
    progress_update(0, total_files, True)

    rc = run_command('RUN >NIL: C:Execute ' + RUN_SCRIPT)
    if rc is None or rc != 0:
        progress_close()
        try:
            os.remove(RUN_SCRIPT)
        except OSError:
            pass
        if backup:
            remove_quiet(backup)   # the archive was not touched
        message('Cannot start LZX.\nSee T:CompressLZX.log.')
        return False

    status = wait_job(total_files)
    timed_out = (status == 'timeout')
    if status == 'cancel':
        stop_job(total_files)            # the wait window stays open
    else:
        progress_close()
    remove_quiet(CLI_FILE)

    if status == 'cancel':
        log('compression interrupted by the user')
        remove_quiet(DONE_FILE)
        remove_quiet(RUN_SCRIPT)
        remove_retry(archive)
        restore_backup(archive, backup)
        progress_close()                 # the wait window closes by itself
        send_channel('REFRESH')
        return False

    try:
        os.remove(DONE_FILE)
    except OSError:
        pass
    try:
        os.remove(RUN_SCRIPT)
    except OSError:
        pass

    if timed_out:
        message('LZX did not finish within the timeout.\n'
                'Check T:VFM_CompressLZX_lzx_%d.log.' % _PID)
        return False

    tail = read_log_tail(LZX_LOG)
    lower = tail.lower()
    success = False
    try:
        if os.path.exists(archive) and os.path.getsize(archive) > 0:
            if 'operation successful' in lower:
                success = True
            elif ('operation failed' in lower or
                  'operation aborted' in lower or
                  'error creating' in lower or
                  'error writing' in lower or
                  'error reading' in lower or
                  'error compressing' in lower or
                  'error copying' in lower or
                  'error opening' in lower or
                  'error, ' in lower or
                  'error;' in lower or
                  'error -' in lower or
                  'cannot open' in lower or
                  'cannot create' in lower or
                  'unable to open' in lower):
                success = False
            else:
                # Some LZX versions print "0 errors" on success; do not
                # reject a valid archive just because of that word.
                success = True
    except Exception:
        success = False

    if not success:
        log('LZX failed')
        restore_backup(archive, backup)
        if tail:
            message('LZX failed.\n\n%s\n\nSee T:CompressLZX.log.' % tail)
        else:
            message('LZX failed.\n\nSee T:CompressLZX.log.')
        return False

    if backup:
        remove_quiet(backup)
    log('LZX ok')
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
                'Install VFM build 35 or newer and check T:CompressLZX.log.')
        return

    if info['count'] <= 0:
        message('Select one or more files in the active pane first.', False)
        return

    files = read_selected(info['listfile'], info['count'])
    if files is None:
        message('Cannot read the selected paths.\n'
                'See T:CompressLZX.log.')
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
        name = ensure_lzx_extension(name)
        archive = join_amiga(dst, name)
    else:
        archive = ask_archive_file(src, default_name)
        if not archive:
            return
        archive = ensure_lzx_extension(archive)

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
    run_lzx(archive, files, src)


if __name__ == '__main__':
    main()
    sys.exit(0)

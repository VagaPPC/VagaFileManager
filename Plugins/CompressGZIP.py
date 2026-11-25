#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: CompressGZIP
# ---------------------------------------------------------------------------
#
# Creates a gzip archive from the files selected in the active pane.
#
# gzip by itself compresses a single file and has no concept of multiple
# directory entries.  To behave like CompressLHA, this add-on packs the
# selection with C:TAR into a temporary TAR and then compresses that TAR
# with C:gzip.  The result is a normal .tar.gz archive.
#
#   - no selection          -> a message asks to select one or more files;
#   - single pane           -> an ASL save requester asks for the archive;
#   - split view            -> asks only for the archive name and stores it
#                              in the other (not active) pane;
#   - compression           -> uses C:gzip and C:TAR (or the same names from
#                              the path).
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
# Diagnostics are appended to T:CompressGZIP.log.
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

LOG = 'T:CompressGZIP.log'

try:
    _PID = os.getpid()
except Exception:
    _PID = int(time.time()) & 0xffff

REQ_OUT = 'T:VFM_CompressGZIP_req_%d.txt' % _PID
GZIP_LOG = 'T:VFM_CompressGZIP_gzip_%d.log' % _PID
RUN_SCRIPT = 'T:VFM_CompressGZIP_run_%d.script' % _PID
DONE_FILE = 'T:VFM_CompressGZIP_done_%d.txt' % _PID
CLI_FILE = 'T:VFM_CompressGZIP_cli_%d.txt' % _PID
ARCHIVER = 'gzip'
TITLE = 'CompressGZIP'

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
            asl.MessageBox('CompressGZIP', text, 'OK')
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

def ensure_gzip_extension(path):
    lower = path.lower()
    if lower.endswith('.tar.gz') or lower.endswith('.tgz'):
        return path
    if lower.endswith('.tar'):
        return path + '.gz'
    if lower.endswith('.gz'):
        return path[:-3] + '.tar.gz'
    return path + '.tar.gz'


def default_archive_name(files):
    """Default archive name from the first selected file or drawer."""
    if not files:
        return 'archive.tar.gz'

    first = files[0]
    name = path_leaf(first)

    try:
        if not os.path.isdir(first):
            lower = name.lower()
            if lower.endswith('.tar.gz'):
                name = name[:-7]
            elif lower.endswith('.tgz'):
                name = name[:-4]
            elif lower.endswith('.gz'):
                name = name[:-3]
            else:
                base, ext = os.path.splitext(name)
                if base:
                    name = base
    except Exception:
        pass

    if not name:
        name = 'archive'
    return ensure_gzip_extension(name)


def ask_archive_mode(path):
    """If the archive exists, ask Add / Overwrite / Cancel."""
    if not os.path.exists(path):
        return 'add'
    if asl is None:
        return 'add'

    try:
        result = asl.MessageBox(
            'CompressGZIP',
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
        'DIALOG CompressGZIP|Archive name:|%s|Create|Cancel' % default, 600)
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
        quote_arg('Save GZIP archive'),
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


# ===== TAR + gzip =====

def find_gzip():
    for candidate in ('C:gzip', 'C:GZIP', 'C:Gzip'):
        try:
            if os.path.exists(candidate):
                return candidate
        except Exception:
            pass
    return 'gzip'


def find_tar():
    for candidate in ('C:TAR', 'C:Tar', 'C:tar'):
        try:
            if os.path.exists(candidate):
                return candidate
        except Exception:
            pass
    return 'TAR'


def read_log_tail(path, size=900):
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


def chunk_names(names, base_length, limit=1500):
    """Split names so every TAR command line stays below 'limit'."""
    chunks = []
    current = []
    length = base_length

    for name in names:
        arg = quote_arg(name)
        if current and (length + 1 + len(arg) > limit):
            chunks.append(current)
            current = []
            length = base_length
        current.append(arg)
        length += 1 + len(arg)

    if current:
        chunks.append(current)
    return chunks


def make_tar_commands(tar, tarfile, names, append):
    """Build TAR commands; the first creates, the rest append."""
    commands = []
    base = '%s -c -f %s' % (quote_arg(tar), quote_arg(tarfile))
    chunks = chunk_names(names, len(base))
    create = not append

    for chunk in chunks:
        if create:
            commands.append('%s -c -f %s %s' %
                            (quote_arg(tar), quote_arg(tarfile),
                             ' '.join(chunk)))
            create = False
        else:
            commands.append('%s -r -f %s %s' %
                            (quote_arg(tar), quote_arg(tarfile),
                             ' '.join(chunk)))
    return commands


def run_gzip(archive, files, src, mode):
    gzip = find_gzip()
    tar = find_tar()
    log('GZIP=%s TAR=%s archive=%s files=%d src=%s mode=%s' %
        (gzip, tar, archive, len(files), src, mode))

    if not files:
        message('No files selected.', False)
        return False

    if not validate_destination(archive, files):
        return False

    names = []
    for path in files:
        name = path_leaf(path)
        if name:
            names.append(name)
    if not names:
        message('Cannot determine the selected file names.')
        return False

    archive_existed = os.path.exists(archive)
    tmp_archive = archive + '.tmp'
    # Keep the temporary TAR next to the destination: T: is usually a RAM
    # disk and a large selection could fill it.
    tar_temp = archive + '.tmp.tar'

    try:
        os.remove(tmp_archive)
    except OSError:
        pass
    try:
        os.remove(tar_temp)
    except OSError:
        pass
    try:
        os.remove(DONE_FILE)
    except OSError:
        pass
    try:
        os.remove(GZIP_LOG)
    except OSError:
        pass

    add_to_existing = (mode == 'add' and archive_existed)

    if add_to_existing:
        rc = run_command('%s -t %s >>%s' %
                         (quote_arg(gzip), quote_arg(archive),
                          quote_arg(GZIP_LOG)))
        if rc not in (0, None):
            message('The existing archive is not a valid gzip file:\n' +
                    archive)
            return False

    commands = make_tar_commands(tar, tar_temp, names, add_to_existing)

    # Run the whole TAR+gzip pipeline asynchronously from a script so the
    # VFM progress bar keeps animating.  A temporary .tmp archive is written
    # first: the existing archive is only replaced after gzip succeeded.
    try:
        f = open(RUN_SCRIPT, 'w')
        f.write('FailAt 21\n')
        # Give gzip a large stack: the default stack of a RUN shell is
        # small, and old command-line tools can overflow it.
        f.write('Stack 262144\n')
        # CLI number of this shell, to stop the packer (C:Break)
        f.write('Echo >%s "$Process"\n' % CLI_FILE)
        f.write('CD %s\n' % quote_arg(src))
        if add_to_existing:
            f.write('%s -d -c %s >%s\n' %
                    (quote_arg(gzip), quote_arg(archive), quote_arg(tar_temp)))
        for command in commands:
            f.write('%s >>%s\n' % (command, GZIP_LOG))
        f.write('%s -9 -c %s >%s\n' %
                (quote_arg(gzip), quote_arg(tar_temp), quote_arg(tmp_archive)))
        f.write('Echo >' + DONE_FILE + '\n')
        f.close()
    except IOError:
        message('Cannot write the GZIP launcher script:\n' + RUN_SCRIPT)
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
        message('Cannot start GZIP.\nSee T:CompressGZIP.log.')
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
        # The existing archive is only replaced at the end: just remove
        # the temporary files.
        remove_quiet(DONE_FILE)
        remove_quiet(RUN_SCRIPT)
        remove_retry(tar_temp)
        remove_retry(tmp_archive)
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
    try:
        os.remove(tar_temp)
    except OSError:
        pass

    if timed_out:
        message('GZIP did not finish within the timeout.\n'
                'Check T:VFM_CompressGZIP_gzip_%d.log.' % _PID)
        return False

    tail = read_log_tail(GZIP_LOG)
    lower = tail.lower()
    success = False
    try:
        success = (os.path.exists(tmp_archive) and
                   os.path.getsize(tmp_archive) > 0 and
                   not ('error' in lower or 'cannot' in lower or
                        'failed' in lower))
    except Exception:
        success = False

    if not success:
        try:
            os.remove(tmp_archive)
        except OSError:
            pass
        log('GZIP failed')
        message('GZIP failed.\n\n%s' % (tail if tail else
                'See T:CompressGZIP.log.'))
        return False

    # Replace the old archive only now that the new one is complete.
    try:
        if os.path.exists(archive):
            os.remove(archive)
    except OSError:
        try:
            os.remove(tmp_archive)
        except OSError:
            pass
        message('Cannot remove the existing archive:\n' + archive)
        return False

    rc = run_command('C:Rename %s %s' %
                     (quote_arg(tmp_archive), quote_arg(archive)))
    if rc is None or rc != 0 or not os.path.exists(archive):
        # Rename should work because both files are in the same drawer.
        try:
            os.rename(tmp_archive, archive)
        except Exception:
            message('Cannot rename the temporary archive to:\n' + archive)
            return False

    # gzip -t verifies the compressed stream.
    rc = run_command('%s -t %s >>%s' %
                     (quote_arg(gzip), quote_arg(archive), quote_arg(GZIP_LOG)))
    if rc not in (0, None):
        message('The archive was created but gzip -t failed.\n'
                'Check T:CompressGZIP.log.')
        return False

    log('GZIP ok')
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
                'Install VFM build 35 or newer and check T:CompressGZIP.log.')
        return

    if info['count'] <= 0:
        message('Select one or more files in the active pane first.', False)
        return

    files = read_selected(info['listfile'], info['count'])
    if files is None:
        message('Cannot read the selected paths.\n'
                'See T:CompressGZIP.log.')
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
        name = ensure_gzip_extension(name)
        archive = join_amiga(dst, name)
    else:
        archive = ask_archive_file(src, default_name)
        if not archive:
            return
        archive = ensure_gzip_extension(archive)

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

    log('archive=%s source=%s split=%d mode=%s' %
        (archive, src, info['split'], mode))
    run_gzip(archive, files, src, mode)


if __name__ == '__main__':
    main()
    sys.exit(0)

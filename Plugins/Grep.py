#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: Grep
# ---------------------------------------------------------------------------
#
# Recursive grep inside text files, with a ProAction/ReAction result list.
#
#   1. asks for the string to search (VFM Reaction dialog);
#   2. recursively scans the active pane path (argv[2]);
#   3. skips binary files and searches only text files;
#   4. shows the matching files in a ProAction listbrowser;
#   5. selecting one (OK or double-click) sends REVEAL to VFM, which
#      navigates the active pane to the file's drawer and preselects it
#      (same behaviour as Find.py).
#
# ProAction is an external ARexx GUI server (http://www.broad.ology.org.uk/
# amiga/proaction).  This add-on only sends its documented commands; none of
# the ProAction source code is included in VFM or in this script.  If
# ProAction is not available, the script falls back to the ASL file
# requester over a temporary drawer of placeholder files.
#
# No ARexx is used to talk to VFM: commands go through the file-based plugin
# channel.  A private ARexx port is created only to receive ProAction GUI
# events.  The plugin receives:
#
#   sys.argv[1] = ARexx port (kept for compatibility)
#   sys.argv[2] = active pane path at launch (search root)
#   sys.argv[3] = VagaFileManager executable
#   sys.argv[4] = public drop port of this instance
#   sys.argv[5] = plugin channel directory (T:VFM_<task>)
#
# Channel protocol (one command):
#   write  <channel>/req_<id>.txt
#   run    VagaFileManager --notify "<drop-port>"
#   read   <channel>/rep_<id>.txt   ->  <rc>|<result>
#
# Diagnostics are appended to T:Grep.log.
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

try:
    import arexx
except ImportError:
    arexx = None

PORT = sys.argv[1] if len(sys.argv) > 1 else 'VAGAFILEMANAGER'
START_PATH = sys.argv[2] if len(sys.argv) > 2 else ''
VFM = sys.argv[3] if len(sys.argv) > 3 else ''
DROP_PORT = sys.argv[4] if len(sys.argv) > 4 else ''
PLUGIN_DIR = sys.argv[5] if len(sys.argv) > 5 else ''

LOG = 'T:Grep.log'

MAX_RESULTS = 500
MAX_DEPTH = 64
MAX_FILE_SIZE = 32 * 1024 * 1024   # skip larger files
SAMPLE_SIZE = 4096
READ_SIZE = 65536

PA_BACKEND = None                  # 'arexx', 'rx' or None
try:
    _PA_PID = os.getpid()
except Exception:
    _PA_PID = int(time.time()) & 0xffff
PA_CMD = 'T:VFM_Grep_PA_%d.cmd' % _PA_PID
PA_OUT = 'T:VFM_Grep_PA_%d.out' % _PA_PID
PA_PORT_SEQ = 0

_seq = 0


# ===== Small helpers =====

def log(text):
    """Append a diagnostic line to T:Grep.log (best effort)."""
    try:
        f = open(LOG, 'a')
        f.write(text + '\n')
        f.close()
    except Exception:
        pass


def message(text):
    """Show a message with ASL (if available) and log it."""
    log('message: ' + text)
    if asl is not None:
        try:
            asl.MessageBox('VFM Grep', text, 'OK')
        except Exception:
            pass


def run_command(cmd):
    """Run a Shell command and wait; None if it cannot start."""
    if amiga is not None:
        try:
            return amiga.system(cmd)
        except Exception:
            log('amiga.system exception')
    try:
        return os.system(cmd)
    except Exception:
        return None


# ===== VFM plugin channel =====

def send_channel(command, timeout=50):
    """Send one command through the plugin file channel.

    Returns (rc, result) or None if the channel is unavailable.
    """
    global _seq

    if not PLUGIN_DIR or not DROP_PORT or not VFM:
        return None

    command = command.replace('\n', ' ').strip()
    if not command:
        return (None, '')

    _seq += 1
    pid = 0
    try:
        pid = os.getpid()
    except Exception:
        pass

    ident = '%d_%d_%d' % (pid, int(time.time() * 1000) % 100000000, _seq)
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


def progress_open(nocancel=False):
    """Open the VFM progress window (indeterminate)."""
    global PROGRESS_OK

    prefix = 'NOCANCEL ' if nocancel else ''
    result = send_channel('PROGRESS %s0 0' % prefix, 6000)
    if result is None or result[0] != 0:
        PROGRESS_OK = False
        return True

    PROGRESS_OK = True
    return result[1].strip() != '1'


def progress_update(done=0, total=0, nocancel=False):
    """Update the progress window.  False means Cancel was pressed."""
    if not PROGRESS_OK:
        return True

    prefix = 'NOCANCEL ' if nocancel else ''
    result = send_channel('PROGRESS %s%d %d' % (prefix, done, total), 6000)
    if result is None or result[0] != 0:
        return True
    return result[1].strip() != '1'


def progress_close():
    global PROGRESS_OK

    if PROGRESS_OK:
        send_channel('PROGRESS CLOSE', 600)
    PROGRESS_OK = None


def ask_word():
    """Ask for the string with VFM's Reaction DIALOG command.

    Format: DIALOG <title>|<prompt>|<default>|<buttons>
    Reply:  <button-index>|<typed-text>
    """
    result = send_channel(
        'DIALOG VFM Grep|String to search:||Search|Cancel', 600)
    if result is None:
        return None

    rc, answer = result
    if rc != 0 or '|' not in answer:
        return None

    idx, sep, text = answer.partition('|')
    if idx.strip() != '1':          # 1 = Search / Return, 2 = Cancel
        return None

    word = text.strip()
    return word if word else None


# ===== Text-file detection and search =====

def is_text_sample(sample):
    """Heuristic: reject NUL bytes and a high ratio of control bytes."""
    if not sample:
        return False
    control = 0
    for ch in sample:
        if isinstance(ch, int):        # Python 3
            value = ch
        else:                          # Python 2
            value = ord(ch)
        if value == 0:
            return False
        # Allow TAB, LF, CR, FF and BS.
        if value < 32 and value not in (8, 9, 10, 12, 13):
            control += 1
    return control * 100 <= len(sample) * 5


def file_matches(path, needle):
    """TRUE if 'path' looks like text and contains needle (case-insensitive)."""
    try:
        needle = needle.lower().encode('latin-1')
    except (AttributeError, UnicodeError):
        needle = needle.lower()
    try:
        size = os.path.getsize(path)
        if size <= 0 or size > MAX_FILE_SIZE:
            return False
        f = open(path, 'rb')
    except (IOError, OSError):
        return False

    try:
        sample = f.read(SAMPLE_SIZE)
        if not is_text_sample(sample):
            return False
        f.seek(0)
        tail = None
        while True:
            chunk = f.read(READ_SIZE)
            if not chunk:
                break
            data = chunk if tail is None else tail + chunk
            if needle in data.lower():
                return True
            # Keep a small overlap so a match across two chunks is found.
            if len(needle) > 1:
                tail = data[-(len(needle) - 1):]
            else:
                tail = None
    except (IOError, OSError):
        return False
    finally:
        try:
            f.close()
        except Exception:
            pass
    return False


def is_link(path):
    try:
        return os.path.islink(path)
    except Exception:
        return False


def find_matches(root, word, progress=None):
    """Recursive search from root.

    Returns (list_of_paths, truncated, canceled).  If progress is given, it
    is called with (scanned, 0) at most every 0.5 s; returning False from it
    raises no exception but stops the search.
    """
    needle = word.lower()
    try:
        needle = needle.encode('latin-1')
    except (AttributeError, UnicodeError):
        pass

    matches = []
    seen = {}
    stack = [(root, 0)]
    truncated = False
    canceled = False
    scanned = 0
    last_progress = 0.0

    while stack:
        if canceled:
            break

        directory, depth = stack.pop()
        if depth > MAX_DEPTH:
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
            if canceled:
                break

            scanned += 1
            if progress is not None:
                now = time.time()
                if (now - last_progress) >= 0.5:
                    last_progress = now
                    if not progress(scanned, 0):
                        canceled = True
                        break

            path = os.path.join(directory, name)

            try:
                if os.path.isdir(path):
                    if not is_link(path):
                        stack.append((path, depth + 1))
                    continue
            except (IOError, OSError):
                continue

            if name.lower().endswith('.info'):
                continue

            if file_matches(path, needle):
                matches.append(path)
                if len(matches) >= MAX_RESULTS:
                    truncated = True
                    return (matches, truncated, False)

    return (matches, truncated, canceled)


# ===== Display helpers =====

def split_amiga_path(path):
    """Return (name, folder) using both '/' and ':' as separators."""
    cut = max(path.rfind('/'), path.rfind(':'))
    if cut >= 0:
        folder = path[:cut]
        if path[cut] == ':':
            folder = folder + ':'
        return (path[cut + 1:], folder)
    return (path, '')


def safe_text(text):
    """Make text safe inside a ProAction TAGSTRING (comma/quote/escape)."""
    out = []
    for ch in text:
        value = ord(ch)
        if ch in ',"*[]\'|()=;' or value < 32 or value == 127:
            out.append('_')
        else:
            out.append(ch)
    result = ''.join(out).strip()
    return result if result else '-'


# ===== ProAction bridge =====

def proaction_running():
    """Return True/False, or None if port enumeration is unavailable."""
    try:
        ports = os.getports()
    except Exception:
        return None
    if ports is None:
        return None
    if isinstance(ports, str):
        return 'PROACTION' in ports.upper()
    for name in ports:
        if str(name).upper() == 'PROACTION':
            return True
    return False


def start_proaction():
    log('starting ProAction')
    run_command('RUN >NIL: APPDIR:ProAction')
    for attempt in range(10):
        time.sleep(0.3)
        if proaction_running() is True:
            return True
    return (proaction_running() is not False)


def detect_pa_backend():
    """Choose arexx.dorexx (fast) or C:RX (works on Python 2.5.6)."""
    global PA_BACKEND

    if PA_BACKEND is not None:
        return PA_BACKEND

    if arexx is not None:
        try:
            rc, rc2, result = arexx.dorexx('REXX', "return 'VFM_GREP_OK'")
            if rc == 0 and result and 'VFM_GREP_OK' in str(result):
                PA_BACKEND = 'arexx'
                log('ProAction backend: arexx')
                return PA_BACKEND
        except Exception:
            pass

    PA_BACKEND = 'rx'
    log('ProAction backend: C:RX')
    return PA_BACKEND


def as_bytes(text):
    """Return a byte string; Python 2.5 strings are already bytes."""
    if sys.version_info[0] >= 3:
        try:
            return text.encode('latin-1')
        except UnicodeError:
            return text.encode('utf-8')
    return text


def pa_rx(command, expect_result):
    """Run a ProAction command through C:RX and parse RC/RESULT."""
    command = as_bytes(command.replace("'", "''"))

    try:
        f = open(PA_CMD, 'wb')          # binary: LF only
        f.write(as_bytes('/* VFM Grep ProAction bridge */\n'))
        f.write(as_bytes('address PROACTION\n'))
        f.write(as_bytes("'") + command + as_bytes("'\n"))
        f.write(as_bytes("say 'RC=' || rc\n"))
        f.write(as_bytes("say 'RESULT=' || result\n"))
        f.close()
    except IOError:
        log('pa_rx: cannot write ' + PA_CMD)
        return (1, '')

    try:
        os.remove(PA_OUT)
    except OSError:
        pass

    run_command('C:RX %s >%s' % (PA_CMD, PA_OUT))

    rc = None
    result = ''
    try:
        f = open(PA_OUT, 'r')
        while True:
            line = f.readline()
            if not line:
                break
            line = line.rstrip('\n\r')
            if line.startswith('RC='):
                try:
                    rc = int(line[3:].strip())
                except ValueError:
                    rc = 1
            elif line.startswith('RESULT='):
                result = line[7:]
        f.close()
    except IOError:
        log('pa_rx: cannot read ' + PA_OUT)

    if rc is None:
        rc = 1
    if rc != 0:
        log('pa_rx: rc=%s cmd=%s' % (rc, command[:90]))
    return (rc, result.strip())


def pa(command, expect_result=False):
    """Send one command to ProAction and return (rc, result)."""
    global PA_BACKEND

    backend = detect_pa_backend()

    if backend == 'arexx':
        try:
            rc, rc2, result = arexx.dorexx('PROACTION', command)
            if result is None:
                result = ''
            result = str(result)
            if rc == 0:
                if not expect_result or result.strip():
                    return (0, result)
                log('pa: empty result for ' + command[:90])
                return (0, '')
            log('pa: rc=%s cmd=%s' % (rc, command[:90]))
            return (rc, result)
        except Exception:
            log('pa: arexx exception, switching to C:RX')
            PA_BACKEND = 'rx'
            backend = 'rx'

    if backend == 'rx':
        return pa_rx(command, expect_result)

    return (1, '')


class PAFailure(Exception):
    pass


def pa_need(command):
    """Send a command whose RESULT is required; raise on failure."""
    rc, result = pa(command, True)
    result = result.strip()
    if rc != 0 or not result:
        raise PAFailure(command)
    return result


def pa_ok(command):
    """Send a command whose result is not needed."""
    rc, result = pa(command, False)
    if rc != 0:
        raise PAFailure(command)


def pa_int(value):
    try:
        return int(str(value).strip().strip('"'))
    except ValueError:
        return -1


def make_pa_port():
    """Create a unique ARexx port for ProAction events."""
    if arexx is None:
        return None

    global PA_PORT_SEQ
    try:
        pid = os.getpid()
    except Exception:
        pid = int(time.time()) & 0xffff

    for attempt in range(5):
        PA_PORT_SEQ += 1
        name = 'VFM_GREP_%d_%d' % (pid % 100000, PA_PORT_SEQ)
        try:
            port = arexx.Port(name)
        except Exception:
            port = None
        if port:
            return port
    return None


def create_pa_gui(portname, wintags):
    """CREATEGUI, starting ProAction once if it is not running."""
    command = 'CREATEGUI PORTNAME "%s" TAGSTRING "%s"' % (portname, wintags)

    rc, result = pa(command, True)
    result = result.strip()
    if rc == 0 and result:
        return result

    running = proaction_running()
    if running is True:
        # ProAction is running: do not start a second server.
        log('ProAction is running but CREATEGUI failed')
        return None

    if start_proaction():
        rc, result = pa(command, True)
        result = result.strip()
        if rc == 0 and result:
            return result
    return None


def pa_get_selected(guikey, listgid):
    rc, result = pa('GETATTR GUIID %s OBJECTID %s TAGNAME "LISTBROWSER_Selected"'
                    % (guikey, listgid), True)
    if rc == 0:
        return pa_int(result)
    return -1


def pa_handle_events(pyport, guikey, listgid, ok_gid, cancel_gid, count):
    """Wait for ProAction events. Returns selected index or -1."""
    last_list_index = -1
    last_list_time = 0.0

    while True:
        try:
            pyport.wait()
        except Exception:
            log('ProAction port wait failed')
            return -1

        msg = pyport.getmsg()
        while msg:
            try:
                cmd = (msg.msg or '').strip()
            except Exception:
                cmd = ''
            try:
                msg.reply()
            except Exception:
                pass

            if cmd == 'QUIT' or cmd[:5] == 'CLOSE':
                return -1

            if cmd[:8] == 'GADGETUP':
                parts = cmd.split()
                # GADGETUP GUIID <key> GADGETID <gid> CODE <code>
                if len(parts) >= 7:
                    key = parts[2]
                    gid = parts[4]
                    code = parts[6]

                    if key != str(guikey):
                        msg = pyport.getmsg()
                        continue

                    if gid == str(ok_gid):
                        idx = pa_get_selected(guikey, listgid)
                        if 0 <= idx < count:
                            return idx
                        # No selection: keep the window open.

                    elif gid == str(cancel_gid):
                        return -1

                    elif gid == str(listgid):
                        # ProAction's GETATTR does not encode
                        # LISTBROWSER_RelEvent (TT_LBRELEVENT), so detect a
                        # double click by timing two releases on the same row.
                        idx = pa_get_selected(guikey, listgid)
                        if idx < 0:
                            idx = pa_int(code)
                        if 0 <= idx < count:
                            now = time.time()
                            if idx == last_list_index and (now - last_list_time) < 0.6:
                                return idx
                            last_list_index = idx
                            last_list_time = now

            msg = pyport.getmsg()
    return -1


def choose_result_proaction(matches, word, truncated):
    """Show the results in a ProAction listbrowser.

    Returns ('ok', path), ('cancel', None) or ('fail', None).
    """
    if arexx is None:
        return ('fail', None)

    pyport = make_pa_port()
    if not pyport:
        log('ProAction: cannot create ARexx port')
        return ('fail', None)

    guikey = None
    listid = None
    ciid = None

    try:
        title = 'VFM Grep - %d files' % len(matches)
        if truncated:
            title = title + ' - first %d' % MAX_RESULTS

        wintags = ''
        wintags += 'WA_Width,760,'
        wintags += 'WA_Height,420,'
        wintags += 'WA_DragBar,1,'
        wintags += 'WA_DepthGadget,1,'
        wintags += 'WA_SizeGadget,1,'
        wintags += 'WA_CloseGadget,1,'
        wintags += 'WA_Activate,1,'
        wintags += 'WA_PubScreenFallBack,1,'
        wintags += 'WA_PubScreenName,Workbench,'
        wintags += 'WINDOW_Position,WPOS_CENTERSCREEN,'
        wintags += 'WA_Title,' + safe_text(title) + ','
        wintags += 'TAG_DONE'

        guikey = create_pa_gui(pyport.name, wintags)
        if not guikey:
            raise PAFailure('CREATEGUI')
        log('ProAction GUI key=%s port=%s' % (guikey, pyport.name))

        root_layout = pa_need(
            'ADDLAYOUT GUIID %s TAGSTRING "LAYOUT_Orientation,LAYOUT_ORIENT_VERT,LAYOUT_SpaceOuter,1,TAG_DONE"'
            % guikey)

        label = 'Word: %s    %d files' % (safe_text(word), len(matches))
        if truncated:
            label = label + ' - first %d' % MAX_RESULTS
        label_gid = pa_need(
            'ADDGADGET GUIID %s GADGETCLASS "button.gadget" TAGSTRING "GA_ReadOnly,1,GA_Text,%s,TAG_DONE"'
            % (guikey, safe_text(label)))
        pa_ok('SETATTRS GUIID %s OBJECTID %s TAGSTRING "LAYOUT_ModifyChild,%s,CHILD_WeightedHeight,0,TAG_DONE"'
              % (guikey, root_layout, label_gid))

        # List object, column info and nodes.
        listid = pa_need('NEWGUIOBJECT GUIID %s OBJECTTYPE "GUIOBJ_List"' % guikey)

        ci_tags = 'LBCIA_Column,0,LBCIA_Title,Name,LBCIA_Weight,35,'
        ci_tags += 'LBCIA_Column,1,LBCIA_Title,Folder,LBCIA_Weight,65,TAG_DONE'
        ciid = pa_need('ALLOCLISTBROWSERCOLUMNINFO GUIID %s COLUMNS 2 TAGSTRING "%s"'
                       % (guikey, ci_tags))

        for index, path in enumerate(matches):
            name, folder = split_amiga_path(path)
            node_tags = 'LBNA_Column,0,LBNCA_CopyText,1,LBNCA_Text,%s,' \
                        'LBNA_Column,1,LBNCA_CopyText,1,LBNCA_Text,%s,TAG_DONE' \
                        % (safe_text(name), safe_text(folder))
            node = pa_need('ALLOCLISTBROWSERNODE GUIID %s COLUMNS 2 TAGSTRING "%s"'
                           % (guikey, node_tags))
            pa_ok('ADDTAIL GUIID %s LISTID %s NODEID %s' % (guikey, listid, node))

        lb_tags = 'LISTBROWSER_Labels,%s,LISTBROWSER_ColumnInfo,%s,' \
                  'LISTBROWSER_ColumnTitles,1,' \
                  'LISTBROWSER_ShowSelected,1,LISTBROWSER_MultiSelect,0,' \
                  'GA_RelVerify,1,TAG_DONE' \
                  % (listid, ciid)
        listgid = pa_need('ADDGADGET GUIID %s GADGETCLASS "listbrowser.gadget" TAGSTRING "%s"'
                          % (guikey, lb_tags))
        pa_ok('SETATTRS GUIID %s OBJECTID %s TAGSTRING "LAYOUT_ModifyChild,%s,CHILD_WeightedHeight,100,TAG_DONE"'
              % (guikey, root_layout, listgid))

        # Button row.
        row = pa_need('ADDLAYOUT GUIID %s TAGSTRING "LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,TAG_DONE"'
                      % guikey)
        ok_gid = pa_need('ADDGADGET GUIID %s GADGETCLASS "button.gadget" TAGSTRING "GA_Text,OK,GA_RelVerify,1,TAG_DONE"'
                         % guikey)
        cancel_gid = pa_need('ADDGADGET GUIID %s GADGETCLASS "button.gadget" TAGSTRING "GA_Text,Cancel,GA_RelVerify,1,TAG_DONE"'
                             % guikey)
        pa_ok('SETATTRS GUIID %s OBJECTID %s TAGSTRING "LAYOUT_ModifyChild,%s,CHILD_WeightedWidth,0,TAG_DONE"'
              % (guikey, row, ok_gid))
        pa_ok('SETATTRS GUIID %s OBJECTID %s TAGSTRING "LAYOUT_ModifyChild,%s,CHILD_WeightedWidth,0,TAG_DONE"'
              % (guikey, row, cancel_gid))
        pa_ok('ENDLAYOUT GUIID %s' % guikey)
        pa_ok('SETATTRS GUIID %s OBJECTID %s TAGSTRING "LAYOUT_ModifyChild,%s,CHILD_WeightedHeight,0,TAG_DONE"'
              % (guikey, root_layout, row))

        pa_ok('OPENGUIWINDOW GUIID %s' % guikey)
        log('ProAction window open')

        index = pa_handle_events(pyport, guikey, listgid, ok_gid, cancel_gid,
                                 len(matches))
        if 0 <= index < len(matches):
            log('ProAction selected index=%d path=%r' % (index, matches[index]))
            return ('ok', matches[index])
        return ('cancel', None)

    except PAFailure:
        log('ProAction GUI build failed')
        return ('fail', None)
    except Exception:
        log('ProAction GUI exception')
        return ('fail', None)
    finally:
        if guikey:
            try:
                pa_ok('CLOSEGUIWINDOW GUIID %s' % guikey)
            except Exception:
                pass
            if listid:
                try:
                    pa_ok('FREELISTBROWSERLIST GUIID %s LISTID %s' % (guikey, listid))
                except Exception:
                    pass
            if ciid:
                try:
                    pa_ok('FREELISTBROWSERCOLUMNINFO GUIID %s COLUMNINFOID %s' % (guikey, ciid))
                except Exception:
                    pass
            try:
                pa_ok('DESTROYGUI GUIID %s' % guikey)
            except Exception:
                pass
        try:
            pyport.close()
        except Exception:
            pass


# ===== ASL fallback =====

def relative_path(root, path):
    """Path relative to the search root, for the placeholder name."""
    base = root.rstrip('/:')
    if base and path.lower().startswith(base.lower()):
        rel = path[len(base):]
        return rel.lstrip('/:') or os.path.basename(path)
    return path


def sanitize_name(text):
    """Make a string safe as an AmigaDOS file name."""
    return safe_text(text).replace('/', '_').replace(':', '_').replace(' ', '_')[:90]


def reset_temp_dir(tempdir):
    """Create an empty temporary drawer, removing a stale copy if present."""
    if os.path.isdir(tempdir):
        try:
            for name in os.listdir(tempdir):
                path = os.path.join(tempdir, name)
                if os.path.isfile(path):
                    os.remove(path)
        except (IOError, OSError):
            pass
        try:
            os.rmdir(tempdir)
        except OSError:
            pass

    try:
        os.mkdir(tempdir)
        return True
    except OSError:
        log('cannot create ' + tempdir)
        return False


def build_result_list(matches, root, tempdir):
    """Create one empty placeholder file per match; return name -> path."""
    mapping = {}
    for index, path in enumerate(matches):
        rel = relative_path(root, path)
        name = '%04d_%s' % (index + 1, sanitize_name(rel))
        full = os.path.join(tempdir, name)
        try:
            f = open(full, 'w')
            f.close()
        except IOError:
            log('cannot create placeholder ' + full)
            continue
        mapping[name] = path
    return mapping


def cleanup_result_list(tempdir, mapping):
    for name in mapping.keys():
        try:
            os.remove(os.path.join(tempdir, name))
        except OSError:
            pass
    try:
        os.rmdir(tempdir)
    except OSError:
        pass


def choose_result_asl_full(matches, root, truncated):
    """Build the ASL placeholder drawer, let the user choose, clean up."""
    try:
        pid = os.getpid()
    except Exception:
        pid = int(time.time()) & 0xffff
    tempdir = 'T:VFM_Grep_%d' % pid

    if not reset_temp_dir(tempdir):
        message('Cannot create the temporary result drawer:\n' + tempdir)
        return None

    mapping = build_result_list(matches, root, tempdir)
    if not mapping:
        cleanup_result_list(tempdir, mapping)
        message('Cannot create the result list.')
        return None

    try:
        title = 'VFM Grep: %d file(s)' % len(matches)
        if truncated:
            title = title + ' (first %d)' % MAX_RESULTS
        if asl is None:
            message('ASL is not available: cannot show the result list.')
            return None
        try:
            result = asl.FileRequest(title=title, drawer=tempdir,
                                     filename='', pattern='#?')
        except Exception:
            log('asl.FileRequest exception')
            return None
        if not result:
            return None
        try:
            drawer, filename = result
        except Exception:
            return None
        if not filename:
            return None

        key = filename
        for sep in ('/', ':'):
            if sep in key:
                key = key.rsplit(sep, 1)[-1]
        return mapping.get(key)
    finally:
        cleanup_result_list(tempdir, mapping)


# ===== Main =====

def main():
    log('start path=%s vfm=%s channel=%s drop=%s' %
        (START_PATH, VFM, PLUGIN_DIR, DROP_PORT))

    if not START_PATH:
        message('No active directory.')
        return

    if not PLUGIN_DIR or not DROP_PORT:
        message('The plugin command channel is not available.\n'
                'Use VFM build 11 or newer.')
        return

    if not os.path.isdir(START_PATH):
        message('The active pane path is not a directory:\n' + START_PATH)
        return

    word = ask_word()
    if not word:
        log('no search string')
        return

    log('search %r from %s' % (word, START_PATH))
    if not progress_open(False):
        progress_close()
        log('search canceled before start')
        return

    matches, truncated, canceled = find_matches(START_PATH, word, progress_update)
    progress_close()

    if canceled:
        log('search canceled')
        return

    matches.sort()
    log('matches=%d truncated=%d' % (len(matches), 1 if truncated else 0))

    if not matches:
        message('No text file contains "%s".' % word)
        return

    chosen = None
    if arexx is not None:
        status, chosen = choose_result_proaction(matches, word, truncated)
        if status == 'cancel':
            log('ProAction: cancelled')
            return
        if status == 'fail':
            log('ProAction unavailable, falling back to ASL')

    if chosen is None:
        chosen = choose_result_asl_full(matches, START_PATH, truncated)

    if not chosen:
        log('no result selected')
        return

    log('selected %r' % chosen)
    result = send_channel('REVEAL ' + chosen)
    if result is None or result[0] != 0:
        message('Cannot reveal the selected file in VagaFileManager.\n'
                'See T:Grep.log for details.')
        return

    log('revealed %r' % chosen)


if __name__ == '__main__':
    main()
    sys.exit(0)

#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: MountSSH
# VFM-Icon: connectremote   (AISS image in tbimages:, shown in the Scripts menu)
# ---------------------------------------------------------------------------
#
# Mounts the files of an SSH server (SFTP) with ssh2-handler:
#
#   1. checks that L:ssh2-handler, filesysbox.library 54.4+ and AmiSSL are
#      there, otherwise explains what is missing and quits;
#   2. asks the server: the computers of the local /24 network with the
#      SSH port (22) open are found with a quick TCP connection to each
#      address; their names come from NetBIOS when they answer it and the
#      label shows the SSH server version; a name or address (and another
#      port) can also be typed;
#   3. shows one form with everything else: user, password, key file,
#      remote folder, read only and where to save the DOSDriver;
#      Browse... chooses the server again;
#   4. writes a DOSDriver named after the server (temporary in T:, or saved
#      in DEVS:DOSDrivers or SYS:Storage/DOSDrivers) and mounts it; the
#      volume is named <server>-<user>, e.g. basebianca-vagappc, the
#      server name being its short DNS name (reverse lookup);
#   5. checks in its own process, with a time limit, that the server
#      answers, then opens it in the VFM pane the plugin was started from.
#
# With an empty password ssh2-handler asks it with its own requester, so
# it is not written in the DOSDriver.  Nothing is remembered between two
# runs.
#
# ssh2-handler Startup template (from its documentation):
#   HOSTADDR/A,PORT/N/K,USER/A,PASSWORD,VOLUME,READONLY/S,NOSSHAGENT/S,
#   KEYFILE/K,ROOTDIR/K
#
# The plugin is complete by itself: the form, chooser, mount check and
# NetBIOS helpers are the same code as in MountSamba.py.
# ---------------------------------------------------------------------------

import errno
import os
import socket
import struct
import sys
import time


# ===== Shared helpers =====
# The same code is in MountSamba.py, MountSSH.py and MountNFS.py: each
# plugin is complete by itself and needs no other file.

# ----- from Grep.py -----


try:
    import amiga
except ImportError:
    amiga = None


try:
    import asl
except ImportError:
    asl = None


START_PATH = sys.argv[2] if len(sys.argv) > 2 else ''


VFM = sys.argv[3] if len(sys.argv) > 3 else ''


DROP_PORT = sys.argv[4] if len(sys.argv) > 4 else ''


PLUGIN_DIR = sys.argv[5] if len(sys.argv) > 5 else ''


DEBUG = len(sys.argv) > 6 and sys.argv[6].upper() == 'DEBUG'


_seq = 0


def log(text, error=False):
    """Append a diagnostic line to LOG (best effort)."""
    if not (DEBUG or error):
        return
    try:
        f = open(LOG, 'a')
        f.write(('ERROR: ' if error else '') + text + '\n')
        f.close()
    except Exception:
        pass


def message(text, error=True):
    """Show a message with ASL (if available) and log it."""
    log('message: ' + text, error)
    if asl is not None:
        try:
            asl.MessageBox(TITLE, text, 'OK')
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


# ----- from MountSamba.py -----


SAVE_CHOICES = ['No', 'DEVS:DOSDrivers', 'SYS:Storage/DOSDrivers']


CONNECT_TIMEOUT = 120      # seconds: the handler may ask for a password


try:
    _PID = os.getpid()
except Exception:
    _PID = int(time.time()) & 0xffff


CHECK_SCRIPT = 'T:VFM_Mount_check_%d.script' % _PID


CHECK_DONE = 'T:VFM_Mount_check_%d.txt' % _PID


CHECK_OUT = 'T:VFM_Mount_out_%d.txt' % _PID


CHOICES_PER_PAGE = 7       # the FORM chooser holds at most 8 entries


PY3 = sys.version_info[0] >= 3


def B(text):
    """ASCII/Latin-1 literal -> byte string."""
    if PY3:
        return text.encode('iso-8859-1')
    return text


def from_latin1(data):
    if PY3:
        return data.decode('iso-8859-1')
    return data


def byte_at(data, pos):
    return struct.unpack_from('B', data, pos)[0]


NB_PORT = 137


def nb_name(name, suffix):
    """First-level encoded NetBIOS name, ready for a question."""
    if name == '*':
        raw = '*' + '\0' * 15
    else:
        raw = name.upper()[:15].ljust(15) + chr(suffix)
    encoded = ''.join([chr(65 + (ord(ch) >> 4)) + chr(65 + (ord(ch) & 15))
                       for ch in raw])
    return B('\x20' + encoded + '\x00')


def nb_packet(trans_id, flags, name, suffix, qtype):
    return struct.pack('>6H', trans_id, flags, 1, 0, 0, 0) + \
        nb_name(name, suffix) + struct.pack('>HH', qtype, 1)


def nb_skip_name(data, pos):
    """Position after an encoded (or compressed) name."""
    while pos < len(data):
        length = byte_at(data, pos)
        if length == 0:
            return pos + 1
        if length >= 0xc0:
            return pos + 2
        pos += 1 + length
    return pos


def nb_parse(data):
    """Answer -> ('NB', [ip, ...]) or ('NBSTAT', [(name, suffix, group)])."""
    if len(data) < 12:
        return None, []
    flags, qd, an = struct.unpack_from('>HHH', data, 2)
    if not (flags & 0x8000) or an == 0:
        return None, []
    pos = 12
    for i in range(qd):
        pos = nb_skip_name(data, pos) + 4
    pos = nb_skip_name(data, pos)
    if pos + 10 > len(data):
        return None, []
    rtype, rclass, ttl, rdlen = struct.unpack_from('>HHIH', data, pos)
    pos += 10
    if rtype == 0x20:
        ips = []
        end = min(pos + rdlen, len(data))
        while pos + 6 <= end:
            ips.append(socket.inet_ntoa(data[pos + 2:pos + 6]))
            pos += 6
        return 'NB', ips
    if rtype == 0x21 and pos < len(data):
        count = byte_at(data, pos)
        pos += 1
        names = []
        for i in range(count):
            if pos + 18 > len(data):
                break
            name = from_latin1(data[pos:pos + 15]).rstrip(' \0')
            suffix = byte_at(data, pos + 15)
            nflags = struct.unpack_from('>H', data, pos + 16)[0]
            names.append((name, suffix, bool(nflags & 0x8000)))
            pos += 18
        return 'NBSTAT', names
    return None, []


def local_address():
    """IP address of the Amiga on the local network, or None."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # connect() on UDP only chooses the route: nothing is sent.
            s.connect(('192.0.2.1', 9))
            ip = s.getsockname()[0]
        finally:
            s.close()
        if ip and not ip.startswith('0.') and not ip.startswith('127.'):
            return ip
    except Exception:
        pass
    try:
        ip = socket.gethostbyname(socket.gethostname())
        if not ip.startswith('127.'):
            return ip
    except Exception:
        pass
    return None


def is_ip(text):
    parts = text.split('.')
    if len(parts) != 4:
        return False
    for p in parts:
        if not p.isdigit() or int(p) > 255:
            return False
    return True


def host_for_mount(name, ip):
    """smb2-handler resolves names with DNS: keep the NetBIOS name only if
    DNS (or the hosts file) knows it, otherwise use the address."""
    try:
        if socket.gethostbyname(name) == ip:
            return name
    except Exception:
        pass
    return ip


def clean(text):
    """Form values cannot contain the separators of the form file."""
    return text.replace('|', '').replace('\n', ' ')


def choice_label(text):
    """A chooser entry: no form separators, not too long (the FORM
    command keeps at most 256 characters for all the entries)."""
    text = clean(text).replace(';', ',')
    if len(text) > 30:
        text = text[:29] + '~'
    return text


def choose(title, labels, found=None):
    """Let the user pick one of the labels; returns its index or None.
    The FORM chooser holds 8 entries, so longer lists come in pages.
    FOUND is the number shown as "found" (default: all the labels; less
    when the last label is an action like "Type a name...")."""
    if found is None:
        found = len(labels)
    page = 0
    pages = (len(labels) + CHOICES_PER_PAGE - 1) // CHOICES_PER_PAGE
    while True:
        first = page * CHOICES_PER_PAGE
        items = [choice_label(t) for t in labels[first:first + CHOICES_PER_PAGE]]
        more = page + 1 < pages
        if more:
            items.append('More...')
        info = '%d found.' % found
        if pages > 1:
            info = '%d found (page %d of %d).' % (found, page + 1, pages)
        values = run_form([
            'TITLE ' + title,
            'FIELD pick|CHOICE|Choose:|%s|0' % ';'.join(items),
            'INFO ' + info,
            'BUTTONS Use|Cancel',
        ])
        if values is None or values.get('BUTTON') != '1':
            return None
        try:
            index = int(values.get('pick', '0'))
        except ValueError:
            index = 0
        if more and index == len(items) - 1:
            page += 1
            continue
        if 0 <= index < len(items):
            return first + index
        return None


FORM_SHOWN = False


def run_form(lines):
    """Show a FORM; returns the values with BUTTON, or None on error.
    The first window opens under the mouse; the next ones open where the
    previous one was (PLACE LAST, VFM build 92), instead of following the
    pointer towards the button just pressed."""
    global FORM_SHOWN

    spec = os.path.join(PLUGIN_DIR, 'plugin.form')
    if FORM_SHOWN:
        lines = lines + ['PLACE LAST']
    FORM_SHOWN = True
    try:
        f = open(spec, 'w')
        f.write('\n'.join(lines) + '\n')
        f.close()
    except IOError:
        message('Cannot write the form file:\n' + spec)
        return None

    result = send_channel('FORM ' + spec, 600)
    if result is None or result[0] != 0:
        message('The FORM command is not available.\n'
                'This plugin needs VagaFileManager 1.1 build 91 or newer.')
        return None

    values = {}
    try:
        f = open(spec + '.out', 'r')
        for line in f.read().split('\n'):
            if '=' in line:
                key, sep, value = line.partition('=')
                values[key] = value
        f.close()
    except IOError:
        message('Cannot read the form answer.')
        return None
    for path in (spec, spec + '.out'):
        try:
            os.remove(path)
        except OSError:
            pass
    return values


def device_in_use(name):
    """True if a device, volume or assign with this name exists.

    C:Assign EXISTS only looks at the DOS list: it does not access the
    device, so no 'insert volume' requester can appear.
    """
    rc = run_command('C:Assign >NIL: %s: EXISTS' % name)
    return rc == 0


def copy_icon(path):
    """Give a saved DOSDriver the default DOSDriver icon, if there is one."""
    for src in ('ENV:Sys/def_dosdriver.info', 'ENVARC:Sys/def_dosdriver.info'):
        if os.path.exists(src):
            rc = run_command('C:Copy >NIL: "%s" TO "%s.info"' % (src, path))
            if rc == 0:
                return
    log('no default DOSDriver icon found for ' + path)


def read_text(path):
    try:
        f = open(path, 'r')
        text = f.read().strip()
        f.close()
        return text
    except IOError:
        return ''


def remove_files(*paths):
    for path in paths:
        try:
            os.remove(path)
        except OSError:
            pass


def wait_share(name):
    """None when NAME: answers, otherwise the reason as text.  The access
    runs in a separate shell so that a server that does not answer never
    blocks VFM or this plugin."""
    remove_files(CHECK_SCRIPT, CHECK_DONE, CHECK_OUT)
    try:
        f = open(CHECK_SCRIPT, 'w')
        # Why prints the reason of the last failure (IoErr of List).
        f.write('FailAt 21\n'
                'C:List >NIL: %s: QUICK\n'
                'Set vfmrc $RC\n'
                'Why >%s\n'
                'If $vfmrc GE 10 VAL\n'
                '  Echo >%s FAIL\n'
                'Else\n'
                '  Echo >%s OK\n'
                'EndIf\n' % (name, CHECK_OUT, CHECK_DONE, CHECK_DONE))
        f.close()
    except IOError:
        return 'Cannot write ' + CHECK_SCRIPT

    progress_open(False)
    send_channel('PROGRESS TEXT Connecting to %s: ...' % name, 600)
    run_command('RUN >NIL: C:Execute ' + CHECK_SCRIPT)

    answer = None
    start = time.time()
    while (time.time() - start) < CONNECT_TIMEOUT:
        time.sleep(0.5)
        answer = read_text(CHECK_DONE)
        if answer:
            break
        if not progress_update(0, 0):
            answer = 'CANCEL'
            break
    progress_close()

    reason = read_text(CHECK_OUT)
    remove_files(CHECK_SCRIPT, CHECK_DONE, CHECK_OUT)
    log('connection check: %r %r' % (answer, reason))
    if answer == 'OK':
        return None
    if answer == 'CANCEL':
        return 'Cancelled while waiting for the server.'
    if not answer:
        return 'No answer within %d seconds.' % CONNECT_TIMEOUT
    return reason or 'The share cannot be read.'


def busy(text):
    """Progress window without buttons: the network calls cannot be
    interrupted, but each one has a time limit."""
    global PROGRESS_OK

    result = send_channel('PROGRESS NOCANCEL NOBUTTONS INDET 0 0', 6000)
    PROGRESS_OK = result is not None and result[0] == 0
    if PROGRESS_OK:
        send_channel('PROGRESS TEXT ' + text, 600)


LOG = 'T:MountSSH.log'
TITLE = 'MountSSH'


HANDLER = 'L:ssh2-handler'
TEMP_DRAWER = 'T:MountSSH'
SSH_PORT = 22
SCAN_BATCH = 32            # sockets open at the same time
SCAN_WAIT = 0.8            # seconds given to each batch to answer
BANNER_WAIT = 1.0

BTN_MOUNT, BTN_BROWSE, BTN_CANCEL = '1', '2', '3'
TYPE_NAME = 'Type a name or address...'

DEFAULTS = {'host': '', 'netbios': '', 'nbhost': '', 'port': str(SSH_PORT), 'user': '',
            'keyfile': '', 'rootdir': '/', 'readonly': '0', 'save': '0'}


# ===== Requirements =====

def check_requirements():
    """True if ssh2-handler, filesysbox.library 54.4+ and AmiSSL are there."""
    missing = []
    if not os.path.exists(HANDLER):
        missing.append('L:ssh2-handler (Enhancer Software)')
    if run_command('C:Version >NIL: filesysbox.library 54 4') != 0:
        missing.append('filesysbox.library 54.4 or newer')
    if not os.path.exists('LIBS:amisslmaster.library'):
        missing.append('AmiSSL v4 (LIBS:amisslmaster.library)')
    if missing:
        message('MountSSH needs:\n\n  ' + '\n  '.join(missing) +
                '\n\nInstall them, then try again.')
        return False
    return True


# ===== Finding the SSH servers =====

def _errnos(*names):
    return tuple([getattr(errno, n) for n in names if hasattr(errno, n)])


CONNECTED = (0,) + _errnos('EISCONN')
PENDING = _errnos('EINPROGRESS', 'EALREADY', 'EWOULDBLOCK', 'EAGAIN')


def scan_batch(ips, port):
    """Addresses of the batch that accept a TCP connection on PORT.

    Non-blocking connect_ex() called again until it reports connected or
    refused: no select() needed, which the AmigaOS Python may not have."""
    pending = {}
    for ip in ips:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setblocking(0)
        try:
            rc = s.connect_ex((ip, port))
        except Exception:
            s.close()
            continue
        if rc in CONNECTED:
            pending[ip] = (s, True)
        elif rc in PENDING:
            pending[ip] = (s, False)
        else:
            s.close()

    found = []
    end = time.time() + SCAN_WAIT
    while pending and time.time() < end:
        for ip in list(pending.keys()):
            s, done = pending[ip]
            if not done:
                try:
                    rc = s.connect_ex((ip, port))
                except Exception:
                    rc = -1
                if rc in PENDING:
                    continue
                done = rc in CONNECTED
            if done:
                found.append((ip, s))
            else:
                s.close()
            del pending[ip]
        time.sleep(0.05)
    for s, done in pending.values():
        s.close()
    return found


def read_banner(s):
    """'SSH-2.0-OpenSSH_9.2p1 Debian-2' -> 'OpenSSH_9.2p1 Debian-2'."""
    try:
        s.setblocking(1)
        s.settimeout(BANNER_WAIT)
        data = from_latin1(s.recv(255))
    except Exception:
        data = ''
    try:
        s.close()
    except Exception:
        pass
    line = data.split('\n')[0].strip()
    if line.startswith('SSH-'):
        parts = line.split('-', 2)
        if len(parts) == 3:
            return parts[2]
    return line


def netbios_names(ips, wait=1.0):
    """NetBIOS computer names of the addresses that answer: {ip: name}."""
    names = {}
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.settimeout(0.2)
        packet = nb_packet(0x5353, 0, '*', 0, 0x21)
        for ip in ips:
            try:
                s.sendto(packet, (ip, NB_PORT))
            except Exception:
                pass
        end = time.time() + wait
        while time.time() < end and len(names) < len(ips):
            try:
                data, addr = s.recvfrom(1024)
            except Exception:
                continue
            kind, items = nb_parse(data)
            if kind != 'NBSTAT':
                continue
            for name, suffix, group in items:
                if suffix == 0 and not group:
                    names[addr[0]] = name
                    break
    finally:
        s.close()
    return names


def discover_ssh(port=SSH_PORT):
    """Computers of the local network with PORT open: list of
    (label, host to mount, NetBIOS name or ''), sorted by address."""
    myip = local_address()
    if not myip:
        raise Exception('the address of this computer is unknown')
    net = myip.rsplit('.', 1)[0]
    ips = ['%s.%d' % (net, n) for n in range(1, 255)]
    found = []
    for start in range(0, len(ips), SCAN_BATCH):
        found += scan_batch(ips[start:start + SCAN_BATCH], port)
    banners = {}
    for ip, s in found:
        banners[ip] = read_banner(s)
    names = netbios_names(list(banners.keys()))

    result = []
    for ip in banners.keys():
        netbios = names.get(ip, '')
        name = dns_name(ip) or netbios
        host = ip
        # The chooser keeps about 30 characters: name (or address) and the
        # first word of the server version, e.g. 'basebianca - OpenSSH_9.2p1'.
        if name:
            host = host_for_mount(name, ip)
            label = name
        else:
            label = ip
        if banners[ip]:
            label += ' - ' + banners[ip].split()[0]
        key = tuple([int(p) for p in ip.split('.')])
        result.append((key, label, host, netbios))
    result.sort()
    log('SSH servers: %r' % result)
    return [(label, host, netbios) for key, label, host, netbios in result]


# ===== Questions =====

def ask_host(port):
    """The server typed by hand; returns (host, port, '') or None."""
    values = run_form([
        'TITLE Server',
        'FIELD host|TEXT|Server:|',
        'FIELD port|TEXT|Port:|%s' % port,
        'INFO Name or IP address of the SSH server.',
        'BUTTONS OK|Cancel',
    ])
    if values is None or values.get('BUTTON') != '1':
        return None
    host = values.get('host', '').strip()
    if not host:
        return None
    return host, values.get('port', '').strip() or str(SSH_PORT), ''


def pick_server(port):
    """Choose the server; returns (host, port, NetBIOS name) or None."""
    busy('Looking for SSH servers on the network...')
    error = None
    servers = []
    try:
        try:
            servers = discover_ssh()
        except Exception:
            error = str(sys.exc_info()[1])
    finally:
        progress_close()
    if error:
        log('SSH server search failed: %s' % error, True)
    if not servers:
        message('No SSH server answered on port %d.\n\n'
                'Type its name or address.' % SSH_PORT, False)
        return ask_host(port)
    labels = [s[0] for s in servers] + [TYPE_NAME]
    index = choose('SSH servers', labels, len(servers))
    if index is None:
        return None
    if index == len(servers):
        return ask_host(port)
    label, host, netbios = servers[index]
    return host, str(SSH_PORT), netbios


def browse(current):
    """Choose the server; returns the new values or None.  User, password
    and the rest are asked in the last form."""
    picked = pick_server(current['port'])
    if not picked:
        return None
    found = dict(current)
    found['host'], found['port'], found['netbios'] = picked
    found['nbhost'] = found['host']
    return found


def ask_parameters(current, password):
    """The last form, already filled in; values with BUTTON or None."""
    values = run_form([
        'TITLE MountSSH',
        'FIELD host|TEXT|Server:|%s' % clean(current['host']),
        'FIELD port|TEXT|Port:|%s' % clean(current['port']),
        'FIELD user|TEXT|User:|%s' % clean(current['user']),
        'FIELD password|PASSWORD|Password:|%s' % clean(password),
        'FIELD keyfile|FILE|Key file:|%s' % clean(current['keyfile']),
        'FIELD rootdir|TEXT|Remote folder:|%s' % clean(current['rootdir']),
        'FIELD readonly|CHECK|Read only|%s' % current['readonly'],
        'FIELD save|CHOICE|Save:|%s|%s' % (';'.join(SAVE_CHOICES),
                                           current['save']),
        'INFO The volume is named %s-<user>.' % server_name(current),
        'INFO Empty password: ssh2-handler asks it when it connects.',
        'INFO Key file is optional.  Remote folder: / or ~ (home of the user).',
        'INFO A saved DOSDriver keeps the password in plain text.',
        'BUTTONS Mount|Browse...|Cancel',
    ])
    if values is None or values.get('BUTTON') in (None, '0', BTN_CANCEL):
        return None
    return values


def remote_folder(values):
    """'~' and '~/...' -> the home of the user on the server: /root for
    root, /home/<user> for the others (Linux servers); ssh2-handler does
    not expand ~ by itself.  Empty or / -> '' (the handler uses /)."""
    folder = values.get('rootdir', '').strip()
    if folder == '~' or folder.startswith('~/'):
        user = values.get('user', '').strip()
        if user == 'root':
            home = '/root'
        else:
            home = '/home/' + user
        folder = home + folder[1:]
    return folder.rstrip('/')


# ===== Checks and DOSDriver =====

def device_name(values):
    """The device is named after the server (see server_name):
    'gundam'; an address without a name -> 'SSH_20' (last number).
    Characters a device name cannot hold become '_'."""
    server = server_name(values)
    if not server:
        return 'SSH'
    if is_ip(server):
        return 'SSH_' + server.split('.')[-1]
    name = ''
    for c in server:
        if c.isalnum() or c in '_-':
            name += c
        else:
            name += '_'
    return name[:30] or 'SSH'


_DNS_NAMES = {}


def dns_name(ip):
    """Short DNS name of an address, like nslookup: 'gundam.vagappc.net'
    -> 'gundam'; '' if the DNS does not know it."""
    if ip not in _DNS_NAMES:
        try:
            name = socket.gethostbyaddr(ip)[0].split('.')[0]
        except Exception:
            name = ''
        if is_ip(name):
            name = ''
        _DNS_NAMES[ip] = name
    return _DNS_NAMES[ip]


def server_name(values):
    """Name of the server for the volume: the short DNS name of its
    address, else the typed name without the domain, else the NetBIOS
    name found by the search, else the address."""
    host = values.get('host', '').strip()
    if not host:
        return ''
    ip = None
    if is_ip(host):
        ip = host
    else:
        try:
            ip = socket.gethostbyname(host)
        except Exception:
            pass
    name = ''
    if ip:
        name = dns_name(ip)
    if not name and not is_ip(host):
        name = host.split('.')[0]
    if not name and values.get('nbhost') == host:
        name = values.get('netbios', '')
    return name or host


def volume_name(values):
    """'basebianca-vagappc': spaces, quotes, ':', '/' and '=' cannot be
    used in a volume name or in the Startup line, they become '_'."""
    text = '%s-%s' % (server_name(values), values.get('user', '').strip())
    for c in ' "=:/':
        text = text.replace(c, '_')
    return text


def validate(values):
    """Returns an error text, or None if the values can be used."""
    if not values.get('host', '').strip():
        return 'Enter the server (name or IP address).'
    if not values.get('user', '').strip():
        return 'Enter the user name.'
    port = values.get('port', '').strip()
    if not port.isdigit() or not (0 < int(port) < 65536):
        return 'The port must be a number (usually 22).'
    for key in ('host', 'user', 'keyfile', 'rootdir', 'password'):
        value = values.get(key, '')
        # The Startup line is parsed with ReadArgs.
        if ' ' in value.strip() or '"' in value or '=' in value:
            if key == 'password':
                return ('The password cannot contain spaces, quotes or =.\n'
                        'Leave it empty: ssh2-handler will ask it.')
            return ('The %s cannot contain spaces, quotes or = '
                    '(ssh2-handler Startup line).' % key)
    return None


def startup_line(values):
    args = [values['host'].strip(), 'PORT=' + values['port'].strip(),
            values['user'].strip()]
    if values.get('password', ''):
        args.append('PASSWORD=' + values['password'])
    args.append('VOLUME=' + volume_name(values))
    if values.get('keyfile', '').strip():
        args.append('KEYFILE=' + values['keyfile'].strip())
    if remote_folder(values):
        args.append('ROOTDIR=' + remote_folder(values))
    if values.get('readonly') == '1':
        args.append('READONLY')
    return ' '.join(args)


def masked_startup(values):
    hidden = dict(values)
    if hidden.get('password'):
        hidden['password'] = '***'
    return startup_line(hidden)


def dosdriver_path(values, name):
    try:
        choice = int(values.get('save', '0'))
    except ValueError:
        choice = 0
    if choice == 1:
        return os.path.join('DEVS:DOSDrivers', name), True
    if choice == 2:
        return os.path.join('SYS:Storage/DOSDrivers', name), True
    # The device takes its name from the file name.
    return os.path.join(TEMP_DRAWER, name), False


def mount(values, name):
    path, saved = dosdriver_path(values, name)
    if not saved and not os.path.isdir(TEMP_DRAWER):
        try:
            os.mkdir(TEMP_DRAWER)
        except OSError:
            message('Cannot create the drawer ' + TEMP_DRAWER)
            return False
    try:
        f = open(path, 'w')
        f.write('Handler   = %s\n'
                'StackSize = 65536\n'
                'Priority  = 5\n'
                'GlobVec   = -1\n'
                'Activate  = 1\n'
                'Startup   = "%s"\n' % (HANDLER, startup_line(values)))
        f.close()
    except IOError:
        message('Cannot write the DOSDriver:\n' + path)
        return False
    if saved:
        copy_icon(path)

    rc = run_command('C:Mount >%s "%s"' % (CHECK_OUT, path))
    output = read_text(CHECK_OUT)
    remove_files(CHECK_OUT)
    if rc != 0:
        log('mount %s failed, rc %s, DOSDriver %s, Startup %s, output: %s'
            % (name, rc, path, masked_startup(values), output), True)
        text = 'Mount %s: failed (return code %s).' % (name, rc)
        if output:
            text += '\n\n' + output
        message(text)
        return False
    return True


# ===== Main =====

def main():
    log('start path=%s channel=%s' % (START_PATH, PLUGIN_DIR))

    if not PLUGIN_DIR or not DROP_PORT:
        message('The plugin command channel is not available.')
        return
    if not check_requirements():
        return

    current = dict(DEFAULTS)
    password = ''
    current = browse(current)
    if not current:
        log('cancelled')
        return

    while True:
        values = ask_parameters(current, password)
        if values is None:
            log('cancelled')
            return
        for key in current:
            if key in values:
                current[key] = values[key]
        password = values.get('password', '')
        if values.get('BUTTON') == BTN_BROWSE:
            found = browse(current)
            if found:
                current = found
            continue
        error = validate(values)
        if error is None:
            name = device_name(values)
            if not device_in_use(name):
                break
            error = ('%s: is already in use (the server may be mounted '
                     'already).' % name)
        message(error, False)

    if not mount(values, name):
        return

    # ssh2-handler may show its password requester meanwhile: the check
    # waits up to MountSamba's CONNECT_TIMEOUT (2 minutes).
    reason = wait_share(name)
    if reason is not None:
        log('%s: does not answer (%s), Startup %s'
            % (name, reason, masked_startup(values)), True)
        message('%s: was mounted, but the server does not answer:\n%s\n\n'
                'Check server, port, user and password.' % (name, reason))
        return

    # Open the volume (server-user), not the device: VFM shows its name.
    volume = volume_name(values)
    log('mounted %s: as %s:' % (name, volume))
    send_channel('CD %s:' % volume)


def run_safely(entry):
    """Run the plugin; an unexpected error is written to the log and
    shown, instead of the plugin vanishing without a word."""
    try:
        entry()
    except SystemExit:
        raise
    except Exception:
        import traceback
        text = ''.join(traceback.format_exception(*sys.exc_info()))
        log('unexpected error:\n' + text, True)
        try:
            progress_close()
        except Exception:
            pass
        last = text.strip().split('\n')[-1]
        message('Unexpected error (details in %s):\n\n%s' % (LOG, last))


if __name__ == '__main__':
    run_safely(main)
    sys.exit(0)

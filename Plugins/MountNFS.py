#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: MountNFS
# VFM-Icon: connectserver   (AISS image in tbimages:, shown in the Scripts menu)
# ---------------------------------------------------------------------------
#
# Mounts an NFSv3 export with nfs-handler (Renaud Schweingruber, Aminet
# comm/net/nfs-handler_rs):
#
#   1. checks that L:nfs-handler and filesysbox.library 54.3+ are there,
#      otherwise explains what is missing and where to download it;
#   2. asks the server: the computers of the local /24 network with the
#      NFS port (2049) open are found with a quick TCP connection to each
#      address (names from DNS or NetBIOS); a name or address can also be
#      typed;
#   3. asks the export: the list comes from the server itself (portmapper
#      on TCP 111, then MOUNT v3 EXPORT, like showmount -e); a path can
#      also be typed;
#   4. shows one form, already filled in: server, export, volume name,
#      UID, GID, read only and where to save the DOSDriver; Browse...
#      starts again;
#   5. writes a DOSDriver named after the volume (temporary in T:, or saved
#      in DEVS:DOSDrivers or SYS:Storage/DOSDrivers), mounts it, checks
#      that the export answers and opens it in the VFM pane.
#
# NFS has no password: the server decides from the address of the Amiga
# and its export options.  The export needs the "insecure" option (TrueNAS:
# "Allow non-root mount"), as Amiga TCP stacks do not use privileged ports.
#
# nfs-handler Startup template (from its readme):
#   HOST/A,EXPORT,VOLUME/K,UID/K/N,GID/K/N,RSIZE/K/N,WSIZE/K/N,DEPTH/K/N,
#   TZ/K,READONLY/S
# HOST must be a dotted-quad IP address: a typed name is resolved first.
#
# The plugin is complete by itself: the form, chooser, mount check and
# NetBIOS helpers are the same code as in MountSamba.py, the network scan
# and DNS names the same as in MountSSH.py.
# ---------------------------------------------------------------------------

import errno
import os
import random
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


NUL = struct.pack('')          # empty byte string


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


def nb_resolve(name, wait=2.0):
    """IP address of a NetBIOS name (broadcast query), or None."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        except Exception:
            pass
        s.settimeout(0.2)
        trans_id = random.randint(1, 0x7fff)
        packet = nb_packet(trans_id, 0x0110, name, 0x20, 0x20)
        targets = ['255.255.255.255']
        myip = local_address()
        if myip:
            targets.append(myip.rsplit('.', 1)[0] + '.255')
        for addr in targets:
            try:
                s.sendto(packet, (addr, NB_PORT))
            except Exception:
                pass
        start = time.time()
        while time.time() - start < wait:
            try:
                data, addr = s.recvfrom(1024)
            except Exception:
                continue
            kind, items = nb_parse(data)
            if kind == 'NB' and items:
                return items[0]
    finally:
        s.close()
    return None


def is_ip(text):
    parts = text.split('.')
    if len(parts) != 4:
        return False
    for p in parts:
        if not p.isdigit() or int(p) > 255:
            return False
    return True


def resolve(host):
    """IP address of a host name, NetBIOS name or address, or None."""
    if is_ip(host):
        return host
    try:
        return socket.gethostbyname(host)
    except Exception:
        pass
    return nb_resolve(host)


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


# ----- from MountSSH.py -----


SCAN_BATCH = 32            # sockets open at the same time


SCAN_WAIT = 0.8            # seconds given to each batch to answer


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


LOG = 'T:MountNFS.log'
TITLE = 'MountNFS'


HANDLER = 'L:nfs-handler'
HANDLER_URL = 'https://aminet.net/package/comm/net/nfs-handler_rs'
FILESYSBOX_URL = 'https://aminet.net/package/util/libs/filesysbox'
TEMP_DRAWER = 'T:MountNFS'
NFS_PORT = 2049
RPC_TIMEOUT = 5

BTN_MOUNT, BTN_BROWSE, BTN_CANCEL = '1', '2', '3'
TYPE_NAME = 'Type a name or address...'
TYPE_PATH = 'Type the path...'

DEFAULTS = {'host': '', 'netbios': '', 'nbhost': '', 'export': '',
            'volume': '', 'uid': '1000', 'gid': '1000', 'readonly': '0',
            'save': '0'}


# ===== Requirements =====

def check_requirements():
    """True if nfs-handler and filesysbox.library 54.3+ are installed;
    otherwise says what is missing and where to download it."""
    missing = []
    if not os.path.exists(HANDLER):
        missing.append('L:nfs-handler\n    ' + HANDLER_URL)
    if run_command('C:Version >NIL: filesysbox.library 54 3') != 0:
        missing.append('filesysbox.library 54.3 or newer\n    ' + FILESYSBOX_URL)
    if missing:
        message('MountNFS needs:\n\n  ' + '\n  '.join(missing) +
                '\n\nInstall them, then try again.')
        return False
    return True


# ===== Finding the NFS servers =====

def discover_nfs():
    """Computers of the local network with the NFS port open: list of
    (label, host, NetBIOS name or ''), sorted by address."""
    myip = local_address()
    if not myip:
        raise Exception('the address of this computer is unknown')
    net = myip.rsplit('.', 1)[0]
    ips = ['%s.%d' % (net, n) for n in range(1, 255)]
    found = []
    for start in range(0, len(ips), SCAN_BATCH):
        for ip, s in scan_batch(ips[start:start + SCAN_BATCH], NFS_PORT):
            try:
                s.close()
            except Exception:
                pass
            found.append(ip)
    names = netbios_names(found)
    result = []
    for ip in found:
        netbios = names.get(ip, '')
        name = dns_name(ip) or netbios
        host = ip
        label = ip
        if name:
            host = host_for_mount(name, ip)
            label = '%s (%s)' % (name, ip)
        key = tuple([int(p) for p in ip.split('.')])
        result.append((key, label, host, netbios))
    result.sort()
    log('NFS servers: %r' % result)
    return [(label, host, netbios) for key, label, host, netbios in result]


# ===== Export list: Sun RPC over TCP (portmapper, MOUNT v3 EXPORT) =====

class RPCError(Exception):
    pass


def xdr_string(data, pos):
    n = struct.unpack_from('>I', data, pos)[0]
    pos += 4
    text = from_latin1(data[pos:pos + n])
    pos += (n + 3) // 4 * 4
    return text, pos


def rpc_call(ip, port, prog, vers, proc, args=None):
    """One RPC call with AUTH_NULL; returns the result bytes."""
    if args is None:
        args = NUL
    xid = random.randint(1, 0x7fffffff)
    # xid, CALL, RPC version 2, program, version, procedure,
    # credentials AUTH_NULL (flavor 0, length 0), verifier AUTH_NULL
    call = struct.pack('>10I', xid, 0, 2, prog, vers, proc, 0, 0, 0, 0) + args
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(RPC_TIMEOUT)
    try:
        try:
            s.connect((ip, port))
        except Exception:
            raise RPCError('The server does not answer on port %d (%s).'
                           % (port, sys.exc_info()[1]))
        # record marking: last fragment bit + length
        s.sendall(struct.pack('>I', 0x80000000 | len(call)) + call)
        reply = NUL
        while True:
            mark = recv_exact(s, 4)
            n = struct.unpack('>I', mark)[0]
            reply += recv_exact(s, n & 0x7fffffff)
            if n & 0x80000000:
                break
    finally:
        s.close()
    rxid, mtype, stat = struct.unpack_from('>III', reply, 0)
    if rxid != xid or mtype != 1 or stat != 0:
        raise RPCError('The server refused the request.')
    vlen = struct.unpack_from('>I', reply, 16)[0]
    pos = 20 + (vlen + 3) // 4 * 4
    accept = struct.unpack_from('>I', reply, pos)[0]
    if accept != 0:
        raise RPCError('The server cannot answer (RPC error %d).' % accept)
    return reply[pos + 4:]


def recv_exact(s, count):
    parts = []
    while count > 0:
        data = s.recv(count)
        if not data:
            raise RPCError('The server closed the connection.')
        parts.append(data)
        count -= len(data)
    return NUL.join(parts)


def list_exports(host):
    """Exported paths of the server, sorted.  Raises RPCError."""
    ip = resolve(host)
    if ip is None:
        raise RPCError('Cannot find the address of %s.' % host)
    # portmapper GETPORT: MOUNT (100005) version 3 on TCP (6)
    res = rpc_call(ip, 111, 100000, 2, 3, struct.pack('>4I', 100005, 3, 6, 0))
    port = struct.unpack_from('>I', res, 0)[0]
    if not port:
        raise RPCError('The server has no NFSv3 MOUNT service.')
    res = rpc_call(ip, port, 100005, 3, 5)
    exports = []
    pos = 0
    while struct.unpack_from('>I', res, pos)[0]:
        path, pos = xdr_string(res, pos + 4)
        # groups allowed for this export: skipped
        while struct.unpack_from('>I', res, pos)[0]:
            group, pos = xdr_string(res, pos + 4)
        pos += 4
        exports.append(path)
    exports.sort()
    log('exports of %s: %r' % (host, exports))
    return exports


# ===== Questions =====

def ask_text(title, label, info):
    values = run_form([
        'TITLE ' + title,
        'FIELD text|TEXT|%s|' % label,
        'INFO ' + info,
        'BUTTONS OK|Cancel',
    ])
    if values is None or values.get('BUTTON') != '1':
        return None
    return values.get('text', '').strip() or None


def pick_server():
    """Returns (host, NetBIOS name) or None."""
    busy('Looking for NFS servers on the network...')
    error = None
    servers = []
    try:
        try:
            servers = discover_nfs()
        except Exception:
            error = str(sys.exc_info()[1])
    finally:
        progress_close()
    if error:
        log('NFS server search failed: %s' % error, True)
    if not servers:
        message('No NFS server answered on port %d.\n\n'
                'Type its name or address.' % NFS_PORT, False)
        host = ask_text('Server', 'Server:', 'Name or IP address of the NFS server.')
        return host and (host, '')
    index = choose('NFS servers', [s[0] for s in servers] + [TYPE_NAME],
                      len(servers))
    if index is None:
        return None
    if index == len(servers):
        host = ask_text('Server', 'Server:', 'Name or IP address of the NFS server.')
        return host and (host, '')
    label, host, netbios = servers[index]
    return host, netbios


def pick_export(host):
    """Returns the exported path or None."""
    busy('Asking %s for its exports...' % host)
    error = None
    exports = []
    try:
        try:
            exports = list_exports(host)
        except Exception:
            error = str(sys.exc_info()[1])
    finally:
        progress_close()
    if error:
        log('export list of %s: %s' % (host, error), True)
        message('Cannot list the exports of %s:\n%s\n\nType the path.'
                % (host, error), False)
    if not exports:
        return ask_text('Export', 'Export:', 'Path exported by the server, e.g. /srv/amiga.')
    index = choose('Exports of ' + host, exports + [TYPE_PATH], len(exports))
    if index is None:
        return None
    if index == len(exports):
        return ask_text('Export', 'Export:', 'Path exported by the server, e.g. /srv/amiga.')
    return exports[index]


def browse(current):
    """Server, then export.  Returns the new values or None."""
    picked = pick_server()
    if not picked:
        return None
    host, netbios = picked
    export = pick_export(host)
    if not export:
        return None
    found = dict(current)
    found['host'] = host
    found['netbios'] = netbios
    found['nbhost'] = host
    found['export'] = export
    found['volume'] = default_volume(found)
    return found


def ask_parameters(current):
    values = run_form([
        'TITLE MountNFS',
        'FIELD host|TEXT|Server:|%s' % clean(current['host']),
        'FIELD export|TEXT|Export:|%s' % clean(current['export']),
        'FIELD volume|TEXT|Volume:|%s' % clean(current['volume']),
        'FIELD uid|TEXT|UID:|%s' % clean(current['uid']),
        'FIELD gid|TEXT|GID:|%s' % clean(current['gid']),
        'FIELD readonly|CHECK|Read only|%s' % current['readonly'],
        'FIELD save|CHOICE|Save:|%s|%s' % (';'.join(SAVE_CHOICES),
                                           current['save']),
        'INFO The device takes the name of the volume.',
        'INFO UID and GID: owner of the files on the server (often 1000).',
        'INFO The export needs the insecure option on the server.',
        'BUTTONS Mount|Browse...|Cancel',
    ])
    if values is None or values.get('BUTTON') in (None, '0', BTN_CANCEL):
        return None
    return values


# ===== Names, checks and DOSDriver =====

def safe_name(text):
    """A device or volume name: letters, digits, '_' and '-'."""
    name = ''
    for c in text.strip():
        if c.isalnum() or c in '_-':
            name += c
        else:
            name += '_'
    return name[:30].strip('_')


def default_volume(values):
    """'/exp/archivio' -> 'archivio'; the export '/' -> the server name."""
    export = values.get('export', '').strip().rstrip('/')
    name = export.split('/')[-1] if export else ''
    if not name:
        name = server_name(values)
    return safe_name(name) or 'NFS'


def validate(values):
    if not values.get('host', '').strip():
        return 'Enter the server (name or IP address).'
    export = values.get('export', '').strip()
    if not export.startswith('/'):
        return 'The export is a path that starts with / (e.g. /srv/amiga).'
    for key in ('host', 'export'):
        value = values.get(key, '')
        if ' ' in value.strip() or '"' in value or '=' in value:
            return ('The %s cannot contain spaces, quotes or = '
                    '(nfs-handler Startup line).' % key)
    if not safe_name(values.get('volume', '')):
        return 'Enter a volume name (letters and digits).'
    for key in ('uid', 'gid'):
        if not values.get(key, '').strip().isdigit():
            return '%s must be a number (often 1000).' % key.upper()
    return None


def startup_line(values, ip):
    args = [ip, values['export'].strip(),
            'VOLUME=' + safe_name(values['volume']),
            'UID=' + values['uid'].strip(),
            'GID=' + values['gid'].strip()]
    if values.get('readonly') == '1':
        args.append('READONLY')
    return ' '.join(args)


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


def mount(values, name, ip):
    path, saved = dosdriver_path(values, name)
    if not saved and not os.path.isdir(TEMP_DRAWER):
        try:
            os.mkdir(TEMP_DRAWER)
        except OSError:
            message('Cannot create the drawer ' + TEMP_DRAWER)
            return False
    startup = startup_line(values, ip)
    try:
        f = open(path, 'w')
        f.write('Handler   = %s\n'
                'StackSize = 65536\n'
                'Priority  = 5\n'
                'GlobVec   = -1\n'
                'Activate  = 1\n'
                'Startup   = "%s"\n' % (HANDLER, startup))
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
            % (name, rc, path, startup, output), True)
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

    current = browse(dict(DEFAULTS))
    if not current:
        log('cancelled')
        return

    while True:
        values = ask_parameters(current)
        if values is None:
            log('cancelled')
            return
        for key in current:
            if key in values:
                current[key] = values[key]
        if values.get('BUTTON') == BTN_BROWSE:
            found = browse(current)
            if found:
                current = found
            continue
        error = validate(values)
        ip = None
        if error is None:
            ip = resolve(values['host'].strip())
            if ip is None:
                error = 'Cannot find the address of %s.' % values['host'].strip()
        if error is None:
            name = safe_name(values['volume'])
            if not device_in_use(name):
                break
            error = ('%s: is already in use (the export may be mounted '
                     'already).' % name)
        message(error, False)

    if not mount(values, name, ip):
        return

    reason = wait_share(name)
    if reason is not None:
        log('%s: does not answer (%s), Startup %s'
            % (name, reason, startup_line(values, ip)), True)
        message('%s: was mounted, but the export does not answer:\n%s\n\n'
                'Check server, export and the insecure option on the server.'
                % (name, reason))
        return

    volume = safe_name(values['volume'])
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

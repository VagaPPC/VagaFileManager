#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: MountFTP
# VFM-Icon: ftp   (AISS image in tbimages:, shown in the Scripts menu)
# ---------------------------------------------------------------------------
#
# Opens an FTP or SFTP site with SFTPHandler (Bjorn Hagstrom, OS4Depot
# driver/handler/sftphandler.lha), the same way as the other Mount
# plugins:
#
#   1. checks that L:sftp-handler, filesysbox.library 54.8+ and
#      clib4.library are there (AmiSSL too for SFTP), otherwise explains
#      what is missing and where to download it;
#   2. asks the server: the computers of the local /24 network with the
#      SFTP (22) or FTP (21) port open are found with a quick TCP
#      connection to each address; the label shows the name (DNS or
#      NetBIOS), the protocol and the server version; a name can also be
#      typed;
#   3. shows one form, already filled in: site name, protocol, server,
#      port, user, password, start folder, key file and "keep the site";
#      Browse... chooses the server again;
#   4. adds the site to ENV:SFTPHandler/sites.xml (and ENVARC: to keep it
#      after a restart), written as sftphprefs writes it, the other sites
#      untouched; mounts SFTP: if it is not mounted (with the DOSDriver of
#      SFTPHandler, or a temporary one like it);
#   5. checks that SFTP:<site> answers and opens it in the VFM pane.
#
# SFTPHandler has one device, SFTP:, with one folder per site, and reads
# sites.xml again by itself within a second: no DOSDriver per server.
# With an empty password sftp-handler asks it when it connects.  A saved
# password is kept in plain text in sites.xml, as sftphprefs does.
#
# The plugin is complete by itself: the form, chooser, mount check and
# network helpers are the same code as in the other Mount plugins.
# ---------------------------------------------------------------------------

import errno
import os
import socket
import struct
import sys
import time


# ===== Shared helpers =====
# The same code is in MountSamba.py, MountSSH.py, MountNFS.py and
# MountFTP.py: each
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


LOG = 'T:MountFTP.log'
TITLE = 'MountFTP'

HANDLER = 'L:sftp-handler'
HANDLER_URL = 'https://os4depot.net/?function=showfile&file=driver/handler/sftphandler.lha'
DEVICE = 'SFTP'
DOSDRIVERS = ['DEVS:DOSDrivers/SFTP', 'SYS:Storage/DOSDrivers/SFTP']
TEMP_DRAWER = 'T:MountFTP'
SITES = 'SFTPHandler/sites.xml'           # in ENV: and ENVARC:
PROTOCOLS = ['SFTP', 'FTP']
PORTS = {'SFTP': 22, 'FTP': 21}
SCAN_BATCH = 32            # sockets open at the same time
SCAN_WAIT = 0.8            # seconds given to each batch to answer
BANNER_WAIT = 1.0
SITE_NAME_MAX = 107

BTN_MOUNT, BTN_BROWSE, BTN_CANCEL = '1', '2', '3'
TYPE_NAME = 'Type a name or address...'

DEFAULTS = {'host': '', 'netbios': '', 'nbhost': '', 'protocol': '0',
            'port': '22', 'user': '', 'folder': '/', 'keyfile': '',
            'site': '', 'proposed': '', 'keep': '0'}


# ===== Requirements =====

def check_requirements():
    """True if sftp-handler, filesysbox.library 54.8+ and clib4.library
    are installed; otherwise says what is missing and where to get it."""
    missing = []
    if not os.path.exists(HANDLER):
        missing.append('L:sftp-handler (SFTPHandler)\n    ' + HANDLER_URL)
    if run_command('C:Version >NIL: filesysbox.library 54 8') != 0:
        missing.append('filesysbox.library 54.8 or newer')
    if not os.path.exists('LIBS:clib4.library'):
        missing.append('clib4.library 2.5 or newer')
    if missing:
        message('MountFTP needs:\n\n  ' + '\n  '.join(missing) +
                '\n\nInstall them, then try again.')
        return False
    return True


def amissl_missing():
    return not os.path.exists('LIBS:amisslmaster.library')


# ===== Finding the FTP and SFTP servers =====

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


def server_banner(s, protocol):
    """First line the server sends: 'SSH-2.0-OpenSSH_9.2p1 Debian' ->
    'OpenSSH_9.2p1', '220 (vsFTPd 3.0.3)' -> '(vsFTPd'."""
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
    if protocol == 'SFTP' and line.startswith('SSH-'):
        parts = line.split('-', 2)
        line = parts[-1]
    elif protocol == 'FTP' and line[:3].isdigit():
        line = line[3:].strip(' -')
    words = line.split()
    if words:
        return words[0].strip('()[]')
    return ''


def discover_servers():
    """Computers of the local network with the SFTP (22) or FTP (21) port
    open: list of (label, host, protocol, NetBIOS name), sorted by address;
    a computer with both ports appears twice."""
    myip = local_address()
    if not myip:
        raise Exception('the address of this computer is unknown')
    net = myip.rsplit('.', 1)[0]
    ips = ['%s.%d' % (net, n) for n in range(1, 255)]
    found = []
    for protocol in PROTOCOLS:
        for start in range(0, len(ips), SCAN_BATCH):
            for ip, s in scan_batch(ips[start:start + SCAN_BATCH], PORTS[protocol]):
                found.append((ip, protocol, server_banner(s, protocol)))
    names = netbios_names(list(set([ip for ip, p, b in found])))

    result = []
    for ip, protocol, banner in found:
        netbios = names.get(ip, '')
        name = dns_name(ip) or netbios
        host = ip
        label = ip
        if name:
            host = host_for_mount(name, ip)
            label = name
        # The chooser keeps about 30 characters.
        label += ' - ' + protocol
        if banner:
            label += ' ' + banner
        key = (tuple([int(p) for p in ip.split('.')]), protocol)
        result.append((key, label, host, protocol, netbios))
    result.sort()
    log('servers: %r' % result)
    return [(label, host, protocol, netbios)
            for key, label, host, protocol, netbios in result]


# ===== Questions =====

def ask_host():
    """The server typed by hand; returns (host, protocol) or None."""
    values = run_form([
        'TITLE Server',
        'FIELD host|TEXT|Server:|',
        'FIELD protocol|CHOICE|Protocol:|%s|0' % ';'.join(PROTOCOLS),
        'INFO Name or IP address of the FTP or SFTP server.',
        'BUTTONS OK|Cancel',
    ])
    if values is None or values.get('BUTTON') != '1':
        return None
    host = values.get('host', '').strip()
    if not host:
        return None
    return host, protocol_name(values.get('protocol', '0')), ''


def pick_server():
    """Returns (host, protocol, NetBIOS name) or None."""
    busy('Looking for FTP and SFTP servers on the network...')
    error = None
    servers = []
    try:
        try:
            servers = discover_servers()
        except Exception:
            error = str(sys.exc_info()[1])
    finally:
        progress_close()
    if error:
        log('server search failed: %s' % error, True)
    if not servers:
        message('No FTP or SFTP server answered on the local network.\n\n'
                'Type its name or address.', False)
        return ask_host()
    index = choose('FTP and SFTP servers', [s[0] for s in servers] + [TYPE_NAME],
                   len(servers))
    if index is None:
        return None
    if index == len(servers):
        return ask_host()
    label, host, protocol, netbios = servers[index]
    return host, protocol, netbios


def protocol_name(index):
    try:
        return PROTOCOLS[int(index)]
    except (ValueError, IndexError):
        return PROTOCOLS[0]


def browse(current):
    """Choose the server; returns the new values or None."""
    picked = pick_server()
    if not picked:
        return None
    found = dict(current)
    found['host'], protocol, found['netbios'] = picked
    found['nbhost'] = found['host']
    found['protocol'] = str(PROTOCOLS.index(protocol))
    found['port'] = str(PORTS[protocol])
    found['site'] = ''
    return found


def ask_parameters(current, password):
    # An empty site means "propose one": <server>-<user>.  It is
    # remembered, so that main() can tell whether it was changed.
    current['proposed'] = ''
    site = current['site']
    if not site:
        site = current['proposed'] = default_site(current)
    values = run_form([
        'TITLE MountFTP',
        'FIELD site|TEXT|Site name:|%s' % clean(site),
        'FIELD protocol|CHOICE|Protocol:|%s|%s' % (';'.join(PROTOCOLS),
                                                   current['protocol']),
        'FIELD host|TEXT|Server:|%s' % clean(current['host']),
        'FIELD port|TEXT|Port:|%s' % clean(current['port']),
        'FIELD user|TEXT|User:|%s' % clean(current['user']),
        'FIELD password|PASSWORD|Password:|%s' % clean(password),
        'FIELD folder|TEXT|Start folder:|%s' % clean(current['folder']),
        'FIELD keyfile|FILE|Key file:|%s' % clean(current['keyfile']),
        'FIELD keep|CHECK|Keep the site after a restart|%s' % current['keep'],
        'INFO Opened as SFTP:<site name>.  Empty user: anonymous FTP.',
        'INFO Empty password: sftp-handler asks it when it connects.',
        'INFO Key file: SFTP only.  The password is saved in plain text.',
        'BUTTONS Mount|Browse...|Cancel',
    ])
    if values is None or values.get('BUTTON') in (None, '0', BTN_CANCEL):
        return None
    return values


# ===== Names and checks =====

def default_site(values):
    """'<server>-<user>' (like MountSSH), or '<server>' for anonymous."""
    server = server_name(values)
    user = values.get('user', '').strip()
    if user:
        return '%s-%s' % (server, user)
    return server


def site_name_problem(name):
    """The rules of sftp-handler for a site name, or None if it is fine."""
    if not name:
        return 'Enter a site name.'
    if len(name) > SITE_NAME_MAX:
        return 'The site name is too long (%d characters at most).' % SITE_NAME_MAX
    if ':' in name or '/' in name:
        return 'The site name cannot contain : or /.'
    if name != name.strip():
        return 'The site name cannot start or end with a space.'
    if name.lower().endswith('.info'):
        return 'The site name cannot end with .info.'
    for c in name:
        if ord(c) < 32 or 127 <= ord(c) <= 159:
            return 'The site name cannot contain control characters.'
    return None


def validate(values):
    error = site_name_problem(values.get('site', ''))
    if error:
        return error
    if not values.get('host', '').strip():
        return 'Enter the server (name or IP address).'
    port = values.get('port', '').strip()
    if port and (not port.isdigit() or not (0 < int(port) < 65536)):
        return 'The port must be a number (SFTP 22, FTP 21).'
    protocol = protocol_name(values.get('protocol', '0'))
    if protocol == 'SFTP' and not values.get('user', '').strip():
        return 'SFTP needs a user name.'
    if protocol == 'SFTP' and amissl_missing():
        return ('SFTP needs AmiSSL 5.1 or newer (LIBS:amisslmaster.library).\n'
                'FTP works without it.')
    return None


# ===== sites.xml (the list of sftp-handler) =====
#
# <?xml version="1.0" encoding="UTF-8"?>
# <sftphandler version="1">
#   <site name="..." protocol="sftp" host="..." port="22" user="..."
#         password="..." folder="..." keyfile="..."/>
# </sftphandler>
# Written the way sftphprefs writes it; the other sites are kept as they
# are.  The handler reads the file again within a second.

ATTRS = ['name', 'protocol', 'host', 'port', 'user', 'password', 'folder',
         'keyfile']
ENTITIES = [('&', '&amp;'), ('<', '&lt;'), ('>', '&gt;'), ('"', '&quot;'),
            ('\t', '&#9;'), ('\n', '&#10;'), ('\r', '&#13;')]


def to_utf8(text):
    """Latin-1 text of the form -> UTF-8 bytes for the file."""
    if PY3:
        return text.encode('utf-8')
    return text.decode('iso-8859-1').encode('utf-8')


def from_utf8(data):
    if PY3:
        return data.decode('utf-8', 'replace')
    return data.decode('utf-8', 'replace').encode('iso-8859-1', 'replace')


def xml_escape(text):
    for c, e in ENTITIES:
        text = text.replace(c, e)
    return text


def xml_unescape(text):
    for c, e in ENTITIES[1:]:
        text = text.replace(e, c)
    return text.replace('&apos;', "'").replace('&amp;', '&')


def read_sites(path):
    """The sites of a sites.xml: list of dicts (attribute -> text), or None
    if the file does not exist."""
    try:
        f = open(path, 'rb')
        data = from_utf8(f.read())
        f.close()
    except IOError:
        return None
    sites = []
    pos = 0
    while True:
        start = data.find('<site', pos)
        if start < 0:
            break
        end = data.find('>', start)
        if end < 0:
            break
        tag = data[start + 5:end]
        pos = end + 1
        if tag and tag[0] not in ' \t\r\n/':
            continue                    # e.g. <sites...>
        site = {}
        i = 0
        while True:
            eq = tag.find('="', i)
            if eq < 0:
                break
            name = tag[i:eq].strip()
            close = tag.find('"', eq + 2)
            if close < 0:
                break
            site[name] = xml_unescape(tag[eq + 2:close])
            i = close + 1
        if site.get('name'):
            sites.append(site)
    return sites


def write_sites(path, sites):
    """Write the list as sftphprefs does; False on error."""
    drawer = path.rsplit('/', 1)[0]
    if not os.path.isdir(drawer):
        try:
            os.mkdir(drawer)
        except OSError:
            log('cannot create ' + drawer, True)
            return False
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<sftphandler version="1">']
    for site in sites:
        text = '  <site'
        for attr in ATTRS:
            if attr == 'keyfile' and site.get('protocol', 'sftp').lower() != 'sftp':
                continue
            text += ' %s="%s"' % (attr, xml_escape(site.get(attr, '')))
        lines.append(text + '/>')
    lines.append('</sftphandler>')
    try:
        f = open(path, 'wb')
        f.write(to_utf8('\n'.join(lines) + '\n'))
        f.close()
    except IOError:
        log('cannot write ' + path, True)
        return False
    return True


def site_entry(values):
    protocol = protocol_name(values.get('protocol', '0'))
    port = values.get('port', '').strip() or str(PORTS[protocol])
    folder = values.get('folder', '').strip()
    if folder == '/':
        folder = ''                     # the folder reached after login
    return {'name': values['site'].strip(), 'protocol': protocol.lower(),
            'host': values['host'].strip(), 'port': port,
            'user': values.get('user', '').strip(),
            'password': values.get('password', ''),
            'folder': folder,
            'keyfile': values.get('keyfile', '').strip()}


def save_site(values):
    """Add (or replace) the site in ENV: and, to keep it, in ENVARC:.
    Returns an error text or None."""
    entry = site_entry(values)
    targets = ['ENV:' + SITES]
    if values.get('keep') == '1':
        targets.append('ENVARC:' + SITES)
    for path in targets:
        sites = read_sites(path)
        if sites is None and path.startswith('ENV:'):
            sites = read_sites('ENVARC:' + SITES)   # ENV: not made yet
        if sites is None:
            sites = []
        sites = [s for s in sites if s.get('name', '').lower() != entry['name'].lower()]
        sites.append(entry)
        if not write_sites(path, sites):
            return 'Cannot write ' + path
        log('site %r written to %s' % (entry['name'], path))
    return None


# ===== SFTP: device =====

def device_mounted():
    return run_command('C:Assign >NIL: %s: EXISTS' % DEVICE) == 0


def mount_device():
    """Mount SFTP: if needed, with the DOSDriver of SFTPHandler, or a
    temporary one made like it.  Returns an error text or None."""
    if device_mounted():
        return None
    path = None
    for p in DOSDRIVERS:
        if os.path.exists(p):
            path = p
            break
    if path is None:
        if not os.path.isdir(TEMP_DRAWER):
            try:
                os.mkdir(TEMP_DRAWER)
            except OSError:
                return 'Cannot create the drawer ' + TEMP_DRAWER
        path = os.path.join(TEMP_DRAWER, DEVICE)
        try:
            f = open(path, 'w')
            f.write('Handler   = %s\n'
                    'StackSize = 262144\n'
                    'Priority  = 5\n'
                    'GlobVec   = -1\n'
                    'Activate  = 1\n' % HANDLER)
            f.close()
        except IOError:
            return 'Cannot write the DOSDriver:\n' + path
    rc = run_command('C:Mount >%s "%s"' % (CHECK_OUT, path))
    output = read_text(CHECK_OUT)
    remove_files(CHECK_OUT)
    if rc != 0:
        log('mount %s: failed, rc %s, DOSDriver %s, output: %s'
            % (DEVICE, rc, path, output), True)
        text = 'Mount %s: failed (return code %s).' % (DEVICE, rc)
        if output:
            text += '\n\n' + output
        return text
    log('mounted %s: from %s' % (DEVICE, path))
    return None


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
    password = ''

    while True:
        values = ask_parameters(current, password)
        if values is None:
            log('cancelled')
            return
        for key in current:
            if key in values:
                current[key] = values[key]
        password = values.get('password', '')
        site = values.get('site', '').strip()
        if not site or site == current['proposed']:
            # the proposed name was not changed: propose it again with the
            # server and user just typed
            values['site'] = default_site(values)
            current['site'] = ''
        if values.get('BUTTON') == BTN_BROWSE:
            found = browse(current)
            if found:
                current = found
            continue
        error = validate(values)
        if error is None:
            break
        message(error, False)

    error = mount_device()
    if error is None:
        error = save_site(values)
    if error is not None:
        message(error)
        return

    site = values['site'].strip()
    path = '%s:%s' % (DEVICE, site)
    time.sleep(1.5)                     # the handler rereads sites.xml
    reason = wait_path(path)
    if reason is not None:
        log('%s does not answer (%s)' % (path, reason), True)
        message('%s does not answer:\n%s\n\n'
                'Check server, port, user and password.' % (path, reason))
        return

    log('opened ' + path)
    send_channel('CD ' + path)


def wait_path(path):
    """None when PATH (e.g. SFTP:site) answers, otherwise the reason as text.  The access
    runs in a separate shell so that a server that does not answer never
    blocks VFM or this plugin."""
    remove_files(CHECK_SCRIPT, CHECK_DONE, CHECK_OUT)
    try:
        f = open(CHECK_SCRIPT, 'w')
        # Why prints the reason of the last failure (IoErr of List).
        f.write('FailAt 21\n'
                'C:List >NIL: "%s" QUICK\n'
                'Set vfmrc $RC\n'
                'Why >%s\n'
                'If $vfmrc GE 10 VAL\n'
                '  Echo >%s FAIL\n'
                'Else\n'
                '  Echo >%s OK\n'
                'EndIf\n' % (path, CHECK_OUT, CHECK_DONE, CHECK_DONE))
        f.close()
    except IOError:
        return 'Cannot write ' + CHECK_SCRIPT

    progress_open(False)
    send_channel('PROGRESS TEXT Connecting to %s ...' % path, 600)
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
    return reason or 'The site cannot be read.'


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

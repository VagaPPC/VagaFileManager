#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: SambaMount
# ---------------------------------------------------------------------------
#
# Mounts an SMB2/3 (Samba, Windows, NAS) share with smb2-handler:
#
#   1. checks that L:smb2-handler and filesysbox.library 54.4+ are there,
#      otherwise explains what is missing and quits;
#   2. browses the network at once, asking one thing at a time (pure
#      Python, socket):
#      a. the server: the computers that share files are found with
#         NetBIOS (UDP 137: a broadcast name query plus a node status
#         query to every address of the local /24 network); a name can
#         also be typed;
#      b. the share: the plugin logs in to the server (SMB 2.0/2.1 on TCP
#         445, NTLMv2, or anonymously first) and lists its disk shares
#         through the srvsvc pipe (DCE/RPC NetrShareEnum); if the server
#         wants a login, user and password are asked before the list;
#      c. user and password for the mount;
#   3. shows one VFM form (FORM command), already filled in, to check
#      host, share, user, password, read only, guest and where to save;
#      its Browse... button starts step 2 again;
#   4. writes a DOSDriver named after the share (temporary in T:, or saved
#      in DEVS:DOSDrivers or SYS:Storage/DOSDrivers) and mounts it with
#      C:Mount: share Archivio -> device Archivio:;
#   5. checks in its own process, with a time limit, that the share
#      answers, so VFM never waits on a server that does not respond;
#   6. opens the share in the VFM pane the plugin was started from.
#
# Nothing is remembered between two runs.  A saved DOSDriver keeps the
# password in plain text, as smb2-handler needs it in its Startup line.
#
# smb2-handler Startup template (from its README):
#   URL/A,USER,PASSWORD,VOLUME,READONLY/S,NOPASSWORDREQ/S
# URL: smb://[<domain>;][<user>[:<password>]@]<host>[:<port>]/<share>/<path>
#
# The channel, log and message helpers come from Grep.py (same drawer).
# The plugin receives the usual arguments (see Grep.py), argv[5] being the
# plugin session directory.
# ---------------------------------------------------------------------------

import binascii
import hashlib
import hmac
import os
import random
import socket
import struct
import sys
import time

import Grep as G

G.LOG = 'T:SambaMount.log'
G.TITLE = 'SambaMount'

log = G.log
message = G.message

HANDLER = 'L:smb2-handler'
SAVE_CHOICES = ['No', 'DEVS:DOSDrivers', 'SYS:Storage/DOSDrivers']
CONNECT_TIMEOUT = 120      # seconds: smb2-handler may ask for a password
TEMP_DRAWER = 'T:SambaMount'

try:
    _PID = os.getpid()
except Exception:
    _PID = int(time.time()) & 0xffff

CHECK_SCRIPT = 'T:VFM_SambaMount_check_%d.script' % _PID
CHECK_DONE = 'T:VFM_SambaMount_check_%d.txt' % _PID
CHECK_OUT = 'T:VFM_SambaMount_out_%d.txt' % _PID

DISCOVER_TIME = 3.0        # seconds spent listening for NetBIOS answers
SMB_TIMEOUT = 10           # seconds for each network operation
CHOICES_PER_PAGE = 7       # the FORM chooser holds at most 8 entries


# ===== Byte helpers (Python 2.5 and Python 3) =====
#
# Python 2.5 has no b'' literals and no bytes type: its str already holds
# bytes.  These helpers build and read byte strings the same way on both.

PY3 = sys.version_info[0] >= 3
NUL = struct.pack('')          # empty byte string


def B(text):
    """ASCII/Latin-1 literal -> byte string."""
    if PY3:
        return text.encode('iso-8859-1')
    return text


def utf16(text):
    """Text typed in the form (Latin-1 on AmigaOS) -> UTF-16LE bytes."""
    if PY3:
        return text.encode('utf-16-le')
    return text.decode('iso-8859-1').encode('utf-16-le')


def from_utf16(data):
    text = data.decode('utf-16-le', 'replace')
    if PY3:
        return text
    return text.encode('iso-8859-1', 'replace')


def from_latin1(data):
    if PY3:
        return data.decode('iso-8859-1')
    return data


def byte_at(data, pos):
    return struct.unpack_from('B', data, pos)[0]


def random_bytes(count):
    return struct.pack('%dB' % count,
                       *[random.randint(0, 255) for i in range(count)])


def hmac_md5(key, data):
    return hmac.new(key, data, hashlib.md5).digest()


def md4(data):
    """MD4 (RFC 1320): the NT password hash.  hashlib often lacks it."""
    mask = 0xffffffff

    def rol(x, n):
        x &= mask
        return ((x << n) | (x >> (32 - n))) & mask

    length = len(data)
    data = data + B('\x80') + B('\x00') * ((55 - length) % 64) + \
        struct.pack('<Q', length * 8)
    a, b, c, d = 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476
    for start in range(0, len(data), 64):
        x = struct.unpack('<16I', data[start:start + 64])
        aa, bb, cc, dd = a, b, c, d
        for i in (0, 4, 8, 12):
            a = rol(a + ((b & c) | (~b & d)) + x[i], 3)
            d = rol(d + ((a & b) | (~a & c)) + x[i + 1], 7)
            c = rol(c + ((d & a) | (~d & b)) + x[i + 2], 11)
            b = rol(b + ((c & d) | (~c & a)) + x[i + 3], 19)
        for i in (0, 1, 2, 3):
            a = rol(a + ((b & c) | (b & d) | (c & d)) + x[i] + 0x5a827999, 3)
            d = rol(d + ((a & b) | (a & c) | (b & c)) + x[i + 4] + 0x5a827999, 5)
            c = rol(c + ((d & a) | (d & b) | (a & b)) + x[i + 8] + 0x5a827999, 9)
            b = rol(b + ((c & d) | (c & a) | (d & a)) + x[i + 12] + 0x5a827999, 13)
        for i in (0, 2, 1, 3):
            a = rol(a + (b ^ c ^ d) + x[i] + 0x6ed9eba1, 3)
            d = rol(d + (a ^ b ^ c) + x[i + 8] + 0x6ed9eba1, 9)
            c = rol(c + (d ^ a ^ b) + x[i + 4] + 0x6ed9eba1, 11)
            b = rol(b + (c ^ d ^ a) + x[i + 12] + 0x6ed9eba1, 15)
        a = (a + aa) & mask
        b = (b + bb) & mask
        c = (c + cc) & mask
        d = (d + dd) & mask
    return struct.pack('<4I', a, b, c, d)


# ===== Finding the servers (NetBIOS, UDP port 137) =====
#
# Two ways at the same time, then the answers are collected for a few
# seconds:
#   - a broadcast name query for '*' (Samba and most NAS answer it);
#   - a node status query sent to every address of the local /24 network
#     (Windows answers it even when it ignores the broadcast).
# The node status answer lists the NetBIOS names of the machine: the ones
# with suffix <20> belong to machines that share files.

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


def discover_servers(wait=DISCOVER_TIME):
    """Machines that share files: list of (name, ip), sorted by name."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    except Exception:
        log('SO_BROADCAST not available', True)
    s.settimeout(0.2)

    myip = local_address()
    log('local address: %s' % myip)
    trans_id = random.randint(1, 0x7fff)
    query = nb_packet(trans_id, 0x0110, '*', 0, 0x20)
    status = nb_packet(trans_id, 0x0000, '*', 0, 0x21)

    targets = ['255.255.255.255']
    if myip:
        net = myip.rsplit('.', 1)[0]
        targets.append(net + '.255')
    for addr in targets:
        try:
            s.sendto(query, (addr, NB_PORT))
        except Exception:
            log('name query to %s failed: %s' % (addr, sys.exc_info()[1]))

    # Sending to 253 addresses can itself take a few seconds (the stack
    # waits for ARP): count the waiting time from here, but listen at
    # least 1.5 seconds after the last packet.
    start = time.time()
    asked = {}
    if myip:
        net = myip.rsplit('.', 1)[0]
        for n in range(1, 255):
            ip = '%s.%d' % (net, n)
            if ip == myip:
                continue
            try:
                s.sendto(status, (ip, NB_PORT))
                asked[ip] = True
            except Exception:
                pass

    found = {}
    end = max(start + wait, time.time() + 1.5)
    while time.time() < end:
        try:
            data, addr = s.recvfrom(1024)
        except socket.timeout:
            continue
        except Exception:
            # Some stacks report ICMP "port unreachable" here: ignore it.
            continue
        kind, items = nb_parse(data)
        ip = addr[0]
        if kind == 'NB':
            if ip not in asked and ip != myip:
                try:
                    s.sendto(status, (ip, NB_PORT))
                    asked[ip] = True
                except Exception:
                    pass
        elif kind == 'NBSTAT':
            server = None
            for name, suffix, group in items:
                if suffix == 0x20 and not group:
                    server = name
                    break
            if server and ip != myip:
                found[ip] = server
    s.close()

    result = [(name, ip) for ip, name in found.items()]
    result.sort()
    log('servers found: %r' % result)
    return result


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


# ===== NTLMv2 authentication, wrapped in SPNEGO =====

NTLM_FLAGS = (0x00000001 |     # UNICODE
              0x00000004 |     # REQUEST_TARGET
              0x00000010 |     # SIGN
              0x00000200 |     # NTLM
              0x00008000 |     # ALWAYS_SIGN
              0x00080000 |     # EXTENDED_SESSIONSECURITY
              0x00800000 |     # TARGET_INFO
              0x20000000 |     # 128
              0x80000000)      # 56
NTLM_ANONYMOUS = 0x00000800
NTLM_SIG = B('NTLMSSP\0')
OID_SPNEGO = B('\x06\x06\x2b\x06\x01\x05\x05\x02')
OID_NTLMSSP = B('\x06\x0a\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a')


def der(tag, content):
    n = len(content)
    if n < 0x80:
        length = struct.pack('B', n)
    elif n < 0x100:
        length = struct.pack('BB', 0x81, n)
    else:
        length = struct.pack('>BH', 0x82, n)
    return struct.pack('B', tag) + length + content


def spnego_init(token):
    mechs = der(0xa0, der(0x30, OID_NTLMSSP))
    return der(0x60, OID_SPNEGO +
               der(0xa0, der(0x30, mechs + der(0xa2, der(0x04, token)))))


def spnego_response(token):
    return der(0xa1, der(0x30, der(0xa2, der(0x04, token))))


def ntlm_negotiate():
    return NTLM_SIG + struct.pack('<II', 1, NTLM_FLAGS) + \
        struct.pack('<HHIHHI', 0, 0, 0, 0, 0, 0)


def ntlm_parse_challenge(blob):
    pos = blob.find(NTLM_SIG)
    if pos < 0:
        raise SMBError('the server sent no NTLM challenge')
    blob = blob[pos:]
    if len(blob) < 48 or struct.unpack_from('<I', blob, 8)[0] != 2:
        raise SMBError('bad NTLM challenge')
    flags = struct.unpack_from('<I', blob, 20)[0]
    challenge = blob[24:32]
    tlen, tmax, toff = struct.unpack_from('<HHI', blob, 40)
    return flags, challenge, blob[toff:toff + tlen]


def av_timestamp(target_info):
    """MsvAvTimestamp of the challenge, or None."""
    pos = 0
    while pos + 4 <= len(target_info):
        av_id, av_len = struct.unpack_from('<HH', target_info, pos)
        if av_id == 0:
            break
        if av_id == 7 and av_len == 8:
            return target_info[pos + 4:pos + 12]
        pos += 4 + av_len
    return None


def ntowfv2(user, password, domain):
    return hmac_md5(md4(utf16(password)), utf16(user.upper() + domain))


def ntlm_authenticate(challenge_blob, user, password, domain):
    """Returns (AUTHENTICATE message, session key or None)."""
    flags, challenge, target_info = ntlm_parse_challenge(challenge_blob)
    flags = (flags & NTLM_FLAGS) | 0x00000001

    if not user and not password:
        flags |= NTLM_ANONYMOUS
        lm, nt, key = B('\0'), NUL, None
    else:
        v2 = ntowfv2(user, password, domain)
        client = random_bytes(8)
        stamp = av_timestamp(target_info)
        if stamp is None:
            now = int((time.time() + 11644473600) * 10000000)
            ts = struct.pack('<Q', now)
        else:
            ts = stamp
        blob = struct.pack('<BBHI', 1, 1, 0, 0) + ts + client + \
            struct.pack('<I', 0) + target_info + struct.pack('<I', 0)
        proof = hmac_md5(v2, challenge + blob)
        nt = proof + blob
        if stamp is None:
            lm = hmac_md5(v2, challenge + client) + client
        else:
            lm = B('\0') * 24
        key = hmac_md5(v2, proof)

    fields = [utf16(domain), utf16(user), utf16('AMIGA'), lm, nt, NUL]
    order = [3, 4, 0, 1, 2, 5]      # header order: LM, NT, domain, user, ws, key
    offset = 64
    offsets = []
    for f in fields:
        offsets.append(offset)
        offset += len(f)
    header = NTLM_SIG + struct.pack('<I', 3)
    for i in order:
        header += struct.pack('<HHI', len(fields[i]), len(fields[i]), offsets[i])
    header += struct.pack('<I', flags)
    return header + NUL.join(fields), key


# ===== SMB2 client (just enough to list the shares) =====

STATUS_OK = 0
STATUS_PENDING = 0x00000103
STATUS_BUFFER_OVERFLOW = 0x80000005
STATUS_MORE_PROCESSING = 0xc0000016

STATUS_TEXT = {
    0xc000006d: 'User or password not accepted.',
    0xc000006e: 'This account cannot log in now.',
    0xc0000064: 'Unknown user.',
    0xc0000072: 'This account is disabled.',
    0xc0000022: 'Access denied.',
    0xc00000cc: 'The server has no such share.',
    0xc0000034: 'The server cannot list its shares (srvsvc).',
}


class SMBError(Exception):
    def __init__(self, text, status=None):
        Exception.__init__(self, text)
        self.status = status


def status_error(status, what):
    text = STATUS_TEXT.get(status)
    if text is None:
        text = '%s failed (status 0x%08X).' % (what, status)
    return SMBError(text, status)


class SMB2(object):
    NEGOTIATE, SESSION_SETUP, LOGOFF, TREE_CONNECT, TREE_DISCONNECT, \
        CREATE, CLOSE, READ, WRITE = 0, 1, 2, 3, 4, 5, 6, 8, 9

    def __init__(self, ip, timeout=SMB_TIMEOUT):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        try:
            self.sock.connect((ip, 445))
        except Exception:
            self.sock.close()
            raise SMBError('The server does not answer on port 445 '
                           '(%s).' % sys.exc_info()[1])
        self.mid = 0
        self.session = 0
        self.tree = 0
        self.dialect = 0
        self.sign_required = False
        self.sign_key = None

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def _recv_exact(self, count):
        parts = []
        while count > 0:
            data = self.sock.recv(count)
            if not data:
                raise SMBError('The server closed the connection.')
            parts.append(data)
            count -= len(data)
        return NUL.join(parts)

    def _send(self, command, body):
        charge = 1
        if self.dialect in (0, 0x0202):
            charge = 0
        flags = 0
        if self.sign_key is not None:
            flags |= 0x00000008
        header = struct.pack('<4sHHIHHIIQIIQ16s', B('\xfeSMB'), 64, charge,
                             0, command, 64, flags, 0, self.mid, 0,
                             self.tree, self.session, B('\0') * 16)
        msg = header + body
        if self.sign_key is not None:
            sig = hmac.new(self.sign_key, msg, hashlib.sha256).digest()[:16]
            msg = msg[:48] + sig + msg[64:]
        self.mid += max(charge, 1)
        self.sock.sendall(struct.pack('>I', len(msg)) + msg)

    def _recv(self):
        while True:
            length = struct.unpack('>I', self._recv_exact(4))[0] & 0xffffff
            msg = self._recv_exact(length)
            if msg[:4] != B('\xfeSMB'):
                raise SMBError('The server does not speak SMB2.')
            status, = struct.unpack_from('<I', msg, 8)
            flags, = struct.unpack_from('<I', msg, 16)
            if status == STATUS_PENDING and (flags & 2):
                continue        # interim answer: the real one follows
            return status, msg

    def request(self, command, body, what, accept=(STATUS_OK,)):
        self._send(command, body)
        status, msg = self._recv()
        if status not in accept:
            raise status_error(status, what)
        return status, msg

    def negotiate(self):
        guid = random_bytes(16)
        body = struct.pack('<HHHHI16sQ', 36, 2, 1, 0, 0, guid, 0) + \
            struct.pack('<HH', 0x0202, 0x0210)
        try:
            status, msg = self.request(self.NEGOTIATE, body, 'Negotiate')
        except SMBError:
            raise SMBError('The server does not accept SMB 2.0/2.1 '
                           '(it may allow only SMB3).')
        security, self.dialect = struct.unpack_from('<HH', msg, 66)
        self.sign_required = bool(security & 2)
        log('dialect 0x%04x, signing required: %s'
            % (self.dialect, self.sign_required))

    def session_setup(self, user, password, domain):
        body_fmt = '<HBBIIHHQ'

        def setup(token):
            body = struct.pack(body_fmt, 25, 0, 1, 0, 0, 64 + 24,
                               len(token), 0) + token
            return self.request(self.SESSION_SETUP, body, 'Login',
                                (STATUS_OK, STATUS_MORE_PROCESSING))

        status, msg = setup(spnego_init(ntlm_negotiate()))
        if status != STATUS_MORE_PROCESSING:
            raise SMBError('The server did not ask for a password.')
        self.session, = struct.unpack_from('<Q', msg, 40)
        off, length = struct.unpack_from('<HH', msg, 68)
        auth, key = ntlm_authenticate(msg[off:off + length],
                                      user, password, domain)
        status, msg = setup(spnego_response(auth))
        session_flags, = struct.unpack_from('<H', msg, 66)
        log('logged in, session flags 0x%x' % session_flags)
        # Guest and anonymous sessions are never signed.
        if self.sign_required and key is not None and not (session_flags & 3):
            self.sign_key = key

    def tree_connect(self, path):
        name = utf16(path)
        body = struct.pack('<HHHH', 9, 0, 72, len(name)) + name
        status, msg = self.request(self.TREE_CONNECT, body, 'Connect to IPC$')
        self.tree, = struct.unpack_from('<I', msg, 36)

    def create_pipe(self, name):
        n = utf16(name)
        body = struct.pack('<HBBIQQIIIIIHHII', 57, 0, 0, 2, 0, 0,
                           0x0012019f, 0, 7, 1, 0, 120, len(n), 0, 0) + n
        status, msg = self.request(self.CREATE, body, 'Open ' + name)
        return msg[64 + 64:64 + 80]

    def write(self, fid, data):
        body = struct.pack('<HHIQ16sIIHHI', 49, 112, len(data), 0, fid,
                           0, 0, 0, 0, 0) + data
        self.request(self.WRITE, body, 'Write')

    def read(self, fid, length=65536):
        body = struct.pack('<HBBIQ16sIIIHHB', 49, 0x50, 0, length, 0, fid,
                           0, 0, 0, 0, 0, 0)
        status, msg = self.request(self.READ, body, 'Read',
                                   (STATUS_OK, STATUS_BUFFER_OVERFLOW))
        off = byte_at(msg, 66)
        count, = struct.unpack_from('<I', msg, 68)
        return msg[off:off + count]

    def close_file(self, fid):
        self.request(self.CLOSE, struct.pack('<HHI16s', 24, 0, 0, fid),
                     'Close')

    def logoff(self):
        try:
            if self.tree:
                self.request(self.TREE_DISCONNECT, struct.pack('<HH', 4, 0),
                             'Disconnect')
            self.request(self.LOGOFF, struct.pack('<HH', 4, 0), 'Logoff')
        except Exception:
            pass


# ===== DCE/RPC over the srvsvc pipe: NetrShareEnum =====

SRVSVC_UUID = '4b324fc8-1670-01d3-1278-5a47bf6ee188'
NDR_UUID = '8a885d04-1ceb-11c9-9fe8-08002b104860'


def uuid_bytes(text):
    p = text.split('-')
    return struct.pack('<IHH', int(p[0], 16), int(p[1], 16), int(p[2], 16)) + \
        binascii.unhexlify(p[3] + p[4])


def rpc_pdu(ptype, call_id, body):
    return struct.pack('<BBBB4sHHI', 5, 0, ptype, 3, B('\x10\0\0\0'),
                       16 + len(body), 0, call_id) + body


def rpc_call(smb, fid, pdu):
    """Send a PDU, return the fragments of the answer (all of them)."""
    smb.write(fid, pdu)
    buf = NUL
    frags = []
    while True:
        while len(buf) < 16 or \
                len(buf) < struct.unpack_from('<H', buf, 8)[0]:
            data = smb.read(fid)
            if not data:
                raise SMBError('Empty answer from the srvsvc pipe.')
            buf += data
        flen = struct.unpack_from('<H', buf, 8)[0]
        frag, buf = buf[:flen], buf[flen:]
        frags.append(frag)
        if byte_at(frag, 3) & 2:        # last fragment
            return frags


def ndr_string(text):
    data = utf16(text + '\0')
    count = len(text) + 1
    out = struct.pack('<III', count, 0, count) + data
    return out + B('\0') * ((4 - len(out) % 4) % 4)


def ndr_read_string(data, pos):
    maxc, offset, count = struct.unpack_from('<III', data, pos)
    pos += 12
    text = from_utf16(data[pos:pos + count * 2]).rstrip('\0')
    pos += count * 2
    pos += (4 - pos % 4) % 4
    return text, pos


def parse_share_enum(stub):
    level, switch, ptr = struct.unpack_from('<III', stub, 0)
    pos = 12
    if not ptr:
        return []
    count, array_ptr = struct.unpack_from('<II', stub, pos)
    pos += 8
    if not array_ptr or count == 0:
        return []
    pos += 4                             # conformant array: max count
    entries = []
    for i in range(count):
        entries.append(struct.unpack_from('<III', stub, pos))
        pos += 12
    shares = []
    for name_ptr, stype, remark_ptr in entries:
        name = remark = ''
        if name_ptr:
            name, pos = ndr_read_string(stub, pos)
        if remark_ptr:
            remark, pos = ndr_read_string(stub, pos)
        shares.append((name, stype, remark))
    return shares


def enum_shares(smb, host):
    fid = smb.create_pipe('srvsvc')
    try:
        bind = struct.pack('<HHIB3xHBx', 4280, 4280, 0, 1, 0, 1) + \
            uuid_bytes(SRVSVC_UUID) + struct.pack('<HH', 3, 0) + \
            uuid_bytes(NDR_UUID) + struct.pack('<I', 2)
        ack = rpc_call(smb, fid, rpc_pdu(11, 1, bind))[0]
        if byte_at(ack, 2) != 12:
            raise SMBError('The server refused the srvsvc service.')

        stub = struct.pack('<I', 0x00020000) + ndr_string('\\\\' + host) + \
            struct.pack('<IIIII', 1, 1, 0x00020004, 0, 0) + \
            struct.pack('<III', 0xffffffff, 0x00020008, 0)
        request = struct.pack('<IHH', len(stub), 0, 15) + stub
        answer = NUL
        for frag in rpc_call(smb, fid, rpc_pdu(0, 2, request)):
            ptype = byte_at(frag, 2)
            if ptype == 3:
                raise SMBError('The server refused to list the shares '
                               '(fault 0x%08X).'
                               % struct.unpack_from('<I', frag, 24)[0])
            if ptype != 2:
                raise SMBError('Unexpected answer from srvsvc.')
            answer += frag[24:]
    finally:
        try:
            smb.close_file(fid)
        except Exception:
            pass
    status, = struct.unpack_from('<I', answer, len(answer) - 4)
    if status != 0:
        raise status_error(status, 'Share list')
    return parse_share_enum(answer)


def split_user(user):
    """'DOMAIN;user' or 'DOMAIN\\user' -> (user, domain)."""
    for sep in (';', '\\'):
        if sep in user:
            domain, name = user.split(sep, 1)
            return name, domain
    return user, ''


def list_shares(host, user, password):
    """Disk shares of a server: list of (name, remark).  Hidden shares
    (ending with $) and printers are left out.  Raises SMBError."""
    ip = resolve(host)
    if ip is None:
        raise SMBError('Cannot find the address of %s.' % host)
    log('listing shares of %s (%s) as %r' % (host, ip, user))
    name, domain = split_user(user)
    smb = SMB2(ip)
    try:
        smb.negotiate()
        smb.session_setup(name, password, domain)
        smb.tree_connect('\\\\%s\\IPC$' % host)
        shares = enum_shares(smb, host)
        smb.logoff()
    finally:
        smb.close()
    result = []
    for sname, stype, remark in shares:
        if (stype & 0x0fffffff) == 0 and not (stype & 0x80000000) \
                and not sname.endswith('$'):
            result.append((sname, remark))
    log('shares: %r' % result)
    return result


# ===== Requirements =====

def check_requirements():
    """True if smb2-handler and filesysbox.library 54.4+ are installed."""
    missing = []
    if not os.path.exists(HANDLER):
        missing.append('L:smb2-handler (smb2fs package)')
    # C:Version returns 0 if the library is at least 54.4, 5 if older and
    # an error if it is not found.
    rc = G.run_command('C:Version >NIL: filesysbox.library 54 4')
    if rc != 0:
        missing.append('filesysbox.library 54.4 or newer')
    if missing:
        message('SambaMount needs:\n\n  ' + '\n  '.join(missing) +
                '\n\nInstall smb2fs and filesysbox, then try again.')
        return False
    return True


DEFAULTS = {'host': '', 'share': '', 'user': '',
            'readonly': '0', 'guest': '0', 'save': '0'}


# ===== Form =====

def clean(text):
    """Form values cannot contain the separators of the form file."""
    return text.replace('|', '').replace('\n', ' ')


BTN_MOUNT, BTN_BROWSE, BTN_CANCEL = '1', '2', '3'


def ask_parameters(current, password):
    """Show the main form; returns the values (with BUTTON) or None if
    the window was closed or Cancel was pressed.  The password is kept
    only in memory, so it is not lost when Browse shows the form again."""
    lines = [
        'TITLE SambaMount',
        'FIELD host|TEXT|Host:|%s' % clean(current['host']),
        'FIELD share|TEXT|Share:|%s' % clean(current['share']),
        'FIELD user|TEXT|User:|%s' % clean(current['user']),
        'FIELD password|PASSWORD|Password:|%s' % clean(password),
        'FIELD readonly|CHECK|Read only|%s' % current['readonly'],
        'FIELD guest|CHECK|Guest (no password)|%s' % current['guest'],
        'FIELD save|CHOICE|Save:|%s|%s' % (';'.join(SAVE_CHOICES), current['save']),
        'INFO The device takes the name of the share%s.' % device_hint(current),
        'INFO Browse... chooses server, share, user and password again.',
        'INFO A saved DOSDriver keeps the password in plain text.',
        'BUTTONS Mount|Browse...|Cancel',
    ]
    values = run_form(lines)
    if values is None or values.get('BUTTON') in (None, '0', BTN_CANCEL):
        return None
    return values


def choice_label(text):
    """A chooser entry: no form separators, not too long (the FORM
    command keeps at most 256 characters for all the entries)."""
    text = clean(text).replace(';', ',')
    if len(text) > 30:
        text = text[:29] + '~'
    return text


def choose(title, labels):
    """Let the user pick one of the labels; returns its index or None.
    The FORM chooser holds 8 entries, so longer lists come in pages."""
    page = 0
    pages = (len(labels) + CHOICES_PER_PAGE - 1) // CHOICES_PER_PAGE
    while True:
        first = page * CHOICES_PER_PAGE
        items = [choice_label(t) for t in labels[first:first + CHOICES_PER_PAGE]]
        more = page + 1 < pages
        if more:
            items.append('More...')
        info = '%d found.' % len(labels)
        if pages > 1:
            info = '%d found (page %d of %d).' % (len(labels), page + 1, pages)
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

    spec = os.path.join(G.PLUGIN_DIR, 'sambamount.form')
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

    result = G.send_channel('FORM ' + spec, 600)
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


# ===== Checks on the values =====

def device_name(share):
    """The device is named after the share: 'Archivio' -> 'Archivio'.
    Characters a device name cannot hold become '_'; None if empty."""
    name = ''
    for c in share.strip().strip('/'):
        if c.isalnum() or c in '_-':
            name += c
        else:
            name += '_'
    name = name[:30]
    if not name.strip('_'):
        return None
    return name


def device_hint(current):
    name = device_name(current.get('share', ''))
    if name:
        return ' (%s:)' % name
    return ''


def device_in_use(name):
    """True if a device, volume or assign with this name exists.

    C:Assign EXISTS only looks at the DOS list: it does not access the
    device, so no 'insert volume' requester can appear.
    """
    rc = G.run_command('C:Assign >NIL: %s: EXISTS' % name)
    return rc == 0


def validate(values):
    """Returns an error text, or None if the values can be used."""
    if not values.get('host', '').strip():
        return 'Enter the host (name or IP address of the server).'
    if not device_name(values.get('share', '')):
        return 'Enter the name of the share.'
    for key in ('host', 'share', 'user', 'password'):
        value = values.get(key, '')
        # The Startup line is parsed with ReadArgs: spaces and quotes would
        # split or break the arguments.
        if ' ' in value.strip() or '"' in value:
            return ('The %s cannot contain spaces or quotes '
                    '(smb2-handler Startup line).' % key)
    # User and password are written inside the URL (user:password@host),
    # so the characters that separate its parts cannot be used.
    for key, bad in (('user', ':@/'), ('password', '@/'), ('host', '@/')):
        for c in bad:
            if c in values.get(key, ''):
                return 'The %s cannot contain the character %s.' % (key, c)
    return None


# ===== DOSDriver and mount =====

def startup_line(values):
    """User and password go inside the URL:
    smb://user:password@HOST/share"""
    user = values.get('user', '').strip()
    password = values.get('password', '')
    if values.get('guest') == '1':
        password = ''
    login = ''
    if user:
        login = user
        if password:
            login += ':' + password
        login += '@'
    url = 'smb://%s%s/%s' % (login, values['host'].strip(),
                             values['share'].strip().strip('/'))
    args = [url]
    if values.get('readonly') == '1':
        args.append('READONLY')
    if values.get('guest') == '1':
        args.append('NOPASSWORDREQ')
    return ' '.join(args)


def dosdriver_text(values):
    return ('Handler   = %s\n'
            'StackSize = 65536\n'
            'Priority  = 5\n'
            'GlobVec   = -1\n'
            'Activate  = 1\n'
            'Startup   = "%s"\n') % (HANDLER, startup_line(values))


def dosdriver_path(values, name):
    """Where to write the DOSDriver: T: or the chosen drawer."""
    try:
        choice = int(values.get('save', '0'))
    except ValueError:
        choice = 0
    if choice == 1:
        return os.path.join('DEVS:DOSDrivers', name), True
    if choice == 2:
        return os.path.join('SYS:Storage/DOSDrivers', name), True
    # A DOSDriver takes the device name from its file name, so the
    # temporary one goes in its own drawer with exactly that name.
    return os.path.join(TEMP_DRAWER, name), False


def copy_icon(path):
    """Give a saved DOSDriver the default DOSDriver icon, if there is one."""
    for src in ('ENV:Sys/def_dosdriver.info', 'ENVARC:Sys/def_dosdriver.info'):
        if os.path.exists(src):
            rc = G.run_command('C:Copy >NIL: "%s" TO "%s.info"' % (src, path))
            if rc == 0:
                return
    log('no default DOSDriver icon found for ' + path)


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
        f.write(dosdriver_text(values))
        f.close()
    except IOError:
        message('Cannot write the DOSDriver:\n' + path)
        return False
    if saved:
        copy_icon(path)
    log('DOSDriver %s (saved: %s)' % (path, saved))

    # Mounted like the Workbench does with DEVS:DOSDrivers: the device
    # name comes from the file name (FROM expects a MountList instead).
    rc = G.run_command('C:Mount >%s "%s"' % (CHECK_OUT, path))
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


def masked_startup(values):
    """The Startup line for the log, with the password hidden."""
    hidden = dict(values)
    if hidden.get('password'):
        hidden['password'] = '***'
    return startup_line(hidden)


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


# ===== Connection check =====

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

    G.progress_open(False)
    G.send_channel('PROGRESS TEXT Connecting to %s: ...' % name, 600)
    G.run_command('RUN >NIL: C:Execute ' + CHECK_SCRIPT)

    answer = None
    start = time.time()
    while (time.time() - start) < CONNECT_TIMEOUT:
        time.sleep(0.5)
        answer = read_text(CHECK_DONE)
        if answer:
            break
        if not G.progress_update(0, 0):
            answer = 'CANCEL'
            break
    G.progress_close()

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


# ===== Searches (Servers... and Shares... buttons) =====

def busy(text):
    """Progress window without buttons: the network calls cannot be
    interrupted, but each one has a time limit."""
    result = G.send_channel('PROGRESS NOCANCEL NOBUTTONS INDET 0 0', 6000)
    G.PROGRESS_OK = result is not None and result[0] == 0
    if G.PROGRESS_OK:
        G.send_channel('PROGRESS TEXT ' + text, 600)


TYPE_NAME = 'Type a name or address...'

# Answers that mean "this login is not accepted": ask user and password.
LOGIN_STATUS = (0xc000006d, 0xc0000022, 0xc0000064, 0xc000006e,
                0xc0000072, 0xc000015b)


def ask_host():
    """Let the user type the server; returns it or None."""
    values = run_form([
        'TITLE Server',
        'FIELD host|TEXT|Server:|',
        'INFO Name or IP address of the computer.',
        'BUTTONS OK|Cancel',
    ])
    if values is None or values.get('BUTTON') != '1':
        return None
    host = values.get('host', '').strip()
    return host or None


def pick_server():
    """Step 1: find the computers that share files; returns the host."""
    busy('Looking for servers on the network...')
    error = None
    servers = []
    try:
        try:
            servers = discover_servers()
        except Exception:
            error = str(sys.exc_info()[1])
    finally:
        G.progress_close()
    if error:
        log('server search failed: %s' % error, True)
    if not servers:
        message('No server answered.\n\n'
                'Computers with NetBIOS turned off (recent Windows) cannot\n'
                'be found: type their name or address.', False)
        return ask_host()
    labels = ['%s (%s)' % (n, ip) for n, ip in servers] + [TYPE_NAME]
    index = choose('Servers', labels)
    if index is None:
        return None
    if index == len(servers):
        return ask_host()
    name, ip = servers[index]
    return host_for_mount(name, ip)


def ask_login(host, login, note):
    """User, password and guest for HOST; returns them or None."""
    lines = [
        'TITLE Login to ' + clean(host),
        'FIELD user|TEXT|User:|%s' % clean(login['user']),
        'FIELD password|PASSWORD|Password:|%s' % clean(login['password']),
        'FIELD guest|CHECK|Guest (no password)|%s' % login['guest'],
    ]
    if note:
        lines.append('INFO ' + clean(note))
    lines.append('BUTTONS OK|Cancel')
    values = run_form(lines)
    if values is None or values.get('BUTTON') != '1':
        return None
    return {'user': values.get('user', '').strip(),
            'password': values.get('password', ''),
            'guest': values.get('guest', '0')}


def fetch_shares(host, login):
    """Returns (shares, None) or (None, SMBError)."""
    password = login['password']
    if login['guest'] == '1':
        password = ''
    busy('Asking %s for its shares...' % host)
    try:
        try:
            return list_shares(host, login['user'], password), None
        except SMBError:
            return None, sys.exc_info()[1]
        except Exception:
            return None, SMBError('Network error: %s' % sys.exc_info()[1])
    finally:
        G.progress_close()


def pick_share(host, login):
    """Step 2: list the shares (asking a login if the server wants one).
    Returns (share, login, asked) or None; asked is True when user and
    password were already asked here."""
    asked = False
    while True:
        shares, error = fetch_shares(host, login)
        if error is None:
            break
        log('share list of %s: %s' % (host, error), True)
        if getattr(error, 'status', None) not in LOGIN_STATUS:
            message('Cannot list the shares of %s:\n\n%s' % (host, error))
            return None
        if login['user'] or asked:
            note = '%s' % error
        else:
            note = 'The server wants a user and a password.'
        login = ask_login(host, login, note)
        if login is None:
            return None
        asked = True
    if not shares:
        message('%s has no shares that can be shown.' % host, False)
        return None
    labels = []
    for name, remark in shares:
        if remark:
            labels.append('%s - %s' % (name, remark))
        else:
            labels.append(name)
    index = choose('Shares of ' + host, labels)
    if index is None:
        return None
    return shares[index][0], login, asked


def browse(values):
    """Browse...: server, then share, then user and password.  Returns
    the new host, share, user, password and guest, or None."""
    host = pick_server()
    if not host:
        return None
    login = {'user': values.get('user', '').strip(),
             'password': values.get('password', ''),
             'guest': values.get('guest', '0')}
    picked = pick_share(host, login)
    if picked is None:
        return None
    share, login, asked = picked
    if not asked:
        login = ask_login(host, login, 'User and password for the mount.')
        if login is None:
            return None
    found = {'host': host, 'share': share}
    found.update(login)
    return found


# ===== Main =====

def main():
    log('start path=%s channel=%s' % (G.START_PATH, G.PLUGIN_DIR))

    if not G.PLUGIN_DIR or not G.DROP_PORT:
        message('The plugin command channel is not available.')
        return
    if not check_requirements():
        return

    # Browse first: server, share, user and password.  The form comes at
    # the end, already filled in, to check the values and mount.
    current = dict(DEFAULTS)
    found = browse(current)
    if not found:
        log('cancelled')
        return
    for key in ('host', 'share', 'user', 'guest'):
        current[key] = found[key]
    password = found['password']

    while True:
        values = ask_parameters(current, password)
        if values is None:
            log('cancelled')
            return
        # keep what was typed for the next form, even after an error
        for key in current:
            if key in values:
                current[key] = values[key]
        password = values.get('password', '')
        button = values.get('BUTTON')
        if button == BTN_BROWSE:
            found = browse(values)
            if found:
                for key in ('host', 'share', 'user', 'guest'):
                    current[key] = found[key]
                password = found['password']
            continue
        error = validate(values)
        if error is None:
            name = device_name(values['share'])
            if not device_in_use(name):
                break
            error = ('%s: is already in use (the share may be mounted '
                     'already).' % name)
        message(error, False)

    if not mount(values, name):
        return

    reason = wait_share(name)
    if reason is not None:
        log('%s: does not answer (%s), Startup %s'
            % (name, reason, masked_startup(values)), True)
        message('%s: was mounted, but the share does not answer:\n%s\n\n'
                'Check host, share, user and password.' % (name, reason))
        return

    log('mounted %s:' % name)
    G.send_channel('CD %s:' % name)


if __name__ == '__main__':
    main()
    sys.exit(0)

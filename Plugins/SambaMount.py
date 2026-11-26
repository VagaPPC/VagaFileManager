#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: SambaMount
# ---------------------------------------------------------------------------
#
# Mounts an SMB2/3 (Samba, Windows, NAS) share with smb2-handler:
#
#   1. checks that L:smb2-handler and filesysbox.library 54.4+ are there,
#      otherwise explains what is missing and quits;
#   2. asks the parameters in one VFM form (FORM command): device, host,
#      share, user, password, volume, read only, guest, where to save;
#   3. writes a DOSDriver (temporary in T:, or saved in DEVS:DOSDrivers or
#      SYS:Storage/DOSDrivers) and mounts it with C:Mount;
#   4. checks in its own process, with a time limit, that the share
#      answers, so VFM never waits on a server that does not respond;
#   5. opens the share in the VFM pane the plugin was started from.
#
# The last values (password excluded) are remembered in
# ENVARC:SambaMount.prefs.  A saved DOSDriver keeps the password in plain
# text, as smb2-handler needs it in its Startup line.
#
# smb2-handler Startup template (from its README):
#   URL/A,USER,PASSWORD,VOLUME,READONLY/S,NOPASSWORDREQ/S
# URL: smb://[<domain>;][<user>[:<password>]@]<host>[:<port>]/<share>/<path>
#
# The channel, log and message helpers come from Grep.py (same drawer).
# The plugin receives the usual arguments (see Grep.py), argv[5] being the
# plugin session directory.
# ---------------------------------------------------------------------------

import os
import sys
import time

import Grep as G

G.LOG = 'T:SambaMount.log'
G.TITLE = 'SambaMount'

log = G.log
message = G.message

HANDLER = 'L:smb2-handler'
PREFS = ['ENV:SambaMount.prefs', 'ENVARC:SambaMount.prefs']
SAVE_CHOICES = ['No', 'DEVS:DOSDrivers', 'SYS:Storage/DOSDrivers']
CONNECT_TIMEOUT = 120      # seconds: smb2-handler may ask for a password

try:
    _PID = os.getpid()
except Exception:
    _PID = int(time.time()) & 0xffff

CHECK_SCRIPT = 'T:VFM_SambaMount_check_%d.script' % _PID
CHECK_DONE = 'T:VFM_SambaMount_check_%d.txt' % _PID


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


# ===== Remembered values =====

def load_prefs():
    prefs = {'device': 'SMB0', 'host': '', 'share': '', 'user': '',
             'volume': '', 'readonly': '0', 'guest': '0', 'save': '0'}
    for path in PREFS:
        try:
            f = open(path, 'r')
        except IOError:
            continue
        for line in f.read().split('\n'):
            if '=' in line:
                key, sep, value = line.partition('=')
                if key in prefs:
                    prefs[key] = value
        f.close()
        break
    return prefs


def save_prefs(values):
    """Remember the form values, never the password."""
    keys = ['device', 'host', 'share', 'user', 'volume', 'readonly',
            'guest', 'save']
    text = ''.join(['%s=%s\n' % (k, values.get(k, '')) for k in keys])
    for path in PREFS:
        try:
            f = open(path, 'w')
            f.write(text)
            f.close()
        except IOError:
            log('cannot write ' + path, True)


# ===== Form =====

def clean(text):
    """Form values cannot contain the separators of the form file."""
    return text.replace('|', '').replace('\n', ' ')


def ask_parameters(prefs):
    """Show the VFM form; returns the values or None if cancelled."""
    spec = os.path.join(G.PLUGIN_DIR, 'sambamount.form')
    lines = [
        'TITLE SambaMount',
        'FIELD device|TEXT|Device:|%s' % clean(prefs['device']),
        'FIELD host|TEXT|Host:|%s' % clean(prefs['host']),
        'FIELD share|TEXT|Share:|%s' % clean(prefs['share']),
        'FIELD user|TEXT|User:|%s' % clean(prefs['user']),
        'FIELD password|PASSWORD|Password:|',
        'FIELD volume|TEXT|Volume:|%s' % clean(prefs['volume']),
        'FIELD readonly|CHECK|Read only|%s' % prefs['readonly'],
        'FIELD guest|CHECK|Guest (no password)|%s' % prefs['guest'],
        'FIELD save|CHOICE|Save:|%s|%s' % (';'.join(SAVE_CHOICES), prefs['save']),
        'INFO A saved DOSDriver keeps the password in plain text.',
        'BUTTONS Mount|Cancel',
    ]
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

    if values.get('BUTTON') != '1':
        return None
    return values


# ===== Checks on the values =====

def device_name(text):
    """'smb0:' -> 'SMB0'; None if it is not a valid device name."""
    name = text.strip().rstrip(':').upper()
    if not name or len(name) > 30:
        return None
    for c in name:
        if not (c.isalnum() or c in '_-'):
            return None
    return name


def device_in_use(name):
    """True if a device, volume or assign with this name exists.

    C:Assign EXISTS only looks at the DOS list: it does not access the
    device, so no 'insert volume' requester can appear.
    """
    rc = G.run_command('C:Assign >NIL: %s: EXISTS' % name)
    return rc == 0


def validate(values):
    """Returns an error text, or None if the values can be used."""
    if not device_name(values.get('device', '')):
        return 'Enter a device name made of letters and digits (e.g. SMB0).'
    if not values.get('host', '').strip():
        return 'Enter the host (name or IP address of the server).'
    if not values.get('share', '').strip():
        return 'Enter the name of the share.'
    for key in ('host', 'share', 'user', 'password', 'volume'):
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
    smb://user:password@HOST/share VOLUME=name"""
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
    if values.get('volume', '').strip():
        args.append('VOLUME=' + values['volume'].strip())
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
    return 'T:SambaMount_' + name, False


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

    rc = G.run_command('C:Mount >NIL: %s: FROM "%s"' % (name, path))
    if rc != 0:
        message('Mount %s: failed (return code %s).' % (name, rc))
        return False
    return True


# ===== Connection check =====

def wait_share(name):
    """True when NAME: answers.  The access runs in a separate shell so
    that a server that does not answer never blocks VFM or this plugin."""
    for path in (CHECK_SCRIPT, CHECK_DONE):
        try:
            os.remove(path)
        except OSError:
            pass
    try:
        f = open(CHECK_SCRIPT, 'w')
        f.write('FailAt 21\n'
                'If EXISTS %s:\n'
                '  Echo >%s OK\n'
                'Else\n'
                '  Echo >%s FAIL\n'
                'EndIf\n' % (name, CHECK_DONE, CHECK_DONE))
        f.close()
    except IOError:
        return False

    G.progress_open(False)
    G.send_channel('PROGRESS TEXT Connecting to %s: ...' % name, 600)
    G.run_command('RUN >NIL: C:Execute ' + CHECK_SCRIPT)

    answer = None
    start = time.time()
    while (time.time() - start) < CONNECT_TIMEOUT:
        time.sleep(0.5)
        try:
            f = open(CHECK_DONE, 'r')
            answer = f.read().strip()
            f.close()
            if answer:
                break
        except IOError:
            pass
        if not G.progress_update(0, 0):
            log('connection check cancelled')
            break
    G.progress_close()

    for path in (CHECK_SCRIPT, CHECK_DONE):
        try:
            os.remove(path)
        except OSError:
            pass
    log('connection check: %r' % answer)
    return answer == 'OK'


# ===== Main =====

def main():
    log('start path=%s channel=%s' % (G.START_PATH, G.PLUGIN_DIR))

    if not G.PLUGIN_DIR or not G.DROP_PORT:
        message('The plugin command channel is not available.')
        return
    if not check_requirements():
        return

    prefs = load_prefs()
    while True:
        values = ask_parameters(prefs)
        if values is None:
            log('cancelled')
            return
        # keep what was typed for the next form, even after an error
        for key in prefs:
            if key in values:
                prefs[key] = values[key]
        error = validate(values)
        if error is None:
            break
        message(error, False)

    name = device_name(values['device'])
    if device_in_use(name):
        message('%s: is already in use.\nChoose another device name.' % name, False)
        return

    save_prefs(values)
    if not mount(values, name):
        return

    if not wait_share(name):
        message('%s: was mounted, but the share does not answer.\n\n'
                'Check host, share, user and password.' % name)
        return

    log('mounted %s:' % name)
    G.send_channel('CD %s:' % name)


if __name__ == '__main__':
    main()
    sys.exit(0)

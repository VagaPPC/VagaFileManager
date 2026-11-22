#!python
# ---------------------------------------------------------------------------
# VagaFileManager plugin: Find
# ---------------------------------------------------------------------------
#
# The plugin asks VFM to run its native FIND command through the file-based
# plugin channel.  VFM then:
#
#   1. opens a requester asking for the file name;
#   2. searches recursively from the active pane path (PATH);
#   3. shows the results in a Reaction listbrowser window;
#   4. when a result is selected (or double-clicked) and confirmed with OK,
#      navigates the active pane to its drawer and selects the file.
#
# The search field accepts AmigaDOS wildcards (#?, ?, [], ~) and the
# common *.  Without wildcards the name is matched as a substring.
#
# No ARexx and no C:RX are used.  The plugin receives:
#
#   sys.argv[1] = ARexx port (kept for compatibility)
#   sys.argv[2] = active pane path at launch
#   sys.argv[3] = VagaFileManager executable
#   sys.argv[4] = public drop port of this instance
#   sys.argv[5] = plugin channel directory (T:VFM_<task>)
#
# Protocol (one command):
#   write  <channel>/req_<id>.txt
#   run    VagaFileManager --notify "<drop-port>"
#   read   <channel>/rep_<id>.txt   ->  <rc>|<result>
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

LOG = 'T:Find.log'
_seq = 0


def log(text):
    """Append a diagnostic line to T:Find.log (best effort)."""
    try:
        f = open(LOG, 'a')
        f.write(text + '\n')
        f.close()
    except Exception:
        pass


def message(text):
    """Show an error with ASL (if available) and log it."""
    log('message: ' + text)
    if asl is not None:
        try:
            asl.MessageBox('Find', text, 'OK')
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


def send_channel(command):
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
    for attempt in range(50):
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

    # The native FIND command asks for the pattern, searches from the
    # active pane (PATH), shows the result window and reveals the selected
    # file.  This plugin only has to send the command through the channel.
    result = send_channel('FIND')
    if result is None:
        message('Cannot send FIND to VagaFileManager.\n'
                'Check T:Find.log and the plugin channel.')
        return

    log('FIND returned rc=%s' % result[0])


if __name__ == '__main__':
    main()
    sys.exit(0)

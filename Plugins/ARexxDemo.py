#!python
# ---------------------------------------------------------------------------
# VagaFileManager addon example: ARexx console with a Reaction interface.
# ---------------------------------------------------------------------------
#
# How VFM starts a Python addon
# -----------------------------
#   C:Python "PROGDIR:Plugins/ARexxDemo.py" "<arexx-port>" "<current-path>"
#            "<vfm-executable>" "<drop-port>" "<plugin-channel-dir>"
#
#   sys.argv[1] = ARexx port of this VFM instance.  It is normally
#                 "VAGAFILEMANAGER"; when several instances are running
#                 the newer ones use "VAGAFILEMANAGER.<task>".
#   sys.argv[2] = path of the active pane when the addon was started.
#   sys.argv[3] = absolute path of the VagaFileManager executable.
#   sys.argv[4] = public drop port of this VFM instance, used by --notify.
#   sys.argv[5] = private plugin channel directory (T:VFM_<task>).
#
# Reaction without BOOPSI
# -----------------------
#   The stock AmigaOS Python does not expose BOOPSI/ReAction classes.
#   VFM therefore offers a small dialog host:
#
#       VagaFileManager --dialog "@T:specfile"
#
#   The spec file has five lines:
#
#       result-file
#       title
#       prompt
#       default value
#       buttons (separated by '|', at most six)
#
#   VFM opens a real window.class window, waits for a button and writes
#
#       <button-index>|<typed-text>
#
#   to result-file.  Index 0 means "window closed", 1 is Return or the
#   first button.  The addon launches this helper with amiga.system()
#   (os.system() as fallback), so the window does not depend on ARexx.
#   If the helper cannot start, the addon retries with the ARexx DIALOG
#   command, which keeps older builds usable when their port replies.
#
# Plugin command channel (no ARexx)
# ---------------------------------
#   This is the normal path.  The plugin writes a request file in
#   argv[5]:
#
#       req_<id>.txt          one command line
#
#   then runs:
#
#       VagaFileManager --notify "<argv[4]>"
#
#   VFM processes the request and writes:
#
#       rep_<id>.txt          <rc>|<result>
#
#   Requests are handled by the same code as the ARexx commands, but on
#   a synthetic RexxMsg: no ARexx host lookup, no RX.  The plugin can
#   therefore use CD, PATH, ENTRIES, SELECTED, DIALOG, ... even when the
#   system ARexx/RX is not working.
#
# ARexx via C:RX (fallback)
# -------------------------
#   If the channel is not available (older VFM), the addon writes a
#   clean temporary script:
#
#       address <port>
#       '<command>'
#       say 'RC='rc
#       say 'RESULT='result
#
#   and runs:
#
#       C:RX T:VFM_ARexxDemo.cmd >T:VFM_ARexxDemo.out
#
#   The addon then reads RC and RESULT from the output file.
#
# Addon API used by this example
# ------------------------------
#   PATH, ENTRIES, SELECTED  query the active pane
#   DIALOG ...               Reaction dialog hosted by VFM (ARexx variant)
#   other commands           sent verbatim (CD, UP, REFRESH, ...)
#
# Installation
# ------------
#   Copy this file to PROGDIR:Plugins, then use
#   "Scripts -> Refresh" in VFM and pick "ARexxDemo" from the menu.
#
# Troubleshooting
# ---------------
#   Every startup, command and error is appended to T:ARexxDemo.log.
#   The request/reply files are in argv[5]; inspect them if the channel
#   times out.  The C:RX fallback files are T:VFM_ARexxDemo.cmd and
#   T:VFM_ARexxDemo.out.
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

# RunPlugin() supplies these arguments (see RunPlugin in the C source).
PORT = sys.argv[1] if len(sys.argv) > 1 else 'VAGAFILEMANAGER'
START_PATH = sys.argv[2] if len(sys.argv) > 2 else ''
VFM = sys.argv[3] if len(sys.argv) > 3 else 'VagaFileManager'
DROP_PORT = sys.argv[4] if len(sys.argv) > 4 else ''
PLUGIN_DIR = sys.argv[5] if len(sys.argv) > 5 else ''

_seq = 0

LOG = 'T:ARexxDemo.log'
SPEC = 'T:VFM_ARexxDemo.spec'
REPLY = 'T:VFM_ARexxDemo.reply'
RXSCRIPT = 'T:VFM_ARexxDemo.cmd'
RXOUT = 'T:VFM_ARexxDemo.out'


def log(text):
    """Append a diagnostic line to T:ARexxDemo.log (best effort)."""
    try:
        f = open(LOG, 'a')
        f.write(text + '\n')
        f.close()
    except Exception:
        pass


def message(text):
    """Show an error with ASL (if available) and always log it."""
    log('message: ' + text)
    if asl is not None:
        try:
            asl.MessageBox('VFM ARexx demo', text, 'OK')
        except Exception:
            pass


def run_command(cmd):
    """Run a Shell command and wait for it; None if it cannot start."""
    if amiga is not None:
        try:
            return amiga.system(cmd)
        except Exception:
            log('amiga.system exception')
    try:
        return os.system(cmd)
    except Exception:
        return None


def dialog_file(prompt, value, buttons):
    """Open a Reaction dialog through "VagaFileManager --dialog".

    The specification travels in a file, so the text may contain spaces
    and quotes without shell-escaping problems.
    Returns (button_index, typed_text), or None if the helper failed.
    """
    try:
        os.remove(REPLY)
    except OSError:
        pass

    try:
        f = open(SPEC, 'w')
        f.write(REPLY + '\n')
        f.write('VFM ARexx demo\n')
        f.write(prompt.replace('\n', ' ') + '\n')
        f.write(value.replace('\n', ' ') + '\n')
        f.write(buttons + '\n')
        f.close()
    except IOError:
        log('cannot write ' + SPEC)
        return None

    cmd = '"%s" --dialog "@%s"' % (VFM, SPEC)
    log('dialog: ' + cmd)
    rc = run_command(cmd)
    if rc is None:
        log('cannot start the dialog helper')
        return None
    log('dialog: helper rc=%s' % rc)

    # Normally amiga.system() waits for the helper to exit; the short
    # poll also covers a system() that starts the command asynchronously.
    # A complete reply always contains the separator after the index.
    answer = None
    for attempt in range(50):
        try:
            f = open(REPLY)
            answer = f.read()
            f.close()
            if '|' in answer:
                break
        except IOError:
            answer = None
        time.sleep(0.1)

    if answer is None or '|' not in answer:
        log('reply file not created: ' + REPLY)
        return None

    # The reply is "<index>|<text>"; split only on the first separator.
    idx, sep, typed = answer.partition('|')
    try:
        return (int(idx), typed if sep else '')
    except ValueError:
        log('bad reply: %r' % answer)
        return None


def arexx_quote(text):
    """Quote a command as an ARexx string literal."""
    if "'" not in text:
        return "'" + text + "'"
    if '"' not in text:
        return '"' + text + '"'
    return "'" + text.replace("'", "''") + "'"


def notify():
    """Ask VFM to process the pending request files."""
    if not VFM or not DROP_PORT:
        return False
    rc = run_command('"%s" --notify "%s"' % (VFM, DROP_PORT))
    log('channel notify: rc=%s' % rc)
    return rc == 0


def send_file(command):
    """Send one command through the file channel.

    Returns (rc, rc2, result) or None if the channel is unavailable.
    """
    global _seq

    if not PLUGIN_DIR or not DROP_PORT:
        return None

    command = command.replace('\n', ' ').strip()
    if not command:
        return (None, None, None)

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

    notify()

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
        return (int(rc), None, result)
    except ValueError:
        log('channel: bad reply %r' % answer)
        return None


def send(command):
    """Use the file channel; fall back to C:RX for older VFM builds."""
    result = send_file(command)
    if result is not None:
        return result
    return send_rx(command)


def send_rx(command):
    """Run one ARexx command through C:RX (fallback)."""
    command = command.replace('\n', ' ').strip()
    if not command:
        return (None, None, None)

    try:
        os.remove(RXOUT)
    except OSError:
        pass

    try:
        # Binary mode forces LF even if Python's text mode would write
        # CRLF: "address HOST\r" is not a valid ARexx host name.
        f = open(RXSCRIPT, 'wb')
        f.write('address ' + PORT + '\n')
        f.write(arexx_quote(command) + '\n')
        f.write("say 'RC='rc\n")
        f.write("say 'RESULT='result\n")
        f.close()
    except IOError:
        log('cannot write ' + RXSCRIPT)
        return (None, None, None)

    cmd = 'C:RX "%s" >"%s"' % (RXSCRIPT, RXOUT)
    log('rx: ' + cmd)
    status = run_command(cmd)
    log('rx: exit=%s' % status)

    # amiga.system() usually waits for RX; the poll covers an
    # asynchronous system() and a partly written output file.
    text = None
    for attempt in range(50):
        try:
            f = open(RXOUT)
            text = f.read()
            f.close()
            if 'RC=' in text or 'RESULT=' in text:
                break
        except IOError:
            text = None
        time.sleep(0.1)

    if text is None:
        log('rx: no output ' + RXOUT)
        return (None, None, None)

    rc = None
    result = None
    lines = text.splitlines()
    for i in range(len(lines)):
        line = lines[i]
        if line.startswith('RC='):
            rc = line[3:].strip()
        elif line.startswith('RESULT='):
            result = '\n'.join([line[7:]] + lines[i + 1:])
            break

    # No structured output: RX probably printed an error message.
    if result is None:
        result = text.strip()

    log('rx: rc=%s result=%r' % (rc, result))
    return (rc, None, result)


def dialog_arexx(prompt, value, buttons):
    """Fallback: ask VFM to host the dialog through the ARexx DIALOG command."""
    rc, rc2, answer = send('DIALOG VFM ARexx demo|' + prompt + '|' +
                           value + '|' + buttons)
    if not answer:
        return None
    idx, sep, typed = answer.partition('|')
    try:
        return (int(idx), typed if sep else '')
    except ValueError:
        log('bad DIALOG reply: %r' % answer)
        return None


def dialog(prompt, value, buttons):
    """File helper first; ARexx DIALOG as a fallback."""
    reply = dialog_file(prompt, value, buttons)
    if reply is not None:
        return reply
    log('file dialog failed; trying ARexx DIALOG')
    return dialog_arexx(prompt, value, buttons)


def short_path(path):
    """Fit the pane path into a one-line dialog label."""
    if not path:
        return '(root)'
    return path if len(path) <= 48 else '...' + path[-45:]


def python_entries(path):
    """Fallback used when ARexx does not return the ENTRIES reply."""
    try:
        names = os.listdir(path if path else '.')
    except Exception:
        return '(cannot read ' + (path or '.') + ')'
    shown = names[:10]
    text = '%d entries' % len(names)
    if shown:
        text = text + ': ' + ', '.join(shown)
    if len(names) > len(shown):
        text = text + ', ...'
    return text


def main():
    log('start port=%s path=%s vfm=%s channel=%s drop=%s' %
        (PORT, START_PATH, VFM, PLUGIN_DIR, DROP_PORT))

    # Ask the running VFM for its version.  This is also the first test
    # of the ARexx port; the generated .cmd/.out files show what RX saw.
    rc, rc2, version = send('VERSION')
    version = version if version else '(no reply)'
    log('VFM version: %s' % version)

    command = 'PATH'

    while True:
        reply = dialog(
            'Path: ' + short_path(START_PATH) +
            '  -  type a command here, then Send:',
            command,
            'Send|PATH|ENTRIES|SELECTED|Close')

        if reply is None:
            message('Cannot open the Reaction dialog.\n'
                    'Check that this file exists:\n' + VFM + '\n'
                    'and see T:ARexxDemo.log for the exact command line.')
            return

        idx, text = reply
        if idx <= 0 or idx == 5:
            break

        if idx == 1:
            # "Send": execute whatever is in the string gadget.
            command = text.strip()
            if not command:
                continue
        elif idx == 2:
            command = 'PATH'
        elif idx == 3:
            command = 'ENTRIES'
        elif idx == 4:
            command = 'SELECTED'

        rc, rc2, result = send(command)

        # If RX returned no reply, fall back to data we already have:
        # the launch path, or Python's own directory listing.
        if result is None or result == '':
            if idx == 2 or command.upper() == 'PATH':
                result = START_PATH if START_PATH else '(root)'
            elif idx == 3 or command.upper() == 'ENTRIES':
                result = python_entries(START_PATH)
            elif idx == 4 or command.upper() == 'SELECTED':
                result = '(no reply; use the VFM window to see the selection)'
            elif rc == 0 or rc == '0':
                result = '(command executed; no text reply)'
            else:
                result = '(command failed; rc=%s)' % rc

        # Show rc too: it proves whether VFM accepted the command even
        # when the returned string is empty.
        dialog('Result of ' + command +
               ' (rc=%s; click OK to continue):' % rc,
               result, 'OK')

    log('end')


if __name__ == '__main__':
    main()
    sys.exit(0)

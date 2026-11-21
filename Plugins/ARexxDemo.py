#!python
# ---------------------------------------------------------------------------
# VagaFileManager addon example: ARexx console with a Reaction interface.
# ---------------------------------------------------------------------------
#
# How VFM starts a Python addon
# -----------------------------
#   C:Python "PROGDIR:Plugins/ARexxDemo.py" "<arexx-port>" "<current-path>" "<vfm-executable>"
#
#   sys.argv[1] = ARexx port of this VFM instance.  It is normally
#                 "VAGAFILEMANAGER"; when several instances are running
#                 the newer ones use "VAGAFILEMANAGER.<task>".
#   sys.argv[2] = path of the active pane when the addon was started.
#   sys.argv[3] = absolute path of the VagaFileManager executable.
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
# The arexx module
# ----------------
#   arexx.dorexx(port, command) returns (rc, rc2, result).
#   rc == 0 means the command was accepted; result is the reply string.
#
#   On some AmigaOS Python 2.5 installations result is always None even
#   for VERSION.  This example therefore displays a reply when it is
#   available and otherwise falls back to the launch path or to Python's
#   own os.listdir().  The ARexx command is still sent: commands such as
#   CD, UP, REFRESH or VIEW ICONS have a visible effect in VFM.
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
#   If no window appears, check that log first: it records the helper
#   command line and its return code.
# ---------------------------------------------------------------------------

import os
import sys
import time

try:
    import amiga
except ImportError:
    amiga = None

try:
    import arexx
except ImportError:
    arexx = None

try:
    import asl
except ImportError:
    asl = None

# RunPlugin() supplies these arguments (see RunPlugin in the C source).
PORT = sys.argv[1] if len(sys.argv) > 1 else 'VAGAFILEMANAGER'
START_PATH = sys.argv[2] if len(sys.argv) > 2 else ''
VFM = sys.argv[3] if len(sys.argv) > 3 else 'VagaFileManager'

LOG = 'T:ARexxDemo.log'
SPEC = 'T:VFM_ARexxDemo.spec'
REPLY = 'T:VFM_ARexxDemo.reply'


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
    try:
        if amiga is not None:
            rc = amiga.system(cmd)
        else:
            rc = os.system(cmd)
    except Exception:
        # Last chance: Python's own os.system().
        try:
            rc = os.system(cmd)
        except Exception:
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


def dialog_arexx(prompt, value, buttons):
    """Fallback: ask VFM to host the dialog through the ARexx DIALOG command."""
    if arexx is None:
        return None
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


def send(command):
    """Send one ARexx command; returns (rc, rc2, reply-or-None)."""
    if arexx is None:
        return (None, None, None)
    try:
        rc, rc2, result = arexx.dorexx(PORT, command)
    except Exception:
        # Keep this compatible with Python 2.5 (no "except ... as ...").
        log('dorexx exception: ' + command)
        return (None, None, None)
    log('dorexx rc=%s rc2=%s result=%r: %s' % (rc, rc2, result, command))
    return (rc, rc2, result)


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
    log('start port=%s path=%s vfm=%s arexx=%s' %
        (PORT, START_PATH, VFM, arexx is not None))

    if arexx is None:
        message('The arexx module is not available in this Python.\n'
                'ARexx commands cannot be sent.')
        return

    # Ask the running VFM for its version.  This is also the first test
    # of the ARexx port: if the reply is None the command was still sent,
    # but the Python module did not return the result string.
    rc, rc2, version = send('VERSION')
    version = version if version else '(no reply)'
    log('VFM version: %s' % version)

    command = 'PATH'

    while True:
        reply = dialog(
            'Path: ' + short_path(START_PATH) + '  -  ARexx command:',
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

        # If the module returned no reply, fall back to data we already
        # have: the launch path, or Python's own directory listing.
        if result is None or result == '':
            if idx == 2 or command.upper() == 'PATH':
                result = START_PATH if START_PATH else '(root)'
            elif idx == 3 or command.upper() == 'ENTRIES':
                result = python_entries(START_PATH)
            elif idx == 4 or command.upper() == 'SELECTED':
                result = '(no reply; use the VFM window to see the selection)'
            else:
                result = '(command sent; no reply from the arexx module)'

        # Show rc/rc2 too: they are enough to prove that the ARexx port
        # accepted the command even when result is empty.
        dialog('Result of ' + command + ' (rc=%s rc2=%s):' % (rc, rc2),
               result, 'OK')

    log('end')


if __name__ == '__main__':
    main()
    sys.exit(0)

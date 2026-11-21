#!python
# ---------------------------------------------------------------------------
# VagaFileManager addon example: ARexx console with a Reaction interface.
# ---------------------------------------------------------------------------
#
# How VFM starts a Python addon
# -----------------------------
#   C:Python "PROGDIR:Plugins/ARexxDemo.py" "<arexx-port>" "<current-path>"
#
#   sys.argv[1] = ARexx port of this VFM instance.  It is normally
#                 "VAGAFILEMANAGER"; when several instances are running
#                 the newer ones use "VAGAFILEMANAGER.<task>".
#   sys.argv[2] = path of the active pane when the addon was started.
#
# The arexx module
# ----------------
#   arexx.dorexx(port, command) returns (rc, rc2, result).
#   rc == 0 means the command was accepted; result is the reply string.
#
# The Reaction interface
# ----------------------
#   The stock AmigaOS Python does not expose BOOPSI/ReAction classes.
#   This addon therefore asks VFM to open a real Reaction window through
#   the ARexx command DIALOG:
#
#       DIALOG <title>|<prompt>|<default>|<buttons>
#
#   VFM shows a window.class window with a label, a string gadget and up
#   to six buttons, then replies:
#
#       <button-index>|<text>
#
#   Button indexes start at 1; 0 means the window was closed.  The text
#   is what the user typed.  Pressing Return in the string gadget acts
#   like the first button.
#
# Addon API used by this example
# ------------------------------
#   PATH, ENTRIES, SELECTED  query the active pane
#   DIALOG ...               Reaction dialog hosted by VFM
#   other commands           sent verbatim (CD, UP, REFRESH, ...)
#
# Installation
# ------------
#   Copy this file to PROGDIR:Plugins, then use
#   "Scripts -> Refresh" in VFM and pick "ARexxDemo" from the menu.
#
# Troubleshooting
# ---------------
#   Every startup and every error is appended to T:ARexxDemo.log.
#   If no window appears, check that log first: it tells whether the
#   arexx module was imported, which VFM version answered (it must be
#   "1.0 build 1" or newer, otherwise DIALOG is missing) and whether
#   the DIALOG command worked.
# ---------------------------------------------------------------------------

import sys

try:
    import arexx
except ImportError:
    arexx = None

try:
    import asl
except ImportError:
    asl = None

# RunPlugin() supplies these two arguments (see RunPlugin in the C source).
PORT = sys.argv[1] if len(sys.argv) > 1 else 'VAGAFILEMANAGER'
START_PATH = sys.argv[2] if len(sys.argv) > 2 else ''

LOG = 'T:ARexxDemo.log'


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


def vfm(command):
    """Send one ARexx command to VFM.

    Returns the reply string, or None if the command failed or the
    arexx module is not available.
    """
    if arexx is None:
        return None
    try:
        rc, rc2, result = arexx.dorexx(PORT, command)
    except Exception:
        # Keep this compatible with Python 2.5 (no "except ... as ...").
        log('dorexx exception')
        return None
    if rc != 0:
        log('dorexx rc=%s rc2=%s: %s' % (rc, rc2, command))
        return None
    return result if result is not None else ''


def dialog(prompt, text, buttons):
    """Show a Reaction dialog inside VFM.

    buttons is a string such as "Run|PATH|ENTRIES|Close".
    Returns (button_index, typed_text); None on ARexx failure.
    """
    spec = 'VFM ARexx demo|' + prompt + '|' + text + '|' + buttons
    answer = vfm('DIALOG ' + spec)
    if answer is None:
        return None
    # The reply is "<index>|<text>"; the text may itself contain '|',
    # so split only on the first separator.
    idx, sep, value = answer.partition('|')
    try:
        return (int(idx), value if sep else '')
    except ValueError:
        log('bad DIALOG reply: %r' % answer)
        return None


def short_path(path):
    """Fit the pane path into a one-line dialog label."""
    if not path:
        return '(root)'
    return path if len(path) <= 48 else '...' + path[-45:]


def main():
    log('start port=%s path=%s arexx=%s' % (PORT, START_PATH, arexx is not None))

    if arexx is None:
        message('The arexx module is not available in this Python.')
        return

    # Ask the VFM instance for its version.  This immediately tells us
    # whether the ARexx port belongs to an old binary without DIALOG.
    version = vfm('VERSION') or '(unknown)'
    log('VFM version: %s' % version)

    # Show the launch path once, as a small demonstration of argv[2].
    # The PATH command is used for the first dialog too, so the user
    # always sees the path of the active pane.
    path = vfm('PATH') or START_PATH
    command = 'PATH'

    while True:
        reply = dialog(
            'Path: ' + short_path(path) + '  -  ARexx command:',
            command,
            'Run|PATH|ENTRIES|SELECTED|Close')

        if reply is None:
            message('Cannot open the Reaction dialog.\n'
                    'VFM version: ' + version + '\n'
                    'Use the latest VMF/VagaFileManager (About must show '
                    'build 1), close all other VFM instances and check '
                    'T:VagaFileManager.log.')
            return

        idx, text = reply
        if idx <= 0 or idx == 5:
            break

        if idx == 1:
            # "Run": execute whatever is in the string gadget.
            command = text.strip()
            if not command:
                continue
        elif idx == 2:
            command = 'PATH'
        elif idx == 3:
            command = 'ENTRIES'
        elif idx == 4:
            command = 'SELECTED'

        result = vfm(command)
        if result is None:
            message('ARexx error while running: ' + command)
            return

        # Refresh the path shown in the next dialog.
        if command.upper() == 'PATH' and result:
            path = result

        # Show the reply.  The field stays editable so the result can be
        # copied or used as the next command.
        dialog('Result of ' + command + ':', result, 'OK')


if __name__ == '__main__':
    main()
    sys.exit(0)

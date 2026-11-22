#!python
# ---------------------------------------------------------------------------
# ARexxProbe.py -- diagnosi ARexx per VagaFileManager (Python 2.5).
# ---------------------------------------------------------------------------
#
# Uso, da un Shell, con VFM gia' avviato:
#
#   C:Python Tools/ARexxProbe.py VAGAFILEMANAGER VMF/VagaFileManager
#
# Il secondo argomento e' il percorso dell'eseguibile VFM; serve per il
# test --findport. Se lo ometti, il test --findport viene saltato.
#
# Il probe:
#   1. chiede a VFM --findport di cercare VAGAFILEMANAGER e WORKBENCH;
#   2. crea T:wb.rexx (solo LF) e lo esegue con C:RX;
#   3. crea T:vfm.rexx (solo LF) e chiede VERSION a VFM;
#   4. scrive tutto in T:ARexxProbe.log e sullo stdout.
#
# I file T:wb.rexx e T:vfm.rexx sono scritti in modalita' binaria, quindi
# non contengono CR: "address HOST\r" non e' un nome host valido.
# ---------------------------------------------------------------------------

import os
import sys
import time

try:
    import amiga
except ImportError:
    amiga = None

PORT = sys.argv[1] if len(sys.argv) > 1 else 'VAGAFILEMANAGER'
VFM = sys.argv[2] if len(sys.argv) > 2 else ''

LOG = 'T:ARexxProbe.log'
WB = 'T:wb.rexx'
WBOUT = 'T:wb.out'
VFMCMD = 'T:vfm.rexx'
VFMOUT = 'T:vfm.out'
PORT1 = 'T:ARexxProbe.vfmport'
PORT2 = 'T:ARexxProbe.wbport'


def log(line):
    """Print and append one diagnostic line."""
    print(line)
    try:
        f = open(LOG, 'a')
        f.write(line + '\n')
        f.close()
    except Exception:
        pass


def run(cmd):
    """Run a Shell command and wait; returns the status or None."""
    log('run: ' + cmd)
    if amiga is not None:
        try:
            return amiga.system(cmd)
        except Exception:
            pass
    try:
        return os.system(cmd)
    except Exception:
        return None


def write_file(path, text):
    """Write a text file with LF only."""
    try:
        f = open(path, 'wb')
        f.write(text)
        f.close()
        return True
    except IOError:
        log('cannot write ' + path)
        return False


def read_file(path):
    try:
        f = open(path, 'rb')
        text = f.read()
        f.close()
        return text
    except IOError:
        return ''


def findport(exe, name, out):
    """Run 'VFM --findport NAME' and return the printed line."""
    if not exe:
        log('findport ' + name + ': skipped (no VFM path)')
        return ''
    try:
        os.remove(out)
    except OSError:
        pass
    run('"%s" --findport %s >"%s"' % (exe, name, out))
    text = read_file(out).strip()
    log('findport ' + name + ': ' + (text if text else '(no output)'))
    return text


def run_rx(script, out, marker):
    """Run C:RX SCRIPT > OUT and wait for marker in the output."""
    try:
        os.remove(out)
    except OSError:
        pass

    status = run('C:RX "%s" >"%s"' % (script, out))

    text = ''
    for attempt in range(50):
        text = read_file(out)
        if marker in text:
            break
        time.sleep(0.1)

    log('rx ' + script + ': status=' + str(status) +
        ' output=' + repr(text))
    return (status, text)


def main():
    log('=== ARexxProbe start ===')
    log('port=' + PORT + ' vfm=' + (VFM if VFM else '(not given)'))

    findport(VFM, PORT, PORT1)
    findport(VFM, 'WORKBENCH', PORT2)

    if write_file(WB, 'address WORKBENCH\nsay \'WB_OK\'\n'):
        run_rx(WB, WBOUT, 'WB_OK')

    if write_file(VFMCMD,
                  'address ' + PORT + '\n' +
                  '\'VERSION\'\n' +
                  'say \'RC=\'rc\n' +
                  'say \'RESULT=\'result\n'):
        run_rx(VFMCMD, VFMOUT, 'RC=')

    log('=== ARexxProbe end ===')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""X11 request logger for comparing a guest (big-endian) and a native (little-endian) run of one program.

Listens on /tmp/.X11-unix/X<listen>, forwards to X<real>, and writes one line per request in a
byte-order independent form: name, decoded fields, payload bytes.  Resource ids the client made
itself are printed relative to its id base, so two connections give the same text.

usage: xreqlog.py LISTEN REAL LOG [--all]
  without --all, the requests an Xlib sends for its own setup (extension queries, sync round
  trips, the resource manager read, keyboard mapping) are left out: SGI's Xlib and the host's
  differ there and the program has no say in them.
"""
import os, socket, struct, sys, threading

listen_n, real_n, logpath = sys.argv[1], sys.argv[2], sys.argv[3]
show_all = '--all' in sys.argv[4:]
log = open(logpath, 'w', buffering=1)
lock = threading.Lock()

# opcode: (name, struct format of the fixed part after the 4-byte header, field names, what follows)
# "what follows": None, 'str' (bytes, length from field n), 'bytes', 'w32' (list of 32-bit words), 'prop'
CORE = {
    1: ('CreateWindow', 'IIhhHHHHII', 'wid parent x y w h bw class visual mask', 'w32'),
    2: ('ChangeWindowAttributes', 'II', 'window mask', 'w32'),
    3: ('GetWindowAttributes', 'I', 'window', None),
    4: ('DestroyWindow', 'I', 'window', None),
    8: ('MapWindow', 'I', 'window', None),
    10: ('UnmapWindow', 'I', 'window', None),
    12: ('ConfigureWindow', 'IHxx', 'window mask', 'w32'),
    14: ('GetGeometry', 'I', 'drawable', None),
    15: ('QueryTree', 'I', 'window', None),
    16: ('InternAtom', 'Hxx', 'n', 'str'),
    17: ('GetAtomName', 'I', 'atom', None),
    18: ('ChangeProperty', 'IIIBxxxI', 'window property type format nunits', 'prop'),
    19: ('DeleteProperty', 'II', 'window property', None),
    20: ('GetProperty', 'IIIII', 'window property type offset length', None),
    22: ('SetSelectionOwner', 'III', 'owner selection time', None),
    23: ('GetSelectionOwner', 'I', 'selection', None),
    24: ('ConvertSelection', 'IIIII', 'requestor selection target property time', None),
    25: ('SendEvent', 'II', 'destination mask', 'event'),
    26: ('GrabPointer', 'IHBBIII', 'window mask pmode kmode confine cursor time', None),
    27: ('UngrabPointer', 'I', 'time', None),
    35: ('AllowEvents', 'I', 'time', None),
    36: ('GrabServer', '', '', None),
    37: ('UngrabServer', '', '', None),
    38: ('QueryPointer', 'I', 'window', None),
    40: ('TranslateCoordinates', 'IIhh', 'src dst x y', None),
    42: ('SetInputFocus', 'II', 'focus time', None),
    43: ('GetInputFocus', '', '', None),
    45: ('OpenFont', 'IHxx', 'fid n', 'str'),
    46: ('CloseFont', 'I', 'font', None),
    47: ('QueryFont', 'I', 'font', None),
    53: ('CreatePixmap', 'IIHH', 'pid drawable w h', None),
    54: ('FreePixmap', 'I', 'pixmap', None),
    55: ('CreateGC', 'III', 'cid drawable mask', 'w32'),
    56: ('ChangeGC', 'II', 'gc mask', 'w32'),
    60: ('FreeGC', 'I', 'gc', None),
    61: ('ClearArea', 'IhhHH', 'window x y w h', None),
    62: ('CopyArea', 'IIIhhhhHH', 'src dst gc sx sy dx dy w h', None),
    63: ('CopyPlane', 'IIIhhhhHHI', 'src dst gc sx sy dx dy w h plane', None),
    78: ('CreateColormap', 'III', 'mid window visual', None),
    84: ('AllocColor', 'IHHHxx', 'cmap r g b', None),
    85: ('AllocNamedColor', 'IHxx', 'cmap n', 'str'),
    91: ('QueryColors', 'I', 'cmap', 'w32'),
    92: ('LookupColor', 'IHxx', 'cmap n', 'str'),
    94: ('CreateGlyphCursor', 'IIIHHHHHHHH', 'cid src mask sc mc fr fg fb br bg bb', None),
    95: ('FreeCursor', 'I', 'cursor', None),
    98: ('QueryExtension', 'Hxx', 'n', 'str'),
    101: ('GetKeyboardMapping', 'BBxx', 'first count', None),
    104: ('Bell', '', '', None),
    115: ('ForceScreenSaver', '', '', None),
    119: ('GetModifierMapping', '', '', None),
    127: ('NoOperation', '', '', None),
}
# requests an Xlib issues on its own behalf
XLIB_OWN = {98, 101, 119, 43, 127}
# atoms SGI's Xlib interns for itself when it opens a display
XLIB_OWN_ATOMS = {b'SGI_EXTRA_PREDEFINES', b'Compose'}
ID_FIELDS = {'wid', 'pid', 'cid', 'fid', 'mid', 'window', 'drawable', 'gc', 'font', 'pixmap', 'cursor', 'src', 'dst',
             'parent', 'owner', 'requestor', 'destination', 'focus', 'confine', 'mask_pix'}


def out(msg):
    with lock:
        log.write(msg + '\n')


class Conn:
    def __init__(self):
        self.e = None
        self.base = None
        self.mask = None
        self.ready = threading.Event()      # set once the setup reply gave the id base

    def xid(self, v):
        if self.base is not None and v and (v & ~self.mask) == self.base:
            return 'own+%x' % (v & self.mask)
        return '%x' % v

    def decode(self, op, d1, body):
        e = self.e
        if op not in CORE:
            if op >= 128 and not show_all:
                return
            out('op%d d1=%d len=%d' % (op, d1, len(body)))
            return
        name, fmt, names, tail = CORE[op]
        if op in XLIB_OWN and not show_all:
            return
        size = struct.calcsize(e + fmt)
        vals = struct.unpack_from(e + fmt, body, 0) if fmt else ()
        names = names.split()
        parts = []
        for n, v in zip(names, vals):
            parts.append('%s=%s' % (n, self.xid(v) if n in ID_FIELDS else v))
        rest = body[size:]
        if tail == 'str':
            n = vals[names.index('n')]
            if op == 16 and not show_all and rest[:n] in XLIB_OWN_ATOMS:
                return
            parts.append('"%s"' % rest[:n].decode('latin1'))
        elif tail == 'w32':
            parts.append('[' + ' '.join('%x' % w for w in struct.unpack_from(e + '%dI' % (len(rest) // 4), rest, 0)) + ']')
        elif tail == 'prop':
            fmtbits, nunits = vals[3], vals[4]
            if fmtbits == 8:
                parts.append('data=%r' % rest[:nunits])
            elif fmtbits == 16:
                parts.append('data=[' + ' '.join('%x' % w for w in struct.unpack_from(e + '%dH' % nunits, rest, 0)) + ']')
            else:
                parts.append('data=[' + ' '.join('%x' % w for w in struct.unpack_from(e + '%dI' % nunits, rest, 0)) + ']')
        elif tail == 'event':
            # 32-byte event: code, detail, sequence, then type dependent.  ClientMessage (33) is decoded by format.
            code = rest[0] & 0x7f
            if code == 33:
                fbits = rest[1]
                win, typ = struct.unpack_from(e + 'II', rest, 4)
                if fbits == 8:
                    data = repr(rest[12:32])
                elif fbits == 16:
                    data = '[' + ' '.join('%x' % w for w in struct.unpack_from(e + '10H', rest, 12)) + ']'
                else:
                    data = '[' + ' '.join('%x' % w for w in struct.unpack_from(e + '5I', rest, 12)) + ']'
                parts.append('ClientMessage format=%d window=%s type=%d data=%s' % (fbits, self.xid(win), typ, data))
            else:
                parts.append('event code=%d raw=%s' % (code, rest[:32].hex()))
        if op in (16, 85, 92, 45) or d1:
            parts.append('d1=%d' % d1)
        # the resource manager read every Xlib does when it opens the display
        if op == 20 and not show_all and vals[1] == 23 and vals[2] == 31:
            return
        out('%s %s' % (name, ' '.join(parts)))


def client_to_server(c, s, conn):
    buf = b''
    state = 'setup'
    try:
        while True:
            data = c.recv(65536)
            if not data:
                break
            s.sendall(data)
            buf += data
            while True:
                if state == 'setup':
                    if len(buf) < 12:
                        break
                    conn.e = '>' if buf[0] == 0x42 else '<'
                    na, nd = struct.unpack(conn.e + 'HH', buf[6:10])
                    total = 12 + ((na + 3) & ~3) + ((nd + 3) & ~3)
                    if len(buf) < total:
                        break
                    buf = buf[total:]
                    state = 'req'
                    conn.ready.wait(5)
                    continue
                if len(buf) < 4:
                    break
                op, d1, ln = struct.unpack(conn.e + 'BBH', buf[:4])
                size = ln * 4
                if ln == 0:      # BIG-REQUESTS
                    if len(buf) < 8:
                        break
                    size = struct.unpack(conn.e + 'I', buf[4:8])[0] * 4
                    if len(buf) < size:
                        break
                    body = buf[8:size]
                else:
                    if len(buf) < size:
                        break
                    body = buf[4:size]
                buf = buf[size:]
                try:
                    conn.decode(op, d1, body)
                except Exception as ex:  # keep the proxy alive on odd requests
                    out('# decode error op=%d %r' % (op, ex))
    except OSError:
        pass
    finally:
        try: s.shutdown(socket.SHUT_RDWR)
        except OSError: pass


def server_to_client(s, c, conn):
    first = b''
    try:
        while True:
            data = s.recv(65536)
            if not data:
                break
            c.sendall(data)
            # resource id base and mask from the connection setup reply
            if conn.base is None and conn.e:
                first += data
                if len(first) >= 20 and first[0] == 1:
                    conn.base, conn.mask = struct.unpack_from(conn.e + 'II', first, 12)
                    conn.ready.set()
    except OSError:
        pass
    finally:
        try: c.shutdown(socket.SHUT_RDWR)
        except OSError: pass


def main():
    path = '/tmp/.X11-unix/X%s' % listen_n
    try: os.unlink(path)
    except FileNotFoundError: pass
    ls = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    ls.bind(path)
    ls.listen(8)
    print('xreqlog listening on', path, flush=True)
    while True:
        c, _ = ls.accept()
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect('/tmp/.X11-unix/X%s' % real_n)
        conn = Conn()
        out('# connection')
        threading.Thread(target=client_to_server, args=(c, s, conn), daemon=True).start()
        threading.Thread(target=server_to_client, args=(s, c, conn), daemon=True).start()


main()

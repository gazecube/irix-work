#!/usr/bin/env python3
"""X11 protocol logging proxy for the IRIX guest.  Listens on /tmp/.X11-unix/X<listen>, forwards to X<real>,
decodes the drawing requests the client sends (the guest is big-endian) into LOG.

usage: xproxy.py LISTEN REAL LOG
"""
import os, socket, struct, sys, threading, time

listen_n, real_n, logpath = sys.argv[1], sys.argv[2], sys.argv[3]
log = open(logpath, 'w', buffering=1)
lock = threading.Lock()

GCBITS = [('function', 0x1), ('plane_mask', 0x2), ('foreground', 0x4), ('background', 0x8), ('line_width', 0x10),
          ('line_style', 0x20), ('cap_style', 0x40), ('join_style', 0x80), ('fill_style', 0x100), ('fill_rule', 0x200),
          ('tile', 0x400), ('stipple', 0x800), ('ts_x', 0x1000), ('ts_y', 0x2000), ('font', 0x4000),
          ('subwindow', 0x8000), ('gexp', 0x10000), ('clip_x', 0x20000), ('clip_y', 0x40000), ('clip_mask', 0x80000),
          ('dash_offset', 0x100000), ('dash_list', 0x200000), ('arc_mode', 0x400000)]


def p(msg):
    with lock:
        log.write('%.3f %s\n' % (time.time(), msg))


def gcvals(e, mask, data):
    out = []
    i = 0
    for name, bit in GCBITS:
        if mask & bit:
            v = struct.unpack(e + 'I', data[i:i + 4])[0]
            i += 4
            out.append('%s=%d' % (name, v) if name not in ('foreground', 'background') else '%s=%d(#%06x)' % (name, v, v))
    return ' '.join(out)


def decode(e, op, d1, body):
    S = lambda fmt, off: struct.unpack_from(e + fmt, body, off)
    if op == 55:      # CreateGC
        gc, drw, mask = S('III', 0)
        p('CreateGC gc=%x %s' % (gc, gcvals(e, mask, body[12:])))
    elif op == 56:    # ChangeGC
        gc, mask = S('II', 0)
        p('ChangeGC gc=%x %s' % (gc, gcvals(e, mask, body[8:])))
    elif op == 59:    # SetClipRectangles
        gc, cx, cy = S('Ihh', 0)
        n = (len(body) - 8) // 8
        rects = [struct.unpack_from(e + 'hhHH', body, 8 + 8 * i) for i in range(n)]
        p('SetClipRectangles gc=%x origin=%d,%d ordering=%d n=%d %s' % (gc, cx, cy, d1, n, ' '.join('(%d,%d %dx%d)' % r for r in rects)))
    elif op == 60:    # FreeGC
        p('FreeGC gc=%x' % S('I', 0))
    elif op == 61:    # ClearArea
        w, x, y, ww, hh = S('IhhHH', 0)
        p('ClearArea win=%x %d,%d %dx%d' % (w, x, y, ww, hh))
    elif op == 62:    # CopyArea
        s, dd, gc, sx, sy, dx, dy, ww, hh = S('IIIhhhhHH', 0)
        p('CopyArea src=%x dst=%x gc=%x %d,%d -> %d,%d %dx%d' % (s, dd, gc, sx, sy, dx, dy, ww, hh))
    elif op == 64:    # PolyPoint
        drw, gc = S('II', 0)
        n = (len(body) - 8) // 4
        pts = [struct.unpack_from(e + 'hh', body, 8 + 4 * i) for i in range(n)]
        p('PolyPoint gc=%x mode=%d n=%d %s' % (gc, d1, n, ' '.join('(%d,%d)' % t for t in pts)))
    elif op == 65:    # PolyLine
        drw, gc = S('II', 0)
        n = (len(body) - 8) // 4
        pts = [struct.unpack_from(e + 'hh', body, 8 + 4 * i) for i in range(n)]
        p('PolyLine gc=%x mode=%d n=%d %s' % (gc, d1, n, ' '.join('(%d,%d)' % t for t in pts)))
    elif op == 66:    # PolySegment
        drw, gc = S('II', 0)
        n = (len(body) - 8) // 8
        segs = [struct.unpack_from(e + 'hhhh', body, 8 + 8 * i) for i in range(n)]
        p('PolySegment gc=%x n=%d %s' % (gc, n, ' '.join('(%d,%d-%d,%d)' % t for t in segs)))
    elif op == 67:    # PolyRectangle
        drw, gc = S('II', 0)
        n = (len(body) - 8) // 8
        rs = [struct.unpack_from(e + 'hhHH', body, 8 + 8 * i) for i in range(n)]
        p('PolyRectangle gc=%x %s' % (gc, ' '.join('(%d,%d %dx%d)' % t for t in rs)))
    elif op in (68, 71):  # PolyArc / PolyFillArc
        drw, gc = S('II', 0)
        n = (len(body) - 8) // 12
        arcs = [struct.unpack_from(e + 'hhHHhh', body, 8 + 12 * i) for i in range(n)]
        p('%s gc=%x %s' % ('PolyArc' if op == 68 else 'PolyFillArc', gc, ' '.join('(%d,%d %dx%d a1=%d a2=%d)' % t for t in arcs)))
    elif op == 69:    # FillPoly
        drw, gc, shape, mode = S('IIBB', 0)
        n = (len(body) - 12) // 4
        pts = [struct.unpack_from(e + 'hh', body, 12 + 4 * i) for i in range(n)]
        p('FillPoly gc=%x shape=%d mode=%d n=%d %s' % (gc, shape, mode, n, ' '.join('(%d,%d)' % t for t in pts)))
    elif op == 70:    # PolyFillRectangle
        drw, gc = S('II', 0)
        n = (len(body) - 8) // 8
        rs = [struct.unpack_from(e + 'hhHH', body, 8 + 8 * i) for i in range(n)]
        p('PolyFillRectangle gc=%x %s' % (gc, ' '.join('(%d,%d %dx%d)' % t for t in rs)))
    elif op == 72:    # PutImage
        drw, gc, ww, hh, dx, dy, lp, dep = S('IIHHhhBB', 0)
        data = body[20:]
        p('PutImage gc=%x fmt=%d %dx%d at %d,%d depth=%d data=%s' % (gc, d1, ww, hh, dx, dy, dep, data[:64].hex()))
    elif op == 53:    # CreatePixmap
        pid, drw, ww, hh = S('IIHH', 0)
        p('CreatePixmap pix=%x %dx%d depth=%d' % (pid, ww, hh, d1))


def client_to_server(c, s):
    buf = b''
    e = None
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
                    e = '>' if buf[0] == 0x42 else '<'
                    na, nd = struct.unpack(e + 'HH', buf[6:10])
                    total = 12 + ((na + 3) & ~3) + ((nd + 3) & ~3)
                    if len(buf) < total:
                        break
                    buf = buf[total:]
                    state = 'req'
                    p('# setup byteorder=%s' % e)
                    continue
                if len(buf) < 4:
                    break
                op, d1, ln = struct.unpack(e + 'BBH', buf[:4])
                size = ln * 4
                if ln == 0:      # BIG-REQUESTS
                    if len(buf) < 8:
                        break
                    ln = struct.unpack(e + 'I', buf[4:8])[0]
                    size = ln * 4
                    if len(buf) < size:
                        break
                    body = buf[8:size]
                else:
                    if len(buf) < size:
                        break
                    body = buf[4:size]
                buf = buf[size:]
                try:
                    decode(e, op, d1, body)
                except Exception as ex:  # keep the proxy alive on odd requests
                    p('# decode error op=%d %r' % (op, ex))
    finally:
        try: s.shutdown(socket.SHUT_RDWR)
        except OSError: pass


def server_to_client(s, c):
    try:
        while True:
            data = s.recv(65536)
            if not data:
                break
            c.sendall(data)
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
    print('xproxy listening on', path, flush=True)
    while True:
        c, _ = ls.accept()
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect('/tmp/.X11-unix/X%s' % real_n)
        p('# connection')
        threading.Thread(target=client_to_server, args=(c, s), daemon=True).start()
        threading.Thread(target=server_to_client, args=(s, c), daemon=True).start()


main()

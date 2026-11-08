vlcmotif <-> vlcd protocol (v0)
===============================

vlcmotif is the 32-bit Motif front end (SGI look, lifted libXm).  vlcd is a 64-bit helper that owns libvlc.  They talk
over a socketpair: the front end starts vlcd and the helper reads commands on stdin and writes events on stdout.
One message per line, UTF-8, fields separated by a single TAB, TAB/LF/CR/% inside a field percent-encoded.
Video is drawn by VLC into an X window the front end owns: EMBED hands over its XID (any process may do that).

Front end -> helper
  EMBED <xid>             window for the video output (a DrawingArea)
  OPEN <mrl|path>...      replace the playlist and start playing the first item
  ADD <mrl|path>...       append to the playlist (plays at once when idle)
  PLAYINDEX <n> | REMOVE <n> | CLEAR | NEXT | PREV
  PLAY | PAUSE | TOGGLE | STOP
  SEEK <permille>         position as 0..1000 of the length
  JUMP <ms>               relative seek, may be negative
  VOLUME <0..125> | MUTE <0|1> | RATE <percent>
  AUDIOTRACK <id> | SPUTRACK <id> | TITLE <n> | CHAPTER <n> | ADDSUB <path> | ASPECT <w:h|default>
  DVDMENU                 jump to the disc menu
  QUIT

Helper -> front end
  READY
  STATE <idle|opening|buffering|playing|paused|stopped|ended|error>
  TIME <ms> <length_ms>   about five times a second while playing
  META <title> <artist> <album> <nowplaying>
  VOLUME <0..125> <muted 0|1>
  TRACKS <kind> <count>   kind is audio, sub, title or chapter; <count> lines follow:
  TRACK <kind> <id> <current 0|1> <name>
  PLAYLIST <count> <current>   then <count> lines: ITEM <index> <name>
  ERROR <text>

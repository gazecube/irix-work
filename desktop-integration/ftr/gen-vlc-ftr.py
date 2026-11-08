#!/usr/bin/env python3
"""Generate vlc.ftr, the file-typing rules that open media with the Motif VLC player (vlcmotif).

Two kinds of rule:
  * overrides of IRIX's own media types (same TYPE name, so a personal rule replaces the system one): the block is taken
    from /usr/lib/filetype/system/*.ftr and only the open commands change; relative icon includes become absolute;
  * new types for formats IRIX never knew, drawn as a document with the VLC cone.

usage: gen-vlc-ftr.py SYSTEM_FILETYPE_DIR ICON_DIR OUT.ftr
"""
import re
import sys
from pathlib import Path

sysdir, icondir, out = Path(sys.argv[1]), sys.argv[2].rstrip('/'), sys.argv[3]

OPEN = 'vlcmotif --play $LEADER $REST'
ALTOPEN = 'vlcmotif --enqueue $LEADER $REST'

# IRIX types that VLC plays: (ftr file, type names)
OVERRIDES = [
    ('sgimovie.ftr', ['QTmdatMovieFile', 'QTmoovMovieFile', 'AVIMovieFile', 'MPEG1VideoBitstream',
                      'MPEG1SystemsBitstream', 'DIFDVMovieFile']),
    ('sgisound.ftr', ['AIFFSoundFile', 'AIFCSoundFile', 'WAVESoundFile', 'SUNSoundFile', 'MPEGAudioBitstream']),
]

KEYWORDS = ('TYPE', 'MATCH', 'LEGEND', 'SUPERTYPE', 'MAP', 'SETVAR', 'CMD', 'MENUCMD', 'ICON', 'SPECIALFILE',
            'OPEN', 'ALTOPEN', 'DROP')


def blocks(text):
    """Split an ftr file into {type name: list of lines}."""
    cur, name, res = [], None, {}
    for line in text.splitlines():
        m = re.match(r'TYPE\s+(\S+)', line)
        if m:
            if name:
                res[name] = cur
            name, cur = m.group(1), [line]
        elif name:
            cur.append(line)
    if name:
        res[name] = cur
    return res


def rewrite(lines, system_dir):
    """Replace CMD OPEN / CMD ALTOPEN (which may span several lines), drop the editor/info menu commands that name
    IRIX-only tools, and make icon includes absolute."""
    res, skip = [], False
    for line in lines:
        s = line.strip()
        word = s.split(None, 1)[0] if s else ''
        if word in KEYWORDS or s.startswith('}') or s.startswith('include') or not s:
            skip = False
        if skip:
            continue
        if re.match(r'\s*CMD\s+OPEN\b', line):
            res.append('    CMD OPEN\t' + OPEN)
            skip = True
            continue
        if re.match(r'\s*CMD\s+ALTOPEN\b', line):
            res.append('    CMD ALTOPEN\t' + ALTOPEN)
            skip = True
            continue
        if word == 'MENUCMD':
            skip = True
            continue
        line = re.sub(r'include\s*\(\s*"\.\./iconlib/', 'include("%s/iconlib/' % system_dir, line)
        line = re.sub(r'include\s*\(\s*"iconlib/', 'include("%s/system/iconlib/' % system_dir, line)
        res.append(line)
    return res


NEW = r'''
# ---- formats IRIX never knew ------------------------------------------------------------------------------------
TYPE VLCMediaFile
    LEGEND	Media file (VLC)
    CMD OPEN	{open}
    CMD ALTOPEN	{altopen}
    MENUCMD	"Add to Playlist"	{altopen}
    ICON	{{
	include("/usr/lib/filetype/iconlib/generic.doc.fti");
	include("{icondir}/vlc.fti");
    }}

TYPE MatroskaFile
    MATCH	long(0) == 0x1a45dfa3;
    LEGEND	Matroska / WebM media file
    MAP		MimeType	video/x-matroska
    SETVAR	DefaultExtension	mkv
    SETVAR	AltExtensions	webm,mka
    SUPERTYPE	VLCMediaFile

TYPE MPEG4MediaFile
    MATCH	string(4,4) == "ftyp";
    LEGEND	MPEG-4 / QuickTime media file
    MAP		MimeType	video/mp4
    SETVAR	DefaultExtension	mp4
    SETVAR	AltExtensions	m4v,m4a,mov,3gp
    SUPERTYPE	VLCMediaFile

TYPE OggMediaFile
    MATCH	string(0,4) == "OggS";
    LEGEND	Ogg media file (Vorbis, Opus, Theora)
    MAP		MimeType	application/ogg
    SETVAR	DefaultExtension	ogg
    SETVAR	AltExtensions	oga,ogv,opus,spx
    SUPERTYPE	VLCMediaFile

TYPE FLACAudioFile
    MATCH	string(0,4) == "fLaC";
    LEGEND	FLAC audio file
    MAP		MimeType	audio/flac
    SETVAR	DefaultExtension	flac
    SUPERTYPE	VLCMediaFile

TYPE MP3AudioFile
    MATCH	string(0,3) == "ID3" || glob("*.mp3");
    LEGEND	MP3 audio file
    MAP		MimeType	audio/mpeg
    SETVAR	DefaultExtension	mp3
    SUPERTYPE	VLCMediaFile

TYPE FLVMediaFile
    MATCH	string(0,3) == "FLV";
    LEGEND	Flash video file
    MAP		MimeType	video/x-flv
    SETVAR	DefaultExtension	flv
    SUPERTYPE	VLCMediaFile

TYPE ASFMediaFile
    MATCH	long(0) == 0x3026b275 && long(4) == 0x8e66cf11;
    LEGEND	Windows Media file (ASF, WMV, WMA)
    MAP		MimeType	video/x-ms-asf
    SETVAR	DefaultExtension	wmv
    SETVAR	AltExtensions	wma,asf
    SUPERTYPE	VLCMediaFile

TYPE RealMediaFile
    MATCH	string(0,4) == ".RMF";
    LEGEND	RealMedia file
    MAP		MimeType	application/vnd.rn-realmedia
    SETVAR	DefaultExtension	rm
    SUPERTYPE	VLCMediaFile

TYPE MPEGTransportStream
    MATCH	(uchar(0) == 0x47 && uchar(188) == 0x47 && uchar(376) == 0x47) ||
		(uchar(4) == 0x47 && uchar(196) == 0x47 && uchar(388) == 0x47);
    LEGEND	MPEG transport stream
    MAP		MimeType	video/mp2t
    SETVAR	DefaultExtension	ts
    SETVAR	AltExtensions	m2ts,mts
    SUPERTYPE	VLCMediaFile

TYPE VLCPlaylistFile
    MATCH	glob("*.m3u") || glob("*.m3u8") || glob("*.pls") || glob("*.xspf") || glob("*.asx");
    LEGEND	Playlist
    SUPERTYPE	VLCMediaFile

# ---- discs -----------------------------------------------------------------------------------------------------
TYPE DiscImageFile
    MATCH	glob("*.iso") && string(32769,5) == "CD001";
    LEGEND	Disc image (DVD / Blu-ray / CD)
    CMD OPEN	{open}
    CMD ALTOPEN	{altopen}
    ICON	{{
	include("/usr/lib/filetype/iconlib/generic.doc.fti");
	include("{icondir}/vlc.fti");
    }}

TYPE DVDVideoDisc
    MATCH	dircontains("VIDEO_TS");
    LEGEND	DVD-Video disc
    SUPERTYPE	Directory
    SPECIALFILE
    CMD OPEN	vlcmotif --play "dvd://$LEADER"
    CMD ALTOPEN	fm $LEADER
    MENUCMD	"Browse Disc Contents"	fm $LEADER
    ICON	{{
	include("/usr/lib/filetype/iconlib/generic.doc.fti");
	include("{icondir}/vlc.fti");
    }}

TYPE BluRayDisc
    MATCH	dircontains("BDMV");
    LEGEND	Blu-ray disc
    SUPERTYPE	Directory
    SPECIALFILE
    CMD OPEN	vlcmotif --play "bluray://$LEADER"
    CMD ALTOPEN	fm $LEADER
    MENUCMD	"Browse Disc Contents"	fm $LEADER
    ICON	{{
	include("/usr/lib/filetype/iconlib/generic.doc.fti");
	include("{icondir}/vlc.fti");
    }}
'''

with open(out, 'w') as w:
    w.write('# Generated by gen-vlc-ftr.py: media files open in the Motif VLC player (vlcmotif).  Do not edit; edit the\n'
            '# generator or ftr/vlc-extra.ftr.\n\n# ---- IRIX media types that VLC plays: same type, new open command -----------------------------------\n')
    for fname, names in OVERRIDES:
        found = blocks((sysdir / 'system' / fname).read_text(errors='replace'))
        for n in names:
            if n not in found:
                sys.stderr.write('warning: %s not in %s\n' % (n, fname))
                continue
            w.write('\n'.join(rewrite(found[n], str(sysdir))).rstrip() + '\n\n')
    w.write(NEW.format(open=OPEN, altopen=ALTOPEN, icondir=icondir))
print('wrote', out)

"""Checks that every text passed to T("…") (and every column name/tip) has an English and a French entry in Source/Lang.cpp.
Usage: python Tools/check_lang.py [--list]"""
import re, glob, os, sys

src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Source')
STR = r'"((?:[^"\\]|\\.)*)"'

def used_keys():
    keys = []
    for f in sorted(glob.glob(os.path.join(src, '*.cpp')) + glob.glob(os.path.join(src, '*.h'))):
        if f.endswith('Lang.cpp'):
            continue
        s = open(f, encoding='utf-8').read()
        keys += re.findall(r'\bT \(\s*' + STR, s)
    s = open(os.path.join(src, 'Columns.h'), encoding='utf-8').read()
    for name, tip in re.findall(r'\{ c\w+, ' + STR + r', \d+, \w+, [^,]+, ' + STR + r' \}', s):
        keys += [name, tip]
    s = open(os.path.join(src, 'WebLookup.h'), encoding='utf-8').read()
    keys += re.findall(STR, s.split('kEngineNames[] =')[1].split(';')[0])
    keys += ['Datei nicht lesbar', 'MIDI-Kopf beschädigt', 'UltraStar-Datei ohne Noten oder Tempo']   # stored error texts (TS)
    out = []
    for k in keys:
        if k not in out:
            out.append(k)
    return out

def table_keys():
    s = open(os.path.join(src, 'Lang.cpp'), encoding='utf-8').read()
    body = s.split('// ---- table begin')[1].split('// ---- table end')[0]
    return set(re.findall(r'\{ ' + STR + r',', body))

keys = used_keys()
if '--list' in sys.argv:
    print('\n'.join(keys)); sys.exit(0)
have = table_keys()
missing = [k for k in keys if k not in have and k not in ('Genre', 'Copyright', 'Format', 'CRC32', 'Tempo', 'Google', 'YouTube', 'Discogs', 'MusicBrainz', 'AllMusic', 'Wikipedia')]
unused = sorted(have - set(keys))
for k in missing: print('FEHLT:', k)
for k in unused: print('unbenutzt:', k)
print(f'{len(keys)} Schlüssel, {len(missing)} fehlen, {len(unused)} unbenutzt')
sys.exit(1 if missing else 0)

#!/usr/bin/env python3
import argparse,json,posixpath,tarfile
from pathlib import PurePosixPath
MAX_ENTRY=32*1024*1024; MAX_TOTAL=256*1024*1024
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('archive'); a=ap.parse_args(); seen=set(); total=0; errors=[]
    try:
      with tarfile.open(a.archive,'r:*') as tf:
        for m in tf.getmembers():
          n=m.name.replace('\\','/')
          pp=PurePosixPath(n)
          if pp.is_absolute() or '..' in pp.parts: errors.append(f'unsafe path:{n}'); continue
          norm=posixpath.normpath(n)
          if norm in seen: errors.append(f'duplicate path:{norm}')
          seen.add(norm)
          if m.issym() or m.islnk(): errors.append(f'link entry rejected:{norm}')
          if m.size>MAX_ENTRY: errors.append(f'oversized entry:{norm}')
          total+=m.size
          if total>MAX_TOTAL: errors.append('archive storage budget exceeded')
    except (tarfile.TarError,OSError) as e: errors.append(f'malformed archive:{e}')
    print(json.dumps({'valid':not errors,'errors':errors,'entry_count':len(seen),'total_bytes':total},indent=2)); return 0 if not errors else 2
if __name__=='__main__': raise SystemExit(main())

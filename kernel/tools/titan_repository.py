#!/usr/bin/env python3
import argparse,re,sys
from pathlib import Path
HEADER='TITAN-REPOSITORY/1'; TOKEN=re.compile(r'^[A-Za-z0-9._-]+$'); SHA=re.compile(r'^[0-9A-Fa-f]{64}$')
def parse(path):
 lines=Path(path).read_text(encoding='utf-8').splitlines();
 if not lines or lines[0].strip()!=HEADER: raise ValueError('bad header')
 repo={}; pkgs=[]; cur=None
 for raw in lines[1:]:
  s=raw.strip()
  if not s or s.startswith('#'): continue
  if s.startswith('[package ') and s.endswith(']'):
   pid=s[9:-1].strip()
   if not TOKEN.fullmatch(pid): raise ValueError('bad package id')
   if any(p['section']==pid for p in pkgs): raise ValueError('duplicate package')
   cur={'section':pid}; pkgs.append(cur); continue
  if '=' not in s: raise ValueError('bad syntax')
  k,v=s.split('=',1); k=k.strip(); v=v.strip(); (cur if cur is not None else repo)[k]=v
 if not TOKEN.fullmatch(repo.get('id','')) or not repo.get('name'): raise ValueError('bad repository metadata')
 for p in pkgs:
  req=['title','version','platform','architecture','url','sha256','size']
  if any(not p.get(k) for k in req): raise ValueError(f'missing field in {p["section"]}')
  if p['platform']!='ps2' or p['architecture']!='mips-r5900': raise ValueError('wrong target')
  if not p['url'].startswith(('https://','http://')): raise ValueError('bad url')
  if not SHA.fullmatch(p['sha256']): raise ValueError('bad sha256')
  if int(p['size'])<=0: raise ValueError('bad size')
 return repo,pkgs
def main():
 ap=argparse.ArgumentParser();ap.add_argument('catalog');a=ap.parse_args()
 try:r,p=parse(a.catalog)
 except Exception as e: print(f'INVALID: {e}',file=sys.stderr);return 2
 print(f'REPOSITORY_OK id={r["id"]} packages={len(p)}');return 0
if __name__=='__main__':raise SystemExit(main())

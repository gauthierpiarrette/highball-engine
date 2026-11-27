#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Read the Wine cache's live counters; require hits AND redirects when validating a trial."""
import argparse,json,pathlib,struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=pathlib.Path)
p.add_argument('--require-active',action='store_true')
a=p.parse_args()
rows=[]
for path in sorted(a.directory.glob('codecache-*.bin')):
    data=path.read_bytes()
    if len(data)<160:p.error('truncated counters: '+str(path))
    magic,version,pid,count,*stats=struct.unpack('<20Q',data[:160])
    if (magic,version,count)!=(0x414f454341434831,1,16):p.error('unknown counters format: '+str(path))
    rows.append({'pid':pid,'lookups':stats[0],'redirects':stats[1],'hits':stats[2],
                 'insertions':stats[3],'unkeyable':stats[4],'jump_hits':stats[5],'fallbacks':stats[6],
                 'mapped_contexts':stats[7],'slots_used':stats[8]})
print(json.dumps(rows,indent=2))
if a.require_active and not any(row['hits']>0 and row['redirects']>0 for row in rows):
    p.exit(1,'No active cache demonstrated: require nonzero hits and redirects for this session.\n')

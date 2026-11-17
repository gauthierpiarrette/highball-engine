#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Write a cache profile after the ring contract has been measured on the target Mac."""
import argparse,hashlib,pathlib,struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("executable",type=pathlib.Path)
p.add_argument("--ring-pointer-rva",required=True,type=lambda v:int(v,0))
p.add_argument("--output-dir",type=pathlib.Path,required=True)
a=p.parse_args()
with a.executable.open("rb") as f:
    header=f.read(65536); f.seek(0); digest=hashlib.file_digest(f,"sha256").hexdigest()
if header[:2]!=b"MZ":p.error("not a PE image")
try:
    pe=struct.unpack_from("<I",header,60)[0]; machine,count=struct.unpack_from("<HH",header,pe+4)
    optional=struct.unpack_from("<H",header,pe+20)[0]; base=struct.unpack_from("<Q",header,pe+48)[0]
    if header[pe:pe+4]!=b"PE\0\0" or machine!=0x8664 or base!=0x140000000 or struct.unpack_from("<H",header,pe+24)[0]!=0x20b:raise ValueError()
    table=pe+24+optional
    ranges=[]
    for i in range(count):
        offset=table+i*40
        if header[offset:offset+8]==b".data\0\0\0":
            size,rva=struct.unpack_from("<II",header,offset+8)
            flags=struct.unpack_from("<I",header,offset+36)[0]
            if flags&0x80000000:ranges.append((rva,rva+size))
except (struct.error,ValueError):p.error("unsupported PE64 layout")
rva=a.ring_pointer_rva
if rva%8 or not any(lo<=rva<=hi-8 for lo,hi in ranges):p.error("pointer RVA must be aligned and inside writable .data")
a.output_dir.mkdir(parents=True,exist_ok=True)
output=a.output_dir/(digest+".cache")
if output.exists():p.error("profile exists; review it explicitly before replacing")
output.write_text(f"aoe4-cache-v1 {digest} {rva:x} 1000000 20\n")
print(output)
print("Profile created from your measured RVA; this does not validate the live ring's size or lifecycle.")

#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Apply the explicitly MIT-licensed AoE sidecar fixes to the pinned baseline."""
import hashlib,json,pathlib,subprocess,sys
repo=pathlib.Path(__file__).resolve().parents[2]
source=pathlib.Path(sys.argv[1]).resolve()
lock=json.loads((repo/"inputs.json").read_text())["aoe4Sidecar"]
sha=subprocess.check_output(["git","rev-parse","HEAD"],cwd=source,text=True).strip()
if sha!=lock["commit"]: sys.exit("Sidecar source commit mismatch")
patches=sorted((repo/"patches/aoe4/sidecar").glob("*.patch"))
if {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in patches}!=lock["patchesSha256"]:
    sys.exit("Sidecar patch checksum mismatch")
for patch in patches:
    subprocess.run(["git","apply","--check",str(patch)],cwd=source,check=True)
    subprocess.run(["git","apply",str(patch)],cwd=source,check=True)
print("Applied combined decoder, GS/TLS fallback and FBLD register-pressure fixes")

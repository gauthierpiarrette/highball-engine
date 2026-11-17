#!/usr/bin/env python3
import hashlib,pathlib,struct,subprocess,sys,tempfile,unittest
HERE=pathlib.Path(__file__).resolve().parents[1]
class Tools(unittest.TestCase):
    def command(self,tool,*args,ok=True):
        result=subprocess.run([sys.executable,str(HERE/tool),*map(str,args)],capture_output=True,text=True)
        self.assertEqual(result.returncode==0,ok,result.stdout+result.stderr)
        return result
    def test_profile_is_hash_bound_and_refuses_wrong_data_address(self):
        with tempfile.TemporaryDirectory() as root:
            root=pathlib.Path(root);exe=root/'RelicCardinal.exe';b=bytearray(1024)
            b[:2]=b'MZ';struct.pack_into('<I',b,60,128);b[128:132]=b'PE\0\0'
            struct.pack_into('<HH',b,132,0x8664,1);struct.pack_into('<H',b,148,240)
            struct.pack_into('<H',b,152,0x20b);struct.pack_into('<Q',b,176,0x140000000)
            b[392:400]=b'.data\0\0\0';struct.pack_into('<II',b,400,0x1000,0x2000)
            struct.pack_into('<I',b,428,0xc0000040);exe.write_bytes(b)
            out=root/'profiles'
            self.command('profile.py',exe,'--ring-pointer-rva','0x2008','--output-dir',out)
            digest=hashlib.sha256(b).hexdigest();profile=out/(digest+'.cache')
            self.assertEqual(profile.read_text(),f'aoe4-cache-v1 {digest} 2008 1000000 20\n')
            self.command('profile.py',exe,'--ring-pointer-rva','0x2008','--output-dir',out,ok=False)
            self.command('profile.py',exe,'--ring-pointer-rva','0x1000','--output-dir',out,ok=False)
            self.command('profile.py',exe,'--ring-pointer-rva','0x2001','--output-dir',out,ok=False)
if __name__=='__main__':unittest.main()

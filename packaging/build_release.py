from pathlib import Path
import hashlib,zipfile,shutil
root=Path(__file__).resolve().parents[1];dist=root/'dist';dist.mkdir(exist_ok=True)
output=dist/'Xuper_TV_by_JonaRuacho_0.14.2_Switch.zip'
runtime=root/'packaging/runtime';nro=root/'xuperswitch.nro';raw=nro.read_bytes()
assert raw[16:20]==b'NRO0'
assert b'0.14.2' in (root/'xuperswitch.nacp').read_bytes()
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in runtime.rglob('*'):
  if p.is_file():z.write(p,p.relative_to(runtime))
 z.write(nro,'switch/xuperswitch/xuperswitch.nro')
 z.write(root/'packaging/LEEME_PRIMERO.txt','LEEME_PRIMERO.txt')
 for p in (root/'licenses').glob('*'):
  if p.is_file():z.write(p,'licencias/'+p.name)
 for name in ['LICENSE','LICENSE.nlohmann-json','COPYRIGHT.mpv','DEPENDENCIAS.md','BUILD.md']:z.write(root/name,'licencias/'+name)
with zipfile.ZipFile(output) as z:
 assert z.testzip() is None
 assert not any(Path(n).name in ['service.json','device_state.json','playback_state.json','library_state.json'] for n in z.namelist())
 import json
 cfg=json.loads(z.read('switch/xuperswitch/service.template.json'))
 assert not any(k in cfg for k in ['installation_serial','device_id','sn','userId','userToken','password'])
source=dist/'Xuper_TV_by_JonaRuacho_0.14.2_Fuentes.zip'
with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for folder in ['source','include','tests','tools','licenses','packaging']:
  for p in (root/folder).rglob('*'):
   if p.is_file() and '__pycache__' not in p.parts:z.write(p,p.relative_to(root))
 for name in ['Makefile','Makefile.host','LICENSE','LICENSE.nlohmann-json','COPYRIGHT.mpv','README.md','BUILD.md','DEPENDENCIAS.md','INSTALAR.md']:z.write(root/name,name)
(dist/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in [output,source]))
print('Release ZIP and source archive verified; no account/activation files included')

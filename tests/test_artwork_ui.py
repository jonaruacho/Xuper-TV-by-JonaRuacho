#!/usr/bin/env python3
"""Local image decoding/cache and real SDL interface render; no provider traffic."""
import http.server,threading,subprocess,tempfile,os,sys,io,collections
from pathlib import Path
from PIL import Image,ImageDraw

def main(binary,font):
    images={}
    for name,fmt,size,color in [('logo.png','PNG',(300,180),'#153e4f'),('poster.jpg','JPEG',(600,900),'#1b6376'),('poster.webp','WEBP',(600,900),'#593346')]:
        im=Image.new('RGB',size,color);d=ImageDraw.Draw(im);w,h=size
        d.ellipse((w*.08,h*.18,w*.92,h*.74),fill='#51c6b1');d.polygon([(0,h),(w//2,h*.45),(w,h)],fill='#122137');d.text((w*.2,h*.12),'XUPER / DEMO',fill='white');f=io.BytesIO();im.save(f,fmt);images['/'+name]=f.getvalue()
    calls=collections.Counter()
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_GET(self):
            calls[self.path]+=1
            assert all(self.headers.get(k) is None for k in ['apk','App','Ranger-Id','Content-Auth','Content-License','Authorization'])
            self.send_response(200 if self.path in images else 404);self.end_headers();self.wfile.write(images.get(self.path,b''))
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    with tempfile.TemporaryDirectory() as tmp:
        folder=Path(tmp);(folder/'font.ttf').write_bytes(Path(font).read_bytes());env=dict(os.environ,SDL_VIDEODRIVER='dummy')
        try:
            for run in range(2):
                subprocess.run([str(Path(binary).resolve()),f'http://127.0.0.1:{server.server_port}'],cwd=folder,env=env,check=True,timeout=30)
                if run==0:counts=calls.copy()
                else:assert counts==calls,'second launch should use SD artwork cache'
            out=Path('work/ui014');out.mkdir(parents=True,exist_ok=True)
            for name in ['home','channels','movies','detail','splash']:Image.open(folder/(name+'.bmp')).save(out/(name+'.png'))
            assert len(list((folder/'artwork').glob('*.img')))==3
            print('Artwork: PNG/JPEG/WebP decoded, cache reused across launches, no account headers sent')
        finally:server.shutdown();server.server_close();thread.join()
if __name__=='__main__':main(*sys.argv[1:])

from Xlib import X, display
import time
d=display.Display(':9'); s=d.screen(); root=s.root
# image-plane child of root with content drawn by a GC (not just background)
img=root.create_window(100,100,400,300,0,24,X.InputOutput,X.CopyFromParent,background_pixel=0x20a040,override_redirect=True)
img.map(); d.sync()
gc=img.create_gc(foreground=0x112233); img.fill_rectangle(gc,150,100,100,100); d.sync()
def px(x,y):
    im=img.get_image(x,y,1,1,X.ZPixmap,0xffffffff); return int.from_bytes(im.data[:4],'little')
print('before: bg %06x rect %06x'%(px(10,10),px(200,150)))
# overlay popup above part of it
ov=[v for dep in s.allowed_depths if dep.depth==8 for v in dep.visuals][0]
cm=root.create_colormap(ov.visual_id, X.AllocNone)
red=cm.alloc_color(65535,0,0).pixel
pop=root.create_window(180,130,200,150,0,8,X.InputOutput,ov.visual_id,background_pixel=red,border_pixel=0,colormap=cm,override_redirect=True)
pop.map(); d.sync(); time.sleep(0.5)
print('popup mapped: bg %06x rect %06x'%(px(10,10),px(200,150)))
pop.unmap(); d.sync(); time.sleep(0.5)
print('popup unmapped: bg %06x rect %06x'%(px(10,10),px(200,150)))
pop.destroy(); img.destroy(); d.sync()

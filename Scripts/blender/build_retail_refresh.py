"""실제 CU·GS25 매장과 제품 사진을 기준으로 다시 만든 편의점 집기·상품.

사진 출처와 채택한 치수는 Docs/RETAIL_AND_NEIGHBORHOOD.md에 남긴다.
인쇄 원화는 RetailPackaging.png의 여섯 칸을 쓴다. 앞면은 -Y, 원점은 바닥이다.
"""

import math
import os
import sys

import bmesh
import bpy

sys.path.insert(0, os.path.dirname(__file__))
import ig_blender_lib as ig
from build_store_products import wrapped_strip

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ART = os.path.join(ROOT, "Content", "SourceArt", "Labels", "Store")


def materials():
    return {
        "white": ig.mat_plastic("도장강판", (.72, .73, .70), .40, bump=0),
        "dark": ig.mat_plastic("차콜ABS", (.025, .029, .032), .43, bump=0),
        "top": ig.mat_plastic("회백색라미네이트", (.54, .52, .48), .46, bump=0),
        "steel": ig.mat_metal("알루미늄", (.64, .66, .66), roughness=.32, streak=.02),
        "paper": ig.mat_plastic("무광종이", (.88, .87, .81), .6, bump=0),
        "led": ig.mat_emissive("백색LED", (.90, .96, 1), 1.2),
        "blue": ig.mat_emissive("광고테두리", (.035, .065, .095), .20),
        "screen": ig.mat_plastic("POS화면", (.025, .042, .05), .25, bump=0),
        "print": ig.mat_image_uv("상품인쇄", os.path.join(ART, "RetailPackaging.png"), roughness=.38),
        "back": ig.mat_image_uv("상품뒷면", os.path.join(ART, "RetailPackagingBack.png"), roughness=.45),
        "pack": ig.mat_image_uv("담뱃갑인쇄", os.path.join(ART, "CigarettePacks.png"), roughness=.52),
        "ad": ig.mat_image_uv("담배장광고", os.path.join(ROOT, "Content", "SourceArt", "T_RetailTobaccoAd_D.png"), roughness=.50),
    }


def finish(name, parts, out, notes, collision=None, size=1024):
    return ig.build_asset(name, "large" if name in ("SM_StoreGondola", "SM_StoreCounter", "SM_TobaccoCabinet") else "prop",
                          parts, out, collision_parts=collision, notes=notes, texture_size=size,
                          uv_margin=.006, preview=True,
                          mirror_print_for_ue=name in ("SM_TobaccoCabinet", "SM_RetailPotato", "SM_RetailShrimp", "SM_RetailCorn", "SM_RetailCupBeef", "SM_RetailCupKimchi", "SM_RetailBiscuit"))


def counter(out):
    ig.reset_scene()
    m = materials()
    parts = [
        ig.box("몸통", (2.20, .56, .80), (0, .02, .5), bevel=.003, material=m["white"]),
        ig.box("걸레받이", (2.16, .48, .10), (0, .03, .05), material=m["dark"]),
        ig.box("라미네이트상판", (2.24, .64, .03), (0, 0, .915), bevel=.004, segments=3, material=m["top"]),
    ]
    for x in (-1.07, -.36, .36, 1.07):
        parts.append(ig.box("도어이음", (.003, .003, .74), (x, -.261, .5), material=m["dark"]))
    finish("SM_StoreCounter", parts, out, "계산대 224×64×93cm. 고객은 -Y, 상판 높이는 월드 99cm. 회백색 라미네이트와 도장 전면.")


def pos(out):
    ig.reset_scene()
    m = materials()
    p = [ig.box("금전함", (.38, .38, .095), (0, 0, .0475), bevel=.008, segments=3, material=m["dark"]),
         ig.box("모니터받침", (.20, .19, .025), (0, .035, .1075), bevel=.008, material=m["dark"]),
         ig.box("모니터기둥", (.05, .06, .18), (0, .075, .205), material=m["dark"]),
         ig.box("직원모니터", (.37, .035, .245), (0, .08, .34), rotation=(math.radians(10), 0, 0), bevel=.009, material=m["dark"]),
         ig.box("화면", (.338, .002, .209), (0, .100, .341), rotation=(math.radians(10), 0, 0), material=m["screen"]),
         ig.box("고객표시기기둥", (.025, .025, .34), (.235, -.10, .17), material=m["dark"]),
         ig.box("고객표시기", (.19, .035, .078), (.235, -.10, .345), bevel=.006, material=m["dark"]),
         ig.box("고객표시창", (.163, .002, .051), (.235, -.119, .345), material=m["screen"]),
         ig.box("영수증프린터", (.13, .175, .13), (-.265, -.02, .065), bevel=.015, segments=3, material=m["dark"]),
         ig.box("용지출구", (.091, .004, .008), (-.265, -.109, .102), material=m["steel"]),
         ig.box("영수증끝", (.078, .035, .001), (-.265, -.117, .106), material=m["paper"]),
         ig.box("서랍틈", (.34, .001, .002), (0, -.191, .071), material=m["steel"])]
    # 손님에게는 모니터 뒷판과 케이블 홈이 보인다.
    for x in range(12):
        p.append(ig.box("방열구", (.004, .002, .050), (-.085 + x * .015, .058, .335), material=m["screen"]))
    finish("SM_RetailPOS", p, out, "한국 매장용 터치 POS. 금전함·고객표시기·감열 프린터. 직원 화면은 +Y.", collision=[], size=1024)


def bell(out):
    ig.reset_scene()
    m = materials()
    p = [ig.cylinder("받침", .043, .009, (0, 0, .0045), segments=32, bevel=.003, material=m["dark"]),
         wrapped_strip("벨돔", [(.038,.009),(.038,.019),(.032,.030),(.022,.038),(.007,.041)], 32, m["steel"]),
         ig.cylinder("누름쇠", .007, .014, (0,0,.044), segments=16, material=m["dark"])]
    finish("SM_ServiceBell", p, out, "계산대 호출벨. 지름 8.6cm, 높이 5.1cm.", collision=[], size=512)


def gondola(out):
    ig.reset_scene()
    m = materials()
    p = [ig.box("등판", (2.36, .04, 1.66), (0, 0, .83), material=m["white"]),
         ig.box("받침", (2.36, .70, .13), (0, 0, .065), bevel=.004, material=m["white"])]
    for x in (-1.19, 0, 1.19):
        p.append(ig.box("기둥", (.025, .075, 1.69), (x, 0, .845), bevel=.002, material=m["white"]))
    for z in (.24, .54, .84, 1.14, 1.44):
        for side in (-1, 1):
            p.append(ig.box("선반", (2.36, .35, .03), (0, side*.205, z), bevel=.002, material=m["white"]))
            p.append(ig.box("가격레일", (2.36, .012, .037), (0, side*.378, z-.002), material=m["steel"]))
            # 가격표는 게임에서 실제 진열 품목과 같은 위치에 붙인다.
    finish("SM_StoreGondola", p, out, "240×77.2×169cm, 120cm 모듈 두 칸. 선반 윗면 월드 31.5/61.5/91.5/121.5/151.5cm. 양면 깊이 35cm.")


def tobacco(out):
    ig.reset_scene()
    m=materials()
    p=[ig.box("뒷판", (2.20,.025,1.40), (0,-.0125,.70), material=m["white"])]
    for x in (-1.09,0,1.09):
        p.append(ig.box("구획기둥", (.02,.18,1.40), (x,-.09,.70), bevel=.002, material=m["steel"]))
    for x in (-.55,.55):
        p.append(ig.box("광고함", (1.04,.08,.30), (x,-.12,1.23), material=m["blue"]))
        p.append(ig.box("광고유백판", (.99,.005,.255), (x,-.163,1.23), material=m["paper"]))
        p.append(ig.image_quad("광고인쇄", (.99,.255), (x,-.167,1.23), m["ad"]))
        for y in (-.19,-.02):
            p.append(ig.box("상단조명", (1.04,.01,.008), (x,y,1.396), material=m["led"]))
    for row,z in enumerate((.10,.27,.44,.61,.78,.95)):
        p.append(ig.box("선반", (2.16,.155,.008), (0,-.10,z), material=m["white"]))
        p.append(ig.box("가격레일", (2.16,.007,.018), (0,-.18,z+.008), material=m["dark"]))
        for col in range(32):
            x=-1.04+col*.067
            design=(col//4+row//2)%8
            p.append(ig.box("담뱃갑", (.055,.023,.088), (x,-.139,z+.048), material=m["paper"]))
            p.append(ig.image_quad("담뱃갑앞면", (.054,.086), (x,-.151,.048+z), m["pack"], uv_rect=ig.atlas_rect(4,2,design)))
        p.append(ig.box("선반조명", (2.12,.008,.006), (0,-.158,z+.125), material=m["led"]))
    finish("SM_TobaccoCabinet",p,out,"220×19×140cm 담배장. 6단 192갑, 4열씩 같은 품목. 2개 광고함과 선반 조명.",size=2048)


def bag(out, index):
    ig.reset_scene()
    m=materials()
    bm=bmesh.new()
    uv=bm.loops.layers.uv.new("ImageUV")
    nx,nz=12,16
    grids=[]
    for side in (-1,1):
        rect=ig.atlas_rect(2,3,index) if side < 0 else ig.atlas_rect(2,2,index)
        grid=[]
        for iz in range(nz+1):
            t=iz/nz
            width=.164*(.90+.10*math.sin(math.pi*t))
            ring=[]
            for ix in range(nx+1):
                s=ix/nx
                bulge=math.sin(math.pi*s)**.55 * math.sin(math.pi*t)**.45
                crease=.0012*math.sin(ix*2.7+iz*1.9)*math.sin(math.pi*t)
                ring.append(bm.verts.new(((s-.5)*width,side*(.002+.041*bulge+crease),t*.245)))
            grid.append(ring)
        grids.append(grid)
        for iz in range(nz):
            for ix in range(nx):
                face=bm.faces.new((grid[iz][ix],grid[iz][ix+1],grid[iz+1][ix+1],grid[iz+1][ix]))
                face.material_index=0 if side < 0 else 1
                for loop in face.loops:
                    z=loop.vert.co.z/.245
                    width=.164*(.90+.10*math.sin(math.pi*z))
                    s=loop.vert.co.x/width+.5
                    if side>0: s=1-s
                    loop[uv].uv=(rect[0]+(rect[2]-rect[0])*s,rect[1]+(rect[3]-rect[1])*z)
    for iz in range(nz):
        for ix in (0,nx):
            bm.faces.new((grids[0][iz][ix],grids[1][iz][ix],grids[1][iz+1][ix],grids[0][iz+1][ix]))
    for iz in (0,nz):
        for ix in range(nx):
            bm.faces.new((grids[0][iz][ix],grids[0][iz][ix+1],grids[1][iz][ix+1],grids[1][iz][ix]))
    bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    mesh=ig._mesh_from_bm("봉지",bm)
    ob=ig._link(bpy.data.objects.new("봉지",mesh))
    mesh.materials.append(m["print"])
    mesh.materials.append(m["back"])
    for face in mesh.polygons: face.use_smooth=True
    p=[ob]
    for z in (.003,.242):
        p.append(ig.box("열접착선",(.15,.006,.005),(0,0,z),material=m["paper"]))
    finish(("SM_RetailPotato","SM_RetailShrimp","SM_RetailCorn")[index],p,out,
           "60~70g 봉지 16.4×8.6×24.5cm. 앞면은 상품명, 뒷면은 영양·보관 정보. 밀봉선과 공기층, 완만한 접힘.",size=1024)


def cups(out,index):
    ig.reset_scene()
    m=materials()
    profile=[(.052,0),(.054,.003),(.0586,.020),(.0655,.050),
             (.069,.061),(.073,.062),(.073,.065),(.067,.066)]
    body=wrapped_strip("사발",profile,128,m["white"])
    # 성형 홈은 용기 표면에 붙어 있다. 수직 막대를 둘러 세우면 아래쪽이
    # 몸통 밖으로 나와 빗살처럼 보인다. 단면 반지름만 0.35mm 변형한다.
    for vertex in body.data.vertices:
        p=vertex.co
        if .002 < p.z < .061:
            r=math.hypot(p.x,p.y);a=math.atan2(p.y,p.x)
            fade=min(1.0,(p.z-.002)/.008,(.061-p.z)/.010)
            radius=r+.00035*math.cos(32*a)*max(0,fade)
            p.x*=radius/r;p.y*=radius/r
    for face in body.data.polygons: face.use_smooth=True
    bottom=ig.cylinder("바닥",.052,.003,(0,0,.0015),segments=48,material=m["white"])
    lid=ig.cylinder("종이뚜껑",.073,.0008,(0,0,.0664),segments=64,material=m["print"])
    layer=lid.data.uv_layers.new(name="ImageUV")
    rect=ig.atlas_rect(2,3,4+index)
    for li,loop in enumerate(lid.data.loops):
        v=lid.data.vertices[loop.vertex_index].co
        layer.data[li].uv=(rect[0]+(rect[2]-rect[0])*(v.x/.15+.5),rect[1]+(rect[3]-rect[1])*(v.y/.15+.5))
    p=[body,bottom,lid,ig.box("뜯는탭",(.018,.012,.0008),(0,-.074,.0664),material=m["paper"])]
    finish(("SM_RetailCupBeef","SM_RetailCupKimchi")[index],p,out,
           "86g 사발면 14.6×14.6×6.68cm. 연속된 흰 성형 용기·0.35mm 홈·인쇄 종이뚜껑. 분리된 리브 원통을 제거. 농심 공식 사발면 사진 참조.",size=1024)


def biscuit(out):
    ig.reset_scene()
    m=materials()
    carton=ig.mat_plastic("갈색인쇄종이",(.105,.031,.008),.52,bump=0)
    body=ig.box("접은종이상자",(.19,.055,.125),(0,0,.0625),bevel=.001,segments=1,material=carton)
    face=ig.image_quad("앞면",(.188,.123),(0,-.028,.0625),m["print"],uv_rect=ig.atlas_rect(2,3,3))
    back=ig.image_quad("뒷면",(.188,.123),(0,.028,.0625),m["back"],rotation=(0,0,math.pi),uv_rect=ig.atlas_rect(2,2,3))
    # 위·옆 접힘 면에도 원화의 작은 제품명을 배치한다. 흰 민무늬 상자가 보이지 않는다.
    top=ig.image_quad("윗면인쇄",(.184,.050),(0,0,.1256),m["back"],rotation=(-math.pi/2,0,0),uv_rect=(.52,.385,.94,.49))
    sides=[]
    for side in (-1,1):
        sides.append(ig.image_quad("옆면인쇄",(.051,.12),(side*.0956,0,.0625),m["back"],rotation=(0,0,side*math.pi/2),uv_rect=(.53,.02,.97,.48)))
    finish("SM_RetailBiscuit",[body,face,back,top]+sides,out,"비스킷 소매 상자 19×5.5×12.5cm. 앞면 원화, 뒷면 제품 정보, 갈색 접힘 면을 각각 매핑했다.",size=1024)


def pet(out):
    ig.reset_scene()
    m=materials()
    # 삼다수 제품 사진의 어깨, 목, 링, 잡는 부분 비례를 기준으로 한 500mL 몸통.
    profile=[(.026,0),(.032,.005),(.033,.012)]
    for z in (.021,.032,.043,.054,.065):
        profile.extend([(.033,z-.002),(.0315,z),(.033,z+.002)])
    profile += [(.033,.075),(.033,.130),(.032,.140),(.030,.150),(.026,.163),(.020,.177),(.014,.187),(.014,.201)]
    body=wrapped_strip("페트병",profile,48,m["white"])
    base=ig.cylinder("바닥",.026,.003,(0,0,.0015),segments=48,material=m["white"])
    ig.build_asset("SM_WaterBottle","prop",[body,base],out,raw_uv=True,preview=True,
                   notes="500mL PET 몸통 지름 6.6cm, 높이 20.1cm. 목 위에 별도 마개. 가로 성형 리브와 둥근 어깨.")


if __name__ == "__main__":
    out=ig.out_root_from_argv()
    wanted=sys.argv[sys.argv.index("--")+2:] if "--" in sys.argv else []
    tasks={"counter":counter,"pos":pos,"bell":bell,"gondola":gondola,"tobacco":tobacco,
           "potato":lambda o:bag(o,0),"shrimp":lambda o:bag(o,1),"corn":lambda o:bag(o,2),
           "beef":lambda o:cups(o,0),"kimchi":lambda o:cups(o,1),"biscuit":biscuit,"pet":pet}
    for key,task in tasks.items():
        if not wanted or key in wanted: task(out)
    ig.log("RETAIL_REFRESH PASS")

# -*- coding: utf-8 -*-
"""为 MuMain【原生客户端】生成挂机点表头 src/source/MUHelper/AfkSpotsData.h

跑法：PGPW='<口令>' python gen_afk_spots_native.py

=== 为什么和网页端是两套数据 ===
网页端 afkSpots.js 是给【服务器离线幽灵】调的：幽灵钉死在半径 R=15 的圆里
（4bit HuntingRange 上限），怪死后在整个刷怪矩形随机重生、圈外的怪没人看见就
站着不回来，所以那边按 R=15 算「圈内期望怪数 qc / 矩形落圈比例 f」。

MuMain 原生客户端是【在线主动巡逻】，挂机设置的找怪范围 iHuntingRange 上限只有 6
（NewUIMuHelper.cpp: MAX_HUNTING_RANGE=6，默认也是 6），实际欧氏找怪距离 =
ceil(sqrt(6^2+6^2)) = 9（MuHelper.cpp ComputeDistanceByRange / GetNearestTarget 用
欧氏距离比较）。把 R=15 的点直接搬过来，圈内怪数按 (9/15)^2 ≈ 1/3 缩水，洛伦西亚
那种大刷怪矩形的圆心处只剩一两只——这就是「走到了没人」。

所以这里用 R=9（严格圆 dx^2+dy^2<=81）重新挑点，让每个点在客户端最大找怪圈内就
有怪。在线玩家会在圈内走动追怪、被观察到的怪也会游荡，所以仍偏好 f≈1 的固定刷怪
窝（1x1 / 整个矩形落进圈里，怪就钉在那片），但不只收 f=1。

输出直接写 AfkSpotsData.h（CNewUIAfkSpotWindow / CMuHelper 消费），不碰网页端。
地形/刷怪判据照抄 OpenMU（见网页端 gen_afk_spots.py 头部说明）：
  角色可站 = TerrainData 值 0 或 1；怪可刷 = 值恰好 0（可走且非安全区）。
"""
import io
import os
import sys
import math
import pg8000

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "MUWebClient", "tools"))
import mu_paths  # noqa: E402

for stream in (sys.stdout, sys.stderr):
    try:
        stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# 客户端最大找怪欧氏半径：iHuntingRange=6 -> ceil(sqrt(72))=9。
RANGE_FIELD = 6
R = int(math.ceil(math.sqrt(RANGE_FIELD * RANGE_FIELD * 2)))   # = 9
R2 = R * R
MIN_SPACING = 2 * R          # 两个圈不重叠（切比雪夫 >= 18）
TOP_N = 8
SAME_SIG_MAX = 2
GRID_STRIDE = 4              # R 小了，候选网格也加密

# 与网页端一致的 19 张可渲染图
MAPSET = sorted([0, 1, 2, 3, 4, 6, 7, 8, 10, 33, 37, 38, 51, 56, 57, 63, 79, 80, 81])

OUT_H = os.path.join(mu_paths.MUMAIN, "src", "source", "MUHelper", "AfkSpotsData.h")

conn = pg8000.connect(host='192.168.1.110', port=5432, user='postgres',
                      password=os.environ['PGPW'], database='openmu')
cur = conn.cursor()

cur.execute('SELECT "Number", "TerrainData" FROM config."GameMapDefinition"')
TD = {}
for no, td in cur.fetchall():
    if td is None:
        continue
    TD[no] = bytes(td[3:]) if len(td) >= 3 + 65536 else bytes(td)


def stand(no, x, y):
    t = TD.get(no)
    if t is None or x < 0 or y < 0 or x >= 256 or y >= 256:
        return False
    return t[y * 256 + x] in (0, 1)


def spawnable(no, x, y):
    t = TD.get(no)
    if t is None or x < 0 or y < 0 or x >= 256 or y >= 256:
        return False
    return t[y * 256 + x] == 0


def snap_to_stand(no, x, y, max_r=10):
    if stand(no, x, y):
        return (x, y)
    for r in range(1, max_r + 1):
        for dx in range(-r, r + 1):
            for dy in range(-r, r + 1):
                if max(abs(dx), abs(dy)) != r:
                    continue
                if stand(no, x + dx, y + dy):
                    return (x + dx, y + dy)
    return None


cur.execute("""
SELECT m."Number", sa."X1", sa."Y1", sa."X2", sa."Y2", COALESCE(sa."Quantity", 0),
       split_part(mo."Designation", '||', 1),
       EXTRACT(EPOCH FROM mo."RespawnDelay"),
       MAX(CASE WHEN split_part(ad."Designation", '||', 1) = 'Level' THEN ma."Value" END)
  FROM config."MonsterSpawnArea" sa
  JOIN config."GameMapDefinition" m ON m."Id" = sa."GameMapId"
  JOIN config."MonsterDefinition" mo ON mo."Id" = sa."MonsterDefinitionId"
  LEFT JOIN config."MonsterAttribute" ma ON ma."MonsterDefinitionId" = mo."Id"
  LEFT JOIN config."AttributeDefinition" ad ON ad."Id" = ma."AttributeDefinitionId"
 WHERE COALESCE(mo."ObjectKind", 0) = 0 AND COALESCE(sa."Quantity", 0) > 0
 GROUP BY m."Number", sa."Id", sa."X1", sa."Y1", sa."X2", sa."Y2", sa."Quantity",
          mo."Designation", mo."RespawnDelay"
""")
AREAS = {}
for no, x1, y1, x2, y2, q, name, rd, lv in cur.fetchall():
    if no not in MAPSET or no not in TD:
        continue
    xa, xb = min(x1, x2), max(x1, x2)
    ya, yb = min(y1, y2), max(y1, y2)
    cells = sum(1 for y in range(ya, yb + 1) for x in range(xa, xb + 1) if spawnable(no, x, y))
    if cells == 0:
        continue
    AREAS.setdefault(no, []).append(dict(
        xa=xa, xb=xb, ya=ya, yb=yb, q=int(q), name=name,
        rd=round(float(rd or 10), 1), cells=cells, lv=int(lv or 1)))
conn.close()


def evaluate(no, cx, cy):
    """圆心 (cx,cy)、欧氏半径 R 的挂机价值。逐格数圈内可刷格，按面积占比摊怪数。"""
    areas = AREAS.get(no, [])
    hit = {}
    for y in range(cy - R, cy + R + 1):
        if y < 0 or y > 255:
            continue
        dy = y - cy
        for x in range(cx - R, cx + R + 1):
            if x < 0 or x > 255:
                continue
            dx = x - cx
            if dx * dx + dy * dy > R2:
                continue
            if not spawnable(no, x, y):
                continue
            for i, a in enumerate(areas):
                if a['xa'] <= x <= a['xb'] and a['ya'] <= y <= a['yb']:
                    hit[i] = hit.get(i, 0) + 1
    mons, qty, sus = [], 0.0, 0.0
    for i, ins in hit.items():
        a = areas[i]
        f = ins / a['cells']
        qc = a['q'] * f
        if qc < 0.05:
            continue
        qty += qc
        if f >= 0.9:
            sus += qc
        mons.append(dict(m=a['name'], q=a['q'], qc=round(qc, 2), lv=a['lv'], f=round(f, 3)))
    if not mons:
        return None
    merged = {}
    for m in mons:
        k = m['m']
        if k in merged:
            o = merged[k]
            tot = o['qc'] + m['qc']
            o['f'] = round((o['f'] * o['qc'] + m['f'] * m['qc']) / max(tot, 1e-6), 3)
            o['qc'] = round(tot, 2)
            o['q'] += m['q']
        else:
            merged[k] = dict(m)
    mons = sorted(merged.values(), key=lambda m: -m['qc'])
    strongest = max(mons, key=lambda m: m['lv'])
    return dict(x=cx, y=cy, qty=round(qty, 1), sus=round(sus, 1),
                monsters=mons, lv=strongest['lv'], lvMin=min(m['lv'] for m in mons))


def candidates(no):
    seen, out = set(), []
    for a in AREAS.get(no, []):
        pts = [((a['xa'] + a['xb']) // 2, (a['ya'] + a['yb']) // 2)]
        if a['xb'] - a['xa'] > GRID_STRIDE or a['yb'] - a['ya'] > GRID_STRIDE:
            for y in range(a['ya'], a['yb'] + 1, GRID_STRIDE):
                for x in range(a['xa'], a['xb'] + 1, GRID_STRIDE):
                    pts.append((x, y))
        for (x, y) in pts:
            p = snap_to_stand(no, x, y)
            if p and p not in seen:
                seen.add(p)
                out.append(p)
    return out


def rough_score(s):
    # 可持续怪窝优先（f>=0.9，站着就有怪），其次圈内总期望怪数。
    return s['sus'] * 4 + s['qty']


def sig(s):
    tot = max(s['qty'], 1e-6)
    return tuple(sorted(m['m'] for m in s['monsters'] if m['qc'] / tot >= 0.1)) \
        or (s['monsters'][0]['m'],)


def monster_label(monsters):
    ordered = sorted(monsters, key=lambda mo: mo.get("qc", 0), reverse=True)
    return ";".join(mo['m'] for mo in ordered[:2])


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


OUT = {}
for no in MAPSET:
    if no not in AREAS:
        OUT[no] = []
        print("map %3d: 无可挂机怪" % no)
        continue
    scored = [s for (x, y) in candidates(no)
              for s in [evaluate(no, x, y)] if s]
    scored.sort(key=rough_score, reverse=True)

    def far_enough(s, picked):
        return all(max(abs(s['x'] - p['x']), abs(s['y'] - p['y'])) >= MIN_SPACING
                   for p in picked)

    picked, used = [], {}
    for s in scored:
        if len(picked) >= TOP_N:
            break
        k = sig(s)
        if used.get(k) or not far_enough(s, picked):
            continue
        picked.append(s)
        used[k] = 1
    for s in scored:
        if len(picked) >= TOP_N:
            break
        k = sig(s)
        if used.get(k, 0) >= SAME_SIG_MAX or not far_enough(s, picked):
            continue
        picked.append(s)
        used[k] = used.get(k, 0) + 1
    picked.sort(key=rough_score, reverse=True)
    OUT[no] = picked
    tag = ' '.join("(%d,%d)q%.0f/s%.0fLv%d" %
                   (p['x'], p['y'], p['qty'], p['sus'], p['lv']) for p in picked)
    print("map %3d (R=%d): %d 点  %s" % (no, R, len(picked), tag))

# ---- 写 AfkSpotsData.h ----
lines = []
lines.append("// AUTO-GENERATED by MuMain/tools/gen_afk_spots_native.py — do not edit by hand.")
lines.append("// 数据源：config.MonsterSpawnArea + TerrainData，按【原生客户端】找怪半径")
lines.append("// iHuntingRange=%d (欧氏 R=%d) 重算；与网页端离线幽灵(R=15)的 afkSpots.js 是两套。" % (RANGE_FIELD, R))
lines.append("#pragma once")
lines.append("")
lines.append("struct AfkSpot")
lines.append("{")
lines.append("    int x;")
lines.append("    int y;")
lines.append("    int lv;        // 这一簇里最强怪的等级")
lines.append("    int lvMin;     // 最低怪等级")
lines.append("    const char* mons;  // 圈内怪数前 2，';' 分隔（英文）")
lines.append("};")
lines.append("")
lines.append("struct AfkSpotList")
lines.append("{")
lines.append("    int world;            // ENUM_WORLD")
lines.append("    const AfkSpot* spots;")
lines.append("    int count;")
lines.append("};")
lines.append("")

lists = []
for no in MAPSET:
    spots = OUT[no]
    if not spots:
        continue
    arr = "AFK_SPOTS_W%d" % no
    lists.append((no, arr, len(spots)))
    lines.append("static const AfkSpot %s[] = {" % arr)
    for sp in spots:
        mons = c_escape(monster_label(sp["monsters"]))
        lines.append("    { %d, %d, %d, %d, \"%s\" },"
                     % (sp["x"], sp["y"], sp["lv"], sp["lvMin"], mons))
    lines.append("};")
    lines.append("")

lines.append("static const AfkSpotList AFK_SPOTS[] = {")
for world, arr, n in lists:
    lines.append("    { %d, %s, %d }," % (world, arr, n))
lines.append("};")
lines.append("")
lines.append("static const int AFK_SPOTS_WORLD_COUNT = sizeof(AFK_SPOTS) / sizeof(AFK_SPOTS[0]);")
lines.append("")
lines.append("inline const AfkSpotList* GetAfkSpotsForWorld(int world)")
lines.append("{")
lines.append("    for (int i = 0; i < AFK_SPOTS_WORLD_COUNT; ++i)")
lines.append("    {")
lines.append("        if (AFK_SPOTS[i].world == world)")
lines.append("            return &AFK_SPOTS[i];")
lines.append("    }")
lines.append("    return nullptr;")
lines.append("}")
lines.append("")

with io.open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines))
print("\n已写入 %s：%d 张图、%d 个点" %
      (OUT_H, len(lists), sum(n for _, _, n in lists)))

#!/usr/bin/env python3
"""디렉터가 런타임에 세우는 소품이 계약대로 설정되는지 확인한다.

씬이 `CreateBlock`으로 짓는 것은 [audit_world_geometry.py](audit_world_geometry.py)가
좌표까지 읽는다. 그런데 밤과 퍼즐의 소품은 씬이 아니라 디렉터가 세운다 —
`SpawnActor<AClass>`로 액터를 띄우고 `Configure`나
`ConfigurePrototypeVisuals`로 메시·재질·크기를 붙인다. 그 경로에는 아무
검사도 없었고, 실제로 이런 것들이 그대로 빌드에 들어가 있었다.

* 민원 대장이 1m 정육면체로 부풀어 관리실 책상을 덮었다
* 조율 렌치가 같은 이유로 1m가 됐다
* 관리실 문짝과 손잡이, 채널 선택기가 재질 없이 엔진 기본 격자로 그려졌다

두 결함의 뿌리가 같다. **같은 인자가 함수마다 뜻이 다르다.**

* `Size / 100` 계열 — 100cm 엔진 큐브를 가정한다. `(100,100,100)`은 스케일 1,
  즉 「메시가 저작된 크기 그대로」다
* `Size / 메시바운드` 계열 — 맞출 실제 크기다. 같은 `(100,100,100)`이
  **1m 정육면체로 강제**된다

호출부는 양쪽에 같은 관용구를 썼다. 그래서 이 감사는 함수 목록을 손으로 적지
않는다. 구현을 읽어 그 함수가 어느 계열인지 스스로 분류하고, 호출부를 그
분류에 대조한다. 새 Configure가 생겨도 따라온다.

설정만 보는 것이 아니라 **자리**도 본다. 크기 인자를 실제 월드 상자로 풀어야
하는데, 저작 메시를 그대로 쓰는 소품의 실제 크기는 소스가 아니라 구운 에셋에
있다. `Docs/mesh_bounds.json`이 그 값이고 `Scripts/export_mesh_bounds.py`가
에디터에서 뽑는다.

보는 것은 다섯이다.

* ``UNIT_MISMATCH``   - 바운드·원시 배율 계열에 `(100,100,100)`을 넘긴 자리
* ``PLACEHOLDER_MATERIAL`` - 재질 자리에 `nullptr`. 엔진 기본 격자가 나온다
* ``UNKNOWN_SEMANTICS`` - 크기 의미를 못 읽은 Configure. 보고만 한다
* ``PROP_OVERLAP``    - 보이는 소품 둘이 서로를 뚫는다
* ``PROP_THROUGH_SCENE`` - 보이는 소품이 씬의 소품을 뚫는다
* ``PROP_SUNK``       - 보이는 소품의 밑면이 얹힌 상판보다 아래다

찾았지만 아직 안 고친 겹침은 `CARRIED_OVERLAPS`에 이유와 함께 적는다.
실패로 세지 않되 매번 화면에 남고, 목록에만 남고 실제로는 안 걸리는
항목도 실패로 본다 — 낡은 미결은 「여기는 원래 이렇다」로 읽힌다.

벽·바닥·책상 같은 구조물과의 겹침과 문은 보지 않는다 — 벽에 박는 밸브,
상판에 얹는 종이, 문틀에 물린 문짝은 겹치는 것이 정상이고, 그 경계를 여기서
새로 정하면 기존 기하 감사와 서로 다른 두 기준이 생긴다.

어디서든 돌아간다.

    python3 Scripts/audit_director_props.py             # 사람이 읽는 보고
    python3 Scripts/audit_director_props.py --json      # 기계가 읽는 보고
    python3 Scripts/audit_director_props.py --check     # 발견되면 exit 1
    python3 Scripts/audit_director_props.py --self-test # 감사 자체를 검사
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from audit_world_geometry import (  # noqa: E402
    Unresolved, Vec3, Rot3, rotate_vector, rotated_extent,
    match_call,
    split_arguments,
    strip_comments,
)

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_ROOT = os.path.join("Source", "IndieGame")

CONFIGURE_NAMES = ("Configure", "ConfigurePrototypeVisuals")

# void/bool AClass::Configure( / ...ConfigurePrototypeVisuals(
# 디렉터의 Configure는 성공 여부를 돌려주므로 bool이다. void만 보면 그
# 일곱은 서명을 못 읽고, 호출부가 「대조하지 못함」으로 남는다 — 무관한
# 호출을 미검사로 세는 셈이다.
DEFINITION_PATTERN = re.compile(
    r"\b(?:void|bool)\s+(?P<owner>A\w+)::(?P<name>"
    + "|".join(CONFIGURE_NAMES)
    + r")\s*\(")
# Ptr = ...SpawnActor<AClass>(
SPAWN_PATTERN = re.compile(
    r"(?P<target>[\w.\->]+?)\s*=\s*[^;=]*?SpawnActor<\s*(?P<owner>A\w+)\s*>\s*\(")
# Ptr->Configure( / Ptr->ConfigurePrototypeVisuals(
CALL_PATTERN = re.compile(
    r"(?P<target>\w+)\s*(?:->|\.)\s*(?P<name>"
    + "|".join(CONFIGURE_NAMES)
    + r")\s*\(")

# 100cm 엔진 큐브를 가정하고 나누는 자리. 이 계열에서 (100,100,100)은 스케일 1이다.
UNIT_SCALE_PATTERN = re.compile(r"/\s*100(?:\.0+)?f?\s*\)")
# 메시의 실제 바운드로 나누는 자리.
TARGET_BOUNDS_PATTERN = re.compile(r"BoxExtent|GetBounds\s*\(")

UNIT_SCALE = "UNIT_SCALE"
TARGET_BOUNDS = "TARGET_BOUNDS"
# 나누지 않고 컴포넌트 스케일에 그대로 넣는 계열. 여기서 (100,100,100)은
# 100배다 — 세 관례 중 가장 조용히 틀리는 자리다.
RAW_SCALE = "RAW_SCALE"
UNKNOWN = "UNKNOWN"

# 「이 메시가 저작된 크기 그대로」를 뜻하려고 쓰는 관용구.
AUTHORED_SIZE_IDIOM = re.compile(
    r"FVector\s*\(\s*100(?:\.0+)?f?\s*(?:,\s*100(?:\.0+)?f?\s*){0,2}\)")


# 찾았지만 아직 안 고친 겹침. 면제가 아니라 **미결**이다 — 면제는 「이대로가
# 맞다」이고 이건 「눈으로 보고 정해야 한다」다. 실패로 세지 않되 매번 적는다.
#
# RAW_SCALE 계열의 자리를 풀 수 있게 되면서 처음 보였다. 둘 다 벽에 붙은
# 장식 상호작용물이고, 벽 장식과 어느 쪽이 물러나야 하는지는 화면에서 정할
# 일이다.
#
# 키는 소품 이름이다. 줄 번호로 잡았더니 그 위에 코드를 한 줄 넣는 것만으로
# 목록이 어긋났고, 같은 겹침이 「더 이상 안 걸린다」와 「발견」을 동시에
# 말했다.
CARRIED_OVERLAPS = {}


def carried_owner(finding) -> str | None:
    """발견이 미결로 적어 둔 소품의 것인지 본다. 상세의 첫 낱말이 이름이다."""
    name = finding.detail.split("이 ", 1)[0].strip()
    return name if name in CARRIED_OVERLAPS else None


@dataclass(frozen=True)
class Signature:
    """하나의 Configure 구현에서 읽어낸 인자 배치와 크기 의미."""

    owner: str
    name: str
    site: str
    mesh_index: int | None
    material_indices: tuple
    size_index: int | None
    semantics: str
    visibility_index: int | None = None

    @property
    def key(self) -> tuple:
        return (self.owner, self.name)


@dataclass(frozen=True)
class Finding:
    code: str
    site: str
    detail: str

    def to_dict(self) -> dict:
        return {"code": self.code, "site": self.site, "detail": self.detail}


def _line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def _parameter_type_and_name(declaration: str) -> tuple:
    """'const FVector& SizeCentimeters' -> ('const FVector&', 'SizeCentimeters')."""
    cleaned = declaration.strip().split("=")[0].strip()
    if not cleaned:
        return "", ""
    parts = cleaned.replace("*", "* ").replace("&", "& ").split()
    if len(parts) < 2:
        return cleaned, ""
    return " ".join(parts[:-1]), parts[-1]


def parse_signatures(sources: dict) -> dict:
    """Configure 구현들을 읽어 (소유 클래스, 이름) -> Signature로 돌려준다."""
    signatures = {}
    non_visual = set()
    for relative_path, raw in sorted(sources.items()):
        text = strip_comments(raw)
        for match in DEFINITION_PATTERN.finditer(text):
            open_paren = text.index("(", match.end() - 1)
            try:
                argument_text, close = match_call(text, open_paren)
            except Unresolved:
                continue
            body_start = text.find("{", close)
            if body_start < 0:
                continue
            depth, index = 0, body_start
            while index < len(text):
                if text[index] == "{":
                    depth += 1
                elif text[index] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                index += 1
            body = text[body_start:index]

            mesh_index = None
            material_indices = []
            size_index = None
            size_name = ""
            visibility_index = None
            for position, declaration in enumerate(
                    split_arguments(argument_text)):
                kind, name = _parameter_type_and_name(declaration)
                if "UStaticMesh*" in kind.replace(" ", "") and mesh_index is None:
                    mesh_index = position
                elif "UMaterialInterface*" in kind.replace(" ", ""):
                    material_indices.append(position)
                elif "FVector" in kind and size_index is None:
                    size_index = position
                    size_name = name
                elif "bool" in kind and "Visible" in name:
                    visibility_index = position

            semantics = UNKNOWN
            if size_name:
                # 크기 인자가 실제로 어떻게 쓰이는지만 본다. 본문 어딘가의
                # 무관한 나눗셈에 걸리지 않도록 이름을 함께 요구한다.
                scale_use = re.search(
                    re.escape(size_name) + r"\s*/\s*100(?:\.0+)?f?", body)
                raw_use = re.search(
                    r"SetRelativeScale3D\s*\(\s*" + re.escape(size_name)
                    + r"\s*\)", body)
                if scale_use or UNIT_SCALE_PATTERN.search(
                        _uses_of(body, size_name)):
                    semantics = UNIT_SCALE
                elif raw_use:
                    semantics = RAW_SCALE
                elif TARGET_BOUNDS_PATTERN.search(body):
                    semantics = TARGET_BOUNDS

            if mesh_index is None and not material_indices:
                # 좌표만 받는 Configure가 있다. 시각 설정이 아니므로 첫 FVector를
                # 크기로 읽으면 안 되고, 그 호출부도 미검사가 아니라 무관이다.
                non_visual.add((match.group("owner"), match.group("name")))
                continue

            site = f"{relative_path}:{_line_of(text, match.start())}"
            signature = Signature(
                match.group("owner"), match.group("name"), site,
                mesh_index, tuple(material_indices), size_index, semantics,
                visibility_index)
            signatures[signature.key] = signature
    return signatures, non_visual


def _uses_of(body: str, name: str) -> str:
    """크기 인자가 나오는 줄만 모아 준다."""
    return "\n".join(
        line for line in body.splitlines() if name in line)


def parse_call_sites(sources: dict) -> list:
    """(파일:줄, 소유 클래스, 호출 이름, 인자 목록)을 모은다."""
    sites = []
    for relative_path, raw in sorted(sources.items()):
        text = strip_comments(raw)
        spawns = [
            (match.start(), match.group("target").strip(), match.group("owner"))
            for match in SPAWN_PATTERN.finditer(text)
        ]
        for match in CALL_PATTERN.finditer(text):
            target = match.group("target")
            owner = None
            for offset, spawned, spawned_owner in spawns:
                if offset < match.start() and spawned.endswith(target):
                    owner = spawned_owner
            if owner is None:
                continue
            open_paren = text.index("(", match.end() - 1)
            try:
                argument_text, _ = match_call(text, open_paren)
            except Unresolved:
                continue
            sites.append((
                f"{relative_path}:{_line_of(text, match.start())}",
                owner,
                match.group("name"),
                [argument.strip() for argument in split_arguments(argument_text)],
            ))
    return sites


# class INDIEGAME_API AIGBatteryPickup : public AIGPickupItem
PARENT_PATTERN = re.compile(
    r"\bclass\s+(?:\w+_API\s+)?(?P<child>A\w+)\s*(?:final\s*)?:\s*public\s+(?P<parent>A\w+)")


def parse_parents(project_root: str) -> dict:
    """자식 클래스 -> 부모 클래스. 헤더의 UCLASS 선언에서 읽는다(감사 본문은 .cpp만 읽는다)."""
    parents = {}
    root = os.path.join(project_root, SOURCE_ROOT)
    for directory, _, files in os.walk(root):
        for filename in files:
            if not filename.endswith(".h"):
                continue
            with open(os.path.join(directory, filename), "r", encoding="utf-8", errors="replace") as handle:
                for match in PARENT_PATTERN.finditer(strip_comments(handle.read())):
                    parents.setdefault(match.group("child"), match.group("parent"))
    return parents


def adopt_inherited(sites: list, signatures: dict, non_visual, parents: dict) -> list:
    """자식 클래스로 띄운 소품의 호출부를 Configure를 정의한 조상에 붙인다.

    건전지 줍기(AIGBatteryPickup)는 AIGPickupItem의 ConfigurePrototypeVisuals를
    물려받는다. 띄운 클래스 이름만 보면 서명을 못 찾아 「대조하지 못함」으로
    남는다 — 실제로는 부모의 계약 그대로 검사해야 하는 자리다.
    """
    adopted = []
    for site, owner, name, arguments in sites:
        resolved_owner = owner
        seen = set()
        while ((resolved_owner, name) not in signatures
               and (resolved_owner, name) not in non_visual
               and resolved_owner in parents
               and resolved_owner not in seen):
            seen.add(resolved_owner)
            resolved_owner = parents[resolved_owner]
        if (resolved_owner, name) in signatures or (resolved_owner, name) in non_visual:
            owner = resolved_owner
        adopted.append((site, owner, name, arguments))
    return adopted


def audit(signatures: dict, sites: list, non_visual=frozenset()) -> tuple:
    """(findings, resolved, unresolved)."""
    findings = []
    resolved = 0
    unresolved = 0

    for signature in sorted(signatures.values(), key=lambda s: s.key):
        if signature.size_index is not None and signature.semantics == UNKNOWN:
            findings.append(Finding(
                "UNKNOWN_SEMANTICS", signature.site,
                f"{signature.owner}::{signature.name}의 크기 인자가 어떻게 "
                f"쓰이는지 읽지 못했다. 호출부를 대조할 수 없다"))

    for site, owner, name, arguments in sites:
        signature = signatures.get((owner, name))
        if signature is None:
            if (owner, name) not in non_visual:
                unresolved += 1
            continue
        resolved += 1

        # 그림을 끄고 상호작용만 맡기는 소품은 재질이 없어도 된다. 씬이 이미
        # 그 자리에 진짜 물건을 세워 둔 경우다.
        hidden = (
            signature.visibility_index is not None
            and signature.visibility_index < len(arguments)
            and arguments[signature.visibility_index].split("*/")[-1].strip()
            == "false")
        if hidden:
            continue

        for index in signature.material_indices:
            if index >= len(arguments):
                continue
            if arguments[index] == "nullptr":
                findings.append(Finding(
                    "PLACEHOLDER_MATERIAL", site,
                    f"{owner}::{name}의 {index}번 재질 인자가 nullptr이다. "
                    f"엔진 기본 격자가 그대로 보인다"))

        if signature.size_index is None or signature.size_index >= len(arguments):
            continue
        size_argument = arguments[signature.size_index]
        if AUTHORED_SIZE_IDIOM.search(size_argument):
            if signature.semantics == TARGET_BOUNDS:
                findings.append(Finding(
                    "UNIT_MISMATCH", site,
                    f"{owner}::{name}은 크기를 메시 바운드로 나눈다. 여기에 넘긴 "
                    f"(100,100,100)은 스케일 1이 아니라 1m 정육면체다 — 저작된 "
                    f"크기를 쓰려면 0을 넘겨라"))
            elif signature.semantics == RAW_SCALE:
                findings.append(Finding(
                    "UNIT_MISMATCH", site,
                    f"{owner}::{name}은 인자를 컴포넌트 스케일에 그대로 넣는다. "
                    f"여기에 넘긴 (100,100,100)은 100배다 — 저작된 크기를 쓰려면 "
                    f"(1,1,1)을 넘겨라"))

    return findings, resolved, unresolved


# --------------------------------------------------------------------------
# 자리 검사. 크기 인자를 실제 월드 상자로 풀어, 보이는 소품끼리 그리고 씬의
# 소품과 관통하는지 본다. 벽·바닥·책상 같은 구조물과의 겹침은 보지 않는다 —
# 벽에 박는 밸브나 상판에 얹는 종이는 겹치는 것이 정상이고, 그 경계를 여기서
# 새로 정하면 기존 기하 감사와 서로 다른 두 기준이 생긴다.
#
# 크기 인자만으로는 월드 상자를 알 수 없다. 저작 메시를 그대로 쓰는 소품이
# 많고, 그 실제 크기는 소스가 아니라 구운 에셋에 있다. Docs/mesh_bounds.json이
# 그 값이고 Scripts/export_mesh_bounds.py가 에디터에서 뽑는다.
# --------------------------------------------------------------------------

# 문은 자리 검사에서 뺀다. 저작된 상자는 「닫힌 문짝」이고, 문은 문틀·문턱·
# 발판과 닿는 것이 정상이며 여닫는 호가 물건 위를 지나가는 것도 설계다.
# 여기서 그 경계를 새로 정하면 오탐만 늘어난다.
DOOR_CLASSES = frozenset({"AIGSwingDoor", "AIGSlidingDoor"})

MESH_BOUNDS_RELATIVE = os.path.join("Docs", "mesh_bounds.json")
ENGINE_UNIT_HALF = 50.0
# 접촉과 관통을 가르는 여유. 0.1cm 띄워 얹은 종이를 관통이라 부르지 않는다.
PENETRATION_TOLERANCE = 0.5

LOCATION_CONSTANT = re.compile(
    r"\bconst\s+FVector\s+(?P<name>\w+)\s*\(\s*(?P<args>[^)]*)\)\s*;")
MESH_BINDING = re.compile(
    r"(?P<name>\w+)\s*=\s*[^;]*?(?:/Game/Meshes/(?P<path>SM_\w+)\."
    r"|LoadMesh\(\s*TEXT\(\s*\"(?P<short>SM_\w+)\")")
FLOAT_LITERAL = re.compile(r"^-?\d+(?:\.\d*)?$")


def _floats(text: str, count: int):
    parts = [part.strip().rstrip("f") for part in text.split(",")]
    if len(parts) != count or not all(FLOAT_LITERAL.match(p) for p in parts):
        return None
    return tuple(float(p) for p in parts)


def load_mesh_bounds(project_root: str) -> dict:
    path = os.path.join(project_root, MESH_BOUNDS_RELATIVE)
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle).get("meshes", {})


def _resolve_mesh(argument: str, bindings: dict):
    """삼항이면 저작 메시 쪽 가지를 고른다. 못 고르면 None."""
    for token in re.split(r"[?:]", argument):
        token = token.strip()
        if token in bindings:
            return bindings[token]
        if token.startswith("SM_"):
            return token
    return None


def _resolve_location(argument: str, constants: dict):
    literal = re.search(r"FVector\s*\(([^()]*)\)", argument)
    if literal:
        values = _floats(literal.group(1), 3)
        if values:
            return values
    name = argument.strip().split("::")[-1].strip()
    return constants.get(name)


def prop_boxes(sources: dict, signatures: dict, mesh_bounds: dict) -> tuple:
    """디렉터 소품의 월드 AABB를 (자리, 이름, 최소, 최대, 보임)으로 돌려준다."""
    resolved, unresolved = [], 0
    for relative_path, raw in sorted(sources.items()):
        text = strip_comments(raw)
        constants = {}
        for match in LOCATION_CONSTANT.finditer(text):
            values = _floats(match.group("args"), 3)
            if values:
                constants[match.group("name")] = values
        bindings = {}
        for match in MESH_BINDING.finditer(text):
            bindings[match.group("name")] = (
                match.group("path") or match.group("short"))

        for spawn in SPAWN_PATTERN.finditer(text):
            if spawn.group("owner") in DOOR_CLASSES:
                continue
            target = spawn.group("target").strip().split("->")[-1]
            open_paren = text.index("(", spawn.end() - 1)
            try:
                spawn_arguments, spawn_close = match_call(text, open_paren)
            except Unresolved:
                continue

            location = None
            rotation = Rot3()
            transform = re.search(r"FTransform\s*\(", spawn_arguments)
            if transform:
                try:
                    inner, _ = match_call(spawn_arguments, transform.end() - 1)
                except Unresolved:
                    inner = ""
                pieces = split_arguments(inner)
                if pieces:
                    rot_match = re.fullmatch(r"FRotator\s*\(([^()]*)\)", pieces[0].strip())
                    rot_values = _floats(rot_match.group(1), 3) if rot_match else None
                    if rot_values:
                        rotation = Rot3(*rot_values)
                    location = _resolve_location(pieces[-1], constants)
            if location is None:
                continue

            call = CALL_PATTERN.search(text, spawn_close)
            if call is None or call.group("target") != target:
                continue
            signature = signatures.get(
                (spawn.group("owner"), call.group("name")))
            if signature is None or signature.size_index is None:
                continue
            try:
                call_text, _ = match_call(text, text.index("(", call.end() - 1))
            except Unresolved:
                continue
            arguments = [a.strip() for a in split_arguments(call_text)]
            if signature.size_index >= len(arguments):
                continue

            visible = not (
                signature.visibility_index is not None
                and signature.visibility_index < len(arguments)
                and arguments[signature.visibility_index]
                .split("*/")[-1].strip() == "false")

            size_argument = arguments[signature.size_index]
            mesh_name = None
            if (signature.mesh_index is not None
                    and signature.mesh_index < len(arguments)):
                mesh_name = _resolve_mesh(
                    arguments[signature.mesh_index], bindings)
            bounds = mesh_bounds.get(mesh_name) if mesh_name else None

            half = origin = None
            explicit = re.search(r"FVector\s*\(([^()]*)\)", size_argument)
            explicit_values = _floats(explicit.group(1), 3) if explicit else None
            authored = bool(AUTHORED_SIZE_IDIOM.search(size_argument))
            zero = "ZeroVector" in size_argument

            if signature.semantics == TARGET_BOUNDS:
                # 이 계열은 메시 바운드 중심이 저작 좌표에 오도록 액터를
                # 되민다. 0이면 저작 크기 그대로다.
                if zero and bounds:
                    half = tuple(bounds["extent"])
                elif explicit_values and not authored:
                    half = tuple(v / 2.0 for v in explicit_values)
                if half is not None:
                    origin = (0.0, 0.0, 0.0)
            elif signature.semantics == UNIT_SCALE:
                # 이 계열은 되밀지 않는다. 메시 원점이 액터 좌표에 그대로
                # 놓이므로 상자 중심은 원점만큼 밀린다.
                if authored and bounds:
                    half = tuple(bounds["extent"])
                    origin = tuple(bounds["origin"])
                elif explicit_values and not authored:
                    scale = tuple(v / 100.0 for v in explicit_values)
                    if bounds:
                        half = tuple(
                            bounds["extent"][i] * scale[i] for i in range(3))
                        origin = tuple(
                            bounds["origin"][i] * scale[i] for i in range(3))
                    else:
                        half = tuple(ENGINE_UNIT_HALF * s for s in scale)
                        origin = (0.0, 0.0, 0.0)
            elif signature.semantics == RAW_SCALE:
                # 크기 인자를 나누지 않고 컴포넌트 스케일에 그대로 넣는 계열.
                # 여기에 분기가 없어서 이 계열의 소품은 자리를 영영 못 풀고
                # 「크기나 좌표가 리터럴이 아니라」로 세어지고 있었다. 리터럴이
                # 맞는데도 그랬다.
                if explicit_values:
                    scale = tuple(explicit_values)
                    if bounds:
                        half = tuple(
                            bounds["extent"][i] * scale[i] for i in range(3))
                        origin = tuple(
                            bounds["origin"][i] * scale[i] for i in range(3))
                    else:
                        half = tuple(ENGINE_UNIT_HALF * s for s in scale)
                        origin = (0.0, 0.0, 0.0)

            if half is None:
                unresolved += 1
                continue
            # 돌려 놓은 밸브나 종이도 실제 월드 축의 상자로 검사한다.
            half = rotated_extent(Vec3(*(v * 2 for v in half)), rotation).as_tuple()
            half = tuple(v / 2 for v in half)
            origin = rotate_vector(Vec3(*origin), rotation).as_tuple()
            centre = tuple(location[i] + origin[i] for i in range(3))
            resolved.append((
                f"{relative_path}:{_line_of(text, call.start())}",
                target,
                tuple(centre[i] - half[i] for i in range(3)),
                tuple(centre[i] + half[i] for i in range(3)),
                visible,
            ))
    return resolved, unresolved


def _penetrates(a_min, a_max, b_min, b_max) -> bool:
    return all(
        min(a_max[i], b_max[i]) - max(a_min[i], b_min[i]) > PENETRATION_TOLERANCE
        for i in range(3))


def audit_placement(boxes: list, scene_props: list) -> list:
    """보이는 소품끼리, 그리고 구조물이 아닌 씬 상자와의 관통을 본다."""
    findings = []
    visible = [box for box in boxes if box[4]]
    for index, prop in enumerate(visible):
        for other in visible[index + 1:]:
            if _penetrates(prop[2], prop[3], other[2], other[3]):
                findings.append(Finding(
                    "PROP_OVERLAP", prop[0],
                    f"{prop[1]}이 {other[1]}을 뚫고 있다 ({other[0]})"))
        for scene in scene_props:
            if _penetrates(prop[2], prop[3], scene.minimum, scene.maximum):
                findings.append(Finding(
                    "PROP_THROUGH_SCENE", prop[0],
                    f"{prop[1]}이 씬 소품을 뚫고 있다 — {scene.label()}"))
    return findings


# 상판 위에 놓았다고 적어 놓고 상판 아래로 들어간 소품을 잡는다. 관통 검사와
# 겹치는 것 같지만 다르다 — 관통은 구조물을 빼지만, 박힘은 바로 그 구조물
# (책상 상판, 자재 더미)이 기준이다. P2에서 세 번 나온 「세워 놓은 종이의
# 중심」이 이 모양이고, 관통 검사만으로는 상판이 구조물이라 빠져나간다.
SUNK_TOLERANCE = 1.0
# 이만큼도 안 걸치면 그 면 위에 놓인 것이 아니다.
SUPPORT_FOOTPRINT_RATIO = 0.5


def _plan_overlap_ratio(prop_min, prop_max, support_min, support_max) -> float:
    """소품 바닥 면적 중 지지면 위에 걸친 비율."""
    area = 1.0
    covered = 1.0
    for axis in (0, 1):
        span = prop_max[axis] - prop_min[axis]
        if span <= 0.0:
            return 0.0
        area *= span
        covered *= max(
            0.0,
            min(prop_max[axis], support_max[axis])
            - max(prop_min[axis], support_min[axis]))
    return covered / area if area > 0.0 else 0.0


def audit_support(boxes: list, surfaces: list) -> list:
    """지지면 상판 아래로 들어간 소품을 찾는다."""
    findings = []
    for site, name, low, high, visible in boxes:
        if not visible:
            continue
        for surface in surfaces:
            top = surface.maximum[2]
            # 소품 바닥이 상판보다 위면 얹힌 것이고, 상판 아래로 완전히
            # 내려가 있으면 그 면과는 무관한 물건이다.
            if low[2] >= top - SUNK_TOLERANCE or high[2] <= top:
                continue
            if _plan_overlap_ratio(
                    low, high, surface.minimum,
                    surface.maximum) < SUPPORT_FOOTPRINT_RATIO:
                continue
            findings.append(Finding(
                "PROP_SUNK", site,
                f"{name}의 밑면이 상판보다 {top - low[2]:.1f}cm 아래다 — "
                f"{surface.label()}"))
            break
    return findings


def read_sources(project_root: str) -> dict:
    sources = {}
    root = os.path.join(project_root, SOURCE_ROOT)
    for directory, _, files in os.walk(root):
        for filename in files:
            if not filename.endswith(".cpp"):
                continue
            absolute = os.path.join(directory, filename)
            relative = os.path.relpath(absolute, project_root).replace("\\", "/")
            with open(absolute, "r", encoding="utf-8", errors="replace") as handle:
                sources[relative] = handle.read()
    return sources


SCENE_SOURCES = (
    "Source/IndieGame/Core/IGPrologueWorldScene.cpp",
)


def scene_surface_boxes():
    """소품이 얹힐 수 있는 수평 상판. 책상·자재 더미·가구가 여기 들어온다."""
    from audit_world_geometry import scan_source
    boxes = []
    for relative_path in SCENE_SOURCES:
        for box in scan_source(relative_path).boxes:
            if box.note in ("authored", "unknown-rotation"):
                continue
            if box.size[0] < 20.0 or box.size[1] < 20.0:
                continue
            boxes.append(box)
    return boxes


def scene_prop_boxes():
    """씬이 짓는 것 중 구조물이 아닌 상자들. 벽·바닥·책상은 뺀다."""
    from audit_world_geometry import scan_source
    boxes = []
    for relative_path in SCENE_SOURCES:
        for box in scan_source(relative_path).boxes:
            if box.note in ("authored", "unknown-rotation"):
                continue
            if box.is_structure() or box.is_dressing():
                continue
            boxes.append(box)
    return boxes


def run(project_root: str) -> tuple:
    sources = read_sources(project_root)
    signatures, non_visual = parse_signatures(sources)
    sites = adopt_inherited(
        parse_call_sites(sources), signatures, non_visual, parse_parents(project_root))
    findings, resolved, unresolved = audit(signatures, sites, non_visual)

    mesh_bounds = load_mesh_bounds(project_root)
    placed, unplaced = prop_boxes(sources, signatures, mesh_bounds)
    findings = findings + audit_placement(placed, scene_prop_boxes())
    findings = findings + audit_support(placed, scene_surface_boxes())
    return (signatures, sites, findings, resolved, unresolved,
            len(placed), unplaced, len(mesh_bounds))


# --------------------------------------------------------------------------
# 자기 검사. 이 감사가 놓쳤던 실제 결함 두 종류를 합성 입력으로 세워 두고,
# 켜지는 것과 꺼지는 것을 함께 본다.
# --------------------------------------------------------------------------

SELF_TEST_EVIDENCE = '''
void AFakeEvidence::Configure(
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const FVector& SizeCentimeters,
	const float NoiseLoudness)
{
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FVector MeshSize = Bounds.BoxExtent * 2.0f;
	const FVector Scale = SizeCentimeters.IsNearlyZero()
		? FVector::OneVector
		: SizeCentimeters / MeshSize;
	PresentationMesh->SetRelativeScale3D(Scale);
	PresentationMesh->SetMaterial(0, Material);
}
'''

SELF_TEST_NOTE = '''
void AFakeNote::ConfigurePrototypeVisuals(
	UStaticMesh* CubeMesh,
	UMaterialInterface* PaperMaterial,
	const FVector& PaperSize,
	const bool bCastPresentationShadow)
{
	PaperMesh->SetStaticMesh(CubeMesh);
	PaperMesh->SetMaterial(0, PaperMaterial);
	PaperMesh->SetRelativeScale3D(PaperSize / 100.0f);
}
'''

# 좌표만 받는 디렉터 Configure. bool을 돌려주고 시각 인자가 없다 —
# 서명으로 읽되 소품 설정이 아니므로 「무관」으로 세야 한다.
SELF_TEST_STAGE = '''
bool AFakeStageDirector::Configure(AFakeScene* Scene)
{
	Stage = Scene;
	return Stage != nullptr;
}
'''

SELF_TEST_CALLS = '''
bool AFakeDirector::BuildStage()
{
	Ledger = World->SpawnActor<AFakeEvidence>(
		AFakeEvidence::StaticClass(), FTransform(), Parameters);
	Ledger->Configure(
		LedgerMesh,
		LedgerMaterial,
		LedgerMesh ? FVector(100.0f, 100.0f, 100.0f) : FVector(21.0f, 1.0f, 29.7f),
		0.06f);

	Fixed = World->SpawnActor<AFakeEvidence>(
		AFakeEvidence::StaticClass(), FTransform(), Parameters);
	Fixed->Configure(
		LedgerMesh,
		LedgerMesh ? LedgerMaterial : nullptr,
		LedgerMesh ? FVector::ZeroVector : FVector(21.0f, 1.0f, 29.7f),
		0.06f);

	Door = World->SpawnActor<AFakeNote>(
		AFakeNote::StaticClass(), FTransform(), Parameters);
	Door->ConfigurePrototypeVisuals(
		CubeMesh, nullptr, FVector(18.0f, 1.2f, 24.0f));

	Sheet = World->SpawnActor<AFakeNote>(
		AFakeNote::StaticClass(), FTransform(), Parameters);
	Sheet->ConfigurePrototypeVisuals(
		CubeMesh, PaperMaterial, FVector(100.0f, 100.0f, 100.0f));

	Orphan->Configure(CubeMesh, nullptr, FVector(1.0f, 1.0f, 1.0f), 0.0f);
	return true;
}
'''


def _self_test() -> int:
    failures = []

    def check(label, actual, expected):
        if actual != expected:
            failures.append(f"{label}: {actual!r} != {expected!r}")

    sources = {
        "Fake/Evidence.cpp": SELF_TEST_EVIDENCE,
        "Fake/Note.cpp": SELF_TEST_NOTE,
        "Fake/Stage.cpp": SELF_TEST_STAGE,
        "Fake/Director.cpp": SELF_TEST_CALLS,
    }
    signatures, non_visual = parse_signatures(sources)
    check("시그니처 수", len(signatures), 2)
    # bool을 돌려주는 Configure도 읽는다. 시각 인자가 없으면 서명이 아니라
    # 「무관」이다 — 그 호출부를 미검사로 세면 사각지대가 부풀어 보인다.
    check("시각 설정이 아닌 것", non_visual, {("AFakeStageDirector", "Configure")})

    evidence = signatures[("AFakeEvidence", "Configure")]
    check("바운드 계열 분류", evidence.semantics, TARGET_BOUNDS)
    check("메시 인자", evidence.mesh_index, 0)
    check("재질 인자", evidence.material_indices, (1,))
    check("크기 인자", evidence.size_index, 2)

    note = signatures[("AFakeNote", "ConfigurePrototypeVisuals")]
    check("100 나눗셈 계열 분류", note.semantics, UNIT_SCALE)
    check("불리언은 크기가 아니다", note.size_index, 2)

    sites = parse_call_sites(sources)
    # Orphan은 SpawnActor로 만들어진 적이 없으므로 클래스를 모른다.
    check("호출부 수", len(sites), 4)

    findings, resolved, unresolved = audit(signatures, sites, non_visual)
    check("대조한 호출부", resolved, 4)
    by_code = {}
    for finding in findings:
        by_code.setdefault(finding.code, []).append(finding.site)

    # 바운드 계열에 넘긴 (100,100,100) 하나만 걸려야 한다. 같은 값을
    # 100 나눗셈 계열에 넘긴 자리는 스케일 1이므로 정상이다.
    check("UNIT_MISMATCH", by_code.get("UNIT_MISMATCH"), ["Fake/Director.cpp:6"])
    # 삼항의 폴백 쪽 nullptr은 미베이크 대비이므로 걸지 않는다.
    check("PLACEHOLDER_MATERIAL",
          by_code.get("PLACEHOLDER_MATERIAL"), ["Fake/Director.cpp:22"])
    check("UNKNOWN_SEMANTICS", by_code.get("UNKNOWN_SEMANTICS"), None)

    # 온전한 배선에서는 아무것도 나오지 않아야 한다.
    clean_findings, _, _ = audit(signatures, [
        ("Clean.cpp:1", "AFakeEvidence", "Configure",
         ["Mesh", "Material", "FVector::ZeroVector", "0.0f"]),
        ("Clean.cpp:2", "AFakeNote", "ConfigurePrototypeVisuals",
         ["CubeMesh", "Paper", "FVector(100.0f, 100.0f, 100.0f)"]),
    ])
    check("무결 상태", clean_findings, [])

    # 그림을 끄고 상호작용만 맡는 소품은 재질이 없어도 걸지 않는다. 씬이
    # 이미 그 자리에 진짜 물건을 세워 둔 경우다.
    hidden_signature = Signature(
        "AFakeEvidence", "Configure", "Fake.cpp:1", 0, (1,), 2,
        TARGET_BOUNDS, 3)
    hidden_findings, _, _ = audit({hidden_signature.key: hidden_signature}, [
        ("Hidden.cpp:1", "AFakeEvidence", "Configure",
         ["Mesh", "nullptr", "FVector(100.0f)", "/*bPresentationVisible=*/false"]),
    ])
    check("숨긴 소품", hidden_findings, [])
    shown_findings, _, _ = audit({hidden_signature.key: hidden_signature}, [
        ("Shown.cpp:1", "AFakeEvidence", "Configure",
         ["Mesh", "nullptr", "FVector(100.0f)", "true"]),
    ])
    check("보이는 소품은 둘 다 걸린다",
          sorted(f.code for f in shown_findings),
          ["PLACEHOLDER_MATERIAL", "UNIT_MISMATCH"])

    # 평평한 손잡이를 세워 붙인 사례. 로컬 XY 크기를 그대로 쓰면 배관을
    # 6cm나 뚫는다고 잘못 판정한다.
    valve_source = {"Valve.cpp": """
        Wheel = World->SpawnActor<AFakeEvidence>(AFakeEvidence::StaticClass(),
            FTransform(FRotator(0, 0, 90), FVector(75, -184, 52)), Params);
        Wheel->Configure(Mesh, Material, FVector(12, 12, 2), 0.0f);
    """}
    valve_boxes, valve_missing = prop_boxes(valve_source, signatures, {})
    check("세운 손잡이 크기", valve_boxes[0][2:4], ((69., -185., 46.), (81., -183., 58.)))
    check("세운 손잡이 해석", valve_missing, 0)

    # 자리 판정. 닿는 것과 뚫는 것을 가른다.
    def box(name, centre, half, visible=True):
        return (f"{name}.cpp:1", name,
                tuple(centre[i] - half[i] for i in range(3)),
                tuple(centre[i] + half[i] for i in range(3)), visible)

    check("맞닿은 것은 통과",
          audit_placement([box("A", (0, 0, 0), (5, 5, 5)),
                           box("B", (10, 0, 0), (5, 5, 5))], []), [])
    check("뚫으면 걸린다",
          [f.code for f in audit_placement(
              [box("A", (0, 0, 0), (5, 5, 5)),
               box("B", (8, 0, 0), (5, 5, 5))], [])],
          ["PROP_OVERLAP"])
    check("숨긴 소품은 자리도 보지 않는다",
          audit_placement([box("A", (0, 0, 0), (5, 5, 5), False),
                           box("B", (8, 0, 0), (5, 5, 5), False)], []), [])
    # 한 축만 스치는 것은 관통이 아니다.
    check("한 축만 겹치면 통과",
          audit_placement([box("A", (0, 0, 0), (5, 5, 5)),
                           box("B", (8, 20, 0), (5, 5, 5))], []), [])

    # 박힘 판정. 실제로 있었던 값을 그대로 쓴다 — 대리인 문자 사본이 24cm
    # 높이로 세워진 채 중심이 Z=80이었고, 관리실 책상 상판은 Z=76이었다.
    class _Surface:
        def __init__(self, minimum, maximum, label):
            self.minimum = minimum
            self.maximum = maximum
            self._label = label

        def label(self):
            return self._label

    desk = _Surface((105.0, -137.5, 0.0), (215.0, -82.5, 76.0), "Desk")
    sunk_note = ("Note.cpp:1", "AgentNote",
                 (196.0, -115.0, 68.0), (214.0, -91.0, 92.0), True)
    rested_note = ("Note.cpp:2", "AgentNote",
                   (196.0, -115.0, 76.1), (214.0, -91.0, 77.3), True)
    check("박힌 종이를 잡는다",
          [f.code for f in audit_support([sunk_note], [desk])], ["PROP_SUNK"])
    check("얹힌 종이는 통과", audit_support([rested_note], [desk]), [])
    # 상판 밖에 있는 물건은 그 상판과 무관하다.
    away = ("Note.cpp:3", "Elsewhere",
            (400.0, -115.0, 68.0), (418.0, -91.0, 92.0), True)
    check("상판 밖은 무관", audit_support([away], [desk]), [])
    # 숨긴 소품은 자리를 보지 않는다.
    hidden_note = sunk_note[:4] + (False,)
    check("숨긴 소품은 박힘도 보지 않는다",
          audit_support([hidden_note], [desk]), [])

    if failures:
        for failure in failures:
            print(f"  FAIL {failure}")
        print(f"DIRECTOR PROP AUDIT SELF-TEST FAIL  {len(failures)} case(s)")
        return 1
    print("PASS director prop audit self-test: signature parsing, three size "
          "conventions read from their own bodies, ternary fallbacks spared, "
          "unspawned callers skipped, contact told apart from penetration, "
          "a sunk sheet told apart from a rested one, hidden props spared, "
          "all codes on and off")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="발견 항목이 있으면 exit 1")
    parser.add_argument("--json", action="store_true",
                        help="기계가 읽는 보고")
    parser.add_argument("--self-test", action="store_true",
                        help="감사 자체를 합성 입력으로 검사한다")
    arguments = parser.parse_args(argv)

    if arguments.self_test:
        return _self_test()

    (signatures, sites, findings, resolved, unresolved,
     placed, unplaced, mesh_count) = run(PROJECT_ROOT)
    # 미결로 적어 둔 자리의 발견은 실패로 세지 않는다. 대신 매번 화면에 남고,
    # 목록에만 남아 있고 실제로는 안 걸리는 항목도 실패로 본다.
    carried_seen = {
        owner for owner in (carried_owner(f) for f in findings) if owner}
    stale_carried = set(CARRIED_OVERLAPS) - carried_seen
    findings = [f for f in findings if carried_owner(f) is None]

    if arguments.json:
        print(json.dumps({
            "signatures": {
                f"{owner}::{name}": signature.semantics
                for (owner, name), signature in sorted(signatures.items())
            },
            "sites": len(sites),
            "resolved": resolved,
            "unresolved": unresolved,
            "placed": placed,
            "unplaced": unplaced,
            "mesh_bounds": mesh_count,
            "findings": [f.to_dict() for f in findings],
        }, indent=2, ensure_ascii=False))
    else:
        counts = {}
        for signature in signatures.values():
            counts[signature.semantics] = counts.get(signature.semantics, 0) + 1
        shape = " ".join(
            f"{key.lower()}={counts[key]}" for key in sorted(counts))
        print(f"DIRECTOR PROP AUDIT  configure={len(signatures)} ({shape}) "
              f"sites={resolved} placed={placed}/{placed + unplaced} "
              f"mesh_bounds={mesh_count} findings={len(findings)}")
        if not mesh_count:
            print("  Docs/mesh_bounds.json이 없다 — 저작 메시를 쓰는 소품의 "
                  "자리를 계산할 수 없다. Scripts/export_mesh_bounds.py로 뽑아라")
        if unplaced:
            print(f"  자리를 풀지 못한 소품 {unplaced}건 — 크기나 좌표가 "
                  f"리터럴이 아니라 계산할 수 없었다")
        if unresolved:
            # 침묵하지 않는다. 대조하지 못한 자리는 통과가 아니라 미검사다.
            print(f"  대조하지 못한 호출부 {unresolved}건 — 이 호출부를 만든 "
                  f"SpawnActor<>를 같은 파일에서 찾지 못했다")
        for owner in sorted(carried_seen):
            print(f"  미결 {owner}\n       {CARRIED_OVERLAPS[owner]}")
        for owner in sorted(set(CARRIED_OVERLAPS) - carried_seen):
            print(f"  미결 목록의 {owner} 은(는) 더 이상 걸리지 않는다. 지워라")
        if stale_carried:
            return 1
        if not findings:
            print("\nevery director-spawned prop is configured to contract")
        else:
            print()
            for finding in findings:
                print(f"  [{finding.code}] {finding.site}")
                print(f"      {finding.detail}")

    return 1 if (arguments.check and findings) else 0


if __name__ == "__main__":
    sys.exit(main())

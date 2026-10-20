"""Answer one question without running a cook: what actually ships?

The cooker does not walk C++. It starts from the directories in
``DirectoriesToAlwaysCook``, the Asset Manager's primary-asset rules and the
maps, then follows the *serialized* references inside those packages. A
texture that only ever appears as a string literal in
``LoadObject<UTexture2D>(nullptr, TEXT("/Game/..."))`` is invisible to it: the
editor finds the asset because the whole project is on disk, and the packaged
build gets a null pointer.

That failure is silent in exactly the wrong way -- the HUD keeps drawing, just
without the picture -- and it only appears in a packaged build, which is the
one build nobody runs while iterating. So it gets a static check instead.

The same walk answers the atlas question. Moving a print texture onto a shared
atlas page is only a saving if the individual texture then stops being cooked,
and it stops being cooked only when the last material that sampled it has been
rebuilt to read the page. Before that rebuild all of them still ship, and the
atlas is pure cost.

The Asset Manager's rules are *parsed*, not pattern-matched. They are the only
per-asset cook rule Unreal offers, so they are the only thing holding an asset
that nothing serialized references -- and a rule Unreal cannot read is a log
line nobody reads and a blank draw in the pak. Every way one can look right
and hold nothing is checked: a field name that is not a field, a CookRule that
is not one of the enum values, a `Package.Object` path whose halves disagree,
an asset that is not in the tree, an AssetBaseClass the named assets are not.
``--explain-rules`` then re-runs reachability with the rules removed and says,
one asset at a time, whether the rule is what keeps it.

``--simulate-rebuild`` answers the atlas half the same way, before the editor
has run. The rebuild changes exactly one edge per material -- the diffuse
sample moves from the texture to the page -- so the simulation makes that edit
to the graph and re-runs reachability. What matters in the output is not that
the textures leave; it is what else does, and what refuses to. A mesh slot
naming a texture directly, a cook rule covering one, a ``T_Photo_`` twin the
material samples instead, a pre-atlas sampler that survived an in-place
update, or a package reachable only through a texture: each is a page paid for
and nothing saved, or a shipping bug the atlas caused.

``--simulate-targeted`` narrows that to the passes that update in place, read
out of the builder's own source, and runs each one both ways. What the pair
proves is not that the retirement works -- no static check can watch Unreal
set a property -- but that it is load-bearing: with it every texture the pass
touches leaves the cook, without it every one stays. A "retired: STAYS" is the
useful failure, because it means something *other* than the pass is holding
the texture.

Two things this deliberately does not do:

* It does not parse Unreal's package format. It scans each package for
  ``/Game/...`` strings, which over-approximates the reference set: some hits
  are property values or thumbnail metadata rather than imports. The bias is
  the safe one. Anything reported as unreachable really is unreachable, and
  the atlas report can only ever under-state the saving.
* It does not know what a cook actually did. It reads the same config the
  cooker reads and follows the same graph, which is enough to catch a
  reference that was never there -- not enough to catch one the cooker
  dropped for its own reasons.

A checkout that did not fetch its LFS content has holes in that graph, and a
hole makes both answers unsound: an asset can look unreachable only because
the package naming it could not be opened. That case skips with a reason
rather than failing or passing.

Run with:
    python Scripts/check_cook_references.py             # report
    python Scripts/check_cook_references.py --check     # exit 1 on findings
    python Scripts/check_cook_references.py --check \\
        --require-atlas-dropped   # after the editor rebuilt the materials
    python Scripts/check_cook_references.py --explain-rules
    python Scripts/check_cook_references.py --simulate-rebuild
    python Scripts/check_cook_references.py --simulate-targeted
    python Scripts/check_cook_references.py --self-test
    python Scripts/check_cook_references.py --json
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import material_passes  # noqa: E402
import texture_atlas_contract  # noqa: E402
import ue_config  # noqa: E402
from texture_atlas_contract import (  # noqa: E402
    ATLAS_EXCLUSIONS,
    ATLAS_TEXTURE_ROOT,
    PRINT_ATLAS_ENTRIES,
    PROJECT_ROOT,
)

PACKAGE_SUFFIXES = (".uasset", ".umap")
CODE_SUFFIXES = (".cpp", ".h")

# `/Game/A/B` and `/Game/A/B.B` both appear; the object suffix is dropped so
# every reference is compared as a package path.
GAME_PATH = re.compile(rb"/Game/[A-Za-z0-9_/]+(?:\.[A-Za-z0-9_]+)?")
GAME_PATH_TEXT = re.compile(r"/Game/[A-Za-z0-9_/]+(?:\.[A-Za-z0-9_]+)?")
# Two adjacent string literals are one string. Long paths in the source are
# split across lines to stay inside the column limit, and a scan that reads
# them as written finds half a path.
LITERAL_JOIN = re.compile(r'"\s*"')

# `UMaterialInterface* CarbonMaterial = LoadObject<UMaterialInterface>(` --
# the pointer a path was loaded into, so a null test on it can be found.
ASSIGNED_LOAD = re.compile(
    r"\*\s*(?P<name>[A-Za-z_]\w*)\s*=\s*(?:\w+::)?"
    r"(?:Load\w*|FindObject\w*|StaticLoad\w*)\s*[<(]",
)
# Wide enough for the two-line call this project's column limit produces, and
# for a comment between the load and its fallback; not wide enough to catch a
# null test on some later, unrelated pointer.
GUARD_LOOKBEHIND = 400
GUARD_LOOKAHEAD = 700

LFS_POINTER_MAGIC = b"version https://git-lfs"

ALWAYS_COOK_DIRECTORY = re.compile(
    r"^\+?DirectoriesToAlwaysCook\s*=\s*\(Path\s*=\s*\"([^\"]+)\"\)",
    re.MULTILINE,
)
NEVER_COOK_DIRECTORY = re.compile(
    r"^\+?DirectoriesToNeverCook\s*=\s*\(Path\s*=\s*\"([^\"]+)\"\)",
    re.MULTILINE,
)
PRIMARY_ASSET_SETTING = "PrimaryAssetTypesToScan"
DETERMINE_ID_SETTING = "bShouldManagerDetermineTypeAndName"
# 스스로 PrimaryAssetId를 내는 클래스. 나머지(Texture2D 등)는 Asset Manager가 ID를
# 정해 줘야 쿠크가 규칙으로 등록한 ID와 객체의 ID를 맞춰 본다.
NATIVE_PRIMARY_CLASSES = {
    "/Script/Engine.PrimaryDataAsset",
    "/Script/Engine.PrimaryAssetLabel",
    "/Script/Engine.World",
}

# FPrimaryAssetTypeInfo and FPrimaryAssetRules, as Unreal declares them. A rule
# is only worth trusting if every field it names is a field that exists: Unreal
# skips what it cannot map and logs, so a misspelled key is a rule that holds
# nothing and says so where nobody looks.
PRIMARY_ASSET_TYPE_FIELDS = frozenset({
    "PrimaryAssetType", "AssetBaseClass", "bHasBlueprintClasses",
    "bIsEditorOnly", "Directories", "SpecificAssets", "Rules",
})
PRIMARY_ASSET_RULE_FIELDS = frozenset({
    "Priority", "ChunkId", "bApplyRecursively", "CookRule", "bIsEditorOnly",
})
COOK_RULES = frozenset({
    "Unknown", "NeverCook", "ProductionNeverCook",
    "DevelopmentAlwaysProductionNeverCook", "DevelopmentAlwaysCook",
    "AlwaysCook",
})
# `/Game/Path/Asset.Object`. Unreal resolves the half after the dot as the
# object inside the package, so for a top-level asset the two halves are the
# same word and a mismatch resolves to nothing.
SOFT_OBJECT_PATH = re.compile(
    r"^(/Game/[A-Za-z0-9_/]+?/([A-Za-z0-9_]+))\.([A-Za-z0-9_]+)$")

# Where create_textured_materials.py writes. The rebuild rewrites references
# from here and nowhere else.
MATERIAL_ROOT = "/Game/Prototype/Materials"


class CookAudit:
    """The cook's reachable set, and everything that disagrees with it."""

    def __init__(self, project_root: str = PROJECT_ROOT):
        self.project_root = project_root
        self.content_dir = os.path.join(project_root, "Content")
        self.source_dir = os.path.join(project_root, "Source")
        self.game_config = os.path.join(project_root, "Config", "DefaultGame.ini")
        self.engine_config = os.path.join(
            project_root, "Config", "DefaultEngine.ini")
        self.atlas_manifest = os.path.join(
            project_root, "Content", "SourceArt", "Atlas", "print_atlas.json")
        self.always_cook: list[str] = []
        self.never_cook: list[str] = []
        self.always_cook_assets: set[str] = set()
        self.primary_asset_rules: list[PrimaryAssetRule] = []
        self.packages: dict[str, str] = {}      # package path -> file path
        self.unreadable: set[str] = set()       # LFS pointers, not fetched
        self.references: dict[str, set[str]] = {}
        self.roots: set[str] = set()
        self.cooked: set[str] = set()
        self.code_paths: dict[str, list[str]] = {}  # package path -> sites
        # Paths whose every load site null-checks the result. A path that is
        # not in the tree is a defect when the code assumes it loaded and a
        # decision when the code has a fallback; without this the audit calls
        # both the same thing and the real one gets lost among the deliberate.
        self.guarded_code_paths: set[str] = set()


def _package_path(audit: CookAudit, file_path: str) -> str:
    relative = os.path.relpath(file_path, audit.content_dir).replace(os.sep, "/")
    return "/Game/" + os.path.splitext(relative)[0]


def _read_config(path: str) -> str:
    if not os.path.isfile(path):
        return ""
    with open(path, "r", encoding="utf-8-sig") as handle:
        return handle.read()


class PrimaryAssetRule:
    """One parsed `+PrimaryAssetTypesToScan=` line, and what is wrong with it."""

    def __init__(self, line: int, text: str, source: str = ""):
        self.line = line
        self.text = text
        self.source = source
        self.name = "?"
        self.base_class = ""
        self.cook_rule = ""
        self.fields: dict = {}
        self.directories: list[str] = []
        self.assets: list[str] = []          # full `/Game/Pkg.Object` paths
        self.packages: list[str] = []        # just `/Game/Pkg`
        self.problems: list[str] = []

    @property
    def always_cook(self) -> bool:
        return self.cook_rule == "AlwaysCook"


def _validate_rule_fields(rule: PrimaryAssetRule, fields: dict) -> None:
    unknown = sorted(set(fields) - PRIMARY_ASSET_TYPE_FIELDS)
    if unknown:
        rule.problems.append(
            "FPrimaryAssetTypeInfo has no field(s) " + ", ".join(unknown)
            + "; Unreal drops what it cannot map"
        )
    for required in ("PrimaryAssetType", "AssetBaseClass", "Rules"):
        if required not in fields:
            rule.problems.append(f"the rule names no {required}")

    nested = fields.get("Rules")
    if not isinstance(nested, dict):
        rule.problems.append("Rules is not a struct")
        return
    unknown = sorted(set(nested) - PRIMARY_ASSET_RULE_FIELDS)
    if unknown:
        rule.problems.append(
            "FPrimaryAssetRules has no field(s) " + ", ".join(unknown))
    rule.cook_rule = str(nested.get("CookRule", ""))
    if rule.cook_rule not in COOK_RULES:
        rule.problems.append(
            f"CookRule {rule.cook_rule!r} is not an EPrimaryAssetCookRule "
            "value")
    for numeric in ("Priority", "ChunkId"):
        if numeric in nested:
            try:
                ue_config.as_int(nested[numeric])
            except ue_config.ConfigSyntaxError as error:
                rule.problems.append(f"Rules.{numeric}: {error}")
    for flag in ("bApplyRecursively", "bIsEditorOnly"):
        if flag in nested:
            try:
                ue_config.as_bool(nested[flag])
            except ue_config.ConfigSyntaxError as error:
                rule.problems.append(f"Rules.{flag}: {error}")


def _validate_rule_assets(audit: CookAudit, rule: PrimaryAssetRule) -> None:
    """Every named asset resolves, and is of the class the rule scans for.

    The Asset Manager only registers an asset that is a subclass of
    AssetBaseClass. A path that resolves to nothing, or to the wrong class,
    leaves the rule holding nothing at all -- which looks exactly like a rule
    that works until the pak is opened.
    """
    expected_class = rule.base_class.rsplit(".", 1)[-1]
    for path in rule.assets:
        match = SOFT_OBJECT_PATH.match(path)
        if match is None:
            rule.problems.append(f"{path} is not a /Game/Package.Object path")
            continue
        package, asset_name, object_name = match.groups()
        if object_name != asset_name:
            rule.problems.append(
                f"{path} names object {object_name} inside package "
                f"{asset_name}; a top-level asset has one name"
            )
            continue
        file_path = audit.packages.get(package)
        if file_path is None:
            rule.problems.append(f"{path} is not in the tree")
            continue
        if package in audit.unreadable:
            continue  # cannot check the class of a package we cannot open
        if expected_class:
            with open(file_path, "rb") as handle:
                blob = handle.read()
            if expected_class.encode("ascii") not in blob:
                rule.problems.append(
                    f"{path} does not look like a {expected_class}; the rule "
                    "would register nothing for it"
                )


def _read_primary_asset_rules(audit: CookAudit) -> None:
    """Parse the Asset Manager rules instead of pattern-matching them."""
    # UAssetManagerSettings는 config=Game이다. DefaultEngine.ini에 둔 규칙은 여기서는
    # 멀쩡히 읽히지만 언리얼은 읽지 않는다. 0.2.4까지 HUD 텍스처 8개가 그렇게 빠졌다.
    misplaced = _read_config(audit.engine_config)
    for line, body in ue_config.ini_entries(misplaced, PRIMARY_ASSET_SETTING):
        rule = PrimaryAssetRule(line, body, audit.engine_config)
        rule.problems.append(
            "Asset Manager 설정은 Game 설정이라 DefaultEngine.ini의 규칙은 "
            "언리얼이 읽지 않는다. DefaultGame.ini로 옮겨야 한다")
        audit.primary_asset_rules.append(rule)
    text = _read_config(audit.game_config)
    determine = [value for _, value in ue_config.ini_entries(text, DETERMINE_ID_SETTING)]
    determines_ids = bool(determine) and determine[-1].strip().lower() == "true"
    for line, body in ue_config.ini_entries(text, PRIMARY_ASSET_SETTING):
        rule = PrimaryAssetRule(line, body, audit.game_config)
        audit.primary_asset_rules.append(rule)
        try:
            fields = ue_config.parse_struct(body)
        except ue_config.ConfigSyntaxError as error:
            rule.problems.append(f"unparseable: {error}")
            continue

        rule.fields = fields
        rule.name = str(fields.get("PrimaryAssetType", "?"))
        rule.base_class = str(fields.get("AssetBaseClass", ""))
        _validate_rule_fields(rule, fields)
        if rule.base_class.strip('"') not in NATIVE_PRIMARY_CLASSES and not determines_ids:
            rule.problems.append(
                f"{rule.base_class}는 PrimaryAssetId를 스스로 내지 않는다. "
                f"{DETERMINE_ID_SETTING}=True가 없으면 쿠크가 ID 불일치로 멈춘다")

        try:
            for entry in ue_config.as_array(fields.get("Directories")):
                if not isinstance(entry, dict) or "Path" not in entry:
                    rule.problems.append(
                        "Directories takes FDirectoryPath structs, "
                        f"found {entry!r}")
                    continue
                rule.directories.append(str(entry["Path"]).rstrip("/"))
            rule.assets = [
                str(entry)
                for entry in ue_config.as_array(fields.get("SpecificAssets"))
            ]
        except ue_config.ConfigSyntaxError as error:
            rule.problems.append(str(error))
            continue

        _validate_rule_assets(audit, rule)
        rule.packages = [
            path.split(".")[0] for path in rule.assets
            if SOFT_OBJECT_PATH.match(path)
        ]
        if not rule.directories and not rule.packages:
            rule.problems.append("the rule names no directory and no asset")

    # Differential: the rules in a project are written to one shape. A rule
    # whose field set differs from every other one is usually a rule that was
    # typed rather than exported, and the difference is the mistake.
    shapes = [
        (rule, frozenset(rule.fields)) for rule in audit.primary_asset_rules
    ]
    counts: dict[frozenset, int] = {}
    for _rule, shape in shapes:
        if shape:
            counts[shape] = counts.get(shape, 0) + 1
    if len(counts) > 1:
        common = max(counts, key=lambda shape: counts[shape])
        for rule, shape in shapes:
            if shape and shape != common:
                rule.problems.append(
                    "field set differs from the other rules in this file: "
                    + ", ".join(sorted(shape ^ common))
                )


def _cook_directories(audit: CookAudit) -> None:
    game = _read_config(audit.game_config)
    audit.always_cook = [
        p.rstrip("/") for p in ALWAYS_COOK_DIRECTORY.findall(game)
    ]
    audit.never_cook = [
        p.rstrip("/") for p in NEVER_COOK_DIRECTORY.findall(game)
    ]

    # The Asset Manager cooks primary assets by its own rules, which name both
    # directories and individual assets the packaging settings never mention.
    # SpecificAssets is the only per-asset cook rule Unreal offers: an asset
    # that nothing serialized references, and that only C++ loads by path,
    # ships because it is named here or it does not ship at all.
    _read_primary_asset_rules(audit)
    for rule in audit.primary_asset_rules:
        if not rule.always_cook:
            continue
        audit.always_cook.extend(rule.directories)
        audit.always_cook_assets.update(rule.packages)

    # One entry per directory, in the order the config named them.
    audit.always_cook = list(dict.fromkeys(audit.always_cook))


def _collect_packages(audit: CookAudit) -> None:
    for directory, _subdirs, files in os.walk(audit.content_dir):
        for name in files:
            if not name.endswith(PACKAGE_SUFFIXES):
                continue
            file_path = os.path.join(directory, name)
            package = _package_path(audit, file_path)
            audit.packages[package] = file_path

            with open(file_path, "rb") as handle:
                blob = handle.read()
            if blob.startswith(LFS_POINTER_MAGIC):
                # Present in the tree, but the bytes are in LFS and were not
                # fetched. Its outgoing references cannot be read.
                audit.unreadable.add(package)
                audit.references[package] = set()
                continue

            found = set()
            for match in GAME_PATH.finditer(blob):
                reference = match.group().decode("ascii").split(".")[0]
                if reference != package:
                    found.add(reference)
            audit.references[package] = found


def _is_under(package: str, directory: str) -> bool:
    return package == directory or package.startswith(directory + "/")


def blind_spots(audit: CookAudit) -> set[str]:
    """Cooked packages whose own references could not be read.

    An unfetched LFS pointer is a hole in the graph, and every verdict that
    depends on the graph is unsound while one is inside the cook set: an
    asset can look unreachable only because the package that names it could
    not be opened. Reported, not guessed at.
    """
    return audit.unreadable & audit.cooked


def _seed_roots(audit: CookAudit) -> None:
    for package in audit.packages:
        if any(_is_under(package, d) for d in audit.never_cook):
            continue
        # Maps are always a cook root: a level that is not cooked is not a
        # game. Everything else has to be named by config.
        if audit.packages[package].endswith(".umap"):
            audit.roots.add(package)
        elif package in audit.always_cook_assets:
            audit.roots.add(package)
        elif any(_is_under(package, d) for d in audit.always_cook):
            audit.roots.add(package)


def _close(audit: CookAudit) -> None:
    pending = list(audit.roots)
    seen = set(pending)
    while pending:
        package = pending.pop()
        for reference in audit.references.get(package, ()):
            if reference in seen:
                continue
            if reference not in audit.packages:
                continue  # a path into a plugin or an asset not in this tree
            if any(_is_under(reference, d) for d in audit.never_cook):
                continue
            seen.add(reference)
            pending.append(reference)
    audit.cooked = seen


def _is_guarded_load(text: str, end: int) -> bool:
    """Does the code test the pointer this literal was loaded into?

    Reads backwards from the literal for the identifier it was assigned to,
    then forwards for a test on that identifier. Deliberately narrow: only an
    assignment of the form `Type* Name = Load...(` counts, and only the next
    few lines are searched, because a null test far from the load is not the
    fallback this is looking for. Anything it cannot read stays unguarded --
    an unnoticed defect costs more than a noisy line about a real fallback.
    """
    head = text[max(0, end - GUARD_LOOKBEHIND):end]
    # The nearest assignment, not the first in the window. Two loads in one
    # function put both in range, and taking the first tests the wrong
    # pointer -- which reads as unguarded whatever the second load's code
    # actually does.
    assignments = ASSIGNED_LOAD.findall(head)
    if not assignments:
        return False
    name = re.escape(assignments[-1])
    tail = text[end:end + GUARD_LOOKAHEAD]
    return re.search(
        rf"(?:!\s*{name}\b"                     # if (!Mat)
        rf"|\b{name}\s*(?:==|!=)\s*(?:nullptr|NULL)"   # Mat == nullptr
        rf"|\b{name}\s*\?"                      # Mat ? A : B
        rf"|\bif\s*\(\s*{name}\s*\))",          # if (Mat)
        tail,
    ) is not None


def _collect_code_paths(audit: CookAudit) -> None:
    unguarded: set[str] = set()
    guarded: set[str] = set()
    for directory, _subdirs, files in os.walk(audit.source_dir):
        for name in files:
            if not name.endswith(CODE_SUFFIXES):
                continue
            file_path = os.path.join(directory, name)
            with open(file_path, "r", encoding="utf-8", errors="replace") as f:
                text = LITERAL_JOIN.sub("", f.read())
            relative = os.path.relpath(file_path, audit.project_root)
            for match in GAME_PATH_TEXT.finditer(text):
                reference = match.group().split(".")[0].rstrip("/")
                if reference.count("/") < 3:
                    continue  # /Game/Dir alone is a directory, not an asset
                sites = audit.code_paths.setdefault(reference, [])
                if relative not in sites:
                    sites.append(relative)
                target = guarded if _is_guarded_load(text, match.end()) \
                    else unguarded
                target.add(reference)
    # Guarded only if every site is. One unguarded load is still a crash.
    audit.guarded_code_paths = guarded - unguarded


def run_audit(project_root: str = PROJECT_ROOT) -> CookAudit:
    audit = CookAudit(project_root)
    # Packages first: validating a rule means resolving the assets it names,
    # which needs to know what is in the tree.
    _collect_packages(audit)
    _cook_directories(audit)
    _seed_roots(audit)
    _close(audit)
    _collect_code_paths(audit)
    return audit


def code_only_assets(audit: CookAudit) -> list[tuple[str, list[str]]]:
    """Assets that exist, that code loads by path, that no cook root reaches.

    These load in the editor and are null in a packaged build.
    """
    findings = []
    for package, sites in sorted(audit.code_paths.items()):
        if package not in audit.packages:
            continue  # built later by a script, or a typo; a separate problem
        if package in audit.cooked:
            continue
        findings.append((package, sites))
    return findings


def code_load_coverage(audit: CookAudit) -> dict[str, str]:
    """Why each asset that only code loads survives the cook."""
    coverage = {}
    for package in sorted(audit.code_paths):
        if package not in audit.packages or package not in audit.cooked:
            continue
        if package in audit.always_cook_assets:
            coverage[package] = "named by an AlwaysCook primary-asset rule"
    return coverage


def _is_content_directory(audit: CookAudit, package: str) -> bool:
    """`/Game/Prototype/Materials` is a printf prefix, not an asset."""
    relative = package[len("/Game/"):].replace("/", os.sep)
    return os.path.isdir(os.path.join(audit.content_dir, relative))


def missing_code_assets(audit: CookAudit) -> list[tuple[str, list[str]]]:
    """Paths code loads that are not in the tree at all."""
    return [
        (package, sites)
        for package, sites in sorted(audit.code_paths.items())
        if package not in audit.packages
        and not _is_content_directory(audit, package)
    ]


def atlas_status(audit: CookAudit, entries=PRINT_ATLAS_ENTRIES) -> dict:
    """How many of the contracted print textures the cook still pulls in."""
    still_cooked = {}
    for stem in entries:
        package = f"{ATLAS_TEXTURE_ROOT}/{stem}"
        if package not in audit.packages:
            continue
        if package not in audit.cooked:
            continue
        referrers = sorted(
            other for other in audit.cooked
            if package in audit.references.get(other, ())
        )
        still_cooked[stem] = referrers
    return {
        "contracted": len(entries),
        "still_cooked": still_cooked,
        "dropped": [stem for stem in entries if stem not in still_cooked],
    }


def atlas_entries_loaded_by_code(
    audit: CookAudit, entries=PRINT_ATLAS_ENTRIES
) -> list[tuple[str, list[str]]]:
    """Contracted atlas entries that C++ also loads by package path.

    A canvas draw has no UV transform, so these cannot read a page -- and the
    string load is invisible to the cooker, so nothing would keep the
    individual texture in the build once its material stops sampling it.
    They belong in ATLAS_EXCLUSIONS, not on a page.
    """
    conflicts = []
    for stem in entries:
        sites = audit.code_paths.get(f"{ATLAS_TEXTURE_ROOT}/{stem}")
        if sites:
            conflicts.append((stem, sites))
    return conflicts


def atlas_rule_cover(audit: CookAudit, entries=PRINT_ATLAS_ENTRIES) -> dict:
    """Which contracted textures a cook rule or directory would hold anyway.

    The whole saving rests on nothing naming these except the materials that
    are about to stop naming them. A rule or an always-cook directory that
    covers one keeps it in the pak after the rebuild, and it is then paying
    for a page it does not use.
    """
    covered = {}
    for stem in entries:
        package = f"{ATLAS_TEXTURE_ROOT}/{stem}"
        reasons = []
        if package in audit.always_cook_assets:
            for rule in audit.primary_asset_rules:
                if package in rule.packages:
                    reasons.append(
                        f"named by the {rule.name} rule "
                        f"({rule.source}:{rule.line})")
        for directory in audit.always_cook:
            if _is_under(package, directory):
                reasons.append(f"inside always-cook directory {directory}")
        if audit.packages.get(package, "").endswith(".umap"):
            reasons.append("is a map, which is always a cook root")
        if reasons:
            covered[stem] = reasons
    return covered


def atlas_photo_shadows(audit: CookAudit, entries=PRINT_ATLAS_ENTRIES) -> dict:
    """Contracted textures that have a `T_Photo_` twin in the tree.

    create_textured_materials._load_texture prefers the photo capture over the
    procedural texture of the same name. Where one exists the material samples
    it, so a page packed from the procedural source would put different art on
    screen -- and the photo twin, not the contracted one, is what stays cooked.
    """
    shadows = {}
    for stem in entries:
        if not stem.startswith("T_"):
            continue
        twin = f"{ATLAS_TEXTURE_ROOT}/T_Photo_{stem[2:]}"
        if twin in audit.packages:
            shadows[stem] = twin
    return shadows


def photo_twin(stem: str) -> str:
    """The `T_Photo_` name create_textured_materials._load_texture prefers."""
    return f"T_Photo_{stem[2:]}" if stem.startswith("T_") else ""


def resolved_print_texture(audit: CookAudit, stem: str) -> str:
    """The texture package a print material actually samples for this stem.

    _load_texture takes the photo capture over the procedural texture of the
    same name, so where a capture exists the material never names the
    contracted texture at all. Simulating the edge that is not there moves
    nothing, drops nothing, and reports a saving that did not happen -- so
    every step that touches an edge has to resolve the name first.
    """
    twin = photo_twin(stem)
    if twin:
        package = f"{ATLAS_TEXTURE_ROOT}/{twin}"
        if package in audit.packages:
            return package
    return f"{ATLAS_TEXTURE_ROOT}/{stem}"


def replaced_textures(audit: CookAudit, entries=PRINT_ATLAS_ENTRIES) -> set[str]:
    """Every texture package a page stands in for, under either name."""
    replaced = set()
    for stem in entries:
        replaced.add(f"{ATLAS_TEXTURE_ROOT}/{stem}")
        twin = photo_twin(stem)
        if twin:
            replaced.add(f"{ATLAS_TEXTURE_ROOT}/{twin}")
    return replaced


def _capture_pair(package: str) -> str:
    """The other half of a `T_X` / `T_Photo_X` pair, whichever half this is."""
    root, _, name = package.rpartition("/")
    if name.startswith("T_Photo_"):
        return f"{root}/T_{name[len('T_Photo_'):]}"
    if name.startswith("T_"):
        return f"{root}/T_Photo_{name[2:]}"
    return ""


def photo_split_loads(audit: CookAudit) -> list[dict]:
    """Textures where code and the materials picked different halves of a pair.

    _load_texture takes the CC0 capture over the procedural texture of the
    same name, so a surface with a capture is the capture everywhere it is
    drawn through a material. A LoadObject path does no such thing: it names
    one file. When the two disagree, the same surface has two appearances --
    the world shows one and the HUD shows the other -- and nothing fails, so
    the only way to notice is to look.

    Only pairs where both halves exist are reported. A path with no capture
    beside it is just a path.
    """
    findings = []
    for package in sorted(audit.code_paths):
        twin = _capture_pair(package)
        if not twin or twin not in audit.packages:
            continue

        def referrers(candidate):
            return sorted(
                other for other in audit.cooked
                if other != candidate
                and candidate in audit.references.get(other, ())
            )

        mine = referrers(package)
        theirs = referrers(twin)
        if theirs and not mine:
            findings.append({
                "loaded": package,
                "drawn": twin,
                "sites": audit.code_paths[package],
                "drawn_by": theirs,
            })
    return findings


def unused_capture_folders(audit: CookAudit) -> list[dict]:
    """`SourceArt/Photo/<Surface>/` folders whose import nothing draws.

    import_photo_textures.py names the asset after the folder, not after the
    file inside it, so the folder name is the only thing that says what a
    capture is. A folder no material consumes is either a mis-file -- the
    scan inside it is not the surface the name claims -- or art that was
    superseded and never removed. Both cost an import every art build and
    leave a plausible-looking name for the next person to reach for.
    """
    photo_dir = os.path.join(
        audit.project_root, "Content", "SourceArt", "Photo")
    if not os.path.isdir(photo_dir):
        return []
    unused = []
    for surface in sorted(os.listdir(photo_dir)):
        folder = os.path.join(photo_dir, surface)
        if not os.path.isdir(folder):
            continue
        package = f"{ATLAS_TEXTURE_ROOT}/T_Photo_{surface}_D"
        if package not in audit.packages:
            continue  # never imported; nothing to be unused
        drawn = [
            other for other in audit.cooked
            if other != package and package in audit.references.get(other, ())
        ]
        if drawn or audit.code_paths.get(package):
            continue
        # What is actually inside, so a mis-file reads as one.
        scans = sorted({
            entry.split("_2K")[0] for entry in os.listdir(folder)
            if "_2K" in entry
        })
        unused.append({
            "surface": surface,
            "package": package,
            "scans": scans,
        })
    return unused


def orphan_capture_pairs(audit: CookAudit) -> list[str]:
    """Halves of a capture pair that nothing references and nothing cooks.

    Not a build problem -- the cook already leaves them out. Reported because
    they are a trap: the next material or path that lands on the unused name
    silently gets the half nobody has looked at in a year.
    """
    orphans = []
    for package in sorted(audit.packages):
        if package in audit.cooked or audit.code_paths.get(package):
            continue
        twin = _capture_pair(package)
        if twin and twin in audit.packages:
            orphans.append(package)
    return orphans


def atlas_stale_samples(audit: CookAudit, entries=PRINT_ATLAS_ENTRIES) -> dict:
    """Packages that reference an atlas page *and* a texture it replaced.

    A material that reads the page has no use for the individual texture, so
    naming both means the pre-atlas sampler survived the rebuild -- which is
    what the in-place update path does if nothing retires it. The material
    draws from the page and the texture still ships: the page is paid for and
    nothing is saved.

    Both names count. A sampler left holding the photo capture is the same
    defect wearing the name that is easier to miss.
    """
    replaced = replaced_textures(audit, entries)
    page_prefix = f"{ATLAS_TEXTURE_ROOT}/{texture_atlas_contract.ATLAS_PAGE_PREFIX}"
    stale = {}
    for package in sorted(audit.cooked):
        references = audit.references.get(package, set())
        if not any(other.startswith(page_prefix) for other in references):
            continue
        left_over = sorted(references & replaced)
        if left_over:
            stale[package] = left_over
    return stale


def atlas_pages(audit: CookAudit, entries=PRINT_ATLAS_ENTRIES) -> dict[str, str]:
    """{stem: page package} from the packed layout, or {} if none is packed.

    Layout only: whether the manifest holds the contracted set is
    build_texture_atlas.py --check's question, not this one's.
    """
    manifest = texture_atlas_contract.try_load_manifest(
        audit.atlas_manifest, strict=False)
    if manifest is None:
        return {}
    pages = {}
    for stem in entries:
        entry = (manifest.get("entries") or {}).get(stem)
        if entry is not None:
            pages[stem] = texture_atlas_contract.page_package_path(
                entry["page"])
    return pages


def _replay(audit: CookAudit, pages: dict[str, str]) -> CookAudit:
    """A copy of the graph, with the atlas pages present but nothing rewired.

    The pages do not exist on disk until the editor imports them, so the copy
    has to carry them or nothing can be reachable through one.
    """
    after = CookAudit(audit.project_root)
    after.always_cook = list(audit.always_cook)
    after.never_cook = list(audit.never_cook)
    after.always_cook_assets = set(audit.always_cook_assets)
    after.primary_asset_rules = audit.primary_asset_rules
    after.packages = dict(audit.packages)
    after.unreadable = set(audit.unreadable)
    after.code_paths = audit.code_paths
    after.references = {
        package: set(references)
        for package, references in audit.references.items()
    }
    for page in set(pages.values()):
        after.packages.setdefault(page, "<imported by the atlas stage>")
        after.references.setdefault(page, set())
    return after


def targeted_pre_atlas_graph(audit: CookAudit, materials: dict[str, str],
                             entries=PRINT_ATLAS_ENTRIES) -> CookAudit:
    """이미 반입한 저장소에서도 교체 전 샘플러를 재현해 부분 갱신을 검사한다."""
    before = _replay(audit, atlas_pages(audit, entries))
    for material, stem in materials.items():
        if material in before.references:
            # 공용 페이지를 쓰는 현재 그래프에서는 옛 텍스처가 이미 빠져 있다.
            # 파일까지 정리됐으면 노드도 복원해야 _close가 옛 간선을 따라간다.
            # 복제 그래프에만 넣고, 다른 패키지가 잡은 참조는 보존한다.
            texture = resolved_print_texture(audit, stem)
            before.packages.setdefault(texture, "<pre-atlas texture fixture>")
            before.references.setdefault(texture, set())
            before.references[material].add(texture)
    _seed_roots(before)
    _close(before)
    return before


def simulate_targeted_pass(
    audit: CookAudit,
    materials: dict[str, str],
    retire: bool,
    entries=PRINT_ATLAS_ENTRIES,
    photo_aware: bool = True,
) -> dict:
    """Run one `IG_*_ONLY` in-place pass over a pre-atlas graph.

    ``materials`` maps a material package to the *contracted stem* its spec
    names. Which texture the material really samples is resolved here, because
    _load_texture prefers a photo capture where one exists.

    An in-place pass appends the atlas graph and reconnects the outputs, so
    the material gains an edge to the page either way. ``retire`` is whether
    _retire_pre_atlas_samples() then takes the old edge away, and
    ``photo_aware`` is whether it recognises the capture's name when it does.
    Those are two separate ways to keep a texture in the cook, and the second
    one only shows up on the assets that have a capture -- so it is modelled
    separately rather than folded into the first.
    """
    pages = atlas_pages(audit, entries)
    after = _replay(audit, pages)
    sampled = {}
    for material, stem in materials.items():
        page = pages.get(stem)
        if page is None or material not in after.references:
            continue
        texture = resolved_print_texture(audit, stem)
        after.references[material].add(page)
        if retire:
            contracted = texture.rsplit("/", 1)[-1] == stem
            if contracted or photo_aware:
                after.references[material].discard(texture)
        sampled[stem] = texture

    _seed_roots(after)
    _close(after)
    return {
        "after": after,
        "sampled": sampled,
        "touched": sorted(sampled),
        "kept": sorted(
            texture for texture in sampled.values() if texture in after.cooked),
        "dropped": sorted(
            texture for texture in sampled.values()
            if texture in audit.cooked and texture not in after.cooked),
        "stale": atlas_stale_samples(after, entries),
    }


def simulate_atlas_rebuild(audit: CookAudit, entries=PRINT_ATLAS_ENTRIES) -> dict:
    """Re-run reachability as if the editor had rebuilt the print materials.

    The rebuild changes exactly one thing per material: the diffuse sample
    moves from the individual texture to the atlas page. So the simulation is
    an edit to the graph, not a guess -- for every package that references a
    contracted texture, swap that edge for an edge to the page the manifest
    put it on. Everything else about the material, including the companion
    normal/roughness maps it also samples, is left alone because the rebuild
    leaves it alone.

    The interesting output is not that the textures leave. It is what *else*
    leaves, because a collateral drop is a shipping bug the atlas caused.
    """
    # Layout only: which stem landed on which page. Whether the manifest holds
    # the contracted set is build_texture_atlas.py --check's question.
    pages = atlas_pages(audit, entries)
    after = _replay(audit, pages)

    # Only the print materials are rebuilt. An edge from anywhere else -- a
    # mesh's material slot, a map, a data asset -- is one create_textured_
    # materials.py never touches, so the simulation must not touch it either:
    # that texture goes on shipping and the page is spent for nothing.
    rewritten = {}
    stragglers = {}
    sampled = {}
    for stem in entries:
        page = pages.get(stem)
        if page is None:
            continue
        # The edge the rebuild moves is the one that exists, which is the
        # photo capture wherever there is one.
        package = resolved_print_texture(audit, stem)
        sampled[stem] = package
        for referrer, references in after.references.items():
            if package not in references:
                continue
            if _is_under(referrer, MATERIAL_ROOT):
                references.discard(package)
                references.add(page)
                rewritten.setdefault(stem, []).append(referrer)
            else:
                stragglers.setdefault(stem, []).append(referrer)

    _seed_roots(after)
    _close(after)

    dropped = sorted(audit.cooked - after.cooked)
    gained = sorted(after.cooked - audit.cooked)
    # What the rebuild is meant to take out is whatever each material was
    # sampling, capture or not -- not the contracted name in the abstract.
    expected = set(sampled.values())
    return {
        "after": after,
        "pages": sorted(set(pages.values())),
        "rewritten": rewritten,
        "stragglers": stragglers,
        "sampled": sampled,
        "dropped": dropped,
        "gained": gained,
        "atlas_dropped": sorted(expected & set(dropped)),
        "atlas_kept": sorted(
            package for package in expected if package in after.cooked),
        "collateral": sorted(set(dropped) - expected),
    }


def cook_rule_problems(audit: CookAudit) -> list[tuple[PrimaryAssetRule, str]]:
    """Every reason a parsed Asset Manager rule would not do what it says."""
    return [
        (rule, problem)
        for rule in audit.primary_asset_rules
        for problem in rule.problems
    ]


def rule_coverage(audit: CookAudit) -> list[dict]:
    """For each per-asset cook rule, what it actually holds in the build.

    An entry is *load-bearing* when the rule is the only thing cooking it:
    code loads it by path, and no other cooked package references it. That is
    the case the rule exists for, and the one worth proving one asset at a
    time.
    """
    coverage = []
    for rule in audit.primary_asset_rules:
        if not rule.packages:
            continue
        entries = []
        for package in rule.packages:
            others = sorted(
                other for other in audit.cooked
                if other != package
                and package in audit.references.get(other, ())
            )
            entries.append({
                "package": package,
                "cooked": package in audit.cooked,
                "root": package in audit.roots,
                "code_sites": audit.code_paths.get(package, []),
                "other_referrers": others,
                "load_bearing": bool(audit.code_paths.get(package))
                and not others,
            })
        coverage.append({
            "type": rule.name,
            "line": rule.line,
            "cook_rule": rule.cook_rule,
            "base_class": rule.base_class,
            "entries": entries,
        })
    return coverage


def gate(
    audit: CookAudit,
    require_atlas_dropped: bool = False,
    entries=PRINT_ATLAS_ENTRIES,
) -> tuple[int, str]:
    """(exit code, closing line) for --check. Separated so it can be tested."""
    # A rule Unreal would not parse, or would parse into something other than
    # what it says, is read off the ini and the tree alone.
    rule_problems = cook_rule_problems(audit)
    if rule_problems:
        return 1, (
            f"FAIL {len(rule_problems)} problem(s) in the Asset Manager cook "
            "rules; they would hold less than they claim"
        )

    # An atlas entry that code also loads by path is read off the contract and
    # the source, so this verdict holds whatever the working tree fetched.
    conflicts = atlas_entries_loaded_by_code(audit, entries)
    if conflicts:
        return 1, (
            f"FAIL {len(conflicts)} atlassed texture(s) are loaded by code; "
            "they belong in ATLAS_EXCLUSIONS"
        )

    # Everything below is read off the reference graph. With a hole in it a
    # finding may be an artefact of the hole, and so may its absence, so
    # neither gate can be honoured. Say which, rather than passing quietly.
    blind = blind_spots(audit)
    if blind:
        return 0, (
            f"SKIP {len(blind)} cooked package(s) not fetched from LFS; "
            "reachability could not be decided. Run git lfs pull."
        )

    code_only = code_only_assets(audit)
    if code_only:
        return 1, (
            f"FAIL {len(code_only)} asset(s) only code reaches; they are "
            "absent from the pak and null at runtime"
        )

    stale = atlas_stale_samples(audit, entries)
    if stale:
        return 1, (
            f"FAIL {len(stale)} material(s) reference both an atlas page and "
            "a texture it replaced; the page is paid for and nothing saved"
        )

    split = photo_split_loads(audit)
    if split:
        return 1, (
            f"FAIL {len(split)} texture(s) are loaded by code from one half of "
            "a capture pair while the materials draw the other"
        )

    still_cooked = atlas_status(audit, entries)["still_cooked"]
    if require_atlas_dropped and still_cooked:
        return 1, (
            f"FAIL {len(still_cooked)} atlassed texture(s) are still cooked; "
            "the print materials were not rebuilt"
        )
    return 0, ""


def _report(audit: CookAudit) -> None:
    print(
        f"COOK REFERENCE AUDIT  packages={len(audit.packages)} "
        f"roots={len(audit.roots)} reachable={len(audit.cooked)} "
        f"unreadable={len(audit.unreadable)}"
    )
    print("  always cook: " + ", ".join(audit.always_cook))
    if audit.never_cook:
        print("  never cook:  " + ", ".join(audit.never_cook))
    blind = blind_spots(audit)
    if blind:
        print(
            f"  {len(blind)} cooked package(s) are unfetched LFS pointers, so "
            "the reference graph has holes and everything below is advisory "
            "only (run git lfs pull)"
        )
    elif audit.unreadable:
        print(
            f"  {len(audit.unreadable)} unfetched LFS pointer(s), none of them "
            "cooked; the reference graph is complete"
        )

    for rule, problem in cook_rule_problems(audit):
        print(f"  [COOK_RULE] {rule.source}:{rule.line} "
              f"{rule.name}: {problem}")

    for rule in rule_coverage(audit):
        held = [e for e in rule["entries"] if e["load_bearing"]]
        redundant = [e for e in rule["entries"] if e["other_referrers"]]
        unused = [
            e for e in rule["entries"]
            if not e["code_sites"] and not e["other_referrers"]
        ]
        print(
            f"  rule {rule['type']} ({rule['cook_rule']}, "
            f"{rule['base_class'].rsplit('.', 1)[-1]}) holds "
            f"{len(rule['entries'])} asset(s): {len(held)} load-bearing, "
            f"{len(redundant)} also referenced elsewhere, {len(unused)} "
            "referenced by nothing"
        )
        for entry in rule["entries"]:
            if not entry["cooked"]:
                print(f"      NOT COOKED {entry['package']}")

    conflicts = atlas_entries_loaded_by_code(audit)
    if conflicts:
        print(f"\n{len(conflicts)} atlas entr(y/ies) are also loaded by code:")
        for stem, sites in conflicts:
            print(f"  [ATLAS_CODE_LOAD] {stem}  <- {', '.join(sites)}")

    split = photo_split_loads(audit)
    if split:
        print(f"\n{len(split)} texture(s) split across a capture pair:")
        for item in split:
            print(f"  [PHOTO_SPLIT] {item['loaded'].rsplit('/', 1)[-1]}")
            print(f"      loaded by {', '.join(item['sites'])}")
            print(f"      but every material draws "
                  f"{item['drawn'].rsplit('/', 1)[-1]} "
                  f"({len(item['drawn_by'])} referrer(s)); the same surface "
                  "has two appearances")

    unused = unused_capture_folders(audit)
    if unused:
        print(f"\n{len(unused)} capture folder(s) nothing draws:")
        for item in unused:
            inside = ", ".join(item["scans"]) or "?"
            print(f"  [UNUSED_CAPTURE] SourceArt/Photo/{item['surface']}/ "
                  f"holds {inside}")
            print(f"      imported as {item['package'].rsplit('/', 1)[-1]} "
                  "every art build; no material samples it")

    orphans = orphan_capture_pairs(audit)
    if orphans:
        captures = [o for o in orphans if "/T_Photo_" in o]
        procedural = [o for o in orphans if "/T_Photo_" not in o]
        print(f"\n{len(orphans)} unused half/halves of a capture pair "
              "(not cooked, not a build problem, but nothing reads them):")
        if procedural:
            print(f"  {len(procedural)} procedural half/halves a capture won: "
                  + ", ".join(o.rsplit("/", 1)[-1] for o in procedural))
        if captures:
            print(f"  {len(captures)} capture half/halves nothing won with: "
                  + ", ".join(o.rsplit("/", 1)[-1] for o in captures))

    findings = code_only_assets(audit)
    if findings:
        print(f"\n{len(findings)} asset(s) reachable only from code:")
        for package, sites in findings:
            print(f"  [CODE_ONLY] {package}")
            print(f"      loaded by {', '.join(sites)}")
            print("      no cooked package references it; null once packaged")

    missing = missing_code_assets(audit)
    if missing:
        unguarded = [(p, s) for p, s in missing
                     if p not in audit.guarded_code_paths]
        guarded = [(p, s) for p, s in missing
                   if p in audit.guarded_code_paths]
        if unguarded:
            print(f"\n{len(unguarded)} path(s) code loads unguarded that are "
                  "not in the tree:")
            for package, sites in unguarded:
                print(f"  [ABSENT] {package}  <- {', '.join(sites)}")
                print("      the load returns null and nothing checks it")
        if guarded:
            print(f"\n{len(guarded)} path(s) code loads that are not in the "
                  "tree, each behind a null check:")
            for package, sites in guarded:
                print(f"  [ABSENT_GUARDED] {package}  <- {', '.join(sites)}")
                print("      falls back at runtime; built by the art pass, "
                      "not committed")

    status = atlas_status(audit)
    kept = len(status["still_cooked"])
    print(
        f"\nprint atlas: {len(status['dropped'])}/{status['contracted']} "
        f"contracted textures are out of the cook, {kept} still in"
    )
    if kept:
        example = sorted(status["still_cooked"])[:3]
        for stem in example:
            referrers = status["still_cooked"][stem] or ["(no referrer found)"]
            print(f"  {stem} <- {', '.join(r.rsplit('/', 1)[-1] for r in referrers)}")
        if kept > len(example):
            print(f"  ... and {kept - len(example)} more")
        print(
            "  expected before the editor rebuild: the committed materials "
            "still sample their own texture."
        )
    print(f"  {len(ATLAS_EXCLUSIONS)} texture(s) deliberately off the atlas")


def command_explain_rules(audit: CookAudit) -> int:
    """Walk every per-asset cook rule entry and say what holds it, and why.

    The counterfactual is the point: for each asset, re-run reachability with
    the rules removed and report whether it survives. An entry that survives
    either way is redundant; one that does not is the rule earning its line.
    """
    without = run_audit(audit.project_root)
    without.always_cook_assets = set()
    without.roots = set()
    without.cooked = set()
    _seed_roots(without)
    _close(without)

    problems = cook_rule_problems(audit)
    print(f"COOK RULES  {audit.game_config}")
    for rule in audit.primary_asset_rules:
        print(
            f"\n  line {rule.line}  {rule.name}  CookRule={rule.cook_rule}  "
            f"AssetBaseClass={rule.base_class}"
        )
        print(f"    parsed fields: {', '.join(sorted(rule.fields))}")
        if rule.directories:
            print(f"    directories:   {', '.join(rule.directories)}")
        for problem in rule.problems:
            print(f"    PROBLEM  {problem}")
        if not rule.packages:
            continue
        print(f"    {len(rule.packages)} named asset(s):")
        for package in rule.packages:
            sites = audit.code_paths.get(package, [])
            held = package in audit.cooked
            survives = package in without.cooked
            if held and not survives:
                verdict = "HELD BY THIS RULE"
            elif held and survives:
                verdict = "cooked anyway (rule redundant here)"
            else:
                verdict = "NOT COOKED"
            loaded = (
                "loaded by " + ", ".join(sites) if sites
                else "not loaded by any code path"
            )
            print(f"      {verdict:34s} {package.rsplit('/', 1)[-1]}")
            print(f"        {loaded}")

    held = sum(
        1 for rule in audit.primary_asset_rules for package in rule.packages
        if package in audit.cooked and package not in without.cooked
    )
    named = sum(len(rule.packages) for rule in audit.primary_asset_rules)
    print(
        f"\n{held}/{named} named asset(s) are in the cook only because of "
        f"these rules; {len(problems)} problem(s) found"
    )
    return 1 if problems else 0


def hud_texture_rule(assets: str, **overrides: str) -> str:
    """A well-formed per-asset AlwaysCook rule, with fields swappable.

    The overrides are what the negative half of the self-test uses: each one
    breaks the rule in a way Unreal reacts to by holding less than the line
    says, without changing anything a regex would notice.
    """
    fields = {
        "PrimaryAssetType": '"T"',
        "AssetBaseClass": '"/Script/Engine.Texture2D"',
        "bHasBlueprintClasses": "False",
        "bIsEditorOnly": "False",
        "Directories": "",
        "SpecificAssets": f"({assets})" if assets else "",
        "Rules": "(Priority=0,ChunkId=-1,bApplyRecursively=False,"
                 "CookRule=AlwaysCook)",
    }
    for key, value in overrides.items():
        fields[key] = value
    body = ",".join(f"{key}={value}" for key, value in fields.items())
    return f"+{PRIMARY_ASSET_SETTING}=({body})"


def command_simulate_rebuild(audit: CookAudit) -> int:
    """Report the whole delta of the material rebuild, and fault the surprises.

    Three ways this can be wrong, all of them checked here rather than
    asserted: something other than a print material references a contracted
    texture, so the rebuild leaves it cooked; a rule or an always-cook
    directory covers one, so it ships regardless; or a texture with a
    `T_Photo_` twin is not actually the one the material samples.
    """
    blind = blind_spots(audit)
    if blind:
        print(
            f"SKIP {len(blind)} cooked package(s) not fetched from LFS. A "
            "texture whose material could not be opened looks unreferenced, "
            "so every entry would report as dropping. Run git lfs pull."
        )
        return 0

    result = simulate_atlas_rebuild(audit)
    covered = atlas_rule_cover(audit)
    shadows = atlas_photo_shadows(audit)
    stale = atlas_stale_samples(audit)
    findings = 0

    print(f"ATLAS REBUILD SIMULATION  {len(PRINT_ATLAS_ENTRIES)} contracted "
          f"textures onto {len(result['pages'])} page(s)")
    if not result["pages"]:
        # The atlas is optional by design: no manifest means the materials
        # keep their own textures and the build still ships.
        print("  SKIP no packed atlas; run Scripts/build_texture_atlas.py")
        return 0

    for stem in PRINT_ATLAS_ENTRIES:
        # The package that actually carries the edge, which is the photo
        # capture wherever one exists.
        package = result["sampled"].get(
            stem, f"{ATLAS_TEXTURE_ROOT}/{stem}")
        referrers = sorted(
            other for other in audit.cooked
            if package in audit.references.get(other, ())
        )
        stragglers = result["stragglers"].get(stem, [])
        note = ""
        if package in result["atlas_dropped"]:
            verdict = "drops out"
        elif package not in audit.cooked:
            verdict = "already out"
        else:
            verdict = "STAYS IN"
            findings += 1
        if stem in covered:
            note = "; " + "; ".join(covered[stem])
            findings += 1
        if stem in shadows:
            # Handled by the simulation, still a finding: a page packed from
            # the procedural source is not the picture the capture was showing.
            note += (f"; sampled through {shadows[stem].rsplit('/', 1)[-1]}, "
                     "so the page holds different art")
            findings += 1
        if stragglers:
            note += "; not a print material: " + ", ".join(
                s.rsplit("/", 1)[-1] for s in stragglers)
            findings += 1
        print(
            f"  {verdict:12s} {stem:32s} "
            f"{len(referrers)} referrer(s){note}"
        )

    print(
        f"\n  {len(result['atlas_dropped'])}/{len(PRINT_ATLAS_ENTRIES)} leave "
        f"the cook, {len(result['atlas_kept'])} stay"
    )
    print(f"  pages enter the cook: {len(result['gained'])} package(s)")
    for package in result["gained"]:
        print(f"      + {package}")
    if result["collateral"]:
        findings += len(result["collateral"])
        print(f"\n  {len(result['collateral'])} package(s) leave that are not "
              "contracted textures -- the rebuild would break these:")
        for package in result["collateral"]:
            print(f"      - {package}")
    else:
        print("  nothing else leaves the cook")

    if stale:
        findings += len(stale)
        print(f"\n  {len(stale)} material(s) already name a page and a texture "
              "it replaced; the pre-atlas sampler survived:")
        for package, left_over in stale.items():
            print(f"      {package.rsplit('/', 1)[-1]} -> "
                  + ", ".join(s.rsplit("/", 1)[-1] for s in left_over))

    if findings:
        print(f"\nFAIL {findings} finding(s): the rebuild would not do what "
              "the atlas contract says")
        return 1
    print("\nPASS every contracted texture leaves the cook on the rebuild, "
          "nothing else does, and no rule or directory holds one back")
    return 0


def exposed_passes(entries=PRINT_ATLAS_ENTRIES) -> list:
    """In-place build passes that touch a material reading the atlas.

    Read out of create_textured_materials.py rather than listed here, so a
    mode added later is caught instead of missed.
    """
    contracted = set(entries)
    exposed = []
    for build_pass in material_passes.material_passes():
        if not (build_pass.update_in_place and build_pass.atlas_aware):
            continue
        textures = {
            material: stem
            for material, stem in material_passes.pass_textures(
                build_pass).items()
            if stem in contracted
        }
        if textures:
            exposed.append((build_pass, textures))
    return exposed


def command_simulate_targeted(audit: CookAudit) -> int:
    """현재 재질에 교체 전 샘플러를 복원해 부분 갱신의 정상·결함 경로를 비교한다.

    옛 텍스처 파일을 정리한 뒤에도 같은 조건을 재현한다. 샘플러를 해제하면
    해당 텍스처가 쿠크에서 빠져야 하고, 해제하지 않으면 남아 있어야 한다.
    """
    blind = blind_spots(audit)
    if blind:
        print(f"SKIP {len(blind)} cooked package(s) not fetched from LFS. "
              "Run git lfs pull.")
        return 0
    if not atlas_pages(audit):
        print("SKIP no packed atlas; run Scripts/build_texture_atlas.py")
        return 0

    exposed = exposed_passes()
    total = sum(len(textures) for _pass, textures in exposed)
    print(f"TARGETED PASS SIMULATION  {len(exposed)} in-place pass(es) reach "
          f"{total} contracted texture(s)")
    findings = 0

    for build_pass, textures in exposed:
        packages = {
            f"{MATERIAL_ROOT}/{material}": stem
            for material, stem in textures.items()
        }
        absent = sorted(m for m in packages if m not in audit.packages)
        live = {m: s for m, s in packages.items() if m in audit.packages}

        # Three ways the pass can end, not two. The middle column is the
        # retirement that only knows the contracted name: it is enough for a
        # texture with no photo capture and does nothing at all for one that
        # has one, which is the failure that hides.
        before = targeted_pre_atlas_graph(audit, live)
        fixed = simulate_targeted_pass(before, live, retire=True)
        contract_only = simulate_targeted_pass(
            before, live, retire=True, photo_aware=False)
        broken = simulate_targeted_pass(before, live, retire=False)
        print(
            f"\n  {build_pass.name}  "
            f"({os.path.basename(material_passes.BUILDER_SOURCE)}:"
            f"{build_pass.line})  {len(textures)} contracted texture(s)"
        )
        if absent:
            print(f"    {len(absent)} material(s) not built yet: "
                  + ", ".join(a.rsplit('/', 1)[-1] for a in absent))
        for material, stem in sorted(live.items()):
            texture = fixed["sampled"].get(stem)
            if texture is None:
                continue
            sampled_name = texture.rsplit("/", 1)[-1]
            capture = sampled_name != stem
            with_fix = "drops" if texture in fixed["dropped"] else "STAYS"
            name_only = "drops" if texture in contract_only["dropped"] else "stays"
            without = "stays" if texture in broken["kept"] else "DROPS"
            flag = ""
            if with_fix != "drops":
                flag = "  <-- the retirement did not take"
                findings += 1
            elif without != "stays":
                flag = "  <-- nothing for the retirement to do"
            elif capture and name_only == "drops":
                flag = "  <-- a capture retired by its contracted name?"
                findings += 1
            shown = sampled_name if not capture else f"{sampled_name} (capture)"
            print(f"    {shown:34s} retired: {with_fix:5s}  "
                  f"by name only: {name_only:5s}  not retired: {without}{flag}")

        if fixed["stale"]:
            findings += len(fixed["stale"])
            print(f"    {len(fixed['stale'])} material(s) would still name "
                  "both the page and the texture after retirement")
        captures = [s for s, t in fixed["sampled"].items()
                    if t.rsplit("/", 1)[-1] != s]
        if captures:
            missed = len(contract_only["kept"])
            print(f"    {len(captures)} texture(s) are sampled through a photo "
                  f"capture; retiring by contracted name alone would leave "
                  f"{missed} in the cook")
        if not broken["kept"]:
            findings += 1
            print("    NOTHING WOULD BREAK without the retirement -- either "
                  "this pass no longer updates in place, or the simulation "
                  "has stopped modelling it")
        else:
            print(f"    without the retirement {len(broken['kept'])} of "
                  f"{len(live)} texture(s) stay in the cook, and "
                  f"{len(broken['stale'])} material(s) name both")

    if findings:
        print(f"\nFAIL {findings} finding(s)")
        return 1
    print(f"\nPASS all {total} texture(s) leave the cook when a targeted pass "
          "retires the pre-atlas sampler, and stay when it does not")
    return 0


def _fixture(root: str, rule: str | None, atlas_material_reads: str,
             determine_ids: bool = True) -> str:
    """A small project: a map, a mesh, a print material and three textures.

    ``rule`` is the Asset Manager line to write, or None for a config with no
    per-asset rule at all. ``atlas_material_reads`` is the package the print
    material samples, which is what changes when the editor rebuilds it to
    read an atlas page.
    """
    os.makedirs(os.path.join(root, "Config"), exist_ok=True)
    with open(os.path.join(root, "Config", "DefaultGame.ini"), "w") as handle:
        handle.write(
            "[/Script/UnrealEd.ProjectPackagingSettings]\n"
            "+DirectoriesToAlwaysCook=(Path=\"/Game/Prototype/Materials\")\n"
            "\n[/Script/Engine.AssetManagerSettings]\n"
        )
        if rule:
            handle.write(rule + "\n")
            if determine_ids:
                handle.write(f"{DETERMINE_ID_SETTING}=True\n")

    def package(relative: str, body: str) -> None:
        path = os.path.join(root, "Content", *relative.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as handle:
            handle.write(body.encode("ascii"))

    # The map is the only root that is not named by a directory rule, so it
    # carries the one reference that proves maps are seeded.
    package("Maps/Level.umap", "/Game/Meshes/SM_Placed")
    package("Meshes/SM_Placed.uasset", "StaticMesh")
    package("Prototype/Materials/M_Print.uasset", atlas_material_reads)
    # Real texture packages carry their class name in the export table, which
    # is what lets the rule's AssetBaseClass be checked against them.
    package("Prototype/Textures/T_Print_D.uasset", "Texture2D")
    package("Prototype/Textures/T_PrintAtlas0_D.uasset", "Texture2D")
    package("Prototype/Textures/T_HudFrame_D.uasset", "Texture2D")

    # A one-entry manifest, so the rebuild simulation has a page to move the
    # contracted texture onto. Geometry only; membership is the packer's check.
    atlas = os.path.join(root, "Content", "SourceArt", "Atlas")
    os.makedirs(atlas, exist_ok=True)
    with open(os.path.join(atlas, "print_atlas.json"), "w") as handle:
        json.dump({
            "version": texture_atlas_contract.MANIFEST_VERSION,
            "page_size": texture_atlas_contract.PAGE_SIZE,
            "gutter": texture_atlas_contract.GUTTER,
            "pages": [{"index": 0, "width": 256, "height": 256}],
            "entries": {
                "T_Print_D": {
                    "page": 0, "x": 0, "y": 0, "w": 128, "h": 128,
                    "uv_scale": [0.5, 0.5], "uv_bias": [0.0, 0.0],
                },
            },
        }, handle)

    # Two capture folders: one whose import a material draws, one whose
    # contents do not match the name on the folder and which nothing draws.
    for surface, scan in (("HudFrame", "Metal032"), ("Wallpaper", "Plaster003")):
        folder = os.path.join(root, "Content", "SourceArt", "Photo", surface)
        os.makedirs(folder, exist_ok=True)
        for role in ("_Color.jpg", "_NormalDX.jpg"):
            with open(os.path.join(folder, f"{scan}_2K-JPG{role}"), "wb") as h:
                h.write(b"jpeg")
    # The mis-filed one has been imported, the way an art build imports every
    # folder it finds -- which is what makes it cost something.
    package("Prototype/Textures/T_Photo_Wallpaper_D.uasset", "Texture2D")

    source = os.path.join(root, "Source", "Player")
    os.makedirs(source, exist_ok=True)
    with open(os.path.join(source, "HUD.cpp"), "w") as handle:
        # Split mid-path across two literals, the way IGListenerEntity.cpp
        # writes a path too long for the column limit. Read as written this
        # is a directory and an unrelated word, not an asset.
        handle.write(
            'X = LoadObject<UTexture2D>(nullptr,\n'
            '    TEXT("/Game/Prototype/Textures/"\n'
            '        "T_HudFrame_D.T_HudFrame_D"));\n'
        )
    # Two loads of packages that are not in the tree: one the code falls back
    # from, one it does not. They differ only in the null check, which is the
    # whole distinction the report draws between them.
    #
    # The unguarded load comes first on purpose. Both assignments sit inside
    # one lookbehind window, so a classifier that takes the first rather than
    # the nearest tests `Assumed` for the `Fallback` load and calls the
    # guarded one unguarded. Written the other way round it gets both right
    # by luck and the ordering is never actually checked.
    with open(os.path.join(source, "Absent.cpp"), "w") as handle:
        handle.write(
            'UMaterialInterface* Assumed = LoadObject<UMaterialInterface>(\n'
            '    nullptr, TEXT("/Game/Prototype/Materials/M_Gone.M_Gone"));\n'
            'UMaterialInterface* Fallback = LoadObject<UMaterialInterface>(\n'
            '    nullptr, TEXT("/Game/Prototype/Materials/M_Late.M_Late"));\n'
            'if (!Fallback)\n'
            '{\n'
            '    Fallback = KnownMaterial;\n'
            '}\n'
            'Assumed->SetScalarParameterValue(TEXT("X"), 1.0f);\n'
        )
    # The same absent path, loaded a second time with a guard. One careful
    # caller does not make the careless one safe, so this must stay unguarded.
    with open(os.path.join(source, "AbsentAgain.cpp"), "w") as handle:
        handle.write(
            'UMaterialInterface* Careful = LoadObject<UMaterialInterface>(\n'
            '    nullptr, TEXT("/Game/Prototype/Materials/M_Gone.M_Gone"));\n'
            'if (!Careful)\n'
            '{\n'
            '    return;\n'
            '}\n'
        )
    # A load passed straight into a call, with no pointer to test. Nothing can
    # be proved about it, and unprovable has to mean unguarded: the other way
    # round, every load the scan cannot read turns into a silent reassurance.
    with open(os.path.join(source, "AbsentInline.cpp"), "w") as handle:
        handle.write(
            'Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(\n'
            '    nullptr, TEXT("/Game/Prototype/Materials/M_Bare.M_Bare")));\n'
        )
    return root


def command_self_test() -> int:
    """Exercise both verdicts on a synthetic project.

    The gate that matters here -- ``--require-atlas-dropped`` -- cannot pass
    in this repository until an editor has rebuilt the print materials, so
    without this it would ship never having been seen to pass at all.
    """
    import contextlib
    import io
    import tempfile

    entries = ("T_Print_D",)
    hud = "/Game/Prototype/Textures/T_HudFrame_D"
    good_rule = hud_texture_rule(f'"{hud}.T_HudFrame_D"')
    with tempfile.TemporaryDirectory() as tmp:
        # 1. Before the rebuild: the material samples its own texture, and
        #    nothing but code names the HUD frame.
        before = _fixture(
            os.path.join(tmp, "before"), None,
            "/Game/Prototype/Textures/T_Print_D")
        audit = run_audit(before)
        assert "/Game/Meshes/SM_Placed" in audit.cooked, \
            "a mesh the map places is not cooked; maps are not seeded as roots"
        assert [p for p, _ in code_only_assets(audit)] == [hud], \
            "a code-only asset was not reported"
        assert atlas_status(audit, entries)["still_cooked"], \
            "an atlassed texture its material still samples looks dropped"
        assert not atlas_entries_loaded_by_code(audit, entries), \
            "a texture code does not load was reported as a code load"

        # 2. After: the material reads the page, and the config names the
        #    frame. Both findings clear, and the texture leaves the cook.
        after = _fixture(
            os.path.join(tmp, "after"), good_rule,
            "/Game/Prototype/Textures/T_PrintAtlas0_D")
        audit = run_audit(after)
        assert not cook_rule_problems(audit), \
            f"a well-formed rule was faulted: {cook_rule_problems(audit)}"
        assert not code_only_assets(audit), \
            "an asset named by SpecificAssets is still reported unreachable"
        assert not atlas_status(audit, entries)["still_cooked"], \
            "a texture nothing references any more still looks cooked"
        assert hud in audit.cooked, "the SpecificAssets rule was not parsed"
        # The rule is load-bearing for that texture, not decoration: with the
        # per-asset rules taken away it drops straight out of the cook.
        holding = [
            entry for rule in rule_coverage(audit)
            for entry in rule["entries"] if entry["load_bearing"]
        ]
        assert [entry["package"] for entry in holding] == [hud], \
            "the rule was not reported as the only thing cooking the texture"

        # 2a. A path that is not in the tree is a defect when the code
        #     dereferences the result and a decision when it falls back. The
        #     two fixture loads differ only in the null check, so anything
        #     that classifies them alike is reading the literal, not the code.
        absent = dict(missing_code_assets(audit))
        late = "/Game/Prototype/Materials/M_Late"
        gone = "/Game/Prototype/Materials/M_Gone"
        bare = "/Game/Prototype/Materials/M_Bare"
        assert set(absent) == {late, gone, bare}, \
            f"the absent code loads were not all found: {sorted(absent)}"
        assert late in audit.guarded_code_paths, \
            "a load with a null check and a fallback was called unguarded"
        assert gone not in audit.guarded_code_paths, \
            "a load nothing checks was called guarded"
        assert bare not in audit.guarded_code_paths, \
            "a load with no pointer to test was called guarded"
        assert len(absent[gone]) == 2, \
            f"a path loaded from two files listed one site: {absent[gone]}"
        # The report is the only surface anyone reads. A finding the audit
        # computes and then does not print is the same as no finding at all.
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            _report(audit)
        rendered = printed.getvalue()
        for marker in ("[ABSENT] " + gone, "[ABSENT_GUARDED] " + late):
            assert marker in rendered, \
                f"the report did not surface {marker}"
        # Neither is a gate failure: an absent path is the art pass's business,
        # not the cook's. Asserted so a future gate change has to be deliberate.
        assert gate(audit, entries=entries)[0] == 0, \
            "an absent code load failed the gate"

        # 2b. Every way a rule can look right and hold nothing. Each of these
        #     passes a regex looking for `CookRule=AlwaysCook` and a quoted
        #     /Game path, which is why the rule is parsed instead.
        tampered = {
            "unparseable": good_rule[:-1],
            "unknown field": hud_texture_rule(
                f'"{hud}.T_HudFrame_D"').replace(
                    "SpecificAssets=", "SpecficAssets="),
            "misspelled cook rule": hud_texture_rule(
                f'"{hud}.T_HudFrame_D"',
                Rules="(Priority=0,ChunkId=-1,CookRule=AlwaysCok)"),
            "object name mismatch": hud_texture_rule(f'"{hud}.T_HudFrame"'),
            "asset not in the tree": hud_texture_rule(
                '"/Game/Prototype/Textures/T_Absent_D.T_Absent_D"'),
            "wrong AssetBaseClass": hud_texture_rule(
                f'"{hud}.T_HudFrame_D"',
                AssetBaseClass='"/Script/Engine.StaticMesh"'),
            "holds nothing": hud_texture_rule(""),
            "non-integer priority": hud_texture_rule(
                f'"{hud}.T_HudFrame_D"',
                Rules="(Priority=high,ChunkId=-1,CookRule=AlwaysCook)"),
            "directories not structs": hud_texture_rule(
                f'"{hud}.T_HudFrame_D"',
                Directories='("/Game/UI")'),
        }
        for name, rule_text in tampered.items():
            root = _fixture(
                os.path.join(tmp, "bad_" + name.replace(" ", "_")), rule_text,
                "/Game/Prototype/Textures/T_PrintAtlas0_D")
            audit = run_audit(root)
            assert cook_rule_problems(audit), \
                f"a rule broken by {name} was accepted"
            code, message = gate(audit, entries=entries)
            assert code == 1 and message.startswith("FAIL"), \
                f"a rule broken by {name} did not fail the gate"

        # Texture2D 규칙은 Asset Manager가 ID를 정하게 해야 쿠크가 멈추지 않는다.
        no_ids = _fixture(
            os.path.join(tmp, "bad_no_determine"), good_rule,
            "/Game/Prototype/Textures/T_PrintAtlas0_D", determine_ids=False)
        audit = run_audit(no_ids)
        assert cook_rule_problems(audit), \
            f"a Texture2D rule without {DETERMINE_ID_SETTING} was accepted"

        # 멀쩡한 규칙도 DefaultEngine.ini에 두면 언리얼은 읽지 않는다(config=Game).
        # 0.2.4까지 HUD 텍스처 8개가 이렇게 빠졌다.
        misplaced = _fixture(
            os.path.join(tmp, "bad_engine_config"), None,
            "/Game/Prototype/Textures/T_PrintAtlas0_D")
        with open(os.path.join(misplaced, "Config", "DefaultEngine.ini"), "w") as handle:
            handle.write("[/Script/Engine.AssetManagerSettings]\n" + good_rule + "\n")
        audit = run_audit(misplaced)
        assert cook_rule_problems(audit), \
            "a rule left in DefaultEngine.ini was accepted"
        code, message = gate(audit, entries=entries)
        assert code == 1 and message.startswith("FAIL"), \
            "a rule left in DefaultEngine.ini did not fail the gate"

        # 2c. The rebuild simulation, forwards. Before the editor runs, the
        #     material still samples its own texture, and moving that one edge
        #     to the page takes the texture out of the cook and nothing else.
        before_root = os.path.join(tmp, "before")
        audit = run_audit(before_root)
        result = simulate_atlas_rebuild(audit, entries)
        print_texture = f"{ATLAS_TEXTURE_ROOT}/T_Print_D"
        page = f"{ATLAS_TEXTURE_ROOT}/T_PrintAtlas0_D"
        assert result["pages"] == [page], \
            f"the manifest's page was not resolved: {result['pages']}"
        assert result["atlas_dropped"] == [print_texture], \
            f"the contracted texture did not leave the cook: {result}"
        assert not result["atlas_kept"] and not result["collateral"], \
            f"the rebuild moved more than the one edge: {result}"
        assert result["gained"] == [page], \
            f"the page did not enter the cook: {result['gained']}"

        # 2d. Every way the rebuild can fail to save anything, or break
        #     something. Each is a real shape: a mesh slot naming the texture
        #     directly, a cook rule covering it, a photo twin the material
        #     actually samples, the pre-atlas sampler surviving an in-place
        #     update, and a package reachable only through the texture.
        def tamper(name, files=None, engine=None):
            root = _fixture(
                os.path.join(tmp, "sim_" + name), engine or good_rule,
                f"{ATLAS_TEXTURE_ROOT}/T_Print_D")
            for relative, body in (files or {}).items():
                path = os.path.join(root, "Content", *relative.split("/"))
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as handle:
                    handle.write(body.encode("ascii"))
            return run_audit(root)

        audit = tamper("mesh_referrer", {
            "Meshes/SM_Placed.uasset": f"StaticMesh {print_texture}"})
        result = simulate_atlas_rebuild(audit, entries)
        assert result["stragglers"].get("T_Print_D") == ["/Game/Meshes/SM_Placed"], \
            "a referrer the rebuild does not touch was not reported"
        assert result["atlas_kept"] == [print_texture], \
            "a texture a mesh also names was reported as leaving the cook"

        audit = tamper("rule_cover", engine=hud_texture_rule(
            f'"{hud}.T_HudFrame_D","{print_texture}.T_Print_D"'))
        assert atlas_rule_cover(audit, entries), \
            "a cook rule covering a contracted texture was not reported"

        audit = tamper("photo_twin", {
            f"Prototype/Textures/T_Photo_Print_D.uasset": "Texture2D"})
        assert atlas_photo_shadows(audit, entries) == {
            "T_Print_D": f"{ATLAS_TEXTURE_ROOT}/T_Photo_Print_D"}, \
            "a photo twin the material would sample instead was not reported"

        audit = tamper("stale_sample", {
            "Prototype/Materials/M_Print.uasset": f"{page} {print_texture}"})
        assert atlas_stale_samples(audit, entries), \
            "a material naming both a page and its old texture was accepted"
        code, message = gate(audit, entries=entries)
        assert code == 1 and message.startswith("FAIL"), \
            "a surviving pre-atlas sampler did not fail the gate"

        audit = tamper("collateral", {
            "Prototype/Textures/T_Print_D.uasset":
                "Texture2D /Game/Meshes/SM_Orphan",
            "Meshes/SM_Orphan.uasset": "StaticMesh"})
        result = simulate_atlas_rebuild(audit, entries)
        assert result["collateral"] == ["/Game/Meshes/SM_Orphan"], \
            f"a package the rebuild orphans was not reported: {result}"

        # 2e. A targeted in-place pass over the pre-atlas graph: the order
        #     this repository is actually in. The pass appends the page and
        #     leaves the old sampler, so retiring it is the whole difference
        #     between the atlas saving something and costing a page for free.
        audit = run_audit(before_root)
        material = f"{MATERIAL_ROOT}/M_Print"
        live = {material: "T_Print_D"}
        fixed = simulate_targeted_pass(audit, live, retire=True, entries=entries)
        broken = simulate_targeted_pass(audit, live, retire=False,
                                        entries=entries)
        assert fixed["dropped"] == [print_texture] and not fixed["kept"], \
            f"retiring the sampler did not take the texture out: {fixed}"
        assert not fixed["stale"], \
            "a retired material still names the page and the texture"
        assert broken["kept"] == [print_texture] and not broken["dropped"], \
            f"the un-retired sampler let the texture go anyway: {broken}"
        assert broken["stale"], \
            "a surviving pre-atlas sampler was not seen naming both"
        assert page in broken["after"].cooked, \
            "the page did not enter the cook on a targeted pass"

        # 이미 반입한 그래프에서도 같은 결함을 재현해야 한다.
        migrated = targeted_pre_atlas_graph(fixed["after"], live, entries)
        migrated_fixed = simulate_targeted_pass(migrated, live, True, entries)
        migrated_broken = simulate_targeted_pass(migrated, live, False, entries)
        assert migrated_fixed["dropped"] == [print_texture]
        assert migrated_broken["kept"] == [print_texture]
        assert not migrated_fixed["stale"] and migrated_broken["stale"]

        # 옛 파일을 지운 저장소에서도 노드와 간선을 함께 되돌려 결함을 재현한다.
        cleaned = _replay(fixed["after"], {})
        cleaned.packages.pop(print_texture)
        cleaned.references.pop(print_texture)
        _seed_roots(cleaned)
        _close(cleaned)
        restored = targeted_pre_atlas_graph(cleaned, live, entries)
        cleaned_fixed = simulate_targeted_pass(restored, live, True, entries)
        cleaned_broken = simulate_targeted_pass(restored, live, False, entries)
        assert print_texture not in cleaned.packages and print_texture not in cleaned.references, \
            "재현용 텍스처가 실제 감사 그래프에 추가됐다"
        assert print_texture in restored.cooked, \
            "삭제된 텍스처의 교체 전 샘플러를 재현하지 못했다"
        assert cleaned_fixed["dropped"] == [print_texture] and not cleaned_fixed["kept"], \
            "옛 파일을 지운 뒤 정상 샘플러 해제를 오판했다"
        assert cleaned_broken["kept"] == [print_texture] and cleaned_broken["stale"], \
            "옛 파일을 지운 뒤 샘플러 해제 누락을 놓쳤다"
        assert gate(cleaned_broken["after"], entries=entries)[0] == 1, \
            "삭제된 옛 텍스처를 되살린 샘플러가 검사를 통과했다"

        # 재질 밖의 참조는 그대로 남아야 한다. 파일 부재를 성공으로 간주하면 안 된다.
        held_elsewhere = _replay(cleaned, {})
        held_elsewhere.references["/Game/Meshes/SM_Placed"].add(print_texture)
        held_before = targeted_pre_atlas_graph(held_elsewhere, live, entries)
        held_fixed = simulate_targeted_pass(held_before, live, True, entries)
        assert held_fixed["kept"] == [print_texture] and not held_fixed["dropped"], \
            "다른 패키지가 잡은 옛 텍스처 참조를 재현 과정에서 없앴다"

        # 2e-bis. The same pass, on a texture that has a photo capture. The
        #     material never names the contracted texture at all -- it names
        #     the capture -- so a retirement that only knows the contracted
        #     name is a no-op, and the saving quietly does not happen for
        #     exactly the assets somebody bothered to photograph.
        twin = f"{ATLAS_TEXTURE_ROOT}/T_Photo_Print_D"
        captured = _fixture(
            os.path.join(tmp, "photo_pass"), good_rule, twin)
        with open(os.path.join(
                captured, "Content", "Prototype", "Textures",
                "T_Photo_Print_D.uasset"), "wb") as handle:
            handle.write(b"Texture2D")
        audit = run_audit(captured)
        assert resolved_print_texture(audit, "T_Print_D") == twin, \
            "the capture was not resolved as the texture the material samples"
        live = {material: "T_Print_D"}
        photo_fixed = simulate_targeted_pass(
            audit, live, retire=True, entries=entries)
        name_only = simulate_targeted_pass(
            audit, live, retire=True, entries=entries, photo_aware=False)
        not_retired = simulate_targeted_pass(
            audit, live, retire=False, entries=entries)
        assert photo_fixed["dropped"] == [twin] and not photo_fixed["stale"], \
            f"a photo-aware retirement did not take the capture out: {photo_fixed}"
        assert name_only["kept"] == [twin] and name_only["stale"], \
            ("retiring by the contracted name alone dropped a capture it "
             f"cannot name: {name_only}")
        assert not_retired["kept"] == [twin] and not_retired["stale"], \
            f"the un-retired capture left the cook anyway: {not_retired}"
        # And the reporting layer sees it as a shadow, because a page packed
        # from the procedural source is also the wrong picture.
        assert atlas_photo_shadows(audit, entries) == {"T_Print_D": twin}, \
            "the capture was not reported as shadowing the contracted texture"

        # 2g. A capture pair split between code and the materials. Nothing
        #     fails: both halves are real textures, both load, and the
        #     surface simply has two appearances -- one in the world, one
        #     wherever the HUD draws it.
        split = tamper("photo_split", {
            f"Prototype/Textures/T_Photo_HudFrame_D.uasset": "Texture2D",
            "Prototype/Materials/M_Frame.uasset":
                f"{ATLAS_TEXTURE_ROOT}/T_Photo_HudFrame_D",
        })
        findings = photo_split_loads(split)
        assert len(findings) == 1 and findings[0]["loaded"] == hud, \
            f"a capture pair split across code and materials was missed: {findings}"
        assert findings[0]["drawn"].endswith("T_Photo_HudFrame_D"), \
            "the half the materials actually draw was misidentified"
        code, message = gate(split, entries=entries)
        assert code == 1 and message.startswith("FAIL"), \
            "a split capture pair did not fail the gate"

        # The same pair with both halves agreeing is not a finding, and a
        # path with no capture beside it is just a path.
        agreed = tamper("photo_agreed", {
            f"Prototype/Textures/T_Photo_HudFrame_D.uasset": "Texture2D",
            "Prototype/Materials/M_Frame.uasset": hud,
        })
        assert not photo_split_loads(agreed), \
            "a capture pair both sides agree on was reported as split"
        assert not photo_split_loads(run_audit(before_root)), \
            "a path with no capture twin was reported as split"

        # The unused half is reported, but only as something to look at: in
        # the agreed fixture both sides draw the procedural texture, so it is
        # the capture that nothing reads.
        orphans = orphan_capture_pairs(agreed)
        assert f"{ATLAS_TEXTURE_ROOT}/T_Photo_HudFrame_D" in orphans, \
            "a half nothing references was not reported as unused"
        assert hud not in orphans, \
            "a half both sides draw was called unused"

        # 2h. A capture folder whose import nothing draws. The folder name is
        #     the only thing that says what the scan is, so a mis-file looks
        #     exactly like a surface that exists -- until nothing samples it.
        folders = unused_capture_folders(split)
        by_surface = {item["surface"]: item for item in folders}
        assert "Wallpaper" in by_surface, \
            "a capture folder no material draws was not reported"
        assert by_surface["Wallpaper"]["scans"] == ["Plaster003"], \
            ("the report does not say what is inside the folder: "
             f"{by_surface['Wallpaper']}")
        assert "HudFrame" not in by_surface, \
            "a capture folder a material draws was called unused"

        # 2f. The pass list is read out of the builder, so the shapes it
        #     relies on have to still be there. Anything it cannot read is an
        #     exception, not a silently shorter list.
        parsed = material_passes.material_passes()
        assert any(p.env is None for p in parsed), "the full pass went missing"
        assert any(p.update_in_place and p.atlas_aware for p in parsed), \
            "no in-place atlas-aware pass was found; the parse has drifted"
        for build_pass in parsed:
            assert build_pass.materials, \
                f"{build_pass.name} parsed to no materials at all"

        # 3. The conflict the atlas contract exists to prevent: a texture on
        #    a page that code also loads by path.
        audit = run_audit(after)
        conflicts = atlas_entries_loaded_by_code(audit, ("T_HudFrame_D",))
        assert conflicts and conflicts[0][0] == "T_HudFrame_D", \
            "an atlassed texture loaded by code was not reported"

        # 4. A checkout that did not fetch LFS. The material is a pointer, so
        #    its texture looks unreferenced -- which is a hole in the graph,
        #    not a finding, and the gate has to say so instead of failing.
        blinded = _fixture(
            os.path.join(tmp, "blinded"), good_rule,
            "/Game/Prototype/Textures/T_Print_D")
        material = os.path.join(
            blinded, "Content", "Prototype", "Materials", "M_Print.uasset")
        with open(material, "wb") as handle:
            # Written out rather than built from LFS_POINTER_MAGIC, so that
            # breaking the constant breaks the detection instead of moving
            # the fixture along with it.
            handle.write(
                b"version https://git-lfs.github.com/spec/v1\n"
                b"oid sha256:0000000000000000000000000000000000000000"
                b"000000000000000000000000\nsize 1\n"
            )
        audit = run_audit(blinded)
        assert blind_spots(audit) == {"/Game/Prototype/Materials/M_Print"}, \
            "an unfetched cooked package was not reported as a blind spot"
        assert not atlas_status(audit, entries)["still_cooked"], \
            "the fixture no longer exercises the hole"
        code, message = gate(audit, require_atlas_dropped=True, entries=entries)
        assert code == 0 and message and message.startswith("SKIP"), \
            f"a hole in the graph was treated as a verdict: {code} {message}"

    print(
        f"PASS cook reference self-test: code-only detection, SpecificAssets "
        f"coverage and load-bearing, atlas drop before and after the material "
        f"rebuild, 5 ways the rebuild can save nothing or break something, "
        f"a targeted in-place pass three ways (retired, retired by contracted "
        f"name only against a photo capture, not retired), "
        f"a capture pair split between code and the materials, a capture "
        f"folder whose import nothing draws, "
        f"an absent path told apart from an absent path with a fallback "
        f"(nearest pointer, every site, no pointer at all), "
        f"the code-load conflict, the unfetched-LFS hole, and "
        f"{len(tampered) + 2} ways a cook rule can look right and hold nothing "
        f"(two of them a rule left in DefaultEngine.ini and a Texture2D rule "
        f"without {DETERMINE_ID_SETTING})"
    )
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when any finding is reported")
    parser.add_argument("--json", action="store_true",
                        help="machine-readable")
    parser.add_argument(
        "--require-atlas-dropped", action="store_true",
        help="also fail while a contracted atlas texture is still cooked; "
             "for use after the editor has rebuilt the print materials")
    parser.add_argument("--self-test", action="store_true",
                        help="run the audit against a synthetic project")
    parser.add_argument(
        "--explain-rules", action="store_true",
        help="walk the Asset Manager cook rules asset by asset and report, "
             "for each, whether the rule is what keeps it in the cook")
    parser.add_argument(
        "--simulate-rebuild", action="store_true",
        help="re-run reachability as if the editor had rebuilt the print "
             "materials, and report the whole delta of that rebuild")
    parser.add_argument(
        "--simulate-targeted", action="store_true",
        help="run each in-place IG_*_ONLY pass over the pre-atlas graph, with "
             "and without retiring the sampler it replaced")
    arguments = parser.parse_args(argv)

    if arguments.self_test:
        return command_self_test()

    audit = run_audit()
    if arguments.explain_rules:
        return command_explain_rules(audit)
    if arguments.simulate_rebuild:
        return command_simulate_rebuild(audit)
    if arguments.simulate_targeted:
        return command_simulate_targeted(audit)
    conflicts = atlas_entries_loaded_by_code(audit)
    code_only = code_only_assets(audit)
    status = atlas_status(audit)

    if arguments.json:
        print(json.dumps({
            "packages": len(audit.packages),
            "roots": sorted(audit.roots),
            "cooked": len(audit.cooked),
            "unreadable": sorted(audit.unreadable),
            "atlas_code_load": {s: sites for s, sites in conflicts},
            "code_only": {p: sites for p, sites in code_only},
            "absent": {
                p: {
                    "sites": sites,
                    "guarded": p in audit.guarded_code_paths,
                }
                for p, sites in missing_code_assets(audit)
            },
            "atlas": status,
            "cook_rules": rule_coverage(audit),
            "cook_rule_problems": [
                {"line": rule.line, "type": rule.name, "problem": problem}
                for rule, problem in cook_rule_problems(audit)
            ],
        }, indent=2))
    else:
        _report(audit)

    if not arguments.check:
        return 0

    code, message = gate(audit, arguments.require_atlas_dropped)
    if message:
        print("\n" + message)
    return code


if __name__ == "__main__":
    sys.exit(main())

"""배포 폴더로 공개용 ZIP과 SHA256SUMS를 만든다.

    python Scripts/make_release_zip.py <배포 폴더> <판 번호> [--edition playtest|release]
        [--date YYYY-MM-DD] [--trailer <mp4>]

- ZIP에는 manifest.json의 files 목록만 해시를 다시 확인하며 넣는다. 검사 실행이 배포
  폴더에 남긴 파일(아이콘 PNG, 컨테이너 목록 등)은 들어가지 않는다.
- Windows/ 아래를 ZIP 루트에 두고, Build/Windows/Guides의 안내문 둘(UTF-8 BOM, CRLF)과
  Build/Windows/Kit의 테스트 도구를 「테스트 도구」 폴더로 넣는다.
- 결과는 Saved/Releases/<판 번호>/에 쓴다. 공개는 사람이 따로 승인한다.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIDES = os.path.join(ROOT, "Build", "Windows", "Guides")
KIT = os.path.join(ROOT, "Build", "Windows", "Kit")
KIT_FOLDER = "테스트 도구"
EDITIONS = {
    "playtest": {"EDITION_KO": "플레이 테스트", "EDITION_EN": "playtest", "EDITION_JA": "プレイテスト",
                 "EDITION_ZH_HANS": "试玩版", "EDITION_ZH_HANT": "試玩版"},
    "release": {"EDITION_KO": "", "EDITION_EN": "", "EDITION_JA": "", "EDITION_ZH_HANS": "", "EDITION_ZH_HANT": ""},
}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fill(text, values):
    for key, value in values.items():
        text = text.replace("{" + key + "}", value)
    # 정식판은 판 이름이 비어서 생긴 겹친 빈칸을 정리한다.
    return "\n".join(re.sub(r" {2,}", " ", line) if "Missing Floor" in line else line
                     for line in text.split("\n"))


def guide_bytes(name, values):
    with open(os.path.join(GUIDES, name), encoding="utf-8") as handle:
        text = fill(handle.read().replace("\r\n", "\n"), values)
    assert not re.search(r"\{[A-Z_]+\}", text), name
    return b"\xef\xbb\xbf" + text.replace("\n", "\r\n").encode("utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("archive")
    parser.add_argument("version")
    parser.add_argument("--edition", choices=sorted(EDITIONS), default="playtest")
    parser.add_argument("--date", default=datetime.date.today().isoformat())
    # 예고편은 Build-Trailer.ps1 실행 폴더의 배포용 영상(MissingFloor-Trailer-hq.mp4)을 직접 준다. 예전에는 기본값이
    # 옛 Saved/Trailer의 영상이라, 지정을 빠뜨리면 지난 예고편이 조용히 들어갔다.
    parser.add_argument("--trailer", default="")
    args = parser.parse_args(argv)

    archive = os.path.abspath(args.archive)
    with open(os.path.join(archive, "manifest.json"), encoding="utf-8-sig") as handle:
        manifest = json.load(handle)
    if manifest.get("hasLocalChanges") is not False:
        print("깨끗한 커밋에서 만든 배포 폴더가 아닙니다.")
        return 1
    day = datetime.date.fromisoformat(args.date)
    values = dict(EDITIONS[args.edition])
    values.update({"VERSION": args.version, "DATE_ISO": day.isoformat(), "DATE_DOT": day.strftime("%Y.%m.%d"),
                   "DATE_JA": f"{day.year}年{day.month}月{day.day}日"})
    guides = {"시작하기.txt": guide_bytes("시작하기.txt", values), "README.txt": guide_bytes("README.txt", values)}
    kit = {}
    for name in sorted(os.listdir(KIT)):
        with open(os.path.join(KIT, name), "rb") as handle:
            data = handle.read()
        if name.endswith(".ps1") and not data.startswith(b"\xef\xbb\xbf"):
            data = b"\xef\xbb\xbf" + data
        if name.endswith((".ps1", ".bat")):
            data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        kit[f"{KIT_FOLDER}/{name}"] = data

    out = os.path.join(ROOT, "Saved", "Releases", args.version)
    os.makedirs(out, exist_ok=True)
    zip_name = f"MissingFloor-{args.version}-Windows.zip"
    zip_path = os.path.join(out, zip_name)
    if os.path.exists(zip_path):
        os.remove(zip_path)
    files = manifest["files"]
    stamp = (day.year, day.month, day.day, 12, 0, 0)
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for entry in sorted(files, key=lambda item: item["path"]):
            source = os.path.join(archive, *entry["path"].split("/"))
            if os.path.getsize(source) != entry["bytes"] or sha256(source) != entry["sha256"]:
                print(f"매니페스트와 다른 파일: {entry['path']}")
                return 1
            z.write(source, entry["path"][len("Windows/"):])
        for name, data in list(guides.items()) + list(kit.items()):
            info = zipfile.ZipInfo(name, date_time=stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)

    expected = {entry["path"][len("Windows/"):] for entry in files} | set(guides) | set(kit)
    with zipfile.ZipFile(zip_path) as z:
        if z.testzip() is not None or set(z.namelist()) != expected:
            print("ZIP을 다시 읽은 목록이 예상과 다릅니다.")
            return 1
        for entry in files:
            if hashlib.sha256(z.read(entry["path"][len("Windows/"):])).hexdigest() != entry["sha256"]:
                print(f"ZIP 안의 파일이 원본과 다릅니다: {entry['path']}")
                return 1

    assets = [zip_name]
    if args.trailer:
        if not os.path.isfile(args.trailer):
            print(f"예고편 영상이 없습니다: {args.trailer}")
            return 1
        shutil.copyfile(args.trailer, os.path.join(out, "MissingFloor-Trailer.mp4"))
        assets.append("MissingFloor-Trailer.mp4")
    else:
        print("예고편 없이 묶습니다(--trailer로 실행 폴더의 -hq 영상을 줄 수 있다).")
    sums = [f"{sha256(os.path.join(out, name))}  {name}" for name in assets]
    with open(os.path.join(out, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(sums) + "\n")
    info = {"file": zip_name, "bytes": os.path.getsize(zip_path), "sha256": sums[0].split()[0],
            "commit": manifest["commit"], "gameFiles": len(files), "guides": sorted(guides),
            "kit": sorted(kit), "edition": args.edition, "verifiedZipContents": True}
    with open(os.path.join(out, "package.json"), "w", encoding="utf-8") as handle:
        json.dump(info, handle, ensure_ascii=False, indent=2)
    print(json.dumps(info, ensure_ascii=False))
    print("\n".join(sums))
    return 0


if __name__ == "__main__":
    sys.exit(main())

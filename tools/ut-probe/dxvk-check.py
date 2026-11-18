#!/usr/bin/env python3
"""Check a Vulkan driver against DXVK's required features.

Reads DXVK's own feature table (src/dxvk/dxvk_device_info.cpp: ENABLE_FEATURE,
ENABLE_EXT and ENABLE_EXT_FEATURE entries whose last argument is `true`) and looks each
required entry up in a `vulkaninfo -j` dump of the driver. Runs on the PC.

  python3 dxvk-check.py dxvk_device_info.cpp vulkaninfo.json

A feature counts as supported if any feature struct in the dump has it set to true; an
extension counts if the dump lists it. The minimum API version and push constant size are
checked too (read from the arguments --api and --push, defaults are DXVK 2.7's: 1.3, 256).
"""
import argparse
import json
import re
import sys


def camel_to_ext(member):
    """extRobustness2 -> VK_EXT_robustness2, khrPipelineLibrary -> VK_KHR_pipeline_library."""
    m = re.match(r"(ext|khr|nv|nvx|amd|android|valve|mesa|arm|qcom|intel|google|huawei)(.*)", member)
    if not m:
        return member
    vendor, rest = m.groups()
    snake = re.sub(r"(?<=[a-z])(?=[A-Z0-9])|(?<=[0-9])(?=[A-Z])", "_", rest).lower()
    snake = re.sub(r"_(\d)", r"\1", snake)  # robustness_2 -> robustness2
    return f"VK_{vendor.upper()}_{snake}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("vulkaninfo")
    ap.add_argument("--api", default="1.3")
    ap.add_argument("--push", type=int, default=256)
    a = ap.parse_args()

    src = open(a.source).read()
    dev = json.load(open(a.vulkaninfo))["capabilities"]["device"]
    exts = set(dev["extensions"])
    true_features = {name for struct in dev["features"].values() for name, v in struct.items()
                     if v is True}
    props = dev["properties"]["VkPhysicalDeviceProperties"]
    api = props["apiVersion"]
    api_str = api if isinstance(api, str) else f"{api >> 22}.{(api >> 12) & 0x3ff}.{api & 0xfff}"
    push = props["limits"]["maxPushConstantsSize"]

    missing, ok = [], 0
    for m in re.finditer(r"ENABLE_FEATURE\(\s*(\w+)\s*(?:\.\s*\w+)?\s*,\s*(\w+)\s*,\s*true\s*\)", src):
        if m.group(2) in true_features:
            ok += 1
        else:
            missing.append(f"feature {m.group(1)}.{m.group(2)}")
    for m in re.finditer(r"ENABLE_EXT\(\s*(\w+)\s*,\s*true\s*\)", src):
        ext = camel_to_ext(m.group(1))
        if ext in exts:
            ok += 1
        else:
            missing.append(f"extension {ext}")
    for m in re.finditer(r"ENABLE_EXT_FEATURE\(\s*(\w+)\s*,\s*(\w+)\s*,\s*true\s*\)", src):
        ext = camel_to_ext(m.group(1))
        if ext in exts and m.group(2) in true_features:
            ok += 1
        else:
            missing.append(f"{ext}.{m.group(2)}" + ("" if ext in exts else " (extension missing)"))

    want = tuple(int(x) for x in a.api.split("."))
    have = tuple(int(x) for x in api_str.split(".")[:2])
    print(f"API version: {api_str} (needs {a.api}): {'ok' if have >= want else 'TOO OLD'}")
    print(f"maxPushConstantsSize: {push} (needs {a.push}): {'ok' if push >= a.push else 'TOO SMALL'}")
    print(f"required entries supported: {ok}, missing: {len(missing)}")
    for x in missing:
        print("  MISSING", x)
    return 1 if missing or have < want or push < a.push else 0


if __name__ == "__main__":
    sys.exit(main())

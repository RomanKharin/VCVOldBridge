# parse .cpp and preset json data to create VCV preset .json files


import json
import os
import sys


def main(module_slug: str):
    BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    SRC_PRESET = os.path.join(BASE, "src", "presets", module_slug)
    SRC_CPP = os.path.join(BASE, "src", f"{module_slug}.cpp")
    DEST_PRESET = os.path.join(BASE, "presets", module_slug)

    mode = 0  # 0 - search, 1 - scan
    params = []
    params_ids = {}
    with open(SRC_CPP, "r") as sf:
        for line in sf.readlines():
            if mode == 0:
                if "enum ParamId" in line:
                    mode = 1
                    continue
            else:
                line = line.strip()
                if line.startswith("{"):
                    line = line[1:].strip()
                if not line:
                    continue
                if line == "PARAMS_LEN":
                    break
                param = line
                if param.endswith(","):
                    param = param[:-1].strip()
                params.append(param)
    for idx, param in enumerate(params):
        params_ids[param] = idx

    default_fn = os.path.join(SRC_PRESET, "default.json")
    default_src = {}
    if os.path.exists(default_fn):
        with open(default_fn, "r") as sf:
            default_src = json.load(sf)

    for item in os.listdir(SRC_PRESET):
        if item == "default.json":
            continue
        full_fn = os.path.join(SRC_PRESET, item)
        if not os.path.isfile(full_fn):
            continue
        if item[-5:].lower() != ".json":
            continue
        with open(full_fn, "r") as sf:
            preset_src = json.load(sf)
        name = item[:-5]

        # combine preset data with default data
        preset_params = []
        for param_name in params:
            values = None
            if param_name in preset_src:
                values = preset_src[param_name]
            elif param_name in default_src:
                values = default_src[param_name]

            if values is None:
                continue
            if values.get("type") == "hidden":
                continue

            param_value = values.get("value", 0)
            preset_params.append({"value": param_value, "id": params_ids[param_name]})

        dest_fn = os.path.join(DEST_PRESET, module_slug, f"{name}.vcvm")
        os.makedirs(os.path.join(DEST_PRESET, module_slug), exist_ok=True)
        with open(dest_fn, "w") as df:

            preset_data = {
                "plugin": "OldBridge",
                "model": module_slug,
                "version": "2.0.0",
                "params": preset_params,
            }
            json.dump(preset_data, df, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: create_presets.py <slug_name>", file=sys.stderr)
        sys.exit(1)
    main(sys.argv[1])

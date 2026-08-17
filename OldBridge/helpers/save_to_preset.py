import json
import os

from create_presets import read_params_from_cpp


def main(module_slug: str, save_fn: str):
    BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    SRC_PRESET = os.path.join(BASE, "src", "presets", module_slug)
    SRC_CPP = os.path.join(BASE, "src", f"{module_slug}.cpp")

    _, cpp_params = read_params_from_cpp(SRC_CPP)

    default_fn = os.path.join(SRC_PRESET, "default.json")
    default_src = {}
    if os.path.exists(default_fn):
        with open(default_fn, "r") as sf:
            default_src = json.load(sf)

    with open(save_fn, "r") as sf:
        save_data = json.load(sf)

    preset_data = {}
    for save_param in save_data.get("params", []):
        id_value = save_param.get("id")
        if not id_value:
            print(f"Skip param without id: `{save_param}`")

        # detect name
        if 0 <= id_value < len(cpp_params):
            param_name = cpp_params[id_value]
        else:
            print(f"Param id is out of array: `{id_value}`")
            continue

        default_data = default_src.get(param_name, {})
        if default_data.get("type") == "hidden":
            print("Skip hidden `{param_name}`")
            continue

        print("Saving param `{param_name}` to preset")
        preset_data[param_name] = {"value": save_param.get("value")}

    print()
    print(json.dumps(preset_data, indent=2, ensure_ascii=False))

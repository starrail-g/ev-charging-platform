#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path

REQUIRED_STATES = {"idle", "reserved", "charging", "fault", "offline", "unknown"}
TOKEN_PATTERN = re.compile(r"@([A-Za-z0-9_.]+)")


def _linear(channel: int) -> float:
    value = channel / 255.0
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def _luminance(color: str) -> float:
    value = color.removeprefix("#")
    red, green, blue = (int(value[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * _linear(red) + 0.7152 * _linear(green) + 0.0722 * _linear(blue)


def contrast_ratio(first: str, second: str) -> float:
    high, low = sorted((_luminance(first), _luminance(second)), reverse=True)
    return (high + 0.05) / (low + 0.05)


def load_and_validate(path: Path) -> dict:
    tokens = json.loads(path.read_text(encoding="utf-8"))
    for theme_name in ("day", "night"):
        theme = tokens["themes"][theme_name]
        missing = REQUIRED_STATES - set(theme["states"])
        if missing:
            raise ValueError(f"{theme_name} missing states: {sorted(missing)}")
        for state, color in theme["states"].items():
            ratio = contrast_ratio(color, theme["background"])
            if ratio < 4.5:
                raise ValueError(f"{theme_name}.{state} contrast {ratio:.2f}:1 < 4.5:1")
        topology_ratio = contrast_ratio(theme["topologyLine"], theme["background"])
        if topology_ratio < 3.0:
            raise ValueError(f"{theme_name}.topologyLine contrast {topology_ratio:.2f}:1 < 3:1")
    return tokens


_UNIT_SUFFIX = {"shape": "px", "motion": "ms"}


def _flatten(value: dict, prefix: str = "") -> dict[str, object]:
    output = {}
    for key, child in value.items():
        name = f"{prefix}.{key}" if prefix else key
        if isinstance(child, dict):
            output.update(_flatten(child, name))
        else:
            output[name] = child
            group = name.split(".")[0]
            unit = _UNIT_SUFFIX.get(group)
            if unit and isinstance(child, (int, float)):
                output[f"{name}{unit}"] = f"{child}{unit}"
    return output


def _expand(template: str, flat: dict[str, object]) -> str:
    def replace(match: re.Match) -> str:
        name = match.group(1)
        if name not in flat:
            raise ValueError(f"unknown token @{name}")
        return str(flat[name])

    return TOKEN_PATTERN.sub(replace, template)


def render_outputs(tokens: dict, root: Path) -> dict[Path, str]:
    flat = _flatten(tokens)
    qss_template = root / "apps/admin-client/resources/theme.template.qss"
    css_template = root / "dashboard/css/theme.template.css"
    theme_color_keys = (
        "background",
        "surface",
        "text",
        "mutedText",
        "decorativeStructure",
        "topologyLine",
        "focusBlue",
        "deepBlue",
    )
    header_lines = ["#pragma once", "#include <QColor>", "namespace ev::theme {"]
    for theme_name in ("day", "night"):
        theme = tokens["themes"][theme_name]
        for key in theme_color_keys:
            if key not in theme:
                continue
            symbol = f"k{theme_name.title()}{key[0].upper()}{key[1:]}"
            header_lines.append(f'inline const QColor {symbol}{{"{theme[key]}"}};')
        for state, color in theme["states"].items():
            symbol = f"k{theme_name.title()}{state.title()}"
            header_lines.append(f'inline const QColor {symbol}{{"{color}"}};')
    # motion 组：毫秒时长常量（Qt 端动画周期与令牌同源，如 kMotionAuroraMs{11000}；
    # 禁止在 C++ 里手写与 design-tokens.json 重复的时长字面量）
    for key, value in tokens["motion"].items():
        symbol = f"kMotion{key[0].upper()}{key[1:]}Ms"
        header_lines.append(f"inline constexpr int {symbol}{{{value}}};")
    header_lines.append("} // namespace ev::theme")
    javascript = (
        "export const themeTokens = "
        + json.dumps(tokens, ensure_ascii=False, indent=2)
        + ";\n"
    )
    return {
        root / "apps/admin-client/resources/generated/theme.qss": _expand(
            qss_template.read_text(encoding="utf-8"), flat
        ),
        root / "apps/admin-client/src/theme/generated/theme_tokens.h": "\n".join(header_lines) + "\n",
        root / "dashboard/css/generated/theme.css": _expand(
            css_template.read_text(encoding="utf-8"), flat
        ),
        root / "dashboard/js/generated/theme-tokens.js": javascript,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    tokens = load_and_validate(root / "libs/common/ui/design-tokens.json")
    outputs = render_outputs(tokens, root)
    dirty = []
    for path, content in outputs.items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                dirty.append(path.relative_to(root).as_posix())
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")
    if dirty:
        print("generated UI tokens are stale: " + ", ".join(dirty), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

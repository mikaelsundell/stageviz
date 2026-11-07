#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check the documented CPython contract without importing the embedded module.

This intentionally recognizes this project's explicit registration/argument-parser
patterns. Fail on unrecognized registrations instead of silently skipping them.
It checks surface and call shape, not native runtime semantics or return types.
"""
import ast
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Call:
    minimum: int
    maximum: int
    keywords: tuple


def function_body(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\([^)]*\)\s*\{", source)
    if not match:
        raise ValueError("Cannot find binding function " + name)
    # Preserve offsets while hiding literals and comments from brace matching.
    masked = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
                    lambda m: " " * len(m[0]), source, flags=re.S)
    opening = match.end() - 1
    depth = 1
    for end in range(opening + 1, len(source)):
        depth += (masked[end] == "{") - (masked[end] == "}")
        if depth == 0:
            return source[opening + 1:end]
    raise ValueError("Unclosed binding function " + name)


def source_contract(root):
    calls = {}
    for path in sorted((root / "sources/python").glob("py*.cpp")):
        source = path.read_text()
        for table in re.finditer(r"PyMethodDef\s+(\w+)\[\]\s*=\s*\{(.*?)}\s*;", source, re.S):
            table_name, entries = table.groups()
            owner_match = re.search(r"(\w+)\.tp_methods\s*=\s*" + table_name + r"\s*;", source)
            if owner_match:
                owner = owner_match[1]
                surface_match = re.search(re.escape(owner) + r'\.tp_name\s*=\s*"([^"]+)"', source)
                if not surface_match:
                    raise ValueError("No tp_name for " + table_name)
                surface = surface_match[1]
            elif re.search(r"\.m_methods\s*=\s*" + table_name + r"\s*;", source):
                surface = "stageviz"
            else:
                module = re.search(r'PyModuleDef\s+\w+\s*=\s*\{\s*PyModuleDef_HEAD_INIT,\s*"([^"]+)".*?\b'
                                   + table_name + r"\b", source, re.S)
                if not module:
                    raise ValueError("Unknown method table owner: " + str(path) + ":" + table_name)
                surface = module[1]
            names = re.findall(r'\{\s*"(\w+)"\s*,', entries)
            parsed = list(re.finditer(r'\{\s*"(\w+)"\s*,\s*(?:reinterpret_cast<PyCFunction>\()?'
                                     r'(\w+)\)?\s*,\s*([^,]+),', entries))
            if len(parsed) != len(names):
                raise ValueError("Unrecognized registration in " + table_name)
            for entry in parsed:
                name, function, flags = entry.groups()
                if "METH_NOARGS" in flags:
                    call = Call(0, 0, ())
                else:
                    body = function_body(source, function)
                    parser = re.search(r'PyArg_ParseTuple(?:AndKeywords)?\(\s*args,\s*(?:kwargs,\s*)?"([^"]+)"', body)
                    if not parser:
                        raise ValueError("Unknown argument parser: " + function)
                    fmt = parser[1].split(":")[0]
                    fmt = re.sub(r"\([^()]*\)", "O", fmt)
                    if not re.fullmatch(r"[Oilnspdzb]*(?:\|[Oilnspdzb]+)?", fmt):
                        raise ValueError("Unsupported parse format: " + function + " " + fmt)
                    minimum = len(fmt.split("|")[0])
                    maximum = len(fmt.replace("|", ""))
                    keywords = ()
                    if "METH_KEYWORDS" in flags:
                        keys = re.search(r'(?:keywords|keys)\[\]\s*=\s*\{([^}]+)\}', body)
                        if not keys:
                            raise ValueError("No keyword names for " + function)
                        keywords = tuple(re.findall(r'"(\w+)"', keys[1]))
                        if len(keywords) != maximum:
                            raise ValueError("Keyword/parser mismatch: " + function)
                    call = Call(minimum, maximum, keywords)
                key = surface + "." + name
                if key in calls:
                    raise ValueError("Duplicate public registration: " + key)
                calls[key] = call
    return calls


def documented_contract(reference):
    calls = defaultdict(list)
    surface = None
    # Fenced examples are syntax-checked separately, never treated as API entries.
    reference = re.sub(r"```.*?```", "", reference, flags=re.S)
    for section in re.split(r"(?=^## )", reference, flags=re.M):
        title = section.splitlines()[0] if section.splitlines() else ""
        if title.startswith("## stageviz."):
            surface = title[3:].strip()
        elif title == "## Viewport capture":
            surface = "stageviz.ViewportCapture"
        else:
            surface = None
        for match in re.finditer(r'`([\w.]+)\(([^`]*)\)\s*->\s*([^`]+)`', section):
            name, parameters, _ = match.groups()
            if name.startswith("stageviz."):
                key = name
            elif name.startswith("render."):
                key = "stageviz.Render." + name.split(".", 1)[1]
            elif surface:
                key = surface + "." + name
            else:
                raise ValueError("Unqualified API signature outside a surface: " + name)
            args = ast.parse("def binding(" + parameters.replace("\n", " ") + "): pass").body[0].args
            if args.vararg or args.kwarg or args.kwonlyargs:
                raise ValueError("Unexpected variadic/keyword-only signature: " + key)
            positional = args.posonlyargs + args.args
            calls[key].append((len(positional) - len(args.defaults), len(positional),
                               tuple(arg.arg for arg in positional)))
    return calls


def check(root=ROOT):
    errors = []
    reference = (root / "skills/references/python-api.md").read_text()
    source = source_contract(root)
    documented = documented_contract(reference)
    for key in sorted(source.keys() - documented.keys()):
        errors.append("Undocumented callable: " + key)
    for key in sorted(documented.keys() - source.keys()):
        errors.append("Unregistered documented callable: " + key)
    for key in sorted(source.keys() & documented.keys()):
        call = source[key]
        for minimum, maximum, names in documented[key]:
            if (minimum, maximum) != (call.minimum, call.maximum):
                errors.append(f"Argument count mismatch: {key}: binding {call.minimum}..{call.maximum}, docs {minimum}..{maximum}")
            if call.keywords and names != call.keywords:
                errors.append(f"Keyword names mismatch: {key}: binding {call.keywords}, docs {names}")

    # The keyword table is the explicit allow-list for an otherwise positional API.
    keyword_block = reference.split("| Surface | Callables accepting keywords |", 1)[1].split("\n\n", 1)[0]
    documented_keywords = set()
    owners = {"Module": "stageviz", "Session": "stageviz.Session", "command": "stageviz.command",
              "UI": "stageviz.UI", "Render": "stageviz.Render", "ViewportCapture": "stageviz.ViewportCapture"}
    for row in keyword_block.splitlines():
        cells = row.split("|")
        if len(cells) > 2 and cells[1].strip() in owners:
            owner = owners[cells[1].strip()]
            documented_keywords.update(owner + "." + name for name in re.findall(r'`(\w+)\(', row))
    actual_keywords = {key for key, call in source.items() if call.keywords}
    for key in sorted(actual_keywords ^ documented_keywords):
        errors.append("Keyword support table mismatch: " + key)

    module = (root / "sources/python/pymodule.cpp").read_text()
    constants = set(re.findall(r'PyModule_AddIntConstant\(module, "(\w+)"', module))
    constant_block = reference.split("## Module constants\n", 1)[1].split("\n## ", 1)[0]
    listed_constants = set()
    for row in constant_block.splitlines():
        if row.startswith("| ") and not row.startswith("| Purpose"):
            listed_constants.update(re.findall(r'`(\w+)`', row))
    for name in sorted(constants ^ listed_constants):
        errors.append("Module constant table mismatch: " + name)
    ui = (root / "sources/python/pyui.cpp").read_text()
    for enum in ("ColorRole", "IconRole", "UIScale", "UIState"):
        actual = re.findall(r'\{ "(\w+)", static_cast<long>\(UI::' + enum + r'::', ui)
        block = reference.split("#### " + enum + " members\n", 1)[1].split("\n###", 1)[0]
        listed = re.findall(r'`(\w+)`', block)
        if len(listed) != len(set(listed)):
            errors.append("Duplicate UI enum members: " + enum)
        for name in sorted(set(actual) ^ set(listed)):
            errors.append("UI enum mismatch: " + enum + "." + name)

    documents = list((root / "skills").rglob("*.md"))
    documents += [root / "README.md", root / "CTest/python/README.md"]
    for path in documents:
        text = path.read_text()
        for index, block in enumerate(re.findall(r"```python\n(.*?)```", text, re.S), 1):
            try:
                compile(block, str(path) + ":block" + str(index), "exec")
            except SyntaxError as error:
                errors.append(f"Invalid Python in {path.relative_to(root)} block {index}: {error}")
        # Check skills links; the root/test READMEs also have non-skill assets.
        if not path.is_relative_to(root / "skills"):
            continue
        # Check repository Markdown targets; anchors need separate heading handling.
        for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', text):
            if "://" in target or target.startswith("#"):
                continue
            filename = target.split("#", 1)[0]
            if filename and not (path.parent / filename).exists():
                errors.append(f"Broken documentation link: {path.relative_to(root)} -> {target}")
    return source, constants, errors


def main():
    try:
        calls, constants, errors = check()
    except (ValueError, IndexError, SyntaxError) as error:
        print("Documentation check cannot interpret the contract:", error, file=sys.stderr)
        return 1
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Python documentation: {len(calls)} callables, {len(constants)} module constants, "
          "4 UI enums, keyword support, Python syntax and local links verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

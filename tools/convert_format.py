#!/usr/bin/env python3
"""
Convert literate Lime modules (Markdown + YAML) to directive format.

This script converts .md files with YAML metadata and ```lime code blocks
to .lime files with %directive-based metadata.

Usage:
    convert_format.py <input.md> <output.lime>
    convert_format.py --help
"""

import argparse
import re
import sys


def parse_yaml_simple(lines):
    """Parse a simple YAML-like block into a dict."""
    result = {}
    for line in lines:
        line = line.rstrip()
        if not line or line.lstrip().startswith('#'):
            continue
        m = re.match(r'^(\w[\w_-]*)\s*:\s*(.*)', line)
        if not m:
            continue
        key = m.group(1)
        value = m.group(2).strip()

        # Handle inline list: [a, b, c]
        if value.startswith('[') and value.endswith(']'):
            items = value[1:-1]
            if items.strip():
                result[key] = [s.strip().strip('"').strip("'")
                               for s in items.split(',') if s.strip()]
            else:
                result[key] = []
        elif value.startswith('"') and value.endswith('"'):
            result[key] = value[1:-1]
        elif value.startswith("'") and value.endswith("'"):
            result[key] = value[1:-1]
        else:
            result[key] = value
    return result


def extract_metadata_and_code(filepath):
    """Extract YAML metadata and lime code blocks from Markdown file."""
    with open(filepath, 'r') as f:
        lines = f.readlines()

    metadata_blocks = []
    code_blocks = []

    yaml_fence_re = re.compile(r'^```ya?ml\s*$')
    lime_fence_re = re.compile(r'^```lime(?:\s+(.+))?\s*$')
    close_fence_re = re.compile(r'^```\s*$')

    i = 0
    while i < len(lines):
        line = lines[i]

        # Check for ```yaml block
        if yaml_fence_re.match(line.rstrip()):
            i += 1
            block_lines = []
            while i < len(lines) and not close_fence_re.match(lines[i].rstrip()):
                block_lines.append(lines[i])
                i += 1
            if i < len(lines):
                metadata_blocks.append(block_lines)
            i += 1
            continue

        # Check for ```lime block
        m = lime_fence_re.match(line.rstrip())
        if m:
            label = m.group(1) or ""
            i += 1
            block_lines = []
            while i < len(lines) and not close_fence_re.match(lines[i].rstrip()):
                block_lines.append(lines[i])
                i += 1
            if i < len(lines):
                code_blocks.append((label.strip(), ''.join(block_lines)))
            i += 1
            continue

        i += 1

    if not metadata_blocks:
        raise ValueError(f"{filepath}: no ```yaml metadata block found")

    metadata = parse_yaml_simple(metadata_blocks[0])
    return metadata, code_blocks


def convert_to_directive_format(metadata, code_blocks):
    """Convert metadata and code blocks to directive format."""
    lines = []

    # Add header comment
    name = metadata.get('name', 'unknown')
    title = name.replace('-', ' ').replace('_', ' ').title()
    lines.append(f"/* {title} Module */")
    lines.append("")

    # Add module directives
    if 'name' in metadata:
        # Convert name format: "pg-config" -> "pg_config"
        module_name = metadata['name'].replace('-', '_')
        lines.append(f"%module_name {module_name}")

    if 'version' in metadata:
        lines.append(f'%module_version "{metadata["version"]}"')

    if 'description' in metadata:
        desc = metadata['description']
        lines.append(f'%module_description "{desc}"')

    lines.append("")

    # Add dependencies
    depends = metadata.get('depends', [])
    if depends:
        for dep in depends:
            # Convert dependency format: "pg-tokens" -> "pg_tokens"
            dep_name = dep.replace('-', '_')
            lines.append(f"%require {dep_name}.")
        lines.append("")

    # Add exports (from provides)
    provides = metadata.get('provides', [])
    if provides:
        # Convert provide format: "pg-config" -> "pg_config"
        exports = [p.replace('-', '_') for p in provides]
        lines.append(f"%export {' '.join(exports)}.")
        lines.append("")

    # Add code blocks
    for label, code in code_blocks:
        if label:
            lines.append(f"/* {label} */")
        lines.append(code.rstrip())
        lines.append("")

    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='Convert literate Lime modules to directive format')
    parser.add_argument('input', help='Input .md file')
    parser.add_argument('output', help='Output .lime file')
    args = parser.parse_args()

    try:
        metadata, code_blocks = extract_metadata_and_code(args.input)
        output = convert_to_directive_format(metadata, code_blocks)

        with open(args.output, 'w') as f:
            f.write(output)

        print(f"Converted {args.input} -> {args.output}")
        print(f"  Module: {metadata.get('name', 'unknown')} v{metadata.get('version', '0.0.0')}")
        print(f"  Depends: {', '.join(metadata.get('depends', [])) or '(none)'}")
        print(f"  Exports: {', '.join(metadata.get('provides', [])) or '(none)'}")

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())

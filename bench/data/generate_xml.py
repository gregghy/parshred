#!/usr/bin/env python3
"""Generate benchmark XML data files for parser testing.

Usage:
    python3 generate_xml.py OUTPUT_DIR

The script writes six files into OUTPUT_DIR:
    small.xml   ~1KB   simple nested elements
    medium.xml  ~1MB   many elements with attributes
    large.xml   ~100MB repeated catalog items
    deep.xml    ~100KB deeply nested (200 levels)
    attrs.xml   ~1MB   elements with many attributes
    text.xml    ~1MB   elements with long text content
"""

import os
import sys


def write_xml(path: str, content: str) -> None:
    """Write a string to a file as UTF-8."""
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def make_small(target_bytes: int = 1024) -> str:
    header = '<?xml version="1.0" encoding="UTF-8"?>\n<root>\n'
    footer = '</root>\n'

    group = (
        '  <group id="{id}">\n'
        '    <item>alpha</item>\n'
        '    <item>beta</item>\n'
        '    <item>gamma</item>\n'
        '  </group>\n'
    )

    # Fill most of the target size with identical nested groups.
    group_len = len(group.format(id=0))
    count = max(1, (target_bytes - len(header) - len(footer)) // group_len)
    body = group.format(id=0) * count

    # Pad to reach the desired approximate size.
    padding_needed = target_bytes - len(header) - len(body) - len(footer)
    if padding_needed > 0:
        body += '  <pad>' + "x" * (padding_needed - 14) + '</pad>\n'

    return header + body + footer


def make_medium(target_bytes: int = 1024 * 1024) -> str:
    header = '<?xml version="1.0" encoding="UTF-8"?>\n<catalog>\n'
    footer = '</catalog>\n'

    item = (
        '  <item id="000001" category="books" price="29.99" stock="true" rating="4.5">\n'
        '    <title>Book Title 1</title>\n'
        '    <author>Author Name 1</author>\n'
        '    <description>Description text here</description>\n'
        '  </item>\n'
    )

    count = (target_bytes - len(header) - len(footer)) // len(item)
    body = item * count
    return header + body + footer


def make_large(target_bytes: int = 100 * 1024 * 1024) -> str:
    """Generate the 100MB file by repeating the same catalog item many times."""
    header = '<?xml version="1.0" encoding="UTF-8"?>\n<catalog>\n'
    footer = '</catalog>\n'

    item = (
        '  <item id="000001" category="books" price="29.99" stock="true" rating="4.5">\n'
        '    <title>Book Title 1</title>\n'
        '    <author>Author Name 1</author>\n'
        '    <description>Description text here</description>\n'
        '  </item>\n'
    )

    count = (target_bytes - len(header) - len(footer)) // len(item)
    body = item * count
    return header + body + footer


def make_deep(target_bytes: int = 100 * 1024) -> str:
    header = '<?xml version="1.0" encoding="UTF-8"?>\n'
    levels = 200

    opens = "".join(f"<l{i}>" for i in range(levels))
    closes = "".join(f"</l{i}>" for i in range(levels - 1, -1, -1))

    overhead = len(header) + len(opens) + len(closes) + len("<leaf></leaf>")
    leaf_text_len = max(1, target_bytes - overhead)
    leaf = "leaf" + " word" * ((leaf_text_len - 4) // 5)

    return header + opens + leaf + closes


def make_attrs(target_bytes: int = 1024 * 1024) -> str:
    header = '<?xml version="1.0" encoding="UTF-8"?>\n<data>\n'
    footer = '</data>\n'

    attrs = " ".join(f'a{i}="v{i}"' for i in range(1, 13))
    record = f'  <record {attrs}/>\n'

    count = (target_bytes - len(header) - len(footer)) // len(record)
    body = record * count
    return header + body + footer


def make_text(target_bytes: int = 1024 * 1024) -> str:
    header = '<?xml version="1.0" encoding="UTF-8"?>\n<document>\n'
    footer = '</document>\n'

    text = (
        "Lorem ipsum dolor sit amet consectetur adipiscing elit sed do "
        "eiusmod tempor incididunt ut labore et dolore magna aliqua "
    ) * 10

    paragraph = f'  <paragraph>{text}</paragraph>\n'
    count = (target_bytes - len(header) - len(footer)) // len(paragraph)
    body = paragraph * count
    return header + body + footer


def main() -> None:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} OUTPUT_DIR", file=sys.stderr)
        sys.exit(1)

    output_dir = sys.argv[1]
    os.makedirs(output_dir, exist_ok=True)

    files = [
        ("small.xml", make_small),
        ("medium.xml", make_medium),
        ("large.xml", make_large),
        ("deep.xml", make_deep),
        ("attrs.xml", make_attrs),
        ("text.xml", make_text),
    ]

    for name, maker in files:
        path = os.path.join(output_dir, name)
        print(f"Generating {name} ...", flush=True)
        content = maker()
        write_xml(path, content)
        size = os.path.getsize(path)
        print(f"  wrote {path} ({size} bytes)", flush=True)


if __name__ == "__main__":
    main()

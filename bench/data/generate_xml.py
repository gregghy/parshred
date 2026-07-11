#!/usr/bin/env python3
"""Generate benchmark XML data files of various sizes and shapes."""

import os
import sys
import random
import string

def random_text(length=20):
    return ''.join(random.choices(string.ascii_lowercase + ' ', k=length))

def generate_small(path):
    """~1 KB simple XML."""
    with open(path, 'w') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<catalog>\n')
        for i in range(10):
            f.write(f'  <item id="{i}" name="item{i}">\n')
            f.write(f'    <price>{random.uniform(1, 100):.2f}</price>\n')
            f.write(f'    <description>{random_text(30)}</description>\n')
            f.write(f'  </item>\n')
        f.write('</catalog>\n')

def generate_medium(path):
    """~1 MB mixed content."""
    with open(path, 'w') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<database>\n')
        i = 0
        while f.tell() < 1_000_000:
            f.write(f'  <record id="{i}" type="data" status="active">\n')
            f.write(f'    <name>{random_text(15)}</name>\n')
            f.write(f'    <email>{random_text(8)}@example.com</email>\n')
            f.write(f'    <address>{random_text(40)}</address>\n')
            f.write(f'    <notes><![CDATA[{random_text(60)}]]></notes>\n')
            f.write(f'    <!-- record {i} -->\n')
            f.write(f'  </record>\n')
            i += 1
        f.write('</database>\n')

def generate_large(path):
    """~100 MB repeated records."""
    with open(path, 'w') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<dataset>\n')
        target = 100_000_000
        i = 0
        record = (
            '  <entry id="{i}" category="bench" priority="high">\n'
            '    <title>Benchmark Entry {i}</title>\n'
            '    <value>{val}</value>\n'
            '    <metadata key="source" value="generated"/>\n'
            '  </entry>\n'
        )
        while f.tell() < target:
            f.write(record.format(i=i, val=random.randint(0, 1000000)))
            i += 1
        f.write('</dataset>\n')

def generate_deep(path):
    """Deeply nested XML (1000 levels)."""
    depth = 1000
    with open(path, 'w') as f:
        f.write('<?xml version="1.0"?>\n')
        for i in range(depth):
            f.write('  ' * i + f'<level{i}>\n')
        f.write('  ' * depth + 'deepest\n')
        for i in range(depth - 1, -1, -1):
            f.write('  ' * i + f'</level{i}>\n')

def generate_attrs(path):
    """Attribute-heavy XML (50 attrs per element)."""
    with open(path, 'w') as f:
        f.write('<?xml version="1.0"?>\n')
        f.write('<root>\n')
        for i in range(2000):
            attrs = ' '.join(f'attr{j}="{random_text(10)}"' for j in range(50))
            f.write(f'  <element {attrs}/>\n')
        f.write('</root>\n')

def generate_text(path):
    """Text-heavy XML (large CDATA sections)."""
    with open(path, 'w') as f:
        f.write('<?xml version="1.0"?>\n')
        f.write('<documents>\n')
        for i in range(100):
            text = random_text(5000)
            f.write(f'  <doc id="{i}">\n')
            f.write(f'    <![CDATA[{text}]]>\n')
            f.write(f'  </doc>\n')
        f.write('</documents>\n')

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(outdir, exist_ok=True)

    print("Generating benchmark XML files...")
    generate_small(os.path.join(outdir, 'small.xml'))
    print("  small.xml done")
    generate_medium(os.path.join(outdir, 'medium.xml'))
    print("  medium.xml done")
    generate_large(os.path.join(outdir, 'large.xml'))
    print("  large.xml done")
    generate_deep(os.path.join(outdir, 'deep.xml'))
    print("  deep.xml done")
    generate_attrs(os.path.join(outdir, 'attrs.xml'))
    print("  attrs.xml done")
    generate_text(os.path.join(outdir, 'text.xml'))
    print("  text.xml done")

if __name__ == '__main__':
    main()

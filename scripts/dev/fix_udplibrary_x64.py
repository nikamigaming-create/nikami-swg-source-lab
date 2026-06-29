"""Fix hardcoded win32 output paths in the x64 IDG for udplibrary."""
import re
from pathlib import Path

p = Path(__file__).resolve().parents[2] / 'src/external/3rd/library/udplibrary/udplibrary.vcxproj'
xml = p.read_text(encoding='utf-8', errors='replace')

# Find x64 IDG
marker = "'$(Configuration)|$(Platform)'=='Release|x64'"
idx = xml.find(marker)
if idx < 0:
    print('x64 IDG not found')
    exit(1)

idg_start = xml.rfind('<ItemDefinitionGroup', 0, idx+1)
idg_end = xml.find('</ItemDefinitionGroup>', idg_start) + len('</ItemDefinitionGroup>')
x64_idg = xml[idg_start:idg_end]

# Replace win32 paths with x64
x64_fixed = x64_idg.replace(
    r'.\..\..\..\..\compile\win32\udplibrary\Release',
    r'.\..\..\..\..\compile\x64\udplibrary\Release'
)

new_xml = xml[:idg_start] + x64_fixed + xml[idg_end:]
p.write_text(new_xml, encoding='utf-8')
print('PATCHED udplibrary x64 output path')

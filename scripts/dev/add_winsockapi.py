import re
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

files = [
    str(ROOT / 'src/engine/shared/library/sharedNetwork/build/win32/sharedNetwork.vcxproj'),
    str(ROOT / 'src/external/3rd/library/udplibrary/udplibrary.vcxproj'),
]

for path in files:
    xml = open(path).read()
    # Find the Release|x64 ItemDefinitionGroup and add _WINSOCKAPI_ to PreprocessorDefinitions
    pattern = r"(<ItemDefinitionGroup Condition=\"'[^']*'=='Release\|x64'\"[^>]*>.*?<PreprocessorDefinitions>)"
    def add_winsock(m):
        return m.group(0).replace('<PreprocessorDefinitions>', '<PreprocessorDefinitions>_WINSOCKAPI_;')
    new_xml = re.sub(pattern, add_winsock, xml, count=1, flags=re.DOTALL)
    if new_xml != xml:
        open(path, 'w').write(new_xml)
        print(f'PATCHED {os.path.basename(path)}')
    else:
        print(f'NO CHANGE {os.path.basename(path)}')

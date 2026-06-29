"""
For vcxproj files that share Release|Win32 ItemDefinitionGroup with Release|x64,
find all ClCompile items that have:
  <PrecompiledHeader Condition="...Release|Win32...">Create</PrecompiledHeader>
and add matching Release|x64 condition unless already present.
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / 'src'

def process_file(path):
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        xml = f.read()
    
    if 'Release|x64' not in xml:
        return False
    
    # Find all PrecompiledHeader Create conditions for Release|Win32 in ClCompile items
    pattern = r"(<PrecompiledHeader Condition=\"'[^']*'=='Release\|Win32'\">Create</PrecompiledHeader>)"
    matches = list(re.finditer(pattern, xml))
    if not matches:
        return False
    
    dirty = False
    # Process in reverse to preserve positions
    for m in reversed(matches):
        original = m.group(0)
        x64_check = original.replace("Release|Win32", "Release|x64")
        # Only add if x64 version not already present after this match
        end_pos = m.end()
        # Look forward up to 200 chars to see if x64 version exists
        lookahead = xml[end_pos:end_pos+300]
        if "Release|x64" not in lookahead or "Create</PrecompiledHeader>" not in lookahead:
            xml = xml[:end_pos] + '\r\n      ' + x64_check + xml[end_pos:]
            dirty = True
    
    if dirty:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(xml)
    return dirty

libs = [
    'clientGraphics','clientObject','clientParticle','clientSkeletalAnimation',
    'clientTerrain','clientTextureRenderer','clientUserInterface','fileInterface',
    'lcdui','localization','localizationArchive','sharedCollision',
    'sharedCommandParser','sharedCompression','sharedDebug','sharedFile',
    'sharedFoundation','sharedFractal','sharedGame','sharedImage','sharedInputMap',
    'sharedIoWin','sharedLog','sharedMath','sharedMemoryManager',
    'sharedMessageDispatch','sharedNetwork','sharedNetworkMessages','sharedObject',
    'sharedPathfinding','sharedRandom','sharedRegex','sharedSkillSystem',
    'sharedStatusWindow','sharedSwitcher','sharedSynchronization','sharedTerrain',
    'sharedThread','sharedUtility','sharedXml','swgClientUserInterface',
    'swgSharedNetworkMessages','swgSharedUtility','udplibrary','unicode',
    'unicodeArchive','zlib',
]

count = 0
for lib in libs:
    for dirpath, _, filenames in os.walk(ROOT):
        for fn in filenames:
            if fn == lib + '.vcxproj':
                path = os.path.join(dirpath, fn)
                if process_file(path):
                    count += 1
                    print(f'PATCHED {lib}')
                else:
                    print(f'OK {lib}')
                break

print(f'\nTotal patched: {count}')

"""
For each library vcxproj:
1. Find the Release|Win32 ItemDefinitionGroup
2. Copy it as a new x64 ItemDefinitionGroup
3. Remove _USE_32BIT_TIME_T from the preprocessor defs in x64 copy
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / 'src'

LIBS = [
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

def find_idg_end(xml, start):
    """Find the closing </ItemDefinitionGroup> tag after start."""
    depth = 1
    i = start
    while i < len(xml) and depth > 0:
        open_idx = xml.find('<ItemDefinitionGroup', i)
        close_idx = xml.find('</ItemDefinitionGroup>', i)
        if open_idx < 0:
            open_idx = len(xml)
        if close_idx < 0:
            break
        if open_idx < close_idx:
            depth += 1
            i = open_idx + 1
        else:
            depth -= 1
            i = close_idx + len('</ItemDefinitionGroup>')
    return i

def process(path):
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        xml = f.read()
    
    # Skip if already has a proper x64 IDG
    if 'ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'Release|x64\'"' in xml:
        print(f'SKIP(has_x64_idg) {os.path.basename(path)[:-7]}')
        return False
    
    # Find the Release|Win32 IDG
    marker = 'ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'Release|Win32\'">'
    idx = xml.find(marker)
    if idx < 0:
        # Try with Or condition (from our previous batch)
        marker2 = "ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|Win32' Or '$(Configuration)|$(Platform)'=='Release|x64'\">"
        idx = xml.find(marker2)
        if idx < 0:
            print(f'NO_IDG {os.path.basename(path)[:-7]}')
            return False
    
    end = find_idg_end(xml, idx + len(marker))
    win32_idg = xml[idx:end]
    
    # Create x64 version
    # Replace the condition
    x64_idg = win32_idg.replace(
        'Condition="\'$(Configuration)|$(Platform)\'==\'Release|Win32\'"',
        'Condition="\'$(Configuration)|$(Platform)\'==\'Release|x64\'"'
    )
    # Also handle Or condition variants
    x64_idg = x64_idg.replace(
        "Condition=\"'$(Configuration)|$(Platform)'=='Release|Win32' Or '$(Configuration)|$(Platform)'=='Release|x64'\"",
        "Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\""
    )
    # Remove _USE_32BIT_TIME_T from preprocessor
    x64_idg = x64_idg.replace(';_USE_32BIT_TIME_T=1', '')
    x64_idg = x64_idg.replace('_USE_32BIT_TIME_T=1;', '')
    
    # Insert x64 IDG after Win32 IDG
    xml = xml[:end] + '\r\n  ' + x64_idg + xml[end:]
    
    # Also revert the Or condition if present (clean up)
    xml = xml.replace(
        "ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|Win32' Or '$(Configuration)|$(Platform)'=='Release|x64'\">",
        "ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|Win32'\">"
    )
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(xml)
    return True

count = 0
for lib in LIBS:
    for dirpath, _, filenames in os.walk(ROOT):
        for fn in filenames:
            if fn == lib + '.vcxproj':
                path = os.path.join(dirpath, fn)
                if process(path):
                    count += 1
                    print(f'PATCHED {lib}')
                break

print(f'\nTotal patched: {count}')

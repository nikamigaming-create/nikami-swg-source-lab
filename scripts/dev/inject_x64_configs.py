"""
Inject Release|x64 project configuration into vcxproj files that already
have the Release|x64 ProjectConfiguration entry but are missing the
Label="Configuration" PropertyGroup and Label="PropertySheets" ImportGroup.
"""
import os
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

CONFIG_BLOCK = (
    '  <PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'Release|x64\'" Label="Configuration">\r\n'
    '    <ConfigurationType>StaticLibrary</ConfigurationType>\r\n'
    '    <PlatformToolset>v120</PlatformToolset>\r\n'
    '    <UseOfMfc>false</UseOfMfc>\r\n'
    '    <CharacterSet>MultiByte</CharacterSet>\r\n'
    '  </PropertyGroup>'
)

IMPORT_BLOCK = (
    '  <ImportGroup Condition="\'$(Configuration)|$(Platform)\'==\'Release|x64\'" Label="PropertySheets">\r\n'
    '    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props"'
    ' Condition="exists(\'$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\')"'
    ' Label="LocalAppDataPlatform" />\r\n'
    '  </ImportGroup>'
)

WIN32_CFG_ANCHOR = 'Release|Win32" Label="Configuration">'
WIN32_IMP_ANCHOR = 'Release|Win32" Label="PropertySheets">'
X64_CFG_CHECK = "Release|x64'\" Label=\"Configuration\""
X64_IMP_CHECK = "Release|x64'\" Label=\"PropertySheets\""

count = 0
for lib in LIBS:
    found = None
    for dirpath, dirnames, filenames in os.walk(ROOT):
        for fn in filenames:
            if fn == lib + '.vcxproj':
                found = os.path.join(dirpath, fn)
                break
        if found:
            break
    if not found:
        print(f'NOTFOUND {lib}')
        continue

    with open(found, 'r', encoding='utf-8-sig', errors='replace') as fh:
        xml = fh.read()

    if 'Release|x64' not in xml:
        print(f'NO_X64 {lib}')
        continue

    dirty = False

    # Add Label=Configuration PropertyGroup after the last Win32 one
    if 'Release|x64" Label="Configuration"' not in xml and "Release|x64'\" Label=\"Configuration\"" not in xml:
        # Find the last Label=Configuration block (Debug, Optimized, or Release Win32) and insert after it
        last_end = -1
        search_start = 0
        while True:
            idx = xml.find('Label="Configuration">', search_start)
            if idx < 0:
                break
            end = xml.find('</PropertyGroup>', idx)
            if end >= 0:
                last_end = end + len('</PropertyGroup>')
                search_start = last_end
        if last_end >= 0:
            xml = xml[:last_end] + '\r\n' + CONFIG_BLOCK + xml[last_end:]
            dirty = True

    # Add ImportGroup
    if X64_IMP_CHECK not in xml:
        idx = xml.find(WIN32_IMP_ANCHOR)
        if idx >= 0:
            end = xml.find('</ImportGroup>', idx)
            if end >= 0:
                insert_pos = end + len('</ImportGroup>')
                xml = xml[:insert_pos] + '\r\n' + IMPORT_BLOCK + xml[insert_pos:]
                dirty = True

    if dirty:
        with open(found, 'w', encoding='utf-8') as fh:
            fh.write(xml)
        count += 1
        print(f'PATCHED {lib}')
    else:
        print(f'OK {lib}')

print(f'\nTotal patched: {count}')

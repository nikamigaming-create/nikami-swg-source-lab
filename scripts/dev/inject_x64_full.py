"""
Properly inject x64 support into each vcxproj using xml.etree.ElementTree.
For each vcxproj:
1. Add Release|x64 ProjectConfiguration
2. Copy Release|Win32 Label=Configuration PropertyGroup -> Release|x64 + fix toolset
3. Add Release|x64 ImportGroup for PropertySheets
4. Add Release|x64 PropertyGroup with OutDir/IntDir
5. Copy Release|Win32 ItemDefinitionGroup -> Release|x64, stripping 32-bit time_t
6. Add Create PrecompiledHeader condition to First*.cpp
"""
import os
import re
import copy
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

# Extra libs beyond the original 47
LIBS.extend(['crypto', 'ui', 'sharedMathArchive', 'sharedMemoryBlockManager', 'fileInterface', 'localizationArchive', 'unicodeArchive'])

NS = 'http://schemas.microsoft.com/developer/msbuild/2003'

def cond(platform, config='Release'):
    return f"'$(Configuration)|$(Platform)'=='{config}|{platform}'"

def nsq(tag):
    return f'{{{NS}}}{tag}'

def process(path):
    # Parse preserving namespaces
    import xml.etree.ElementTree as ET
    ET.register_namespace('', NS)
    
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        content = f.read()
    
    # Work with raw text for structural injection to preserve formatting
    # Parse just for validation
    try:
        ET.fromstring(content)
    except ET.ParseError as e:
        print(f'BADXML {os.path.basename(path)}: {e}')
        return False
    
    # Check if already has x64 IDG specifically
    x64_idg_marker = "<ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">"
    if x64_idg_marker in content:
        print(f'SKIP(already) {os.path.basename(path)[:-7]}')
        return False
    
    dirty = False
    
    # 1. Insert x64 ProjectConfiguration before Release|Win32 ProjectConfiguration
    old_pc = '    <ProjectConfiguration Include="Release|Win32">'
    new_pc = ('    <ProjectConfiguration Include="Release|x64">\r\n'
              '      <Configuration>Release</Configuration>\r\n'
              '      <Platform>x64</Platform>\r\n'
              '    </ProjectConfiguration>\r\n' + old_pc)
    if old_pc in content:
        content = content.replace(old_pc, new_pc, 1)
        dirty = True
    
    # 2. Insert Label=Configuration PropertyGroup for x64 after Release|Win32 one
    # Find the Release|Win32 configuration block end
    win32_cfg_marker = "'Release|Win32'\" Label=\"Configuration\">"
    if win32_cfg_marker in content:
        idx = content.find(win32_cfg_marker) 
        end_tag = '</PropertyGroup>'
        end_pos = content.find(end_tag, idx) + len(end_tag)
        # Extract the block and duplicate for x64
        start_pos = content.rfind('<PropertyGroup', 0, idx)
        win32_cfg_block = content[start_pos:end_pos]
        x64_cfg_block = win32_cfg_block.replace("'Release|Win32'", "'Release|x64'")
        # Ensure PlatformToolset=v120
        if '<PlatformToolset>' not in x64_cfg_block:
            x64_cfg_block = x64_cfg_block.replace('</PropertyGroup>',
                '    <PlatformToolset>v120</PlatformToolset>\r\n  </PropertyGroup>')
        content = content[:end_pos] + '\r\n  ' + x64_cfg_block + content[end_pos:]
        dirty = True
    
    # 3. Insert ImportGroup for x64 PropertySheets
    win32_imp_marker = "'Release|Win32'\" Label=\"PropertySheets\">"
    if win32_imp_marker in content:
        idx = content.find(win32_imp_marker)
        end_tag = '</ImportGroup>'
        end_pos = content.find(end_tag, idx) + len(end_tag)
        start_pos = content.rfind('<ImportGroup', 0, idx)
        win32_imp_block = content[start_pos:end_pos]
        x64_imp_block = win32_imp_block.replace("'Release|Win32'", "'Release|x64'")
        content = content[:end_pos] + '\r\n  ' + x64_imp_block + content[end_pos:]
        dirty = True
    
    # 4. Insert OutDir/IntDir PropertyGroup for x64 after Release|Win32 OutDir block
    # Win32 OutDir block looks like: <PropertyGroup Condition="...Release|Win32...">\n    <OutDir>
    win32_outdir_marker = "'Release|Win32'\">\r\n    <OutDir>"
    if win32_outdir_marker not in content:
        win32_outdir_marker = "'Release|Win32'\">\n    <OutDir>"
    if win32_outdir_marker in content:
        idx = content.find(win32_outdir_marker)
        end_tag = '</PropertyGroup>'
        end_pos = content.find(end_tag, idx) + len(end_tag)
        start_pos = content.rfind('<PropertyGroup', 0, idx)
        win32_out_block = content[start_pos:end_pos]
        # Build x64 OutDir block (replace win32 path with x64)
        x64_out_block = win32_out_block.replace("'Release|Win32'", "'Release|x64'")
        x64_out_block = re.sub(r'compile\\win32\\', r'compile\\x64\\', x64_out_block)
        x64_out_block = re.sub(r'compile/win32/', r'compile/x64/', x64_out_block)
        # Also strip unnecessary properties from outdir block keeping only OutDir/IntDir
        # (keep it simple - just change platform path)
        content = content[:end_pos] + '\r\n  ' + x64_out_block + content[end_pos:]
        dirty = True
    
    # 5. Insert x64 ItemDefinitionGroup copied from Release|Win32
    win32_idg_search = '<ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'Release|Win32\'">'
    if win32_idg_search in content:
        idx = content.find(win32_idg_search)
        # Find the matching </ItemDefinitionGroup>
        depth = 0
        i = idx
        while i < len(content):
            open_idx = content.find('<ItemDefinitionGroup', i+1)
            close_idx = content.find('</ItemDefinitionGroup>', i+1)
            if close_idx < 0:
                break
            if open_idx > 0 and open_idx < close_idx:
                depth += 1
                i = open_idx
            else:
                if depth > 0:
                    depth -= 1
                    i = close_idx
                else:
                    end_pos = close_idx + len('</ItemDefinitionGroup>')
                    break
        
        win32_idg = content[idx:end_pos]
        x64_idg = win32_idg.replace("=='Release|Win32'", "=='Release|x64'")
        # Remove 32-bit time_t
        x64_idg = x64_idg.replace(';_USE_32BIT_TIME_T=1', '')
        x64_idg = x64_idg.replace('_USE_32BIT_TIME_T=1;', '')
        content = content[:end_pos] + '\r\n  ' + x64_idg + content[end_pos:]
        dirty = True
    
    # 6. Add Create PrecompiledHeader for Release|x64 to First*.cpp ClCompile entries
    # Patterns like: <PrecompiledHeader Condition="'$(Configuration)|$(Platform)'=='Release|Win32'">Create</PrecompiledHeader>
    # Add matching x64 right after
    pch_pattern = re.compile(
        r"(<PrecompiledHeader Condition=\"'\$\(Configuration\)\|\$\(Platform\)'=='Release\|Win32'\">Create</PrecompiledHeader>)"
    )
    def add_x64_pch(m):
        original = m.group(0)
        x64_version = original.replace("Release|Win32", "Release|x64")
        return original + '\r\n      ' + x64_version
    new_content = pch_pattern.sub(add_x64_pch, content)
    if new_content != content:
        content = new_content
        dirty = True
    
    if not dirty:
        print(f'NOOP {os.path.basename(path)[:-7]}')
        return False
    
    # Validate before writing
    try:
        import xml.etree.ElementTree as ET
        ET.fromstring(content)
    except ET.ParseError as e:
        print(f'INVALID_XML {os.path.basename(path)[:-7]}: {e}')
        return False
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
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

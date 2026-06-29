"""Fix winsock include guards in sharedNetwork source files."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / 'src/engine/shared/library/sharedNetwork/src/win32'

OLD_GUARD = '#if !defined(_WIN64) && !defined(_WINSOCK2API_)\r\n#include <winsock.h>\r\n#else\r\n#include <winsock2.h>\r\n#include <ws2tcpip.h>\r\n#endif'
OLD_GUARD_UNIX = '#if !defined(_WIN64) && !defined(_WINSOCK2API_)\n#include <winsock.h>\n#else\n#include <winsock2.h>\n#include <ws2tcpip.h>\n#endif'
NEW_GUARD = '#ifndef _WINSOCK2API_\r\n#ifdef _WIN64\r\n#include <winsock2.h>\r\n#include <ws2tcpip.h>\r\n#else\r\n#include <winsock.h>\r\n#endif\r\n#endif'

for fn in os.listdir(ROOT):
    if not fn.endswith('.cpp') and not fn.endswith('.h'):
        continue
    path = os.path.join(str(ROOT), fn)
    xml = open(path, 'rb').read().decode('utf-8', errors='replace')
    changed = False
    if OLD_GUARD in xml:
        xml = xml.replace(OLD_GUARD, NEW_GUARD)
        changed = True
    elif OLD_GUARD_UNIX in xml:
        xml = xml.replace(OLD_GUARD_UNIX, NEW_GUARD)
        changed = True
    if changed:
        open(path, 'w', encoding='utf-8').write(xml)
        print(f'PATCHED {fn}')
    else:
        print(f'OK {fn}')

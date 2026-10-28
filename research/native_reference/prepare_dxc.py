"""Extract a checksum-pinned Linux DXC locally; never install or change PATH."""
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

HERE = Path(__file__).resolve().parent
URL = 'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz'
EXPECTED = '96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1'


def main():
    target = HERE / 'tools/dxc-1.9.2609'
    target.mkdir(parents=True, exist_ok=False)
    with urllib.request.urlopen(URL, timeout=90) as response:
        data = response.read(20 * 1024 * 1024)
    if len(data) != 13297763 or hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('DXC release archive size/hash mismatch')
    archive = target / 'release.tar.gz'
    archive.write_bytes(data)
    with tarfile.open(archive) as tar:
        tar.extractall(target / 'root', filter='data')
    files = {str(p.relative_to(target)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in (target / 'root').rglob('*') if p.is_file()}
    (target / 'manifest.json').write_text(json.dumps({'url': URL, 'archive_sha256': EXPECTED,
                                                    'digest_source': 'GitHub release asset API digest over HTTPS',
                                                    'files': files}, indent=2) + '\n')
    print('Verified/extracted private DXC:', target)


if __name__ == '__main__':
    main()

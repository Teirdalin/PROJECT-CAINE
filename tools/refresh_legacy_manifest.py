#!/usr/bin/env python3
"""Refresh CAINE's public legacy metadata; never download mod payloads."""
import argparse
import concurrent.futures
import datetime as dt
import json
import re
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

PATTERNS = {
    'up': r'^VTMBup\s*(\d+\.\d+(?:\.\d+)?)$',
    'antitribu': r'^Antitribu\s+(\d+\.\d+(?:\.\d+)?)$',
    'tfn': r'(?:Final Nights|TFN)\s*(\d+\.\d+(?:\.\d+)?)',
    'rtx': r'RTX.*?(\d+\.\d+\.\d+)',
    'cqm': r'^Clan Quest Mod\s*(\d+\.\d+(?:\.\d+)?)\s+Full Installer',
    'ce': r'Camarilla Edition\s*(\d+\.\d+(?:\.\d+)?)',
    'extreme': r'^BLOODLINES EXTREME\s*(\d+\.\d+(?:\.\d+)?)',
}

def parse_release(mod, body):
    if len(body) > 2 * 1024 * 1024 or b'<!DOCTYPE' in body.upper() or b'<!ENTITY' in body.upper():
        raise ValueError('Unsupported feed document')
    root = ET.fromstring(body)
    releases = []
    for item in root.findall('./channel/item'):
        match = re.search(PATTERNS[mod['id']], item.findtext('title', ''), re.I)
        if not match:
            continue
        url = item.findtext('link', '')
        parsed, project = urllib.parse.urlsplit(url), urllib.parse.urlsplit(mod['moddb'])
        official = parsed.path.startswith(project.path + '/')
        renderer = mod['id'] == 'rtx' and parsed.path.startswith('/downloads/vampire-the-masquerade-bloodlines-rtx-remaster-')
        if parsed.scheme != 'https' or parsed.netloc != project.netloc or not (official or renderer):
            raise ValueError('Feed contains an untrusted release link')
        version = match.group(1)
        releases.append((tuple(map(int, version.split('.'))), version, url, item.findtext('pubDate', '')))
    if not releases:
        raise ValueError('No full release found')
    _, version, url, published = max(releases)
    return version, url, published

def refresh(mod):
    row = dict(mod)
    request = urllib.request.Request(mod['feed'], headers={'User-Agent': 'PROJECT-CAINE-Metadata/1'})
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            final = urllib.parse.urlsplit(response.geturl())
            original = urllib.parse.urlsplit(mod['feed'])
            if final.scheme != 'https' or final.netloc != original.netloc:
                raise ValueError('Unexpected feed redirect')
            body = response.read(2 * 1024 * 1024 + 1)
        version, url, published = parse_release(mod, body)
        # A failed parser cannot invent a release, and older feeds cannot
        # silently replace a newer maintainer-verified full release.
        old = tuple(map(int, row['releaseVersion'].split('.'))) if row['releaseVersion'] else ()
        if tuple(map(int, version.split('.'))) >= old:
            row.update(releaseVersion=version, url=url, releaseDate=published)
        row['releaseChecked'] = dt.datetime.now(dt.timezone.utc).isoformat()
        row.pop('sourceError', None)
    except (OSError, ValueError, ET.ParseError, urllib.error.URLError):
        row['sourceError'] = 'Official source check unavailable; last known release retained.'
    return row

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--catalog', type=Path, default=Path(__file__).resolve().parents[1] / 'scripts/legacy-catalog.json')
    parser.add_argument('--offline-check', action='store_true')
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding='utf-8-sig'))
    assert catalog['schema'] == 1 and len(catalog['mods']) <= 16
    for mod in catalog['mods']:
        assert mod['id'] in PATTERNS and mod['feed'].startswith('https://rss.moddb.com/')
    if args.offline_check:
        print(f"CAINE metadata catalog OK: {len(catalog['mods'])} official sources, no payloads")
        return
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        catalog['mods'] = list(pool.map(refresh, catalog['mods']))
    temporary = args.catalog.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(catalog, indent=2) + '\n', encoding='utf-8')
    temporary.replace(args.catalog)
    verified = sum('sourceError' not in m for m in catalog['mods'])
    print(f"Legacy metadata refreshed: {verified}/{len(catalog['mods'])} sources verified; failures retain prior versions")

if __name__ == '__main__':
    main()
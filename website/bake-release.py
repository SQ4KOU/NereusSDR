#!/usr/bin/env python3
"""Write a published release's file sizes and date into the home page.

The home page names the release's files ahead of publication, but their sizes
and the release date are only known once GitHub has the release. Run this
after the release is published and before deploying:

    website/bake-release.py v2026.10.1

It reads the release with `gh release view`, refuses a draft or prerelease,
and fails without writing anything if the page links a file the release does
not have. --release-json reads a saved `gh release view --json
publishedAt,isDraft,isPrerelease,assets` answer instead, for testing.
"""
import argparse, datetime, json, re, subprocess, sys
from pathlib import Path

REPO = 'boydsoftprez/NereusSDR'
PAGE = Path(__file__).resolve().parent / 'public/index.html'

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument('tag', help='release tag, for example v2026.10.1')
parser.add_argument('--page', type=Path, default=PAGE)
parser.add_argument('--release-json', type=Path)
args = parser.parse_args()

if not re.fullmatch(r'v\d+\.\d+\.\d+', args.tag):
    sys.exit(f'Not a release tag: {args.tag}')
if args.release_json:
    release = json.loads(args.release_json.read_text())
else:
    answer = subprocess.run(
        ['gh', 'release', 'view', args.tag, '--repo', REPO,
         '--json', 'publishedAt,isDraft,isPrerelease,assets'],
        capture_output=True, text=True)
    if answer.returncode != 0:
        sys.exit(f'gh could not read {args.tag}: {answer.stderr.strip()}')
    release = json.loads(answer.stdout)
if release.get('isDraft') or release.get('isPrerelease') or not release.get('publishedAt'):
    sys.exit(f'{args.tag} is not a published release yet')
sizes = {a['name']: a['size'] for a in release['assets']}
published = datetime.datetime.fromisoformat(release['publishedAt'].replace('Z', '+00:00'))
stamp = published.astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')

page = args.page.read_text()
prefix = f'https://github.com/{REPO}/releases/download/{args.tag}/'
linked = re.findall(re.escape(prefix) + r'([^"]+)"', page)
if not linked:
    sys.exit(f'The page links no {args.tag} files')
missing = sorted(set(name for name in linked if name not in sizes))
if missing:
    sys.exit(f'{args.tag} has no ' + ', '.join(missing))


def size(match):
    name = match.group(2)
    return f'{match.group(1)}{sizes[name] / 1e6:.1f} MB{match.group(3)}'


page, filled = re.subn(
    r'(<a data-asset="[^"]+" href="' + re.escape(prefix) + r'([^"]+)">(?:(?!</a>).)*?<span class="dl__size">)[^<]*(</span>)',
    size, page, flags=re.S)
long_date = f'{published.day} {published:%B %Y}'
page, dated = re.subn(r'(<time data-release-date datetime=")[^"]*(">)[^<]*(</time>)',
                      lambda m: f'{m.group(1)}{stamp}{m.group(2)}{long_date}{m.group(3)}', page)
page, listed = re.subn(r'(<time data-release-history datetime=")[^"]*(">)[^<]*(</time>)',
                       lambda m: f'{m.group(1)}{stamp}{m.group(2)}{published.day} {published:%b %Y}{m.group(3)}', page)
if not (filled and dated and listed):
    sys.exit(f'Found {filled} sizes, {dated} release dates and {listed} history dates; expected all three')
args.page.write_text(page)
print(f'{args.tag}: {filled} file sizes, {dated} release dates and {listed} history date, '
      f'published {stamp}; {len(set(linked))} linked files all present')

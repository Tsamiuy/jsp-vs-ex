#!/usr/bin/env python3
"""Plan a release tag, or reserve it with --execute; never move an existing tag.

A same-commit tag with no release resumes a failed build. A draft or published
release is left alone. Intended for the contents:write release workflow token
(which can see drafts); --execute is never needed for a read-only inspection.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path


def api(endpoint, *, pages=False, fields=None):
    cmd = ['gh', 'api', '--method', 'POST' if fields else 'GET', endpoint]
    if pages:
        cmd += ['--paginate', '--slurp']
    for key, value in (fields or {}).items():
        cmd += ['-f', f'{key}={value}']
    result = subprocess.run(cmd, capture_output=True, text=True, check=True, timeout=120)
    data = json.loads(result.stdout)
    return [item for page in data for item in page] if pages else data


def prepare(repo, version, sha, execute=False):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
        raise ValueError('repository must be OWNER/NAME')
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version):
        raise ValueError('version must be X.Y.Z')
    if not re.fullmatch(r'[a-f0-9]{40}', sha):
        raise ValueError('commit must be a full lowercase SHA-1')
    prefix = f'repos/{repo}'
    tag = f'v{version}'
    refs = api(f'{prefix}/git/matching-refs/tags/{tag}')
    if not isinstance(refs, list):
        raise TypeError('invalid references response')
    exact = [ref for ref in refs if ref['ref'] == f'refs/tags/{tag}']
    releases = api(f'{prefix}/releases?per_page=100', pages=True)
    if any(release['tag_name'] == tag for release in releases):
        return False, f'{tag} already has a release; leaving it unchanged.'
    if exact:
        obj, seen = exact[0]['object'], set()
        while obj['type'] == 'tag':
            if obj['sha'] in seen or len(seen) >= 10:
                raise ValueError('cyclic or excessively nested annotated tag')
            seen.add(obj['sha'])
            obj = api(f"{prefix}/git/tags/{obj['sha']}")['object']
        if obj['type'] != 'commit' or obj['sha'] != sha:
            raise ValueError(f'{tag} points to another object; refusing to reuse or move it')
        return True, f'{tag} matches this commit and has no release; resume the build.'
    if execute:
        created = api(f'{prefix}/git/refs', fields={'ref': f'refs/tags/{tag}', 'sha': sha})
        if created.get('ref') != f'refs/tags/{tag}' or created.get('object', {}).get('sha') != sha:
            raise ValueError('unexpected response while creating the tag; inspect before retrying')
    return True, f'{"Created" if execute else "Would create"} {tag} at {sha}.'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repo', required=True)
    ap.add_argument('--version', required=True)
    ap.add_argument('--sha', required=True)
    ap.add_argument('--output', type=Path, help='append Actions outputs after a successful --execute')
    ap.add_argument('--execute', action='store_true')
    args = ap.parse_args()
    try:
        build, message = prepare(args.repo, args.version, args.sha, args.execute)
        if args.execute and args.output:
            with args.output.open('a', encoding='utf-8') as stream:
                stream.write(f'version={args.version}\nbuild={str(build).lower()}\n')
        print(message)
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc:
        print(f'release tag: {exc}')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

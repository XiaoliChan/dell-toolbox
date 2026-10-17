#!/usr/bin/env python3
"""Prepare build inputs first; commit/tag/publish only after the workflow's gates.

Uses only Python's standard library. Plans live in .git, never in a release commit.
No token is read by this module: git/gh use the runner's GITHUB_TOKEN credentials.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess

VERSION_FILES = ('src/core/AppInfo.h', 'CMakeLists.txt', 'res/app.rc')
RELEASE_FILES = (*VERSION_FILES, 'CHANGELOG.md')
PLAN = Path('.git/dtb-release-plan.json')
NOTES = Path('.git/dtb-release-notes.md')


def run(*args, check=True):
    result = subprocess.run(args, text=True, encoding='utf-8',
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise RuntimeError(f'{args[0]} failed ({result.returncode}): {result.stderr.strip() or result.stdout.strip()}')
    return result


def git(*args):
    return run('git', *args).stdout.strip()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def replace_once(text, pattern, value):
    text, count = re.subn(pattern, lambda _: value, text, flags=re.M)
    require(count == 1, f'Expected one version field matching {pattern!r}, found {count}')
    return text


def version():
    app = Path(VERSION_FILES[0]).read_text(encoding='utf-8')
    match = re.search(r'kVersion\s*=\s*"(\d+\.\d+\.\d+)"', app)
    require(match is not None, 'Missing application version')
    value = match.group(1)
    cmake = Path(VERSION_FILES[1]).read_text(encoding='utf-8')
    require(re.search(r'^\s*VERSION ' + re.escape(value) + r'\s*$', cmake, re.M), 'CMake version drift')
    rc = Path(VERSION_FILES[2]).read_text(encoding='utf-8')
    number = value.replace('.', ',') + ',0'
    for key, expected in [('VER_FILEVERSION', number), ('VER_PRODUCTVERSION', number),
                          ('VER_FILEVERSION_STR', f'"{value}.0\\0"'),
                          ('VER_PRODUCTVERSION_STR', f'"{value}\\0"')]:
        require(re.search(r'^#define ' + key + r'\s+' + re.escape(expected) + '$', rc, re.M), f'{key} drift')
    require(all(int(n) <= 65535 for n in value.split('.')), 'Windows version component overflow')
    return value


def bump(value):
    major, minor, patch = map(int, value.split('.'))
    require(patch < 65535, 'Patch overflow; update major/minor explicitly')
    return f'{major}.{minor}.{patch + 1}'


def set_version(value):
    path = Path(VERSION_FILES[0])
    text = path.read_text(encoding='utf-8')
    path.write_text(replace_once(text, r'inline constexpr const char\* kVersion = "[^"]+";',
                                f'inline constexpr const char* kVersion = "{value}";'), encoding='utf-8')
    path = Path(VERSION_FILES[1])
    path.write_text(replace_once(path.read_text(encoding='utf-8'), r'^    VERSION \d+\.\d+\.\d+$',
                                f'    VERSION {value}'), encoding='utf-8')
    path = Path(VERSION_FILES[2])
    text = path.read_text(encoding='utf-8')
    for key, replacement in [('VER_FILEVERSION', value.replace('.', ',') + ',0'),
                             ('VER_PRODUCTVERSION', value.replace('.', ',') + ',0'),
                             ('VER_FILEVERSION_STR', f'"{value}.0\\0"'),
                             ('VER_PRODUCTVERSION_STR', f'"{value}\\0"')]:
        text = replace_once(text, '^#define ' + key + r' .+$', '#define ' + key + ' ' + replacement)
    path.write_text(text, encoding='utf-8')
    require(version() == value, 'Version update failed')


def remote_refs():
    output = git('ls-remote', 'origin')
    return dict((ref, sha) for sha, ref in (line.split() for line in output.splitlines()))


def tag_commit(refs, tag):
    return refs.get(f'refs/tags/{tag}^{{}}', refs.get(f'refs/tags/{tag}'))


def release_base(sha):
    match = re.search(r'^DTB-Release-Base: ([0-9a-f]{40,64})$', git('show', '-s', '--format=%B', sha), re.M)
    return match.group(1) if match else None


def notes_from_changelog(value):
    text = Path('CHANGELOG.md').read_text(encoding='utf-8')
    match = re.search(r'^## v' + re.escape(value) + r' .+?\n(.*?)(?=^## v[0-9]|\Z)', text, re.M | re.S)
    require(match and match.group(1).strip(), 'Missing release changelog entry')
    return match.group(1).strip() + '\n'


def make_notes(base, value, previous_ref=None):
    previous = run('git', 'describe', '--tags', '--abbrev=0', '--match', 'v*', previous_ref or base, check=False)
    revision = f'{previous.stdout.strip()}..{base}' if previous.returncode == 0 else base
    log = git('log', '--format=- %s (%h)', revision)
    require(bool(log), 'No changes since the previous release tag')
    return (f'## Changes\n\n{log}\n\n## Downloads\n\n'
            'Use the portable ZIP or the optional Windows x64 installer. Administrator access is required.\n')


def reusable(candidate, base, refs, expected):
    if release_base(candidate) != base or git('show', '-s', '--format=%P', candidate) != base:
        return False
    changed = set(git('diff', '--name-only', base, candidate).splitlines())
    if not changed or not changed <= set(RELEASE_FILES):
        return False
    return tag_commit(refs, 'v' + expected) == candidate


def prepare(event, ref, publish):
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Tracked working tree must be clean')
    base = git('rev-parse', 'HEAD')
    current = version()
    mode = 'validate'
    notes = ''
    target = current
    refs = remote_refs() if publish else {}
    if publish and ref.startswith('refs/tags/'):
        tag = ref.removeprefix('refs/tags/')
        require(tag == 'v' + current, 'Tag/application version mismatch')
        require(tag_commit(refs, tag) == base, 'Remote tag does not point to the built commit')
        mode = 'tag'
        notes = notes_from_changelog(current) if Path('CHANGELOG.md').exists() else make_notes(base, current, base + '^')
    elif publish:
        require(ref == 'refs/heads/main', 'Publishing a branch is permitted only from main')
        remote = refs.get('refs/heads/main')
        require(remote, 'origin/main does not exist')
        git('fetch', 'origin', 'main', '--tags')
        own_base = release_base(base)
        if remote == base and own_base and reusable(base, own_base, refs, current):
            mode = 'reuse'
            notes = notes_from_changelog(current)
        elif remote != base:
            target = bump(current)
            require(reusable(remote, base, refs, target), 'origin/main advanced; rebuild its latest commit instead')
            git('checkout', '--detach', remote)
            require(version() == target, 'Existing release commit version mismatch')
            mode = 'reuse'
            notes = notes_from_changelog(target)
        else:
            target = bump(current)
            require(tag_commit(refs, 'v' + target) is None, 'Next version tag already exists on another commit')
            notes = make_notes(base, target)
            set_version(target)
            changelog = Path('CHANGELOG.md')
            previous = changelog.read_text(encoding='utf-8') if changelog.exists() else '# Changelog\n\n'
            date = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
            entry = f'## v{target} — {date}\n\n{notes}\n'
            if previous.startswith('# Changelog\n'):
                previous = previous[len('# Changelog\n'):].lstrip()
            changelog.write_text('# Changelog\n\n' + entry + previous, encoding='utf-8')
            git('add', '--', *RELEASE_FILES)
            mode = 'new'
    plan = dict(mode=mode, base=base, head=git('rev-parse', 'HEAD'), version=target,
                tag='v' + target, tree=git('write-tree'), notes=notes)
    PLAN.write_text(json.dumps(plan, indent=2), encoding='utf-8')
    NOTES.write_text(notes, encoding='utf-8')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
            output.write(f'version={target}\ntag=v{target}\nmode={mode}\n')
    print(json.dumps({key: plan[key] for key in ('mode', 'version', 'tag', 'head')}))
    return plan


def finalize(push=True):
    plan = json.loads(PLAN.read_text(encoding='utf-8'))
    require(plan['mode'] != 'validate', 'Validation-only run cannot publish')
    require(git('rev-parse', 'HEAD') == plan['head'], 'HEAD changed after preparation')
    require(not git('diff', '--name-only'), 'Tracked source changed after preparation')
    require(git('write-tree') == plan['tree'], 'Prepared source tree changed')
    require(version() == plan['version'], 'Build/release version mismatch')
    refs = remote_refs()
    if plan['mode'] == 'new':
        require(refs.get('refs/heads/main') == plan['base'], 'origin/main advanced during build; refusing publication')
        require(tag_commit(refs, plan['tag']) is None, 'Tag appeared during build; refusing collision')
        staged = set(git('diff', '--cached', '--name-only').splitlines())
        require(staged <= set(RELEASE_FILES) and bool(staged), 'Unexpected staged changes')
        git('-c', 'user.name=github-actions[bot]', '-c', 'user.email=41898282+github-actions[bot]@users.noreply.github.com',
            'commit', '-m', f'chore(release): {plan["tag"]}\n\nDTB-Release-Base: {plan["base"]}')
        git('tag', plan['tag'])
        if push:
            git('push', '--atomic', 'origin', 'HEAD:refs/heads/main', f'refs/tags/{plan["tag"]}')
    else:
        require(tag_commit(refs, plan['tag']) == plan['head'], 'Remote release tag changed')
        if plan['mode'] == 'reuse':
            require(refs.get('refs/heads/main') == plan['head'], 'origin/main advanced during retry')
    plan['release_sha'] = git('rev-parse', 'HEAD')
    PLAN.write_text(json.dumps(plan, indent=2), encoding='utf-8')
    print(f'Release commit: {plan["release_sha"]}')
    return plan


def publish_release():
    plan = json.loads(PLAN.read_text(encoding='utf-8'))
    require(plan.get('release_sha'), 'Finalize must succeed before publishing')
    require(tag_commit(remote_refs(), plan['tag']) == plan['release_sha'], 'Remote tag drift before upload')
    files = [Path('dell-toolbox-windows-x64.zip'), Path(f'dist/DellToolbox-{plan["version"]}-setup.exe')]
    require(all(path.is_file() and path.stat().st_size > 0 for path in files), 'Required release artifacts missing')
    NOTES.write_text(plan['notes'], encoding='utf-8')
    # List succeeds or fails explicitly; do not interpret arbitrary API errors as "not found".
    releases = json.loads(run('gh', 'api', '--paginate', '--slurp', 'repos/{owner}/{repo}/releases').stdout)
    existing = next((release for page in releases for release in page if release['tag_name'] == plan['tag']), None)
    if existing:
        run('gh', 'release', 'edit', plan['tag'], '--title', 'Dell Toolbox ' + plan['tag'],
            '--notes-file', str(NOTES), '--draft=false', '--prerelease=false')
        run('gh', 'release', 'upload', plan['tag'], *map(str, files), '--clobber')
    else:
        run('gh', 'release', 'create', plan['tag'], *map(str, files), '--verify-tag',
            '--title', 'Dell Toolbox ' + plan['tag'], '--notes-file', str(NOTES))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'finalize', 'publish'))
    parser.add_argument('--event', default=os.environ.get('GITHUB_EVENT_NAME', 'workflow_dispatch'))
    parser.add_argument('--ref', default=os.environ.get('GITHUB_REF', 'refs/heads/main'))
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.event, args.ref, args.publish)
    elif args.command == 'finalize':
        finalize()
    else:
        publish_release()


if __name__ == '__main__':
    main()

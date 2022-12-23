# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities."""

import hashlib
import re
import subprocess

def get_all_commits(from_ref, to_ref):
    """Get all commits from `from_ref`..`to_ref`."""
    cmd = ['git', 'log', '--oneline', '--reverse', '--topo-order',
           '--no-merges', '--abbrev=12', '--format=%h%x01%s',
           f'{from_ref}..{to_ref}']
    commits = subprocess.check_output(cmd, encoding='utf-8', errors='ignore')

    return [tuple(commit.split('\001', 2)) for commit in commits.splitlines()]

def parse_all_commits(file_path):
    """Parse all commits from a todo file."""
    commits = list()
    with open(file_path) as rf:
        for line in rf:
            _, sha, title = line.rstrip().split(' ', 2)
            commits.append((sha, title))

    return commits

def calc_patch_id(sha):
    """Calculate patch ID."""
    show = subprocess.Popen(['git', 'show', sha], stdout=subprocess.PIPE)

    cmd = ['git', 'patch-id', '--stable']
    patch_id = subprocess.check_output(cmd, stdin=show.stdout,
                                       encoding='utf-8', errors='ignore')
    return patch_id.split(' ', 1)[0]

def get_author_date(sha):
    """Get author date."""
    cmd = ['git', 'log', '--format=%at', '-n', '1', sha]
    return subprocess.check_output(cmd, encoding='utf-8', errors='ignore')

def get_affected_files(sha):
    """Get affected files."""
    cmd = ['git', 'diff', '--name-only', f'{sha}^!']
    return subprocess.check_output(cmd, encoding='utf-8', errors='ignore')

def calc_md5(s):
    """Calculate MD5."""
    m = hashlib.md5()
    m.update(s.encode('utf-8'))
    return m.hexdigest()

def to_todo_string(commits):
    """Transform a /sha, title/ list to rebase instructions."""
    return '\n'.join([f'pick {sha} {title}' for sha, title in commits])

def get_conflict_files():
    """Get file paths of conflicts."""
    cmd = ['git', 'diff', '--name-only']
    diff = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    files = subprocess.check_output(['uniq'], stdin=diff.stdout,
                                    encoding='utf-8', errors='ignore')
    return files.splitlines()

def try_rebase(from_ref, todo):
    """Try to rebase from `from_ref` by the given rebase instructions."""
    temp_file = '/tmp/try_rebase.todo'
    with open(temp_file, 'w') as wf:
        wf.write(todo)

    env = dict()
    env['GIT_SEQUENCE_EDITOR'] = f'cat {temp_file} >'

    subprocess.check_call(['git', 'checkout', '-q', from_ref])

    if not todo:
        return True, None

    try:
        subprocess.check_call(['git', 'rebase', '-q', '-i', from_ref],
                              env=env, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        conflicts = get_conflict_files()
        subprocess.check_call(['git', 'rebase', '--abort'])
        return False, conflicts
    else:
        return True, None

def try_am(patch):
    """Try `git am`."""
    try:
        subprocess.check_call(['git', 'am', '--quiet', patch])
    except subprocess.CalledProcessError:
        subprocess.check_call(['git', 'am', '--abort'])
        return None

    m = re.search(r'Subject: \[PATCH \d+/(\d+)', open(patch).read())
    if m:
        n = m.group(1)
    else:
        n = '1'

    commits = subprocess.check_output(['git', 'log', '-q', '-n', n, '--reverse',
                                       '--abbrev=12', '--format=%h%x01%s'],
                                      encoding='utf-8', errors='ignore')
    return [tuple(commit.split('\001', 2)) for commit in commits.splitlines()]

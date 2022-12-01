#!/usr/bin/env python3

# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script for generating replacements.

The script helps to save fix effort at the beginning of rebasing ChromeOS
kernel.  If one fixes something in -rcX, ideally, the effort can be reused
when -rcX+1 is out.

Example:
$ python3 gen_todo.py -o todo_file ...
$ GIT_SEQUENCE_EDITOR="cat todo_file >" git rebase -i v6.1-rc1
$ git checkout -b chromeos-6.1-rc1

[ Start to work on cleaning chromeos-6.1-rc1. ]

$ python3 gen_replacement.py -f v6.1-rc1
[ The script will suspend to background after then. ]
$ git rebase -i v6.1-rc1
[ Squash a few patches to previous one. ]
$ fg
[ The script saves a replacement to patches/ folder. ]

[ And repeat to run gen_replacement.py again for next replacement. ]

isort:skip_file
"""

import argparse
import glob
import logging
import os
import signal
import subprocess
import sys

import util

PATCHES_DIR = 'chromeos/patches'
ID_FILE = os.path.join(PATCHES_DIR, 'ID')
DROPLIST_FILE = os.path.join(PATCHES_DIR, 'DROPLIST')
REBASE_TODO_FILE = '.git/rebase-merge/git-rebase-todo'

try:
    os.mkdir(PATCHES_DIR)
except FileExistsError:
    pass

def normal_mode(from_ref, to_ref):
    old_commits = util.get_all_commits(from_ref, to_ref)
    print('Only 1 replacement is supported.  Run `fg` after replaced.')
    os.kill(os.getpid(), signal.SIGTSTP)
    new_commits = util.get_all_commits(from_ref, to_ref)

    # Assume the first sha difference is the starting point of the replacement.
    for idx, ((o_sha, _), (n_sha, _)) in enumerate(zip(old_commits, new_commits)):
        if o_sha != n_sha:
            from_idx = idx
            break
    else:
        return None, None

    # Assume the last title difference it the ending point of the replacement.
    for idx, ((_, o_title), (_, n_title)) in \
            enumerate(zip(reversed(old_commits), reversed(new_commits))):
        if o_title != n_title:
            break

    to_idx_old = len(old_commits) - idx
    if to_idx_old < from_idx:
        to_idx_old = from_idx + 1
    to_idx_new = len(new_commits) - idx
    if to_idx_new < from_idx:
        to_idx_new = from_idx + 1

    # In case of that the replacement's last titles are the same.
    for idx, ((o_sha, _), (n_sha, _)) in \
            enumerate(zip(old_commits[to_idx_old:], new_commits[to_idx_new:])):
        o_pid = util.calc_patch_id(o_sha)
        n_pid = util.calc_patch_id(n_sha)

        if o_pid == n_pid:
            to_idx_old += idx
            to_idx_new += idx
            break
    else:
        return None, None

    dest_shas = [sha for sha, _ in new_commits[from_idx:to_idx_new]]
    src_shas = [sha for sha, _ in old_commits[from_idx:to_idx_old]]

    logging.debug('Replaced from %s to %s', src_shas, dest_shas)
    return dest_shas, src_shas

def rebase_mode(head):
    ret = subprocess.check_output(['git', 'log', '-n', '1', '--abbrev=12',
                                   '--format=%h', head],
                                  encoding='utf-8', errors='ignore')
    src_shas = [ret.rstrip()]

    todo = open(REBASE_TODO_FILE).read()
    with open(REBASE_TODO_FILE, 'w') as wf:
        wf.write('break\n')
        wf.write(todo)

    print('Run `fg` after resolved the conflict.')
    os.kill(os.getpid(), signal.SIGTSTP)

    ret = subprocess.check_output(['git', 'log', '-n', '1', '--abbrev=12',
                                   '--format=%h', 'HEAD'],
                                  encoding='utf-8', errors='ignore')
    dest_shas = [ret.rstrip()]

    logging.debug('Replaced from %s to %s', src_shas, dest_shas)
    return dest_shas, src_shas

def drop_mode(sha):
    ret = subprocess.check_output(['git', 'log', '-n', '1', '--format=%s',
                                   sha],
                                  encoding='utf-8', errors='ignore')
    title = ret.rstrip()

    patch_id = util.calc_patch_id(sha)

    with open(DROPLIST_FILE, 'a') as wf:
        wf.write(f'{patch_id} {title}\n')

    logging.debug(f'Added "{patch_id} {title}" to DROPLIST')

def gen_patch(dest_shas, src_shas):
    """Generate patch for a replacement from `src_shas` to `dest_shas`."""
    patch_ids = [util.calc_patch_id(sha) for sha in src_shas]
    notes = '\n'.join(patch_ids)

    subprocess.check_call(['git', 'notes', 'add', '-m', notes, dest_shas[0]],
                          encoding='utf-8', errors='ignore')

    try:
        n = int(open(ID_FILE).read())
    except FileNotFoundError:
        n = 1

    subprocess.check_call(['git', 'format-patch', '--notes',
                           '--start-number', f'{n}', '-o', PATCHES_DIR,
                           dest_shas[0] + '^!'],
                          encoding='utf-8', errors='ignore')

    if len(dest_shas) > 1:
        pattern = os.path.join(PATCHES_DIR, f'{n:04}-*.patch')
        file_path = glob.glob(pattern)[0]
        subprocess.check_call(['git', 'format-patch', '--notes',
                               f'--output={file_path}',
                               dest_shas[0] + '^..' + dest_shas[-1]],
                              encoding='utf-8', errors='ignore')

    subprocess.check_call(['git', 'notes', 'remove', dest_shas[0]],
                          encoding='utf-8', errors='ignore')

    n += 1
    open(ID_FILE, 'w').write(f'{n}')

def main():
    """Main entry."""
    logging.basicConfig(stream=sys.stderr, level=logging.DEBUG)

    parser = argparse.ArgumentParser()
    parser.add_argument('-m', '--mode', choices=['normal', 'rebase', 'drop'],
                        default='normal')
    args, _ = parser.parse_known_args()

    if args.mode == 'normal':
        parser.add_argument('-f', '--from-ref', required=True)
        parser.add_argument('-t', '--to-ref', default='HEAD')
        args = parser.parse_args()
        dest_shas, src_shas = normal_mode(args.from_ref, args.to_ref)
        gen_patch(dest_shas, src_shas)
    elif args.mode == 'rebase':
        parser.add_argument('--use-head', action='store_true')
        args = parser.parse_args()
        if args.use_head:
            dest_shas, src_shas = rebase_mode('HEAD')
        else:
            dest_shas, src_shas = rebase_mode('REBASE_HEAD')
        gen_patch(dest_shas, src_shas)
    elif args.mode == 'drop':
        parser.add_argument('sha')
        args = parser.parse_args()
        drop_mode(args.sha)

if __name__ == '__main__':
    main()

#!/usr/bin/env python3

# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script for re-arranging continous rebase kernel to ChromeOS kernel.

This script generates git rebase instructions which can be used with
`git rebase -i` from deduplicating and reordering patches.

Deduplication:
Drop commits if patch-id is the same.

Reorder:
- If the commit title is the same, latter should follow former.
- FIXUP should follow FIXUP target.  For example, "FIXUP: A: B: C" should
  follow "A: B: C".

Reorder would introduce new merge conflicts.  The script tries to find another
position for the patch in the case.

Reorder does not always apply.  The script leaves the patch position as is if
no better position was found.

isort:skip_file
"""

from collections import OrderedDict
from itertools import chain
import argparse
import glob
import logging
import os
import re
import subprocess
import sys

import util

PATCHES_DIR = 'chromeos/patches'
DROPLIST_FILE = os.path.join(PATCHES_DIR, 'DROPLIST')
PATCHES_FILES = os.path.join(PATCHES_DIR, '*.patch')

PATCH_IDS_NOTES = re.compile(r'(?:---\n\nNotes:\n)(.*?)(?:\n\n)',
                             re.MULTILINE | re.DOTALL)
FIXUP_TITLE = re.compile(r'FIXUP:\s*(.*)')

try:
    os.mkdir(PATCHES_DIR)
except FileExistsError:
    pass

def deduplicate_commits_patch_id(commits):
    """Deduplicate commits according to patch-id."""
    res, dedup = list(), set()

    for r in commits:
        sha, _ = r

        patch_id = util.calc_patch_id(sha)
        if patch_id in dedup:
            logging.info('Dropped duplicate (patch-id) %s', r)
            continue

        dedup.add(patch_id)
        res.append(r)

    return res

def deduplicate_commits_title(commits):
    """Deduplicate commits according to title."""
    res, dedup = list(), set()

    for r in commits:
        sha, title = r
        at = util.get_author_date(sha)
        md5 = util.calc_md5(util.get_affected_files(sha))

        k = title, at, md5
        if k in dedup:
            logging.info('Dropped duplicate (title) %s', r)
            continue

        dedup.add(k)
        res.append(r)

    return res

def drop_commits(commits, patch_ids):
    """Drop commits from a pre-defined DROPLIST file."""
    if not os.path.exists(DROPLIST_FILE):
        return

    with open(DROPLIST_FILE) as rf:
        for line in rf:
            patch_id, _ = line.rstrip().split(' ', 1)

            try:
                # Linear search for the index.
                idx = patch_ids.index(patch_id)

                logging.info('Dropped %s', commits[idx])
                commits.pop(idx)
                patch_ids.pop(idx)
            except ValueError:
                pass

def apply_patches(commits, patch_ids, from_ref):
    """Applying patches from a pre-defined set of patches."""
    applied = 0

    for file_path in sorted(glob.iglob(PATCHES_FILES)):
        logging.debug('Trying to apply %s', file_path)

        m = PATCH_IDS_NOTES.search(open(file_path).read())
        if not m:
            logging.warning('Failed to find patch IDs in %s', file_path)
            continue

        ps = [p.strip() for p in m.group(1).splitlines()]
        ps_len = len(ps)

        for idx in range(len(patch_ids)):
            if patch_ids[idx:idx+ps_len] != ps:
                continue

            ret, _ = util.try_rebase(from_ref, util.to_todo_string(commits[:idx]))
            if not ret:
                logging.warning('Failed to rebase before applying %s', file_path)
                break

            cs = util.try_am(file_path)
            if cs is None:
                break
            commits[idx:idx+ps_len] = cs
            patch_ids[idx:idx+ps_len] = [util.calc_patch_id(sha) for sha, _ in cs]

            logging.info('Applied %s', file_path)
            applied += 1
            break
        else:
            logging.debug('Failed to find patch-id sequence %s', ps)

    if not applied:
        logging.info('No applicable patches.')

def separate_vm_configs():
    """Separate commits from VM-specific."""
    paths = ['arch/x86/configs/x86_64_arcvm_defconfig',
             'arch/arm64/configs/arm64_arcvm_defconfig',
             'arch/x86/configs/chromiumos-*',
             'arch/arm64/configs/chromiumos-*']
    vm_configs = []

    for p in paths:
        ret = subprocess.check_output(['git', 'log', '--oneline', '--reverse',
                                       '--abbrev=12', '--format=%h%x01%s', '--',
                                       p], encoding='utf-8', errors='ignore')
        vm_configs += [tuple(c.split('\001', 2)) for c in ret.splitlines()]

    return vm_configs


def separate_chromeos_commits(from_ref, commits):
    """Separate commits from VM-specific and ./chromeos/ folder."""
    vm_configs = separate_vm_configs()

    ret = subprocess.check_output(['git', 'log', '--oneline', '--reverse',
                                   '--abbrev=12', '--format=%h%x01%s', '--',
                                   './chromeos/scripts'],
                                  encoding='utf-8', errors='ignore')
    scripts = [tuple(c.split('\001', 2)) for c in ret.splitlines()]

    ret = subprocess.check_output(['git', 'log', '--oneline', '--reverse',
                                   '--abbrev=12', '--format=%h%x01%s', '--',
                                   './chromeos/config'],
                                  encoding='utf-8', errors='ignore')
    config = [tuple(c.split('\001', 2)) for c in ret.splitlines()]

    ret = subprocess.check_output(['git', 'log', '--oneline', '--reverse',
                                   '--abbrev=12', '--format=%h%x01%s', '--',
                                   './chromeos'],
                                  encoding='utf-8', errors='ignore')
    others = [tuple(c.split('\001', 2)) for c in ret.splitlines()]

    chromeos_commits, dedup = list(), set()
    for t in chain(vm_configs, scripts, config, others):
        if t in dedup:
            continue
        chromeos_commits.append(t)
        dedup.add(t)

    # Cannot use sha as keys because they are just rebased.
    chromeos_titles = set([title for _, title in chromeos_commits])
    res = list()

    for sha, title in commits:
        if title not in chromeos_titles:
            res.append((sha, title))

    return res, chromeos_commits

def get_latest_fixup_target(file_paths):
    """Get the latest FIXUP target."""
    try:
        ret = subprocess.check_output(['git', 'log', '--oneline', '-n', '1',
                                       '--abbrev=12', '--format=%s', '--',
                                       ' '.join(file_paths)],
                                      encoding='utf-8', errors='ignore')
        return ret.rstrip()
    except subprocess.CalledProcessError:
        return None

def reorder_commits(from_ref, commits):
    """Reorder commits."""
    ret, _ = util.try_rebase(from_ref, util.to_todo_string(commits))
    if not ret:
        logging.warning('Current commits are not applicable to reorder')
        return commits

    commits, chromeos_commits = separate_chromeos_commits(from_ref, commits[:])

    patches, sid, prev_title = OrderedDict(), 0, None

    def to_commit_list():
        nonlocal patches
        return [r for v in patches.values() for r in v]
    def get_next_seq():
        nonlocal sid
        sid +=1
        return f'seq_{sid}'

    for idx, r in enumerate(commits):
        _, title = r

        m = FIXUP_TITLE.match(title)
        if m:
            # Trim FIXUP prefix.
            title = m.group(1)

        if title not in patches:
            # It doesn't need a reorder.
            patches[title] = [r]
            prev_title = title
            continue

        if title == prev_title:
            # It doesn't need a reorder because it's already in the right position.
            patches[title].append(r)
            continue

        logging.info('Trying to reorder for %s', r)
        patches[title].append(r)
        prev_title = title

        ret, conflicts = util.try_rebase(from_ref,
                            util.to_todo_string(to_commit_list()))
        if ret:
            continue

        logging.debug('Starting to find a better FIXUP target for %s', r)
        patches[title].pop()

        ret, _ = util.try_rebase(from_ref, util.to_todo_string(commits[:idx]))
        if not ret:
            logging.warning('Failed to rebase for finding a better FIXUP target')

            title = get_next_seq()
            patches[title] = [r]
            continue

        target_title = get_latest_fixup_target(conflicts)
        if target_title is None:
            logging.warning('Failed to find a better FIXUP target')

            title = get_next_seq()
            patches[title] = [r]
            continue

        m = FIXUP_TITLE.match(target_title)
        if m:
            target_title = m.group(1)
        logging.debug('Trying to put %s after %s', r, target_title)
        patches[target_title].append(r)

        ret, _ = util.try_rebase(from_ref, util.to_todo_string(to_commit_list()))
        if ret:
            continue

        logging.info('Failed to find a better FIXUP target')
        patches[target_title].pop()

        title = get_next_seq()
        patches[title] = [r]

    return to_commit_list() + chromeos_commits

def main():
    """Main entry."""
    logging.basicConfig(stream=sys.stderr, level=logging.INFO)

    parser = argparse.ArgumentParser()
    parser.add_argument('-i', '--input')
    parser.add_argument('-o', '--output')
    parser.add_argument('-f', '--from-ref', required=True)
    parser.add_argument('-t', '--to-ref', default='HEAD')
    parser.add_argument('-r', '--reorder', action='store_true')
    parser.add_argument('-d', '--debug', action='store_true')
    args = parser.parse_args()

    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)

    logging.info('Loading commits')
    if not args.input:
        commits = util.get_all_commits(args.from_ref, args.to_ref)
    else:
        commits = util.parse_all_commits(args.input)

        ret, _ = util.try_rebase(args.from_ref, util.to_todo_string(commits))
        if not ret:
            logging.error('Failed to rebase according to the input file')
            return

        # The commits' content could slightly change after rebase.
        commits = util.get_all_commits(args.from_ref, 'HEAD')

    logging.info('Deduplicating via patch-id')
    commits = deduplicate_commits_patch_id(commits[:])

    logging.info('Deduplicating via title')
    commits = deduplicate_commits_title(commits[:])

    logging.info('Calculating patch-ids')
    patch_ids = [util.calc_patch_id(sha) for sha, _ in commits]

    logging.info('Dropping commits')
    drop_commits(commits, patch_ids)

    logging.info('Applying patches')
    apply_patches(commits, patch_ids, args.from_ref)

    if args.reorder:
        logging.info('Reordering commits')
        commits = reorder_commits(args.from_ref, commits[:])

    logging.info('Finalizing')
    out = util.to_todo_string(commits)
    if args.output:
        open(args.output, 'w').write(out)
    else:
        print(out)

if __name__ == '__main__':
    main()

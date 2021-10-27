#!/usr/bin/env python3
# SPDX-License-Identifier: Apache License 2.0 OR GPL-2.0
#
# SPDX-FileCopyrightText: 2021, Facebook, Inc. and its affiliates. All Rights Reserved.
# SPDX-FileCopyrightText: 2021, Western Digital Corporation or its affiliates.
#
# Original file from https://github.com/facebook/rocksdb 
# Modified by Dennis Maisenbacher <dennis.maisenbacher@wdc.com>

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import sys
import time
import random
import re
import tempfile
import subprocess
import shutil
import argparse

minimal_testcase_params = {
    "reopen": 20,
    "expected_values_path": lambda: setup_expected_values_file(),
    "clear_column_family_one_in": 0,
    "block_size": 16384,
    "writepercent": 35,
    "readpercent": 45,
    "prefixpercent": 5,
    "iterpercent": 10,
    "delpercent": 4,
    "delrangepercent": 1,
}

blackbox_default_params = {
    # total time for this script to test db_stress
    "duration": 600,
    # time for one db_stress instance to run
    "interval": 120,
    # since we will be killing anyway, use large value for ops_per_thread
    "ops_per_thread": 100000000,
    "set_options_one_in": 10000,
    "test_batches_snapshots": 1,
}

whitebox_default_params = {
    "duration": 600,
    "log2_keys_per_lock": 10,
    "ops_per_thread": 200000,
    "random_kill_odd": 888887,
    "test_batches_snapshots": lambda: random.randint(0, 1),
}

simple_default_params = {
    "allow_concurrent_memtable_write": lambda: random.randint(0, 1),
    "column_families": 1,
    "experimental_mempurge_threshold": lambda: 10.0*random.random(),
    "max_background_compactions": 1,
    "max_bytes_for_level_base": 67108864,
    "memtablerep": "skip_list",
    "prefixpercent": 0,
    "readpercent": 50,
    "prefix_size" : -1,
    "target_file_size_base": 16777216,
    "target_file_size_multiplier": 1,
    "test_batches_snapshots": 0,
    "write_buffer_size": 32 * 1024 * 1024,
    "level_compaction_dynamic_level_bytes": False,
    "paranoid_file_checks": lambda: random.choice([0, 1, 1, 1]),
}

blackbox_simple_default_params = {
    "open_files": -1,
    "set_options_one_in": 0,
}

whitebox_simple_default_params = {}

_TEST_DIR_ENV_VAR = 'TEST_TMPDIR'
_DEBUG_LEVEL_ENV_VAR = 'DEBUG_LEVEL'

stress_cmd = "../../../../db_stress"

def is_release_mode():
    return os.environ.get(_DEBUG_LEVEL_ENV_VAR) == "0"

def get_dbname(test_name):
    test_dir_name = "rocksdb_crashtest_" + test_name
    test_tmpdir = os.environ.get(_TEST_DIR_ENV_VAR)
    if test_tmpdir is None or test_tmpdir == "":
        dbname = tempfile.mkdtemp(prefix=test_dir_name)
    else:
        dbname = test_tmpdir + "/" + test_dir_name
        shutil.rmtree(dbname, True)
        os.mkdir(dbname)
    return dbname

expected_values_file = None
def setup_expected_values_file():
    global expected_values_file
    if expected_values_file is not None:
        return expected_values_file
    expected_file_name = "rocksdb_crashtest_" + "expected"
    test_tmpdir = os.environ.get(_TEST_DIR_ENV_VAR)
    if test_tmpdir is None or test_tmpdir == "":
        expected_values_file = tempfile.NamedTemporaryFile(
            prefix=expected_file_name, delete=False).name
    else:
        # if tmpdir is specified, store the expected_values_file in the same dir
        expected_values_file = test_tmpdir + "/" + expected_file_name
        if os.path.exists(expected_values_file):
            os.remove(expected_values_file)
        open(expected_values_file, 'a').close()
    return expected_values_file

def is_direct_io_supported(dbname):
    with tempfile.NamedTemporaryFile(dir=dbname) as f:
        try:
            os.open(f.name, os.O_DIRECT)
        except BaseException:
            return False
        return True

def finalize_and_sanitize(params, unknown_params):
    for key in {**params, **dict.fromkeys(unknown_params,"")}:
        if key.startswith("--fs_uri"):
            if 'db' in params:
                print("Found '--fs_uri' flag. Removeing the 'db' flag.")
                del params['db']
                break
            else:
                for unknown_param in unknown_params:
                    if "-db=" in str(unknown_param):
                        print("Found '--fs_uri' flag. Removeing the 'db' flag from the additional params.")
                        unknown_params.remove(unknown_param)
                        break
                break

    dest_params = dict([(k,  v() if callable(v) else v)
                        for (k, v) in params.items()])

    if (dest_params.get("test_batches_snapshots") == 1):
        dest_params["delpercent"] += dest_params["delrangepercent"]
        dest_params["delrangepercent"] = 0

    return dest_params

def gen_cmd_params(args):
    params = {}
    params.update(minimal_testcase_params)
    if args.test_type == 'blackbox':
        params.update(blackbox_default_params)
    if args.test_type == 'whitebox':
        params.update(whitebox_default_params)
    if args.simple:
        params.update(simple_default_params)
        if args.test_type == 'blackbox':
            params.update(blackbox_simple_default_params)
        if args.test_type == 'whitebox':
            params.update(whitebox_simple_default_params)

    for k, v in vars(args).items():
        if v is not None:
            params[k] = v
    return params

def gen_cmd(params, unknown_params):
    finalzied_params = finalize_and_sanitize(params, unknown_params)
    cmd = [stress_cmd] + [
        '--{0}={1}'.format(k, v)
        for k, v in [(k, finalzied_params[k]) for k in sorted(finalzied_params)]
        if k not in set(['test_type', 'simple', 'duration', 'interval',
                         'random_kill_odd', 'test_best_efforts_recovery', 'stress_cmd'])
        and v is not None] + unknown_params
    return cmd

def inject_inconsistencies_to_db_dir(dir_path):
    files = os.listdir(dir_path)
    file_num_rgx = re.compile(r'(?P<number>[0-9]{6})')
    largest_fnum = 0
    for f in files:
        m = file_num_rgx.search(f)
        if m and not f.startswith('LOG'):
            largest_fnum = max(largest_fnum, int(m.group('number')))

    candidates = [
        f for f in files if re.search(r'[0-9]+\.sst', f)
    ]
    deleted = 0
    corrupted = 0
    for f in candidates:
        rnd = random.randint(0, 99)
        f_path = os.path.join(dir_path, f)
        if rnd < 10:
            os.unlink(f_path)
            deleted = deleted + 1
        elif 10 <= rnd and rnd < 30:
            with open(f_path, "a") as fd:
                fd.write('12345678')
            corrupted = corrupted + 1
    print('Removed %d table files' % deleted)
    print('Corrupted %d table files' % corrupted)

    # Add corrupted MANIFEST and SST
    for num in range(largest_fnum + 1, largest_fnum + 10):
        rnd = random.randint(0, 1)
        fname = ("MANIFEST-%06d" % num) if rnd == 0 else ("%06d.sst" % num)
        print('Write %s' % fname)
        with open(os.path.join(dir_path, fname), "w") as fd:
            fd.write("garbage")

def execute_cmd(cmd, timeout):
    child = subprocess.Popen(cmd, stderr=subprocess.PIPE,
                             stdout=subprocess.PIPE)
    print("Running db_stress with pid=%d: %s\n\n"
          % (child.pid, ' '.join(cmd)))

    try:
        outs, errs = child.communicate(timeout=timeout)
        hit_timeout = False
        print("WARNING: db_stress ended before kill: exitcode=%d\n"
              % child.returncode)
    except subprocess.TimeoutExpired:
        hit_timeout = True
        child.kill()
        print("KILLED %d\n" % child.pid)
        outs, errs = child.communicate()

    return hit_timeout, child.returncode, outs.decode('utf-8'), errs.decode('utf-8')


# This script runs and kills db_stress multiple times. It checks consistency
# in case of unsafe crashes in RocksDB.
def blackbox_crash_main(args, unknown_args):
    cmd_params = gen_cmd_params(args)
    dbname = get_dbname('blackbox')
    exit_time = time.time() + cmd_params['duration']

    print("Running blackbox-crash-test with \n"
          + "interval_between_crash=" + str(cmd_params['interval']) + "\n"
          + "total-duration=" + str(cmd_params['duration']) + "\n")

    while time.time() < exit_time:
        cmd = gen_cmd(dict(
            list(cmd_params.items())), unknown_args)
        #+ list({'db': dbname}.items())), unknown_args)

        hit_timeout, retcode, outs, errs = execute_cmd(cmd, cmd_params['interval'])

        if not hit_timeout:
            print('Exit Before Killing')
            print('stdout:')
            print(outs)
            print('stderr:')
            print(errs)
            sys.exit(2)

        for line in errs.split('\n'):
            if line != '' and  not line.startswith('WARNING'):
                print('stderr has error message:')
                print('***' + line + '***')

        time.sleep(1)  # time to stabilize before the next run

        if args.test_best_efforts_recovery:
            inject_inconsistencies_to_db_dir(dbname)

        time.sleep(1)  # time to stabilize before the next run

    # we need to clean up after ourselves -- only do this on test success
    shutil.rmtree(dbname, True)


# This python script runs db_stress multiple times. Some runs with
# kill_random_test that causes rocksdb to crash at various points in code.
def whitebox_crash_main(args, unknown_args):
    cmd_params = gen_cmd_params(args)
    dbname = get_dbname('whitebox')

    cur_time = time.time()
    exit_time = cur_time + cmd_params['duration']
    half_time = cur_time + cmd_params['duration'] // 2

    print("Running whitebox-crash-test with \n"
          + "total-duration=" + str(cmd_params['duration']) + "\n")

    total_check_mode = 4
    check_mode = 0
    kill_random_test = cmd_params['random_kill_odd']
    kill_mode = 0

    while time.time() < exit_time:
        print("Setting up whitebox run with check_mode %s and kill_mode %s" % (check_mode, kill_mode))
        if check_mode == 0:
            additional_opts = {
                # use large ops per thread since we will kill it anyway
                "ops_per_thread": 100 * cmd_params['ops_per_thread'],
            }
            # run with kill_random_test, with three modes.
            # Mode 0 covers all kill points. Mode 1 covers less kill points but
            # increases change of triggering them. Mode 2 covers even less
            # frequent kill points and further increases triggering change.
            if kill_mode == 0:
                additional_opts.update({
                    "kill_random_test": kill_random_test,
                })
            elif kill_mode == 1:
                if cmd_params.get('disable_wal', 0) == 1:
                    my_kill_odd = kill_random_test // 50 + 1
                else:
                    my_kill_odd = kill_random_test // 10 + 1
                additional_opts.update({
                    "kill_random_test": my_kill_odd,
                    "kill_exclude_prefixes": "WritableFileWriter::Append,"
                    + "WritableFileWriter::WriteBuffered",
                })
            elif kill_mode == 2:
                # TODO: May need to adjust random odds if kill_random_test
                # is too small.
                additional_opts.update({
                    "kill_random_test": (kill_random_test // 5000 + 1),
                    "kill_exclude_prefixes": "WritableFileWriter::Append,"
                    "WritableFileWriter::WriteBuffered,"
                    "PosixMmapFile::Allocate,WritableFileWriter::Flush",
                })
            # Run kill mode 0, 1 and 2 by turn.
            kill_mode = (kill_mode + 1) % 3
        elif check_mode == 1:
            # normal run with universal compaction mode
            additional_opts = {
                "kill_random_test": None,
                "ops_per_thread": cmd_params['ops_per_thread'],
                "compaction_style": 1,
            }
            # Single level universal has a lot of special logic. Ensure we cover
            # it sometimes.
            if random.randint(0, 1) == 1:
                additional_opts.update({
                    "num_levels": 1,
                })
        elif check_mode == 2:
            # normal run with FIFO compaction mode
            # ops_per_thread is divided by 5 because FIFO compaction
            # style is quite a bit slower on reads with lot of files
            additional_opts = {
                "kill_random_test": None,
                "ops_per_thread": cmd_params['ops_per_thread'] // 5,
                "compaction_style": 2,
            }
        else:
            # normal run
            additional_opts = {
                "kill_random_test": None,
                "ops_per_thread": cmd_params['ops_per_thread'],
            }

        cmd = gen_cmd(dict(list(cmd_params.items())
            + list(additional_opts.items())), unknown_args)
        #+ list({'db': dbname}.items())), unknown_args)

        print("Running:" + ' '.join(cmd) + "\n")  # noqa: E999 T25377293 Grandfathered in

        # If the running time is 15 minutes over the run time, explicit kill and
        # exit even if white box kill didn't hit. This is to guarantee run time
        # limit, as if it runs as a job, running too long will create problems
        # for job scheduling or execution.
        # TODO detect a hanging condition. The job might run too long as RocksDB
        # hits a hanging bug.
        hit_timeout, retncode, stdoutdata, stderrdata = execute_cmd(
            cmd, exit_time - time.time() + 900)
        msg = ("check_mode={0}, kill option={1}, exitcode={2}\n".format(
               check_mode, additional_opts['kill_random_test'], retncode))

        print(msg)
        print(stdoutdata)
        print(stderrdata)

        if hit_timeout:
            print("Killing the run for running too long")
            break

        expected = False
        if additional_opts['kill_random_test'] is None and (retncode == 0):
            # we expect zero retncode if no kill option
            expected = True
        elif additional_opts['kill_random_test'] is not None and retncode <= 0:
            # When kill option is given, the test MIGHT kill itself.
            # If it does, negative retncode is expected. Otherwise 0.
            expected = True

        if not expected:
            print("TEST FAILED. See kill option and exit code above!!!\n")
            sys.exit(1)

        stderrdata = stderrdata.lower()
        errorcount = (stderrdata.count('error') -
                      stderrdata.count('got errors 0 times'))
        print("#times error occurred in output is " + str(errorcount) +
                "\n")

        if (errorcount > 0):
            print("TEST FAILED. Output has 'error'!!!\n")
            sys.exit(2)
        if (stderrdata.find('fail') >= 0):
            print("TEST FAILED. Output has 'fail'!!!\n")
            sys.exit(2)

        # First half of the duration, keep doing kill test. For the next half,
        # try different modes.
        if time.time() > half_time:
            # we need to clean up after ourselves -- only do this on test
            # success
            shutil.rmtree(dbname, True)
            os.mkdir(dbname)
            cmd_params.pop('expected_values_path', None)
            check_mode = (check_mode + 1) % total_check_mode

        time.sleep(1)  # time to stabilize after a kill


def main():
    global stress_cmd

    parser = argparse.ArgumentParser(description="This script runs and kills \
        db_stress multiple times")
    parser.add_argument("test_type", choices=["blackbox", "whitebox"])
    parser.add_argument("--simple", action="store_true")
    parser.add_argument("--test_best_efforts_recovery", action='store_true')
    parser.add_argument("--stress_cmd")

    all_params = dict(list(minimal_testcase_params.items())
                      + list(blackbox_default_params.items())
                      + list(whitebox_default_params.items())
                      + list(simple_default_params.items())
                      + list(blackbox_simple_default_params.items())
                      + list(whitebox_simple_default_params.items()))

    for k, v in all_params.items():
        parser.add_argument("--" + k, type=type(v() if callable(v) else v))
    # unknown_args are passed directly to db_stress
    args, unknown_args = parser.parse_known_args()

    test_tmpdir = os.environ.get(_TEST_DIR_ENV_VAR)
    if test_tmpdir is not None and not os.path.isdir(test_tmpdir):
        print('%s env var is set to a non-existent directory: %s' %
                (_TEST_DIR_ENV_VAR, test_tmpdir))
        sys.exit(1)

    if args.stress_cmd:
        stress_cmd = args.stress_cmd
    if args.test_type == 'blackbox':
        blackbox_crash_main(args, unknown_args)
    if args.test_type == 'whitebox':
        whitebox_crash_main(args, unknown_args)
    # Only delete the `expected_values_file` if test passes
    if os.path.exists(expected_values_file):
        os.remove(expected_values_file)


if __name__ == '__main__':
    main()

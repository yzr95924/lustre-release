#!/usr/bin/python3
# -*- coding: UTF-8 -*-
"""
util library
"""

import datetime
import subprocess
import socket
import getpass
import os
import signal
import time

def get_local_strftime(utc_datetime, fmt):
    """
    get local time in str format
    """
    pass

def get_current_utc_time():
    """
    get current UTC time with the time zone info
    """
    return datetime.datetime.now()

def get_format_cur_filename(cur_filename: str):
    """
    get formatted current filename
    """
    ret_filename = ""
    if cur_filename[-4:].lower() in [".pyc", ".pye"]:
        ret_filename = cur_filename[:-4] + ".py"
    else:
        ret_filename = cur_filename
    ret_filename = os.path.normcase(ret_filename)
    ret_filename = os.path.normpath(ret_filename)
    return ret_filename

def read_one_line(filename: str):
    """
    open file and read one line
    """
    with open(filename, "r", encoding="utf-8") as fd:
        line = fd.readline().rstrip("\n")
    return line

def process_is_alive(pid):
    """
    true is process exists and is not yet stuck in Zombie state
    """
    proc_stat = ""
    path = "/proc/%s/stat" % pid
    try:
        proc_stat = read_one_line(path)
    except IOError:
        if not os.path.exists(path):
            return False
        raise

    return proc_stat.split()[2] != "Z"

def send_signal_to_pid(pid, signal):
    """
    send a signal to a process id
    return true if the process terminated successfully
    otherwise false
    """
    try:
        os.kill(pid, signal)
    except OSError:
        # process may die before
        pass

    for try_time in range(5):
        if not process_is_alive(pid):
            return True
        time.sleep(1)

    # process is still alive
    return False

def kill_subprocess(cur_process: subprocess.Popen):
    """
    kill the job
    """
    # check if the sub_process is still alive
    if cur_process.poll() is not None:
        return cur_process.poll()

    # the subprocess is not terminated
    # kill it via an series of signals
    signal_queue = [signal.SIGTERM, signal.SIGKILL]
    for sig in signal_queue:
        send_signal_to_pid(cur_process.pid, sig)
        if cur_process.poll() is not None:
            return cur_process.poll()
    return 0
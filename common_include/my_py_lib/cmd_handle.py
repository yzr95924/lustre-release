"""
lib for handling CMD
"""

import time
import os
import datetime
import io
import subprocess
import select

from my_py_lib import util

_G_SHELL_LANG_OPTION = "LANG=en_US.UTF-8 "

class CmdResult():
    """
    all commands will return a command result of this class
    """
    def __init__(self, stdout="", stderr="", exit_stat=None, duration=0):
        self.cr_exit_stat = exit_stat
        self.cr_stdout = stdout
        self.cr_stderr = stderr
        self.cr_duration = duration
        self.cr_timeout = False # whether timeout happens

    def cr_clear(self):
        """
        clear the result
        """
        self.cr_stdout = ""
        self.cr_stderr = ""
        self.cr_duration = 0
        self.cr_exit_stat = None
        self.cr_timeout = False

class CmdJob():
    """
    object for each running command
    """
    def __init__(self, command: str, timeout=None,  stdout_tee=None,
                stderr_tee=None, stdin=None, return_stdout=None,
                return_stderr=True, quit_func=None,
                flush_tee=False, silent=False):
        self.cj_command = command;
        self.cj_result = CmdResult()
        self.cj_timeout = timeout
        self.cj_stdout_tee = stdout_tee
        self.cj_stderr_tee = stderr_tee
        self.cj_quit_func = quit_func
        self.cj_silent = silent

        if isinstance(stdin, str):
            self.cj_str_stdin = stdin
            self.cj_stdin = subprocess.PIPE
        else:
            self.cj_str_stdin = None
            self.cj_stdin = None

        if return_stdout:
            self.cj_stdout_file = io.BytesIO()
        if return_stderr:
            self.cj_stderr_file = io.BytesIO()
        self.cj_started = False
        self.cj_killed = False
        self.cj_start_time = None
        self.cj_stop_time = None
        self.cj_max_stop_time = None
        self.cj_subprocess = None
        self.cj_return_stderr = return_stderr
        self.cj_return_stdout = return_stdout
        self.cj_flush_tee = flush_tee

    def cj_run_start_func(self):
        """
        start to run the command
        """
        if self.cj_started:
            return -1
        else:
            self.cj_started = True
        self.cj_start_time = time.time()
        if self.cj_timeout:
            self.cj_max_stop_time = self.cj_timeout + self.cj_start_time
        run_shell = "/bin/bash"
        self.cj_subprocess = subprocess.Popen(_G_SHELL_LANG_OPTION + self.cj_command,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True,
            executable=run_shell, stdin=self.cj_stdin)
        return 0

    def cj_run_stop_func(self):
        """
        stop the job even when it is running
        """
        self.cj_kill_func()
        self.cj_post_exit_func()
        return self.cj_result

    def cj_post_exit_func(self):
        """
        after exit, process the outputs and compute the duration
        """
        self.cj_process_output_func(is_stdout=True, final_read=True)
        self.cj_process_output_func(is_stdout=False, final_read=True)
        if self.cj_stdout_tee:
            self.cj_stdout_tee.flush()

    def cj_kill_func(self):
        """
        kill the job
        """
        self.cj_result.cr_exit_stat = util.kill_subprocess(self.cj_subprocess)
        self.cj_killed = True

    def cj_wait_for_cmd_func(self):
        """
        wait util the cmd exists
        """
        read_list = []
        write_list = []
        reverse_dict = {}

        read_list.append(self.cj_subprocess.stdout)
        read_list.append(self.cj_subprocess.stderr)
        reverse_dict[self.cj_subprocess.stdout] = True
        reverse_dict[self.cj_subprocess.stderr] = False

        if self.cj_str_stdin is not None:
            write_list.append(self.cj_subprocess.stdin)

        if self.cj_timeout:
            time_left = self.cj_max_stop_time - time.time()
        else:
            # not select time out
            time_left = None

        while not self.cj_timeout or time_left > 0:
            # will return when write to stdin or when there is
            # stdout/stderr output we can read (include when it is EOF)
            # 1 second timeout
            read_ready, write_ready, _ = select.select(read_list, write_list,
                [], 1)

            # os.read() has to be used instead of subproc.stdout.read()
            # which will otherwise block
            for file_obj in read_ready:
                is_stdout = reverse_dict[file_obj]
                self.cj_process_output_func(is_stdout)
                if self.cj_flush_tee:
                    if is_stdout:
                        self.cj_stderr_tee.flush()

            for file_obj in write_ready:
                # write PIPE_BUF bytes without blocking
                # POSIX requires PIPE_BUF >= 512
                file_obj.write(self.cj_str_stdin[:512].encode("utf-8"))
                self.cj_str_stdin = self.cj_str_stdin[512:]

                # no more input data, close stdin, remove it from select set
                if not self.cj_str_stdin:
                    write_list.remove(file_obj)
                    file_obj.close()

            self.cj_result.cr_exit_stat = self.cj_subprocess.poll()
            if self.cj_result.cr_exit_stat is not None:
                return

            if self.cj_timeout:
                time_left = self.cj_max_stop_time - time.time()

            if self.cj_quit_func is not None and self.cj_quit_func():
                break

        # if timeout, kill the process
        self.cj_kill_func()
        return

    def cj_process_output_func(self, is_stdout=True, final_read=False):
        """
        process the stdout or stderr
        """
        buf = None
        if is_stdout:
            pipe = self.cj_subprocess.stdout
            if self.cj_return_stdout:
                buf = self.cj_stdout_file
            tee = self.cj_stdout_tee
        else:
            pipe = self.cj_subprocess.stderr
            if self.cj_return_stderr:
                buf = self.cj_stderr_file
            tee = self.cj_stderr_tee

        if final_read:
            # read all data that we can from pipe and stop
            data = bytes()
            while select.select([pipe], [], [], 0)[0]:
                tmp_data = os.read(pipe.fileno(), 1024)
                if len(tmp_data) == 0:
                    break
                data += tmp_data
        else:
            # perform a single read
            data = os.read(pipe.fileno(), 1024)

        if buf is not None:
            buf.write(data)
        if tee:
            tee.write(data)

    def cj_run_func(self):
        """
        run the command, wait unit it exits and return the result
        """
        # do not all run for more than twice currently
        if self.cj_started:
            return self.cj_result

        ret = self.cj_run_start_func()
        if ret:
            self.cj_result.cr_exit_stat = ret
            # TODO: neet to fix the logger

        self.cj_wait_for_cmd_func()
        self.cj_post_exit_func()
        return self.cj_result


def run(command: str, timeout=None, stdout_tee=None, stderr_tee=None,
        stdin=None, return_stdout=True, return_stderr=True, quit_func=None,
        flush_tee=False, silent=False):
    """
    run a command
    """
    if not isinstance(command, str):
        stderr = "type of command is not a string"
        return CmdResult(stderr=stderr, exit_stat=-1)

    job = CmdJob(command, timeout=timeout, stdout_tee=stdout_tee,
        stderr_tee=stderr_tee, stdin=stdin, return_stdout=return_stdout,
        return_stderr=return_stderr, quit_func=quit_func, flush_tee=flush_tee,
        silent=silent)
    return job.cj_run_

class CmdHandle():
    # an instance for handling CMD
    def __init__(self):
        pass

    def cmd_handle_run(self, log, cmd):
        pass
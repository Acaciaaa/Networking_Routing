#
# Autotest Assigment 3 Routing Simulation of Computer Networking
# Interaction with the routing agent as a child process,
# using a thread to read the child's stdout and push it into a thread-safe queue.
#
# Tested with Python 3.8
#
# The implementation is based on Eli Bendersky
# [https://eli.thegreenplace.net/2017/interacting-with-a-long-running-child-process-in-python/]
#
# Written by TA: Rui Ding, 2021.12.3.
# For usage, please refer to README.md in this folder
#

import os
import sys
import queue
import subprocess
import time
import threading
import atexit
import getopt

#
# global vars, modify with caution
#

AGENT = "agent"
ROUTER = "router"
COMMON_H = "common.h"
AGENT_C = "agent.c"
ROUTER_C = "router.c"
MAKEFILE = "Makefile"

HANDIN_DIR = "../Handin/"
TESTCASE_DIR = "TestCases/"
TEST_FILE = ""

DEBUG = False
START_SOCK = 10000

TERMINATE_TIME_OUT = 0.2
WAIT_TIME = 0.4
ITERATION = 1

SINGLE_CASE = None

HELP_MSG = f"Usage: {sys.argv[0]} <handin file> [OPTION]...\n\n"\
f"OPTIONS\t\tDESCRIPTION\t\t\t\t\tDEFAULT\n"\
f"-v\t\tverbose output\n"\
f"-h\t\tthis info\n"\
f"-i <iter>\tthe number of iterations on each case.\t\t{ITERATION}\n"\
f"-w <time>\twaiting time per router on each issued command.\t{WAIT_TIME}\n"\
f"-t <time>\tterminate in <time> second(s).\t\t\t{TERMINATE_TIME_OUT}\n"\
f"-c <case>\tspecify one case to run\t\t\t\t{SINGLE_CASE}"

#
# wrapper functions
#


# log and exit
def log_and_exit(err_msg, exit_code=1):
    if not err_msg.endswith('\n'):
        err_msg += '\n'
    sys.stderr.write(err_msg)
    sys.exit(exit_code)


# run a command and get its return code
def run_command(arg_list):
    return subprocess.call(arg_list,
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)


# peer thread to get output in real time
def output_reader(proc, outq):
    for line in iter(proc.stdout.readline, ''):
        outq.put(line)


#
# core functions
#


# initialize testing environment
def init():
    global TEST_FILE, WAIT_TIME, TERMINATE_TIME_OUT, ITERATION, SINGLE_CASE, DEBUG
    # check arguments
    try:
        TEST_FILE = sys.argv[1]
        arg_tup, __ = getopt.getopt(sys.argv[2:], "w:t:i:c:hv")
        for __ in arg_tup:
            if __[0] == "-w":
                WAIT_TIME = float(__[1])
            elif __[0] == "-t":
                TERMINATE_TIME_OUT = float(__[1])
            elif __[0] == "-i":
                ITERATION = int(__[1])
            elif __[0] == "-c":
                SINGLE_CASE = __[1]
            elif __[0] == "-h":
                log_and_exit(HELP_MSG)
            elif __[0] == "-v":
                DEBUG = True
    except (getopt.GetoptError, ValueError, IndexError):
        log_and_exit(HELP_MSG)

    # copy students' file to the current folder
    ret_code = run_command(
        ["/usr/bin/cp", "-rf", f"{HANDIN_DIR}{TEST_FILE}", "./"])
    if ret_code:
        log_and_exit(f"Error: handin file '{TEST_FILE}' not found.")
    # copy test data to the current folder
    test_cases = [
        __ for __ in os.listdir(TESTCASE_DIR) if __.startswith("case")
    ]
    for __ in test_cases:
        ret_code = run_command(
            ["/usr/bin/cp", "-rf", f"{TESTCASE_DIR}{__}", f"{TEST_FILE}/"])
        if ret_code:
            log_and_exit(f"Error: failed when copy test data.")
    # change working directory
    os.chdir(TEST_FILE)
    # register call back function on exit
    atexit.register(lambda: run_command(
        ["/usr/bin/bash", "-c", f"cd ..; rm -rf {TEST_FILE}"]))


# scrutinize students' handin and refresh the executables
def check_and_make():
    # remove the stale executables if there are any
    files = os.listdir(".")
    if AGENT in files:
        run_command(["/usr/bin/rm", "-f", AGENT])
    if ROUTER in files:
        run_command(["/usr/bin/rm", "-f", ROUTER])
    # check the required handin files
    ret_code = run_command(
        ["/usr/bin/bash", "-c",
         f"ls -R | grep -F {COMMON_H}"])  # disable regex
    if ret_code:
        log_and_exit(f"Error: file '{COMMON_H}' missing.")
    ret_code = run_command(
        ["/usr/bin/bash", "-c", f"ls -R | grep -F {AGENT_C}"])  # disable regex
    if ret_code:
        log_and_exit(f"Error: file '{AGENT_C}' missing.")
    ret_code = run_command(
        ["/usr/bin/bash", "-c",
         f"ls -R | grep -F {ROUTER_C}"])  # disable regex
    if ret_code:
        log_and_exit(f"Error: file '{ROUTER_C}' missing.")
    ret_code = run_command(
        ["/usr/bin/bash", "-c", f"ls -R | grep -i {MAKEFILE}"])  # ingore case
    if ret_code:
        log_and_exit(f"Error: file '{MAKEFILE}' missing.")
    # make
    ret_code = run_command([f"/usr/bin/make"])
    if ret_code:
        log_and_exit(f"Error: make error.")
    # check router and agent
    files = os.listdir(".")
    if AGENT not in files:
        log_and_exit(f"Error: '{AGENT}' not found.")
    if ROUTER not in files:
        log_and_exit(f"Error: '{ROUTER}' not found.")


# to avoid reuse the port in TIME_WAIT time
def modify_location_txt(rel_path_loc):
    global START_SOCK
    content = []
    with open(rel_path_loc, "r") as fp:
        content = fp.readlines()
    with open(rel_path_loc, "w") as fp:
        for line in content:
            disass = line.split(",")
            if len(disass) == 3:
                disass[1] = str(START_SOCK)
                START_SOCK += 1
                line = ",".join(disass)
            fp.write(line)


# test for a single case
# Return True on successful execution (but the students' output can mismatch).
# Return false on a broken pipe (which usaully implies that socket has not been released).
def one_test_case(rel_path_loc: str, rel_path_top: str, rel_path_cmd: str,
                  rel_path_ans: str):
    # testing objects
    router_no = 0
    router_info = {}
    router_setup = {}
    agent_cmd = []
    agent = None
    ret_val = True

    # parse location.txt
    with open(rel_path_loc, "r") as fp:
        router_no = int(fp.readline())
        for __ in range(router_no):
            ip, port, id = fp.readline().split(",")
            port, id = int(port), int(id)
            router_info[id] = (ip, port)

    # parse command.txt
    with open(rel_path_cmd, "r") as fp:
        # add the trailing '\n'
        agent_cmd = [
            __ if __.endswith('\n') else __ + '\n' for __ in fp.readlines()
        ]

    # open answer.txt
    ans_fd = open(rel_path_ans, "r")

    # setup routers
    for id, (ip, port) in router_info.items():
        router_setup[id] = subprocess.Popen(
            ['./router', rel_path_loc, rel_path_top,
             str(id)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            encoding='utf-8')
    # setup agent
    agent = subprocess.Popen(['./agent', rel_path_loc],
                             stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL,
                             encoding='utf-8')

    # start peer thread
    outq = queue.Queue()
    thd = threading.Thread(target=output_reader, args=(agent, outq))
    thd.start()

    # issue command
    try:
        print(rel_path_cmd)
        for cmd in agent_cmd:
            print(">", cmd, end='')
            # if it is a show command, prepare the answer
            ans_lines = []
            stu_lines = []
            if cmd.startswith("show"):
                while True:
                    line = ans_fd.readline().strip(" \n\t\r")
                    if not line:
                        break
                    ans_lines.append(line)
            if DEBUG:
                for __ in ans_lines:
                    print("#", __)
            # issue command and wait
            agent.stdin.write(cmd)
            agent.stdin.flush()  # in case it is buffered
            time.sleep(WAIT_TIME * router_no)
            # get output
            try:
                while True:
                    line = outq.get(block=False)
                    stu_lines.append(line)
                    if DEBUG: print(f"  {line}", end='')
            except queue.Empty:
                pass
            time.sleep(0.1)
            # if it is a show command, compare the result
            while stu_lines and ans_lines:
                if ans_lines[0] in stu_lines[0]:
                    ans_lines.pop(0)
                stu_lines.pop(0)
            if ans_lines:
                print("\033[1;31m" "Error: " "\033[0m" "output mismatch.")
    # socket currently being unavailable
    except BrokenPipeError:
        print("\033[1;34m" "Caveat: " "\033[0m" "Broken pipe.")
        print("Switch to another socket.")
        ret_val = False

    finally:
        # This is in 'finally' so that we can terminate the child if something
        # goes wrong
        for id, proc in router_setup.items():
            proc.terminate()
            try:
                proc.wait(timeout=TERMINATE_TIME_OUT)
                if DEBUG:
                    print(f"== router {id} exited with rc =", proc.returncode)
            except subprocess.TimeoutExpired:
                if DEBUG: print(f"router {id} did not terminate in time")
        agent.terminate()
        try:
            agent.wait(timeout=TERMINATE_TIME_OUT)
            if DEBUG: print(f"== agent exited with rc =", proc.returncode)
        except subprocess.TimeoutExpired:
            if DEBUG: print(f"agent did not terminate in time")
    # wait the peer thread to terminate, since it is terminated
    thd.join()
    return ret_val


# run test
def run_test():
    test_cases = sorted([
        __ for __ in os.listdir(f"../{TESTCASE_DIR}") if __.startswith("case")
    ])

    if SINGLE_CASE:
        test_cases = [__ for __ in test_cases if SINGLE_CASE in __]

    for prefix in test_cases:
        # path to location.txt and topology.txt
        rel_path_loc = f"{prefix}/{prefix}-location.txt"
        rel_path_top = f"{prefix}/{prefix}-topology.txt"

        # bind command and answer
        cmd_list = sorted([__ for __ in os.listdir(prefix) if "command" in __])
        ans_list = sorted([__ for __ in os.listdir(prefix) if "answer" in __])
        test = list(zip(cmd_list, ans_list))
        for rel_path_cmd, rel_path_ans in test:
            rel_path_cmd = f"{prefix}/{rel_path_cmd}"
            rel_path_ans = f"{prefix}/{rel_path_ans}"

            left = ITERATION
            fail = 0
            while left:
                # modify location.txt so as to avoid imminent reuse of the socket
                modify_location_txt(rel_path_loc)

                if one_test_case(rel_path_loc, rel_path_top, rel_path_cmd,
                                 rel_path_ans):
                    left -= 1
                else:
                    fail += 1
                if fail == 8:
                    print("\033[1;31m" "Error: " "\033[0m" "fail to run.")
                    break


if __name__ == '__main__':
    init()
    check_and_make()
    run_test()
import os
import getopt
import sys

stap_cmd_prefix = "sudo stap "
stap_option = "-d kernel " \
            "-d lustre " \
            "-d mdc -d mdt " \
            "-d obdclass -d osc " \
            "-d lov -d lmv -d ofd " \
            "-d ptlrpc "

def Usage():
    print("{name} -i [stap script]".format(name = __file__))
    exit()

if __name__ == "__main__":
    options = "-i:-h"
    opts, args = getopt.getopt(sys.argv[1:], options)

    if (len(sys.argv[1:]) == 0):
        Usage()

    stap_script_path = ""
    for opt_name, opt_value in opts:
        if (opt_name == "-i"):
            stap_script_path = opt_value
        elif (opt_name == "-h"):
            Usage()
        else:
            print("invalid option")
            Usage()

    cmd = stap_cmd_prefix + stap_option + stap_script_path
    print(cmd)
    os.system(cmd)
import os
import multiprocessing

llmount_script="llmount.sh"
llmount_clean_script="llmountcleanup.sh"
number_of_cpu = multiprocessing.cpu_count()

path_option = ["/usr/lib64/lustre/tests"]

def StopLocalTest(valid_option):
    print("stop local test")
    llmount_clean_script_full_path = os.path.join(valid_option, llmount_clean_script)
    cmd = "sudo bash " + llmount_clean_script_full_path
    print(cmd)
    os.system(cmd)

def CompileInstall():
    print("compile Lustre")
    cmd = "make -j" + str(number_of_cpu)
    print(cmd)
    os.system(cmd)
    print("install new packages")
    cmd = "sudo make install"
    print(cmd)
    os.system(cmd)

def RestartLocalTest(valid_option):
    print("re-start local test")
    llmount_script_full_path = os.path.join(valid_option, llmount_script)
    cmd = "sudo bash " + llmount_script_full_path
    print(cmd)
    os.system(cmd)

if __name__ == "__main__":
    print("check the path of {llmount_script} and {llmount_clean_script}".format(
        llmount_script = llmount_script,
        llmount_clean_script = llmount_clean_script
    ))

    valid_option = ""
    for option in path_option:
        llmount_script_full_path = os.path.join(option, llmount_script)
        if (os.path.exists(llmount_script_full_path)):
            print("find the valid full path {full_path}".format(
                full_path = llmount_script_full_path
            ))
            valid_option = option
            break

    StopLocalTest(valid_option)
    CompileInstall()
    RestartLocalTest(valid_option)
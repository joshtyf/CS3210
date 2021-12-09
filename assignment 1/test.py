import subprocess
import sys

def compile(wDir: str):
    print("*** Compiling ***")
    print("running make clean, output: ", end="")
    result = subprocess.run(["make", "clean"], cwd=wDir, capture_output=True)
    print(result.stdout.decode(), end="")
    print("running make build, output: ", end="")
    result = subprocess.run(["make", "build"], capture_output=True, cwd=wDir)
    print(result.stdout.decode())
    if (result.returncode != 0):
        print("Errors: {err}".format(err=result.stderr.decode()))
        exit(1)

def run(wDir: str, nThreads: int):
    print("*** Running ***")
    for i in range(7):
        program = "./goi.out"
        inp = "sample_inputs/sample{i}.in".format(i=i)
        out = "death_toll.out"
         
        print("Running: {program} {inp} {out} {nThreads}".format(program=program, inp=inp, out=out, nThreads=nThreads))
        result = subprocess.run([program, inp, out, nThreads], cwd=wDir, capture_output=True)
        if (len(result.stderr) == 0):
            print("Output: {output}".format(output=result.stdout.decode()))
        else:
            print("Error: {err}".format(err=result.stderr.decode()))
            exit()
        result = subprocess.run(["diff", out, "sample_outputs/sample{i}.out".format(i=i)], capture_output=True, cwd=wDir)
        if (len(result.stdout) > 0):
            print(result.stdout)

if __name__ == "__main__":
    if (len(sys.argv) < 3):
        print("Usage: <folder> <nThreads>")
        exit()

    folder = sys.argv[1]
    wDir = "./{folder}".format(folder=folder)
    nThreads = sys.argv[2]
    compile(wDir)
    run(wDir, nThreads)

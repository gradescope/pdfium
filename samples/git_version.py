import subprocess
import sys

def version():
    return subprocess.check_output(["git", "describe", "--tags"]).strip()

def write_version(output_path):
    output = """#define VERSION \"{0}\"
""".format(version())
    with open(output_path, "w") as f:
        f.write(output)

write_version(sys.argv[1])

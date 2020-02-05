import subprocess

def version():
    return subprocess.check_output(["git", "describe", "--tags"]).strip()

def write_version(output_path):
    output = """#define VERSION \"{0}\"""".format(version())
    with open(output_path, "w") as f:
        f.write(output)

write_version("version.h")

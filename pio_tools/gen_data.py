Import("env")
import os
import subprocess

from SCons.Script import COMMAND_LINE_TARGETS

if "idedata" in COMMAND_LINE_TARGETS:
    env.Exit(0)


def ReadAndMaybeMinifyFiles(fullPath):
    # TODO: split html, css and js. they must be handled different!
    _, extension = os.path.splitext(fullPath)
    if not extension in ['.html', '.js', '.css']:
        with open(fullPath, "rb") as f:
            return f.read()
    originalSize = os.stat(fullPath).st_size
    result = subprocess.run(['./node_modules/minify/bin/minify.js',
                           fullPath], stdout=subprocess.PIPE)
    minifiedContent = result.stdout
    print("Minified '%s' with from %d to %d bytes" % (fullPath, originalSize, len(minifiedContent)))
    return minifiedContent


def GenData():
    dataDir = os.path.join(env["PROJECT_DIR"], "data")
    print("dataDir = %s" % dataDir)
    genDir = os.path.join(env.subst("$BUILD_DIR"), 'inline_data')
    print("genDir = %s" % genDir)
    if not os.path.exists(dataDir):
        return
    if not os.path.exists(genDir):
        os.mkdir(genDir)
    env.Append(CPPPATH=[genDir])

    files = sorted(file for file in os.listdir(dataDir)
                   if os.path.isfile(os.path.join(dataDir, file)))

    out = "// WARNING: Autogenerated by pio_tools/gen_data.py, don't edit manually.\n"
    out += "#ifndef OWIE_GENERATED_DATA_H\n"
    out +="#define OWIE_GENERATED_DATA_H\n\n"
    for name in files:
        varName = name.upper().replace(".", "_")
        sizeName = varName + "_SIZE"
        storageArrayName = varName + "_PROGMEM_ARRAY"
        out += (
            "static const unsigned char %s[] PROGMEM = {\n  " % storageArrayName)
        firstByte = True
        fileContent = ReadAndMaybeMinifyFiles(os.path.join(dataDir, name))
        column = 0
        for b in fileContent:
            if not firstByte:
                out += ","
            else:
                firstByte = False
            column = column + 1
            if column > 20:
                column = 0
                out += "\n  "
            out += str(b)
        out += "};\n"
        out += "#define %s FPSTR(%s)\n" % (varName, storageArrayName)
        out += "#define %s sizeof(%s)\n\n" % (sizeName, storageArrayName)
    out += "\n#endif // OWIE_GENERATED_DATA_H\n"
    with open(os.path.join(genDir, "data.h"), 'w') as f:
        f.write(out)
    print("Wrote data.h\n")

GenData()

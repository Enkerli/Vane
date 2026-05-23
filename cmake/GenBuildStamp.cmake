# Writes a BuildStamp.h to OUTPUT every time it is run.
# Invoked as a build-time custom command so the timestamp is always current.
#   cmake -DOUTPUT=<path> -P GenBuildStamp.cmake
string(TIMESTAMP STAMP "%b %d %Y  %H:%M")
file(WRITE "${OUTPUT}" "#pragma once\n#define VANE_BUILD_STAMP \"${STAMP}\"\n")

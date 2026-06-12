# PIDaim

> [!WARNING]
> the program need run in the main monitor, or it may have problem

use pid to aim
in msys64/ucrt64

```
# MinGW toolchain (g++, gdb, make, etc.)
pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain

# CMake and Ninja (optional)
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja

# OpenCV (pre‑built for MinGW)
pacman -S mingw-w64-ucrt-x86_64-opencv
the program need run in the main monitor, or it may have problem
```

```
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:/msys64/ucrt64/lib/cmake/opencv4"
# or your own path to cmake
```

use "mingw32-make" in .\build\ to build exe file
now using **p: 22.5 i: 15 d: 0.0005 ** for the base game test.

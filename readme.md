# Profile View

## About

Profile View is a basic application used to visualize profile data from a JS-50 scan head using the Pinchot C API. The program utilizes Dear ImGui to provide a cross platform graphics stack that can plot scan data.

The code provided here should be considered to be a "work in progress" and is not guaranteed to be free from error.

## Dependencies
Here are some dependecies you may need (list based on Ubuntu 23.04)
```
sudo apt install libxinerama-dev libglfw3-dev libx11-dev libxcursor-dev libxi-dev
```

## Compiling

CMake is used to orchestrate compilation. Outside of the usual CMake dependencies, a local copy of the Pinchot C API code needs to be provided. The open source version is available [here](https://github.com/JoeScan-Inc/pinchot-c-api). The path to the API directory must be passed to CMake when generating the build files as demonstrated below:
```
mkdir build
cd build
cmake -DPINCHOT_API_ROOT_DIR=path/to/pinchot/c/api ..
# build files generated, can now run make or build using Visual Studio
```

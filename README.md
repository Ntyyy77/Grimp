# EpiGrimp - Day 1 prototype

This is the Day 1 prototype of EpiGrimp (Epitech project). It's a minimal Qt6 app with:
- main window, menu and toolbar
- interactive canvas you can draw on with the mouse
- open/save basic images (PNG/JPG/BMP)

## Build (example)
mkdir build && cd build
cmake ..   # if your Qt installation isn't auto-detected, add -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build .

## Run
./EpiGrimp

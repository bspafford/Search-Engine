git clone --recursive https://github.com/bspafford/Search-Engine
cd backend/external/uWebSockets/uSockets
make
cd ../../..
cmake -S . -B build
cmake --build build

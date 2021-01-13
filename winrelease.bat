rd /S /Q release
rd /S /Q build
mkdir release
mkdir build
cd build

setx CUDA_TOOLKIT_ROOT_DIR "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v20.0"
cmake .. -G "Visual Studio 15 2017 Win64" -DETHASHCL=ON -DAPICORE=ON -DBINKERN=OFF -DETHASHCUDA=ON -T v140
cmake --build . --config Release
cd ..
copy build\ethminer\Release\ethminer.exe release

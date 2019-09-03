cd tools
./format_code.sh
cd ..
if [ ! -d "build_linux" ]; then
    mkdir build_linux
fi
cd build_linux
cmake ..
make clean
make -j2 >1.log 2>2.log
cd ..

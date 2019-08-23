cd tools
./format_code.sh
cd ..
mkdir build_linux
cd build_linux
cmake ..
make clean
make -j2 >1.log 2>2.log
cd ..
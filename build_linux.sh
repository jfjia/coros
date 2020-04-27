cd tools
./format_code.sh
cd ..
if [ ! -d "build_linux" ]; then
    mkdir build_linux
fi
cd build_linux
cmake -DWITH_EXAMPLES=ON ..
make clean
make -j2 >1.log 2>2.log
cat 2.log
cd ..

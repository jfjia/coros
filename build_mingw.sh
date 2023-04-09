cd tools
./format_code.sh
cd ..
if [ ! -d "build_mingw" ]; then
    mkdir build_mingw
fi
cd build_mingw
cmake -DWITH_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles" ..
mingw32-make clean
mingw32-make -j2 >1.log 2>2.log
cat 2.log
cd ..

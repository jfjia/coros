cd tools
./format_code.sh
cd ..
if [ ! -d "build_mingw" ]; then
    mkdir build_mingw
fi
cd build_mingw
cmake -G "MSYS Makefiles" ..
make clean
make -j2 >1.log 2>2.log
cat 2.log
cd ..

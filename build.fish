#!/usr/bin/env fish
ninja run-dev -- main.lut
echo "--------------------"
echo "compiling"
echo "--------------------"
gcc test.o -o test -lraylib
rm ./test.o
echo "--------------------"
echo "running"
echo "--------------------"
./test
rm ./test

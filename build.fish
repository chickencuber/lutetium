#!/usr/bin/env fish
function cleanup --on-event fish_exit
    rm -f ./test.o ./test
end

ninja run-dev -- main.lut
or exit
echo "--------------------"
echo "linking"
echo "--------------------"
gcc test.o -o test -lraylib
or exit
echo "--------------------"
echo "running"
echo "--------------------"
./test
echo exited with: $status

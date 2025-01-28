@echo off

setlocal

cmake -B build -S .
cmake --build build --config Debug

exit /B

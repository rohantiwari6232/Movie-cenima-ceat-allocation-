#!/bin/sh
# Linux/Mac: needs libsqlite3-dev (sudo apt install libsqlite3-dev)
g++ -std=c++17 main.cpp -lsqlite3 -o cineseat && echo "Build OK -> ./cineseat"

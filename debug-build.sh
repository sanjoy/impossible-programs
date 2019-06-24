#!/bin/bash

clang++ -fsanitize=undefined -fsanitize=address -g3 -O1 -Wall -Werror main.cc -o main -std=c++17 -DENABLE_LOG

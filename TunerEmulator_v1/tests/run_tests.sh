#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I.. protocol_tests.cpp ../protocol.cpp ../receiver.cpp -o protocol_tests
./protocol_tests
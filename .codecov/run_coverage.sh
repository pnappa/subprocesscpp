#!/bin/bash

# assumes we're in the project root
./coverage
echo "uploading to codecov"
bash <(curl -s https://codecov.io/bash)
echo "cleaning up code coverage files"
rm -rfv *.gcov
rm -rfv *.gcda
rm -rfv *.gcno
rm -rfv coverage

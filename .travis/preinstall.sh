#!/bin/bash

# install catch2 testing library (into the repo, who cares)
wget https://github.com/catchorg/Catch2/releases/download/v2.4.2/catch.hpp -O $TRAVIS_BUILD_DIR/catch.hpp

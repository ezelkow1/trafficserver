#!/bin/sh -l

set -x
set -e
source /opt/rh/gcc-toolset-11/enable

# We do not support CMake builds for the 9.x branch.
if [ "${GITHUB_PR_TARGET_BRANCH}" == "9.0.x" -o \
     "${GITHUB_PR_TARGET_BRANCH}" == "9.1.x" -o \
     "${GITHUB_PR_TARGET_BRANCH}" == "9.2.x" ]
then
        echo "CMake builds are not supported for the 9.x branch."
        echo "Falling back to automake."
        autoreconf -fiv
        ./configure \
          --with-quiche=/opt/quiche \
          --with-openssl=/opt/boringssl \
          --enable-experimental-plugins \
          --enable-example-plugins \
          --prefix=/tmp/ats/ \
          --enable-werror \
          --enable-debug \
          --enable-ccache
          make -j4 V=1 Q=
          make -j 2 check VERBOSE=Y V=1
          make install
          /tmp/ats/bin/traffic_server -K -k -R 1
else
   cmake -B cmake-build-quiche -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DENABLE_QUICHE=ON -DCMAKE_BUILD_TYPE=Debug -DBUILD_EXPERIMENTAL_PLUGINS=ON -Dquiche_ROOT=/opt/quiche -DOPENSSL_ROOT_DIR=/opt/boringssl -DCMAKE_INSTALL_PREFIX=/tmp/ats_quiche
   cmake --build cmake-build-quiche -j4 -v
   cmake --install cmake-build-quiche
   pushd cmake-build-quiche
   ctest -j4 --output-on-failure --no-compress-output -T Test
   /tmp/ats_quiche/bin/traffic_server -K -k -R 1
   popd
fi

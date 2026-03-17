# Convenience wrapper for out-of-tree builds
# Buildroot uses src/CMakeLists.txt directly via QT_CLUSTER_DEMO_SITE
cmake_minimum_required(VERSION 3.10)
project(qt-cluster-demo-wrapper LANGUAGES CXX)
add_subdirectory(src)

# =============================
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(PkgConfig REQUIRED)


# FFTW3
# FFTW3 via pkg-config (Ubuntu/Debian packages don't ship CMake config files)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
find_package(ZLIB REQUIRED)

# FFmpeg libs: avformat, avcodec, avutil, swresample
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)


add_executable(dsd_inspector
src/main.cpp
)


target_include_directories(dsd_inspector PRIVATE
PkgConfig::FFTW3
PkgConfig::AVFORMAT
PkgConfig::AVCODEC
PkgConfig::AVUTIL
PkgConfig::SWRESAMPLE
)


# Link
target_link_libraries(dsd_inspector PRIVATE
    PkgConfig::FFTW3
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    ZLIB::ZLIB        # <-- προστέθηκε
)

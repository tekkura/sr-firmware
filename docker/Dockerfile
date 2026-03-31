# Set the base image hash via build-arg (default: amd64)
ARG BASE_IMAGE_HASH=sha256:04f510bf1f2528604dc2ff46b517dbdbb85c262d62eacc4aa4d3629783036096
FROM ubuntu:24.04@${BASE_IMAGE_HASH}

# Add metadata labels
LABEL maintainer="topherbuckley@gmail.com"
LABEL description="Smartphone Robot Firmware Build Environment"
ARG VERSION=dev
LABEL version="${VERSION}"

# Set environment variable for external libraries directory
ENV EXTERNAL_LIBRARIES_DIR=/opt/external

# Create a directory for external libraries
RUN mkdir -p $EXTERNAL_LIBRARIES_DIR

# add the project path to the gdbinit file so can automate gdb startup
RUN echo "add-auto-load-safe-path /project/.gdbinit" >> /root/.gdbinit

# Install build tools and dependencies for CMake and OpenOCD
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    libtool \
    pkg-config \
    texinfo \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    python3 \
    python3-pip \
    libssl-dev \
    libusb-1.0-0-dev \
    libhidapi-dev \
    gdb-multiarch

# Install Pico-SDK checking out version 1.5.1
RUN git clone --branch master https://github.com/raspberrypi/pico-sdk.git $EXTERNAL_LIBRARIES_DIR/pico-sdk && \
    cd $EXTERNAL_LIBRARIES_DIR/pico-sdk && \
    git checkout 6a7db34 && \
    git submodule update --init
ENV PICO_SDK_PATH=$EXTERNAL_LIBRARIES_DIR/pico-sdk

# Clone openocd and build from source checking out v0.12.0 (else cannot get >0.11.0)
RUN git clone --branch sdk-2.0.0 https://github.com/raspberrypi/openocd.git $EXTERNAL_LIBRARIES_DIR/openocd && \
    cd $EXTERNAL_LIBRARIES_DIR/openocd && \
    git checkout cf9c0b41c && \
    ./bootstrap && \
    ./configure && \
    make -j $(nproc) && \
    make install

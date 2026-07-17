FROM --platform=linux/amd64 mcr.microsoft.com/devcontainers/base:ubuntu-22.04

ARG DEBIAN_FRONTEND=noninteractive

# Build dependencies for the Mutable Instruments firmware sources. The x86_64
# container is deliberate: Mutable's original ARM GCC 4.8 toolchain has no
# Apple Silicon/Linux ARM build.
RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        autotools-dev \
        avrdude \
        avr-libc \
        binutils-avr \
        build-essential \
        bzip2 \
        ca-certificates \
        gcc-avr \
        git \
        libc6:i386 \
        libncurses5:i386 \
        libstdc++6:i386 \
        make \
        pkg-config \
        python2.7 \
        python3 \
        python3-matplotlib \
        python3-numpy \
        python3-scipy \
        python3-serial \
        wget \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Original Mutable Instruments ARM compiler, required by the project makefiles.
RUN wget --progress=dot:giga --timeout=30 --tries=3 \
        https://launchpad.net/gcc-arm-embedded/4.8/4.8-2013-q4-major/+download/gcc-arm-none-eabi-4_8-2013q4-20131204-linux.tar.bz2 \
        -O /tmp/arm-toolchain.tar.bz2 \
    && mkdir -p /usr/local/arm-4.8.3 \
    && tar -xjf /tmp/arm-toolchain.tar.bz2 -C /usr/local/arm-4.8.3 --strip-components=1 \
    && ln -s /usr/local/arm-4.8.3 /usr/local/arm \
    && rm /tmp/arm-toolchain.tar.bz2

ENV PATH="/usr/local/arm-4.8.3/bin:${PATH}"
ENV MPLBACKEND="Agg"
ENV PYTHONPATH="."

# The legacy makefiles invoke an unversioned Python 2 executable and look for
# CrossPack's AVR tools at this path. Ubuntu 22.04 only packages SciPy for
# Python 3, but Plaits' resource compiler is Python 2 code; install the final
# Python-2-compatible NumPy and SciPy wheels for that compiler.
RUN wget -q https://bootstrap.pypa.io/pip/2.7/get-pip.py -O /tmp/get-pip.py \
    && python2.7 /tmp/get-pip.py \
    && python2.7 -m pip install --no-cache-dir \
        matplotlib==2.2.5 numpy==1.16.6 scipy==1.2.3 \
    && rm /tmp/get-pip.py \
    && ln -s /usr/bin/python2.7 /usr/local/bin/python \
    && ln -s /usr /usr/local/CrossPack-AVR

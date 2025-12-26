FROM ubuntu:noble

COPY build/_docker/sources.list.d /etc/apt/sources.list.d

RUN rm /etc/apt/sources.list.d/ubuntu.sources && \
    test -f /etc/apt/sources.list.d/ubuntu.`uname -m` && mv /etc/apt/sources.list.d/ubuntu.`uname -m` /etc/apt/sources.list.d/ubuntu.`uname -m`.sources; \
    dpkg --add-architecture amd64 && \
    apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        doxygen \
        file \
        gcc-x86-64-linux-gnu g++-x86-64-linux-gnu \
        git \
        git-lfs\
        graphviz \
        libssl-dev:amd64 \
        mingw-w64 \
    && git lfs install --system \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN git config --global --add safe.directory /src

# Set a working directory (optional)
WORKDIR /app

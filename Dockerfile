FROM nvidia/cuda:11.3.0-devel-ubuntu18.04 AS build

RUN apt-get update && apt-get install -y git perl python3-pip mesa-common-dev libdbus-1-dev software-properties-common
RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test
RUN apt-get update && apt-get install -y g++-10
RUN pip3 install cmake --upgrade
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 10
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 10

WORKDIR /app

# Copying only .git files to init submodule to be able to use docker cache more
COPY .git .git
COPY .gitmodules .gitattributes ./
RUN git submodule update --init --recursive

# Copying everything 
COPY . .
RUN mkdir build
WORKDIR /app/build

ARG DETHASHCUDA=ON
ARG DETHASHCL=ON

RUN cmake .. -DETHASHCUDA=$DETHASHCUDA -DETHASHCL=$DETHASHCL
RUN cmake --build .

# For run nvidia container toolkit needs to be installed on host
# How to: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html#docker
# Run docker: docker build -t nsfminer . && docker run --gpus all -e POOL="stratum+tcp://ikru.eth:x@us-east.ethash-hub.miningpoolhub.com:20535" nsfminer 
FROM nvidia/cuda:11.3.0-base-ubuntu18.04
ENV POOL="Pool connection"
WORKDIR /app
COPY --from=build /app/ ./

CMD nvidia-smi && ./build/nsfminer/nsfminer -U -P "${POOL}"

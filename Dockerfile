FROM nvidia/cuda:11.3.0-devel-ubuntu18.04 AS build

RUN apt-get update && apt-get install -y git perl python3-pip mesa-common-dev libdbus-1-dev
RUN pip3 install cmake --upgrade

WORKDIR /app

# Copying only .git files to init submodule to be able to use docker cache more
COPY .git .git
COPY .gitmodules .gitattributes ./
RUN git submodule update --init --recursive

# Copying everything 
COPY . .
RUN mkdir build
WORKDIR /app/build

RUN cmake .. -DETHASHCUDA=ON -DETHASHCL=OFF
RUN cmake --build .

# For run nvidia container toolkit needs to be installed on host
# How to: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html#docker
# Run docker: docker build -t nsfminer . && docker run --gpus all nsfminer 
FROM nvidia/cuda:11.3.0-base-ubuntu18.04
WORKDIR /app

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get install -y nvidia-driver-460

COPY --from=build /app/ ./

CMD nvidia-smi && ./build/nsfminer/nsfminer -U -P stratum+tcp://ikru.eth:x@us-east.ethash-hub.miningpoolhub.com:20535

# NVIDIA Powermizer

Simple NVIDIA GPU Powermizer control tool.

## Introduction

NVIDIA Powermizer is a tool for controlling the power state of NVIDIA GPUs. It can automatically adjust the power state based on GPU utilization to achieve energy savings, and reduce heat. It is mainly designed for datacenter GPU cards where hardware-based or driver-based powermizer is not available.

## Features

- Automatically adjust power state based on GPU utilization
- Support taking encoder and decoder utilization into account
- Support custom utilization thresholds and time thresholds
- Support multiple GPUs

## Building

Just type `make`. Make sure you have GNU C++ compiler and NVIDIA NVML development libraries installed.

```sh
make
```

The output would be `nvidia-powermizer`.

## Usage

Run the program with the required parameters:

```sh
./nvidia-powermizer [options]
```

### Parameters

- `-h, --help`: Print help message
- `-b, --boost <util>`: Set the utilization threshold to boost power state
- `-l, --low-power <util>`: Set the utilization threshold to lower power state
- `-B, --boost-time <ms>`: Set the time to boost power state (milliseconds)
- `-L, --low-power-time <ms>`: Set the time to lower power state (milliseconds)
- `-c, --coder`: Enable encoder and decoder utilization
- `-v, --verbose`: Increase verbosity

### Example

```sh
./nvidia-powermizer -b 70 -l 30 -B 5000 -L 10000 -c -v
```

This command sets the following parameters:
- Boost power state utilization threshold to 70%
- Lower power state utilization threshold to 30%
- Boost power state time threshold to 5000 milliseconds
- Lower power state time threshold to 10000 milliseconds
- Enable encoder and decoder utilization
- Increase verbosity

## License

Copyright (c) 2025 dqs105
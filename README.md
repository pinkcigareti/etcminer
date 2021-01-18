# nsfminer (no stinkin' fees)

[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg)](https://github.com/RichardLitt/standard-readme)
[![Gitter](https://img.shields.io/gitter/room/nwjs/nw.js.svg)][Gitter]

> Ethereum miner with OpenCL, CUDA and stratum support

**nsfminer** is an Ethash GPU mining application: with nsfminer you can mine every coin which relies on an Ethash Proof of Work.

## Features

* OpenCL mining
* Nvidia CUDA mining
* realistic benchmarking
* stratum mining without proxy
* Automatic devices configuration
* farm failover (getwork + stratum)

## Table of Contents

* [Usage](#usage)
    * [Examples connecting to pools](#examples-connecting-to-pools)
* [Build](#build)
    * [Building from source](#building-from-source)
* [Contribute](#contribute)

## Usage

**nsfminer** is a command line program. This means you launch it either
from a Windows command prompt or Linux console, or create shortcuts to
predefined command lines using a Linux Bash script or Windows batch/cmd file.
For a full list of available command, please run:

```sh
nsfminer --help
```

### Examples connecting to pools

Check our [samples](docs/POOL_EXAMPLES_ETH.md) to see how to connect to different pools.

## Build

### Building from source

See [docs/BUILD.md](docs/BUILD.md) for build/compilation details.

## Contribute

[![Gitter](https://img.shields.io/gitter/room/ethereum-mining/ethminer.svg)][Gitter]

To meet the community, ask general questions and chat about the miner join [the ethminer channel on Gitter][Gitter].

All bug reports, pull requests and code reviews are very much welcome.

## License

Licensed under the [GNU General Public License, Version 3](LICENSE).

[Gitter]: https://gitter.im/ethereum-mining/ethminer

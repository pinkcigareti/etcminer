```shell
General options:
  -h [ --help ]             This help message
  -H [ --help-module ] arg  Help for a given module, one of: cl, cu, api, misc,
                            con, test, conf or reboot
  -V [ --version ]          The version number
  -P [ --pool ] arg         One or more Stratum pool or http (getWork) 
                            connection as URL(s)
                            
                            scheme://[user[.workername][:password]@]hostname:po
                            rt[/...]
                            
                            For details and some samples how to fill in this 
                            value please use
                            nsfminer --help-module con
                            
                            
  -F [ --config ] arg       Configuration file name. See '-H conf' for details.
  -G [ --opencl ]           Mine/Benchmark using OpenCL only
  -U [ --cuda ]             Mine/Benchmark using CUDA only


OpenCL options:
  --cl-work arg (=128)  Set the work group size, valid values are 64 128 or 256
  --cl-split            Force split-DAG mode. May improve performance on older 
                        GPU models.


CUDA options:
  --cu-block arg (=128) Set the block size, valid values are 32, 64, 128, or 
                        256
  --cu-streams arg (=2) Set the number of streams per GPU, valid values 1, 2 or
                        4


API options:
  --api-bind arg        Set the API address:port the miner should listen on. 
                        Use negative port number for readonly mode
  --api-port arg (=0)   Set the API port, the miner should listen on all bound 
                        addresses. Use negative numbers for readonly mode
  --api-password arg    Set the password to protect interaction with API 
                        server. If not set, any connection is granted access. 
                        Be advised passwords are sent unencrypted


Miscellaneous options:
  -v [ --verbosity ] arg (=0)  Set output verbosity level. Use the sum of :
                               1 - log per GPU status lines
                               2 - log per GPU solutions
                               
  --getwork-recheck arg (=500) Set polling interval for new work in getWork 
                               mode. Value expressed in milliseconds. It has no
                               meaning in stratum mode
  --retry-delay arg (=0)       Delay in seconds before reconnection retry
  --retry-max arg (=3)         Set number of reconnection retries to same pool.
                               Set to 0 for infinite retries.
  --work-timeout arg (=180)    If no new work received from pool after this 
                               amount of time the connection is dropped. Value 
                               expressed in seconds.
  --response-timeout arg (=2)  If no response from pool to a stratum message 
                               after this amount of time the connection is 
                               dropped
  -R [ --report-hashrate ]     Report miner hash rate to the pool
  --display-interval arg (=5)  Statistic display interval in seconds
  --HWMON arg (=0)             GPU hardware monitoring level. Can be one of:
                               0 - No monitoring
                               1 - Monitor temperature and fan percentage
                               2 - As 1 plus monitor power drain
  --exit                       Stop miner whenever an error is encountered
  --failover-timeout arg (=0)  Sets the number of minutes miner can stay 
                               connected to a fail-over pool before trying to 
                               reconnect to the primary (the first) connection.
  --nocolor                    Monochrome display log lines
  --syslog                     Use syslog appropriate output (drop timestamp 
                               and channel prefix)
  -L [ --list-devices ]        Lists the detected OpenCL/CUDA devices and 
                               exits. Can be combined with -G or -U flags
  --tstop arg (=0)             Suspend mining on GPU which temperature is above
                               this threshold. Implies --HWMON 1. If not set or
                               zero no temp control is performed
  --tstart arg (=0)            Resume mining on previously overheated GPU when 
                               temp drops below this threshold. Implies --HWMON
                               1. Must be lower than --tstop
  -n [ --nonce ] arg           Hex string specifying the upper bits of miner's 
                               start nonce. Can be used to ensure multiple 
                               miners are not searching overlapping nonce 
                               ranges.
  --devices arg                List of space separated device numbers to be 
                               used
  --seq                        Generate DAG sequentially, one GPU at a time.


Connections specifications :

    Whether you need to connect to a stratum pool or to make use of
    getWork polling mode (generally used to solo mine) you need to specify
    the connection  making use of -P command line argument filling up the
    URL. The URL is in the form:

     scheme://[user[.workername][:password]@]hostname:port[/...].

    where 'scheme' can be any of :

    getwork    for http getWork mode
    stratum    for tcp stratum mode
    stratums   for tcp encrypted stratum mode
    Example 1: -P getwork://127.0.0.1:8545
    Example 2: -P stratums://0x012345678901234567890234567890123.miner1@ethermine.org:5555
    Example 3: -P stratum://0x012345678901234567890234567890123.miner1@nanopool.org:9999/john.doe%40gmail.com
    Example 4: -P stratum://0x012345678901234567890234567890123@nanopool.org:9999/miner1/john.doe%40gmail.com

    Please note: if your user or worker or password do contain characters
    which may impair the correct parsing (namely any of . : @ # ?) you have
    to enclose those values in backticks( ` ASCII 096) or Url Encode them
    Also note that backtick has a special meaning in *nix environments thus
    you need to further escape those backticks with backslash.

    Example : -P stratums://\`account.121\`.miner1:x@ethermine.org:5555
    Example : -P stratums://account%2e121.miner1:x@ethermine.org:5555
    (In Windows backslashes are not needed)

    Common url encoded chars are
    . (dot)      %2e
    : (column)   %3a
    @ (at sign)  %40
    ? (question) %3f
    # (number)   %23
    / (slash)    %2f
    + (plus)     %2b

    You can add as many -P arguments as you want. Every -P specification
    after the first one behaves as fail-over connection. When also the
    the fail-over disconnects miner passes to the next connection
    available and so on till the list is exhausted. At that moment
    miner restarts the connection cycle from the first one.
    An exception to this behavior is ruled by the --failover-timeout
    command line argument. See 'nsfminer -H misc' for details.

    The special notation '-P exit' stops the failover loop.
    When miner reaches this kind of connection it simply quits.

    When using stratum mode miner tries to auto-detect the correct
    flavour provided by the pool. Should be fine in 99% of the cases.
    Nevertheless you might want to fine tune the stratum flavour by
    any of of the following valid schemes :

    stratum+ssl stratum+tcp stratum1+ssl stratum1+tcp stratum2+ssl stratum2+tcp stratum3+ssl stratum3+tcp 

    where a scheme is made up of two parts, the stratum variant + the tcp
    transport protocol

    Stratum variants :

        stratum     Stratum
        stratum1    Eth Proxy compatible
        stratum2    EthereumStratum 1.0.0 (nicehash)
        stratum3    EthereumStratum 2.0.0

    Transport variants :

        tcp         Unencrypted tcp connection
        ssl         Encrypted tcp connection


Test options:
  -M [ --benchmark ] arg Mining test. Used to test hashing speed. Specify the 
                         block number to test on.
  -Z [ --simulate ] arg  Mining test. Used to test hashing speed. Specify the 
                         block number to test on.


Configuration file details:

Place command line options in a file, for example:

  --api-port 40000
  --report-hashrate
  --HWMON 1
  -P
    stratums://0x2ceCE0...b3caa0F6e86.rig0@eth-us-east.flexpool.io:5555
    stratums://0x2ceCE0...b3caa0F6e86.rig0@eth-us-west.flexpool.io:5555
  -v 7 --display-interval 15


Miner reboots:

    The user may create a reboot script that will be invoked
    if ever the miner deems it needs to restart. That can happen
    if requested via the API, or if the miner detects a hung
    GPU. The script is invoked with 1 parameter, 'api_miner_reboot'
    for API reboots, and 'hung_miner_reboot' for hung GPUs

    The script needs a specific file name and must be first in
    the search path.

    For Linux:   reboot.sh

    For Windows: reboot.bat
```

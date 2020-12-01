# root-throughput

A tool to measure what throughput can be expected from ROOT for a given combination of system, dataset and number of threads.

## Usage

```bash
$ root-throughput --trees t --files f1.root f2.root --branches x y z --threads 4

**** Multi-thread test ****
Real time to setup MT run:      0.190665 s
CPU time to setup MT run:       0.18 s
Real time:                      0.490502 s
CPU time:                       0.98 s
Uncompressed data read:         80000000 bytes
Throughput:                     155.543 MB/s
```

If data is stored on a local disk, the operating system might cache all or part of it in memory after the first read. If this is indeed the scenario in which the application will run, no problem. If, in "real life", data is typically only read once in a while and should not be expected to be available in the filesystem cache, consider clearing the cache before running `root-throughput` (e.g., on most Linux systems, by executing `echo 3 > /proc/sys/vm/drop_caches` as a superuser).

### Ok but what do I do with this information?

Right off the bat, `root-throughput` tells you how fast you can provide ROOT data to your analysis logic: it's the last line of its output.
This information might be useful to estimate analysis runtimes, or, if the `Real time` number returned by this tool is significantly lower than what the actual analysis takes, it indicates that runtimes are probably dominated by the analysis' logic itself, and optimizing user logic might result in visible speed improvements.

#### I/O-bound applications

If `Real time` is much higher than `CPU time / number of threads`, then the network or the hardware is not keeping up with ROOT's decompression.
Possible ways to get higher throughput in such a situation are:
- caching/copying just the events and branches you need on a fast local storage
- converting the file to a slower but higher-compression algorithm, so that less bytes per physical event need to travel from storage to memory

For very large number of threads and/or data stored on one or few spinning disks, it could also be interesting to check whether reducing the number of threads results in increased throughput: many concurrent reads at different locations might degrade the performance of the disk.  

#### Decompression-bound applications

If `Real time` is around the same as `CPU time / number of threads` and `Throughput` is lower than what the read-out capability of the storage should be, then ROOT's decompression is not keeping up with the data read-out speed on that system. Converting the file to a faster compression algorithm might improve throughput significantly at the cost of increase dataset size.  

## Building root-throughput

In an environment in which a recent-enough ROOT installation is present (only tested with 6.22, but anything from 6.18 onwards should work):

```bash
$ cd root-throughput/
$ mkdir build && cd build
$ cmake .. && cmake --build . [-- -j4]
$ ./src/root-throughput # runs a couple of tests
```

# ⚠️ root-readspeed is now part of ROOT itself, this repository is not up to date

# root-readspeed

A tool to measure what throughput can be expected from ROOT for a given combination of system, dataset and number of threads.

## Usage

```
root-readspeed --trees tname1 [tname2 ...] --files fname1 [fname2 ...]
               --branches bname1 [bname2 ...] [--threads nthreads]
root-readspeed (--help|-h)
```

### Example

```bash
$ ./src/root-readspeed --trees t --files test1.root test2.root --threads 2 --branches x
Real time to setup MT run:      0.0862889 s
CPU time to setup MT run:       0.09 s
Real time:                      0.172477 s
CPU time:                       0.34 s
Uncompressed data read:         80000000 bytes
Throughput:                     442.343 MB/s
```

### Beware of caching

If data is stored on a local disk, the operating system might cache all or part of it in memory after the first read. If this is indeed the scenario in which the application will run, no problem. If, in "real life", data is typically only read once in a while and should not be expected to be available in the filesystem cache, consider clearing the cache before running `root-readspeed` (e.g., on most Linux systems, by executing `echo 3 > /proc/sys/vm/drop_caches` as a superuser).

## Where is the bottleneck?

Right off the bat, `root-readspeed` tells you how quickly ROOT data can be served to your analysis logic: the last line of its output is the number of *uncompressed* MBytes that could be read per second. This measurement includes time spent in disk or network I/O plus the time spent decompressing the data.

This information might be directly useful to estimate analysis runtimes -- just remember that `Throughput` reports *uncompressed* MBs/s, while ROOT data stored on disk is typically compressed so the *compressed* or on-disk MBs read per second will be around `Throughput / compression_factor` MB/s.

The numbers reported can also be used to figure out what the runtime bottleneck is for a given application:

|If|Then|
|--|----|
|`Real time` is significantly lower than the runtimes of the actual application when reading the same data|[Application logic is the bottleneck](#application-logic-is-the-bottleneck)| 
|`Real time` is much higher than `CPU time / number of cores[1]` |[Raw I/O is the bottleneck](#raw-io-is-the-bottleneck)|
|1. `Real time` is around the same as `CPU time / number of cores[1]` and 2.`Throughput` is lower than what the read-out capability of the storage should be|[Decompression is the bottleneck](#decompression-is-the-bottleneck)|

<sup><sub>
[1] `Number of cores` is the actual number of physical CPU cores used by `root-readspeed` during its run. If no `--threads` argument was passed, this is 1. If the `--threads` argument is lower or equal than the number of available physical cores, then `number of cores` can be assumed equal to `number of threads`. Otherwise, if running with more threads than physical cores or if running in shared environments concurrently to other heavy applications, things get a bit hairy -- the effective `number of cores` in that case would be generally lower than `number of threads`. Running a theoretically-perfectly-scaling, CPU-only application might indicate the effective `number of cores` by means of its scaling behavior.
</sub></sup>

### Application logic is the bottleneck

If the `Real time` number returned by this tool is significantly lower than what the actual analysis takes when running on the same data in the same environment, this indicates that runtimes are probably dominated by the analysis' logic itself, and optimizing this logic might result in visible speed improvements.
Increasing the number of cores might also provide performance benefits if the slow parts of the analysis' logic can be parallelized.

On Linux, tools such as [perf and flamegraphs](http://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html) can be used to inspect where the program spends its CPU cycles.

### Raw I/O is the bottleneck

If `Real time` is much higher than `CPU time / number of threads`, then the network or the hardware is not keeping up with ROOT's decompression.
Possible ways to get higher throughput in such a situation are:
- caching/copying just the events and branches you need on a fast local storage
- converting the file to a slower but higher-compression algorithm, so that less bytes per physical event need to travel from storage to memory

Increasing the number of cores will likely not result in any performance improvement. For very large number of threads and/or data stored on one or few spinning disks, it could also be interesting to check whether *reducing* the number of threads results in increased throughput: many concurrent reads at different locations might degrade the performance of the disk.

### Decompression is the bottleneck

If `Real time` is around the same as `CPU time / number of threads` and `Throughput` is lower than what the read-out capability of the storage should be, then ROOT's decompression is not keeping up with the data read-out speed on that system. Converting the file to a faster compression algorithm might improve throughput significantly at the cost of increase dataset size. Increasing the number of cores should result in almost-ideal scaling.

## Building root-readspeed

In an environment in which a recent-enough ROOT installation is present:

```bash
$ cd root-readspeed/
$ mkdir build && cd build
$ cmake [-DROOTREADSPEED_TESTS=ON] .. && cmake --build . [-- -j4]
$ ./src/root-readspeed --test
```

The tip of the main branch requires ROOT v6.24 or later. Tag [`compiles-with-v6.22`](https://github.com/eguiraud/root-readspeed/tree/compiles-with-v6.22) points to an older revision which works with v6.22.

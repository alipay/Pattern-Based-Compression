# PBC - High-Ratio Compression for Machine-Generated Data
PBC(Pattern-Based Compression) is a fast lossless compression algorithm, which specifically targets patterns in machine-generated data to achieve Pareto-optimality in most cases.  Unlike traditional data block-based methods, PBC compresses data on a per-record basis, facilitating rapid random access. The specific technical details are introduced in paper "High-Ratio Compression for Machine-Generated Data" which has been accepted by [SIGMOD 2024](https://2024.sigmod.org/) and has been published in arxiv: <https://arxiv.org/pdf/2311.13947.pdf>

## Benchmarks
For reference, several fast compression algorithms were tested and compared on a machine with an Intel(R) Xeon(R) Platinum 8369B CPU @ 2.90GHz and 755GB main memory running Linux kernel version 4.19.91. [Android](dataset/Android) and [Apache](dataset/Apache) dataset are logs  and [urls](dataset/urls) dataset contains different urls. Here we show only a subset of our experiments and details can be seen in paper ["High-Ratio Compression for Machine-Generated Data"](https://arxiv.org/pdf/2311.13947.pdf). For a comprehensive set of benchmarks and a detailed analysis of the results, please refer to our paper.

| Dataset name | Compressor Name | Compress Ratio | Compression Speed (MB/s) | Decompression Speed (MB/s)|
| ------------| --------------- | ----------| ---------| ---------|
| Android     | FSST            | 0.576     | 261.78   | 2096.87  |
| Android     | LZ4             | 0.560     | 30.92    | 1282.84  |
| Android     | Zstd            | 0.543     | 18.62    | 284.01   |
| Android     | PBC             | 0.347     | 60.40    | 3231.56  |
| Android     | PBC_FSST        | 0.245     | 53.13    | 1580.05  |
| Apache      | FSST            | 0.322     | 320.72   | 3039.89  |
| Apache      | LZ4             | 0.349     | 31.31    | 1773.38  |
| Apache      | Zstd            | 0.411     | 12.07    | 343.56   |
| Apache      | PBC             | 0.151     | 48.85    | 3140.39  |
| Apache      | PBC_FSST        | 0.104     | 43.32    | 1909.66  |
| urls        | FSST            | 0.413     | 195.89   | 1807.98  |
| urls        | LZ4             | 0.456     | 22.15    | 1247.63  |
| urls        | Zstd            | 0.611     | 11.35    | 158.91   |
| urls        | PBC             | 0.299     | 63.67    | 2029.16  |
| urls        | PBC_FSST        | 0.248     | 55.11    | 1043.43  |

PBC can utilize other compression encoder to further compress the data that has already been compressed by PBC. Currently supported compression algorithms include FSE, FSST, and ZSTD. Depending on the compression algorithm used, they are referred to as PBC_ONLY(only use pbc), PBC_FSE, PBC_FSST, and PBC_ZSTD, respectively.

## Quickstart
Here we give a quick start about how to use pbc. You can refer to the codes in directory example.
1. Build and install PBC
```bash
cd pbc
./build.sh -r       # build pbc, default is Debug version, -r means Release version,
./run_tests.sh      # run pbc tests
./install_pbc.sh    # install pbc library and header file
```

2. Build with pbc library
```bash
cd example
clang++ pbc_train_pattern.cc -L/usr/local/lib/pbc -I/usr/local/include -lpbc -lpbc_fse -lpbc_fsst -lzstd -lhs -lpthread -o pbc_train_pattern
clang++ pbc_compress.cc -L/usr/local/lib/pbc -I/usr/local/include -lpbc -lpbc_fse -lpbc_fsst -lzstd -lhs -lpthread -o pbc_compress
```
Example code ([example/pbc_train_pattern.cc](example/pbc_train_pattern.cc), [example/pbc_compress.cc](example/pbc_compress.cc)) is provided.

3. Run example
```bash
cp ../dataset/Apache Apache
./pbc_train_pattern Apache Apache.pat
./pbc_compress Apache Apache.pat
```

## Terminal tool
Terminal tool supports four operations: --train-pattern, --test-compress, --compress and --decompress.
* Train the pattern
    ```bash
    ./bin/pbc --train-pattern -i dataset/Apache -p dataset/Apache.pat --compress-method pbc_fsst --pattern-size 50 --train-data-number 1000 --train-thread-num 64
    ```
* Test Compress Ratio
    ```bash
    ./bin/pbc --test-compress -i dataset/Apache -p dataset/Apache.pat --compress-method pbc_fsst
    ```
* File Compress
    ```bash
    ./bin/pbc --compress -i dataset/Apache -p dataset/Apache.pat -o dataset/Apache.compress
    ```
* File Decompress
    ```bash
    ./bin/pbc --decompress -i dataset/Apache.compress -p dataset/Apache.pat -o dataset/Apache.origin
    ```

### Detailed parameter usage:
```
Usage: pbc [OPTIONS] [arg [arg ...]]
  --help             Output this help and exit.
  --train-pattern -i <inputFile> -p <patternFile> [--compress-method <pbc_only/pbc_fse/pbc_fsst/pbc_zstd>] [--pattern-size <pattern_size>] [--train-data-number <train_data_number>] [--train-thread-num <train_thread_num>] [--varchar].
  --test-compress -i <inputFile> -p <patternFile> [--compress-method <pbc_only/pbc_fse/pbc_fsst/pbc_zstd>] [--varchar].
  -c/--compress -i <inputFile> -p <patternFile> [-o <outputFile>].
  -d/--decompress -i <inputFile> -p <patternFile> [-o <outputFile>].
  -i <inputFile>           Input File, train-pattern/test-compress(not default), compress/decompress(default: stdin).
  -p <patternFile>         Pattern File, not default.
  -o <outputFile>          Output File, only effected when compress/decompress, default is stdout.
  --compress-method        Compress method, one of pbc_only, pbc_fse, pbc_fsst, pbc_zstd, default is pbc_only.
  --pattern-size           The number of expected generate, default is 20.
  --train-data-number      The number of data used for training pattern, default is 500.
  --train-thread-num       The thread num used for training pattern, default is 16.
  --varchar                Data type of input file, only effected when train-pattern and test-compress, default is Record(split by '\n').

Examples:
  pbc --train-pattern -i inputFile -p patternFile --compress-method pbc_fsst --pattern-size 50 --train-data-number 1000 --train-thread-num 64 --varchar
  pbc --test-compress -i inputFile -p patternFile --compress-method pbc_fsst --varchar
  pbc --compress -i inputFile -p patternFile -o outputFile
  cat inputFile | pbc --compress -p patternFile > outputFile
  pbc --decompress -i inputFile -p patternFile -o outputFile
  cat inputFile | pbc --decompress -p patternFile > outputFile
```

## License

Licensed under the [Apache License, Version 2.0](LICENSE)
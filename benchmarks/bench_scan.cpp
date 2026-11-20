// disktree_bench — times the scan + build + index pipeline.
//
//   disktree_bench <path> [runs=3] [threads...]
//   disktree_bench D:\ 5 1 4 12
//
// For each thread count, runs the pipeline `runs` times and prints the
// median, min and max wall time. The first run of the first thread
// count warms the OS file cache; for a cold-cache number, reboot and
// use runs=1.

#include "disktree/core/builder.hpp"
#include "disktree/core/index.hpp"
#include "disktree/core/scanner.hpp"
#include "disktree/utils/humanize.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        fmt::print("usage: disktree_bench <path> [runs=3] [threads...]\n");
        return 2;
    }
    std::string path = argv[1];
    int runs = argc > 2 ? std::max(1, std::stoi(argv[2])) : 3;

    std::vector<size_t> thread_counts;
    for (int i = 3; i < argc; ++i) thread_counts.push_back(size_t(std::stoul(argv[i])));
    if (thread_counts.empty())
        thread_counts = {1, std::max<size_t>(1, std::thread::hardware_concurrency())};

    namespace hu = disktree::utils;
    fmt::print("Benchmark: {}  ({} runs each)\n\n", path, runs);
    fmt::print("{:>8} {:>12} {:>12} {:>12} {:>12} {:>12}\n",
               "threads", "median", "min", "max", "files", "size");

    for (size_t threads : thread_counts) {
        std::vector<double> times;
        uint64_t files = 0, bytes = 0;
        for (int r = 0; r < runs; ++r) {
            disktree::utils::ProgressTracker progress;
            disktree::Scanner scanner(progress);
            disktree::Scanner::Config cfg;
            cfg.exclude_patterns = disktree::Scanner::default_excludes();
            cfg.n_threads = threads;

            auto t0 = std::chrono::steady_clock::now();
            auto result = scanner.scan(path, cfg);
            disktree::Builder{}.build(result);
            disktree::Index index;
            index.build(result);
            auto t1 = std::chrono::steady_clock::now();

            times.push_back(std::chrono::duration<double>(t1 - t0).count());
            files = result.total_files;
            bytes = result.total_size;
        }
        std::sort(times.begin(), times.end());
        fmt::print("{:>8} {:>12} {:>12} {:>12} {:>12} {:>12}\n", threads,
                   hu::format_duration(times[times.size() / 2]),
                   hu::format_duration(times.front()),
                   hu::format_duration(times.back()),
                   hu::format_count(files), hu::format_bytes(bytes));
    }
    return 0;
}

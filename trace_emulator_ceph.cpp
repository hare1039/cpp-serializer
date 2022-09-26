#include "trace_reader.hpp"
#include "serializer.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <functional>

#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>
#include <random>
#include <chrono>

template<typename Function>
auto record(Function &&f) -> long int
{
    //std::chrono::high_resolution_clock::time_point;
    auto const start = std::chrono::high_resolution_clock::now();
    std::invoke(f);
    auto const now = std::chrono::high_resolution_clock::now();
    auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
    return relativetime;
}

template<typename Iterator>
void stats(Iterator start, Iterator end, std::string const memo = "")
{
    int const size = std::distance(start, end);

    double sum = std::accumulate(start, end, 0.0);
    double mean = sum / size, var = 0;

    for (; start != end; start++)
        var += std::pow((*start) - mean, 2);

    var /= size;
    cout << fmt::format("{0} avg={1  :.3f} sd={2:.3f}", memo, mean, std::sqrt(var));
}

void readtest (int const times, int const bufsize, std::string const filename ,std::string const memo = "")
{
    std::list<double> records;
    for (int i = 0; i < times; i++)
    {
        records.push_back(record([&]() {
            fstream file;
            file.open(fmt::format("/mnt/mycephfs/{}", filename), ios::in);
            for (int i = 0; i < bufsize - 1; i++)
            {
                file << "t";
            }
            file << "\n";
            file.close();
        }));
    }

    // stats(records.begin(), records.end(), fmt::format("read {}", memo));
}

void writetest (int const times, int const bufsize, std::string const filename, std::string const memo = "")
{
    std::list<double> records;
    for (int i = 0; i < times; i++)
    {
        records.push_back(record([&]() {
            fstream file;
            file.open(fmt::format("/mnt/mycephfs/{}", filename));
            for (int i = 0; i < bufsize - 1; i++)
            {
                file << "t";
            }
            file << "\n";
            file.close();
        }));
    }
    // stats(records.begin(), records.end(), fmt::format("write {}", memo));
}

int main(int argc, char const *argv[])
{
    if (argc < 2) {
        printf("usage: trace_emulator <tracefile_path>\n");
    }

    printf("%s\n",argv[1]);
    trace_parser parser(argv[1], 10);

    for (auto row = parser.trace_begin(); row != parser.trace_end(); row++)
    {
        if (row->access_type == read_op) {
            // Ommiting reads for now since the trace reads point to files that do not exist on the FS.
            // readtest(1, row->blob_bytes, row->blob_name, "");
            continue;
        } else {
            writetest(1, row->blob_bytes, row->blob_name, "");
        }
    }
    return 0;
}

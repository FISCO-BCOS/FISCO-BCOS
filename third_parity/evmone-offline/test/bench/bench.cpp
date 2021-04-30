// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include <benchmark/benchmark.h>
#include <evmc/evmc.hpp>
#include <evmc/loader.h>
#include <evmone/analysis.hpp>
#include <evmone/evmone.h>
#include <test/utils/utils.hpp>

#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>


#if HAVE_STD_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#else
#include "filesystem.hpp"
namespace fs = ghc::filesystem;
#endif

using namespace benchmark;

namespace
{
constexpr auto gas_limit = std::numeric_limits<int64_t>::max();
auto vm = evmc::VM{};

constexpr auto inputs_extension = ".inputs";

inline evmc::result execute(bytes_view code, bytes_view input) noexcept
{
    auto msg = evmc_message{};
    msg.gas = gas_limit;
    msg.input_data = input.data();
    msg.input_size = input.size();
    return vm.execute(EVMC_CONSTANTINOPLE, msg, code.data(), code.size());
}

void execute(State& state, bytes_view code, bytes_view input) noexcept
{
    auto total_gas_used = int64_t{0};
    auto iteration_gas_used = int64_t{0};
    for (auto _ : state)
    {
        auto r = execute(code, input);
        iteration_gas_used = gas_limit - r.gas_left;
        total_gas_used += iteration_gas_used;
    }
    state.counters["gas_used"] = Counter(static_cast<double>(iteration_gas_used));
    state.counters["gas_rate"] = Counter(static_cast<double>(total_gas_used), Counter::kIsRate);
}

void analyse(State& state, bytes_view code) noexcept
{
    auto bytes_analysed = uint64_t{0};
    for (auto _ : state)
    {
        auto r = evmone::analyze(EVMC_PETERSBURG, code.data(), code.size());
        DoNotOptimize(r);
        bytes_analysed += code.size();
    }
    state.counters["size"] = Counter(static_cast<double>(code.size()));
    state.counters["rate"] = Counter(static_cast<double>(bytes_analysed), Counter::kIsRate);
}

struct benchmark_case
{
    std::shared_ptr<bytes> code;
    bytes input;
    bytes expected_output;

    void operator()(State& state) noexcept
    {
        {
            auto r = execute(*code, input);
            if (r.status_code != EVMC_SUCCESS)
            {
                state.SkipWithError(("failure: " + std::to_string(r.status_code)).c_str());
                return;
            }

            if (!expected_output.empty())
            {
                auto output = bytes_view{r.output_data, r.output_size};
                if (output != expected_output)
                {
                    auto error = "got: " + hex(output) + "  expected: " + hex(expected_output);
                    state.SkipWithError(error.c_str());
                    return;
                }
            }
        }

        execute(state, *code, input);
    }
};


void load_benchmark(const fs::path& path, const std::string& name_prefix)
{
    const auto base_name = name_prefix + path.stem().string();

    std::ifstream file{path};
    std::string code_hex{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

    code_hex.erase(
        std::remove_if(code_hex.begin(), code_hex.end(), [](auto x) { return std::isspace(x); }),
        code_hex.end());

    auto code = std::make_shared<bytes>(from_hex(code_hex));

    RegisterBenchmark((base_name + "/analysis").c_str(), [code](State& state) {
        analyse(state, *code);
    })->Unit(kMicrosecond);

    enum class state
    {
        name,
        input,
        expected_output
    };

    auto base = benchmark_case{};
    base.code = std::move(code);

    auto inputs_path = path;
    inputs_path.replace_extension(inputs_extension);
    if (!fs::exists(inputs_path))
    {
        RegisterBenchmark(base_name.c_str(), base)->Unit(kMicrosecond);
    }
    else
    {
        auto st = state::name;
        auto inputs_file = std::ifstream{inputs_path};
        auto input = benchmark_case{};
        auto name = std::string{};
        for (std::string l; std::getline(inputs_file, l);)
        {
            switch (st)
            {
            case state::name:
                if (l.empty())
                    continue;
                input = base;
                name = base_name + '/' + std::move(l);
                st = state::input;
                break;

            case state::input:
                input.input = from_hexx(l);
                st = state::expected_output;
                break;

            case state::expected_output:
                input.expected_output = from_hexx(l);
                RegisterBenchmark(name.c_str(), input)->Unit(kMicrosecond);
                st = state::name;
                break;
            }
        }
    }
}

void load_benchmarks_from_dir(const fs::path& path, const std::string& name_prefix = {})
{
    std::vector<fs::path> subdirs;
    std::vector<fs::path> files;

    for (auto& e : fs::directory_iterator{path})
    {
        if (e.is_directory())
            subdirs.emplace_back(e);
        else if (e.path().extension() != inputs_extension)
            files.emplace_back(e);
    }

    std::sort(std::begin(subdirs), std::end(subdirs));
    std::sort(std::begin(files), std::end(files));

    for (const auto& f : files)
        load_benchmark(f, name_prefix);

    for (const auto& d : subdirs)
        load_benchmarks_from_dir(d, name_prefix + d.filename().string() + '/');
}

/// The error code for CLI arguments parsing error in evmone-bench.
/// The number tries to be different from EVMC loading error codes.
constexpr auto cli_parsing_error = -3;


/// Parses evmone-bench CLI arguments and registers benchmark cases.
///
/// The following variants of number arguments are supported (including argv[0]):
///
/// 2: evmone-bench benchmarks_dir
///    Uses evmone VM, loads all benchmarks from benchmarks_dir.
/// 3: evmone-bench evmc_config benchmarks_dir
///    The same as (2) but loads custom EVMC VM.
/// 4: evmone-bench code_hex_file input_hex expected_output_hex.
///    Uses evmone VM, registers custom benchmark with the code from the given file,
///    and the given input. The benchmark will compare the output with the provided
///    expected one.
int parseargs(int argc, char** argv)
{
    // Arguments' placeholders:
    const char* evmc_config{};
    const char* benchmarks_dir{};
    const char* code_hex_file{};
    const char* input_hex{};
    const char* expected_output_hex{};

    if (argc == 2)
    {
        benchmarks_dir = argv[1];
    }
    else if (argc == 3)
    {
        evmc_config = argv[1];
        benchmarks_dir = argv[2];
    }
    else if (argc == 4)
    {
        code_hex_file = argv[1];
        input_hex = argv[2];
        expected_output_hex = argv[3];
    }
    else
        return cli_parsing_error;  // Incorrect number of arguments.


    if (evmc_config)
    {
        auto ec = evmc_loader_error_code{};
        vm = evmc::VM{evmc_load_and_configure(evmc_config, &ec)};

        if (ec != EVMC_LOADER_SUCCESS)
        {
            if (const auto error = evmc_last_error_msg())
                std::cerr << "EVMC loading error: " << error << "\n";
            else
                std::cerr << "EVMC loading error " << ec << "\n";
            return static_cast<int>(ec);
        }

        std::cout << "Benchmarking " << evmc_config << "\n\n";
    }
    else
    {
        vm = evmc::VM{evmc_create_evmone()};
        std::cout << "Benchmarking evmone\n\n";
    }

    if (benchmarks_dir)
    {
        load_benchmarks_from_dir(benchmarks_dir);
    }
    else
    {
        std::ifstream file{code_hex_file};
        std::string code_hex{
            std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        code_hex.erase(std::remove_if(code_hex.begin(), code_hex.end(),
                           [](auto x) { return std::isspace(x); }),
            code_hex.end());

        auto b = benchmark_case{};
        b.code = std::make_shared<bytes>(from_hex(code_hex));
        b.input = from_hex(input_hex);
        b.expected_output = from_hex(expected_output_hex);
        RegisterBenchmark(code_hex_file, b)->Unit(kMicrosecond);
    }
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    try
    {
        Initialize(&argc, argv);

        const auto ec = parseargs(argc, argv);

        if (ec == cli_parsing_error && ReportUnrecognizedArguments(argc, argv))
            return ec;

        if (ec != 0)
            return ec;

        RunSpecifiedBenchmarks();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return -1;
    }
}

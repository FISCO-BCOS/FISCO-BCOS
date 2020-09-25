#include "libmetering/GasInjector.h"
#include "src/common.h"
#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>

using namespace std;

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        cerr << "The number of parameters not equal to 2" << endl;
        return 0;
    }
    std::vector<uint8_t> file_data;
    auto result = wabt::ReadFile(argv[1], &file_data);
    boost::filesystem::path p(argv[1]);
    if (Succeeded(result))
    {
        wasm::GasInjector injector(wasm::GetInstructionTable());
        auto ret = injector.InjectMeter(file_data);
        if (ret.status == wasm::GasInjector::Status::Success)
        {
            ofstream out("metric_" + p.filename().generic_string(),
                std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
            out.write((const char*)ret.byteCode->data(), ret.byteCode->size());
            out.close();
            cout << "InjectMeter success" << endl;
            return 0;
        }
        cerr << "InjectMeter failed, reason:"
             << (ret.status == wasm::GasInjector::Status::InvalidFormat ? "invalid format" :
                                                                          "bad instruction")
             << endl;
    }
    cerr << "Read file failed" << endl;
    return -1;
}
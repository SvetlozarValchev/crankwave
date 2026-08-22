#include "parity_driver.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kOutputCapacity = UINT32_C(4) * 1024U * 1024U;

[[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    const std::string bytes{std::istreambuf_iterator<char>{stream},
                            std::istreambuf_iterator<char>{}};
    return {bytes.begin(), bytes.end()};
}

void write_bytes(const std::filesystem::path &path,
                 const std::vector<std::uint8_t> &bytes) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        throw std::runtime_error{"could not open output " + path.string()};
    }
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error{"could not write output " + path.string()};
    }
}

[[nodiscard]] const std::uint8_t *
data_or_null(const std::vector<std::uint8_t> &value) noexcept {
    return value.empty() ? nullptr : value.data();
}

[[nodiscard]] std::uint32_t extent(const std::vector<std::uint8_t> &value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error{"parity input exceeds the fixed-width adapter"};
    }
    return static_cast<std::uint32_t>(value.size());
}

} // namespace

int main(const int argc, const char *const *argv) {
    try {
        if (argc != 8) {
            throw std::runtime_error{
                "usage: native-parity <engine.json> <scenario.json> "
                "<ir-id> <ir.wav> <accessory-id> <accessory.json> <output>"};
        }
        const auto engine = read_bytes(argv[1]);
        const auto scenario = read_bytes(argv[2]);
        const std::string ir_id = argv[3];
        const auto ir = read_bytes(argv[4]);
        const std::string accessory_id = argv[5];
        const auto accessory = read_bytes(argv[6]);

        std::vector<std::uint8_t> output(kOutputCapacity);
        std::uint32_t output_size = 0;
        const auto status = crankwave_wasm_parity_run(
            data_or_null(engine), extent(engine), data_or_null(scenario),
            extent(scenario), reinterpret_cast<const std::uint8_t *>(ir_id.data()),
            static_cast<std::uint32_t>(ir_id.size()), data_or_null(ir), extent(ir),
            reinterpret_cast<const std::uint8_t *>(accessory_id.data()),
            static_cast<std::uint32_t>(accessory_id.size()), data_or_null(accessory),
            extent(accessory), output.data(), extent(output), &output_size);
        if (status != 0U) {
            throw std::runtime_error{"parity driver failed with status " +
                                     std::to_string(status)};
        }
        output.resize(output_size);
        write_bytes(argv[7], output);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "native parity failure: " << error.what() << '\n';
        return 1;
    }
}

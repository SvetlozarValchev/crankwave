#include "dsp/fixed_fft.hpp"
#include "dsp/static_ir_conversion.hpp"
#include "presentation/pcm16_ir_decoder.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) throw std::runtime_error{"cannot open " + path.string()};
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error{"cannot size " + path.string()};
    std::vector<std::byte> result(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(result.data()),
               static_cast<std::streamsize>(result.size()));
    if (!input) throw std::runtime_error{"cannot read " + path.string()};
    return result;
}

void write_u64le(std::ofstream &output, std::uint64_t value) {
    std::byte bytes[8]{};
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::byte>((value >> (8U * index)) & 0xffU);
    }
    output.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

} // namespace

int main(int argc, char **argv) try {
    if (argc != 4) {
        std::cerr << "usage: dump-ir-spectrum IR.wav configured-gain "
                     "spectrum-complex-f64le.bin\n";
        return 2;
    }
    char *gain_end = nullptr;
    const double configured_gain = std::strtod(argv[2], &gain_end);
    if (gain_end == argv[2] || *gain_end != '\0') {
        throw std::runtime_error{"configured gain is not a decimal number"};
    }
    const auto ir_bytes = read_bytes(argv[1]);
    const auto decoded_result =
        engine_sim_offline::presentation::decode_pcm16_ir_wave(ir_bytes);
    const auto *decoded =
        std::get_if<engine_sim_offline::presentation::DecodedPcm16Ir>(
            &decoded_result);
    if (decoded == nullptr) throw std::runtime_error{"IR decoder rejected fixture"};
    auto coefficients = engine_sim_offline::dsp::convert_static_ir(
        decoded->samples, decoded->meaningful_support_frames, configured_gain);
    coefficients.resize(
        engine_sim_offline::dsp::FixedConvolutionKernel::coefficient_count,
        0.0);
    const engine_sim_offline::dsp::FixedConvolutionKernel kernel{coefficients};
    std::ofstream output{argv[3], std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error{"cannot create spectrum output"};
    for (const auto value : kernel.spectrum()) {
        write_u64le(output, std::bit_cast<std::uint64_t>(value.real()));
        write_u64le(output, std::bit_cast<std::uint64_t>(value.imag()));
    }
    if (!output) throw std::runtime_error{"cannot write spectrum output"};
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}

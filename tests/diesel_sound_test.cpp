#include "crankwave/artifacts/crankwave_package.hpp"
#include "crankwave/authoring/json.hpp"
#include "diesel/diesel_sound.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <stop_token>

namespace {
void expect(bool value, const char *message) {
    if (!value)
        throw std::runtime_error{message};
}
void replace(std::string &source, std::string_view from, std::string_view to) {
    const auto at = source.find(from);
    expect(at != std::string::npos, "fixture replacement source missing");
    source.replace(at, from.size(), to);
}
} // namespace
int main() {
    try {
        std::ifstream file{CRANKWAVE_DIESEL_EXAMPLE_PATH};
        expect(file.good(), "diesel example is readable");
        const std::string source{std::istreambuf_iterator<char>{file}, {}};
        const auto parsed = crankwave::diesel::parse_sound_definition(source);
        expect(std::holds_alternative<crankwave::diesel::SoundDefinition>(parsed),
               "tracked diesel definition compiles");
        const auto &definition = std::get<crankwave::diesel::SoundDefinition>(parsed);
        expect(definition.rpms.size() == 8 && definition.fueling.size() == 6,
               "example declares full grid");
        expect(definition.runtime_version == 2 &&
                   definition.pulses.front().minimum_duration_fraction_01 == 0.08,
               "example requests articulated radiation and metered injection");
        expect(definition.cylinder.gas_substeps == 32,
               "sound capture uses refined gas integration");
        for (const auto &[from, to] :
             {std::pair{"\"minimum_duration_fraction_01\": 0.08",
                        "\"minimum_duration_fraction_01\": 0"},
              std::pair{"\"block_highpass_hz\": 450", "\"block_highpass_hz\": -1"},
              std::pair{"\"bore_m\"", "\"unknown_bore\""},
              std::pair{"\"compression_ratio\": 18", "\"compression_ratio\": 1"},
              std::pair{"\"full_fuel_mass_kg\": 3e-05", "\"full_fuel_mass_kg\": -1"},
              std::pair{"\"frequency_hz\": 680", "\"frequency_hz\": 100000"},
              std::pair{"\"schema\": \"crankwave/engine\",",
                        "\"schema\": \"crankwave/engine\", \"schema\": "
                        "\"crankwave/engine\","}}) {
            auto invalid = source;
            replace(invalid, from, to);
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)),
                   "malformed model rejected");
        }
        auto reduced = source;
        const auto rpms_begin = reduced.find("\"rpms\": [");
        const auto rpms_end = reduced.find(']', rpms_begin);
        reduced.replace(rpms_begin, rpms_end - rpms_begin + 1, "\"rpms\": [800, 1600]");
        const auto fuels_begin = reduced.find("\"fueling\": [");
        const auto fuels_end = reduced.find(']', fuels_begin);
        reduced.replace(fuels_begin, fuels_end - fuels_begin + 1,
                        "\"fueling\": [0, 1]");
        replace(reduced, "\"samples_per_cycle\": 8192", "\"samples_per_cycle\": 2048");
        std::stop_source stopped;
        stopped.request_stop();
        const auto cancelled =
            crankwave::diesel::bake_sound(reduced, "{}", stopped.get_token());
        expect(std::holds_alternative<crankwave::diesel::SoundError>(cancelled) &&
                   std::get<crankwave::diesel::SoundError>(cancelled).cancelled,
               "cancelled bake returns no material");
        const auto first = crankwave::diesel::bake_sound(reduced, "{}");
        if (const auto *error = std::get_if<crankwave::diesel::SoundError>(&first))
            throw std::runtime_error{error->message};
        const auto second = crankwave::diesel::bake_sound(reduced, "{}");
        expect(std::holds_alternative<crankwave::diesel::BakedSound>(second),
               "repeated bake succeeds");
        const auto &material = std::get<crankwave::diesel::BakedSound>(first);
        expect(material.cell_count == 4 && material.members.size() == 4,
               "bake produces complete carrier members");
        expect(material.members ==
                   std::get<crankwave::diesel::BakedSound>(second).members,
               "identical model produces identical source bytes");
        std::vector<crankwave::artifacts::CrankwavePackEntry> entries;
        for (const auto &member : material.members)
            entries.push_back({member.path, member.bytes});
        expect(std::holds_alternative<crankwave::artifacts::CrankwavePackageDescriptor>(
                   crankwave::artifacts::validate_crankwave_package_tree(entries)),
               "native descriptor and payload hashes bind the diesel runtime");
        const auto &runtime = material.members[2].bytes;
        auto document = crankwave::authoring::parse_json(
            {reinterpret_cast<const char *>(runtime.data()), runtime.size()});
        expect(std::holds_alternative<crankwave::authoring::JsonDocument>(document),
               "runtime manifest is valid JSON");
        const auto cells =
            std::get<crankwave::authoring::JsonDocument>(document).root().find("cells");
        expect(cells.at(0).find("manifold_pressure_pa_abs").number() ==
                   cells.at(1).find("manifold_pressure_pa_abs").number(),
               "fuel cells preserve equal actual intake pressure");
        expect(*cells.at(1).find("indicated_work_j").number() >
                   *cells.at(0).find("indicated_work_j").number(),
               "direct injection changes cycle work");
        const auto ural_path = std::filesystem::path{CRANKWAVE_DIESEL_EXAMPLE_PATH}
                                   .parent_path()
                                   .parent_path() /
                               "ural4320-mechanical-v8" / "engine.json";
        std::ifstream ural_file{ural_path};
        expect(ural_file.good(), "Ural example is readable");
        const std::string ural_source{std::istreambuf_iterator<char>{ural_file}, {}};
        const auto ural = crankwave::diesel::parse_sound_definition(ural_source);
        expect(std::holds_alternative<crankwave::diesel::SoundDefinition>(ural),
               "Ural bank/exhaust model compiles");
        const auto &ural_definition =
            std::get<crankwave::diesel::SoundDefinition>(ural);
        expect(ural_definition.runtime_version == 11 &&
                   ural_definition.cylinder.bore_m == 0.13 &&
                   ural_definition.cylinder.stroke_m == 0.14 &&
                   ural_definition.cylinder.compression_ratio == 16.5,
               "Ural example preserves the selected V8 geometry and radiation version");
        for (const auto &[from, to] :
             {std::pair{"\"reflection\": -0.65", "\"reflection\": 1"},
              std::pair{"\"delay_s\": 0.0042", "\"delay_s\": 0"},
              std::pair{"\"right-bank\"", "\"left-bank\""},
              std::pair{"\"decay_s\": 0.018", "\"decay_s\": 0"},
              std::pair{"\"seed\": 238650", "\"seed\": 1.5"},
              std::pair{"\"valve_gain_pcm\": 0.14", "\"valve_gain_pcm\": -1"},
              std::pair{"\"valve_decay_s\": 0.0018", "\"valve_decay_s\": 1"},
              std::pair{"\"valve_frequency_hz\": 650", "\"valve_frequency_hz\": 1"},
              std::pair{"\"pumping_gain_pcm\": 0.012", "\"pumping_gain_pcm\": 1"},
              std::pair{"\"pumping_lowpass_hz\": 850", "\"pumping_lowpass_hz\": 1"},
              std::pair{"\"seed\": 4320725", "\"seed\": 0.5"},
              std::pair{"\"valve_attack_s\": 0.0012", "\"valve_attack_s\": 0"},
              std::pair{"\"fueling_threshold_01\": 0.2", "\"fueling_threshold_01\": 0"},
              std::pair{"\"gain\": 0.16", "\"gain\": 2"},
              std::pair{"\"lowpass_hz\": 550", "\"lowpass_hz\": 10"},
              std::pair{"\"transition_s\": 0.04", "\"transition_s\": 0"},
              std::pair{"\"bass_gain\": 0.18", "\"bass_gain\": -1"},
              std::pair{"\"movement_gain\": 0.48", "\"movement_gain\": 2"},
              std::pair{"\"crossover_hz\": 180", "\"crossover_hz\": 1"},
              std::pair{"\"mechanical_gain\": 1.6", "\"mechanical_gain\": 3"},
              std::pair{"\"gain_pcm_per_kg_s\": 0.4", "\"gain_pcm_per_kg_s\": 5"},
              std::pair{"\"highpass_hz\": 250", "\"highpass_hz\": 0"},
              std::pair{"\"lowpass_hz\": 1800", "\"lowpass_hz\": 4000"},
              std::pair{"\"envelope_s\": 0.002", "\"envelope_s\": 0"},
              std::pair{"\"seed\": 432008", "\"seed\": 0.5"},
              std::pair{"\"coasting_balance\"", "\"missing_coasting_balance\""},
              std::pair{"\"decay_s\": 0.012", "\"decay_s\": 0"},
              std::pair{"\"attack_s\": 0.004", "\"attack_s\": 0"},
              std::pair{"\"secondary_bank_gain\": 0.55", "\"secondary_bank_gain\": 2"},
              std::pair{"\"fueling_start_01\": 0.2", "\"fueling_start_01\": 0"},
              std::pair{"\"fueling_full_01\": 0.5", "\"fueling_full_01\": 0.2"}}) {
            auto invalid = ural_source;
            replace(invalid, from, to);
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)),
                   "malformed acoustic parameters rejected natively");
        }
        std::cout << "diesel definition, deterministic settled capture, cancellation "
                     "and carrier binding passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

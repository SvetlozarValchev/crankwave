#include "crankwave/artifacts/crankwave_package.hpp"
#include "crankwave/authoring/json.hpp"
#include "diesel/diesel_sound.hpp"

#include <filesystem>
#include <bit>
#include <cmath>
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
              std::pair{"\"fueling_start_01\": 0.2", "\"fueling_start_01\": -0.01"},
              std::pair{"\"fueling_full_01\": 0.5", "\"fueling_full_01\": 0.2"}}) {
            auto invalid = ural_source;
            replace(invalid, from, to);
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)),
                   "malformed acoustic parameters rejected natively");
        }
        // An opt-in boundary grid must affect native charge capture, while
        // preserving the legacy two-channel sources where boundaries agree.
        auto compact_ural = ural_source;
        for (const auto &[key, value] :
             {std::pair{"rpms", "[800, 1600]"},
              std::pair{"fueling", "[0, 1]"}}) {
            const auto begin = compact_ural.find(std::string{"\""} + key + "\": [");
            const auto end = compact_ural.find(']', begin);
            compact_ural.replace(begin, end - begin + 1,
                                 std::string{"\""} + key + "\": " + value);
        }
        replace(compact_ural, "\"samples_per_cycle\": 8192", "\"samples_per_cycle\": 2048");
        std::string boundary_map = "\"operating_boundaries\":[";
        for (int i = 0; i < 4; ++i) {
            if (i) boundary_map += ',';
            boundary_map += "{\"rpm\":" + std::to_string(i < 2 ? 800 : 1600) +
                ",\"fueling_01\":" + std::to_string(i % 2) +
                ",\"intake\":{\"pressure_pa_abs\":" +
                std::to_string(i == 3 ? 210000 : 101325) +
                ",\"temperature_k\":320,\"oxygen_mass_fraction\":0.23},"
                "\"exhaust\":{\"pressure_pa_abs\":105000,\"temperature_k\":700,"
                "\"oxygen_mass_fraction\":0.08}}";
        }
        boundary_map += "],";
        auto mapped_source = compact_ural;
        replace(mapped_source, "\"cylinders\": [", boundary_map + "\"cylinders\": [");
        const auto mapped_definition = crankwave::diesel::parse_sound_definition(mapped_source);
        expect(std::holds_alternative<crankwave::diesel::SoundDefinition>(mapped_definition) &&
                   std::get<crankwave::diesel::SoundDefinition>(mapped_definition).runtime_version == 12,
               "mapped boundaries explicitly select version 12");
        for (const auto &[from, to] :
             {std::pair{"\"rpm\":1600", "\"rpm\":800"},
              std::pair{"\"pressure_pa_abs\":210000", "\"pressure_pa_abs\":400000"},
              std::pair{"\"fueling_01\":1", "\"fueling_01\":0"},
              std::pair{"\"rpms\": [800, 1600]", "\"rpms\": [800, 1600, 2000]"}}) {
            auto invalid = mapped_source;
            replace(invalid, from, to);
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)),
                   "incomplete, reordered or out-of-range boundary maps rejected");
        }
        const auto plain_result = crankwave::diesel::bake_sound(compact_ural, "{}");
        const auto mapped_result = crankwave::diesel::bake_sound(mapped_source, "{}");
        expect(std::holds_alternative<crankwave::diesel::BakedSound>(plain_result) &&
                   std::holds_alternative<crankwave::diesel::BakedSound>(mapped_result),
               "mapped and legacy native captures settle");
        const auto &plain_bytes = std::get<crankwave::diesel::BakedSound>(plain_result).members[3].bytes;
        const auto &mapped_bytes = std::get<crankwave::diesel::BakedSound>(mapped_result).members[3].bytes;
        auto inertial_source = mapped_source;
        replace(inertial_source, "\"loaded_pressure_texture\":",
                "\"inline_inertia\":{\"reciprocating_mass_kg\":0.45,\"frequency_hz\":110,"
                "\"q\":2.5,\"gain_pcm_per_n\":0.0004,\"highpass_hz\":140,\"lowpass_hz\":500},"
                "\"loaded_pressure_texture\":");
        const auto inertial_definition = crankwave::diesel::parse_sound_definition(inertial_source);
        expect(std::holds_alternative<crankwave::diesel::SoundDefinition>(inertial_definition) &&
                   std::get<crankwave::diesel::SoundDefinition>(inertial_definition).runtime_version == 14,
               "inline inertial radiation explicitly selects version 14");
        for (const auto &[from, to] :
             {std::pair{"\"reciprocating_mass_kg\":0.45", "\"reciprocating_mass_kg\":0"},
              std::pair{"\"frequency_hz\":110", "\"frequency_hz\":0"},
              std::pair{"\"lowpass_hz\":500", "\"lowpass_hz\":200"}}) {
            auto invalid = inertial_source;
            replace(invalid, from, to);
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)),
                   "invalid inertia authoring is rejected before baking");
        }
        const auto inertial_result = crankwave::diesel::bake_sound(inertial_source, "{}");
        expect(std::holds_alternative<crankwave::diesel::BakedSound>(inertial_result),
               "inertial radiation carrier bakes");
        const auto &inertial_members = std::get<crankwave::diesel::BakedSound>(inertial_result).members;
        expect(inertial_members[3].bytes == mapped_bytes,
               "kinematic inertia leaves all captured gas forces and flows byte-exact");
        const std::string inertial_manifest(reinterpret_cast<const char *>(inertial_members[2].bytes.data()),
                                            inertial_members[2].bytes.size());
        auto inertial_document = crankwave::authoring::parse_json(inertial_manifest);
        expect(std::holds_alternative<crankwave::authoring::JsonDocument>(inertial_document),
               "inertia manifest is valid JSON");
        const auto geometry = std::get<crankwave::authoring::JsonDocument>(inertial_document)
                                  .root().find("inertial_geometry");
        const auto &inertial_cylinder =
            std::get<crankwave::diesel::SoundDefinition>(inertial_definition).cylinder;
        expect(geometry.find("crank_radius_m").number() == inertial_cylinder.stroke_m / 2 &&
                   geometry.find("connecting_rod_length_m").number() == inertial_cylinder.connecting_rod_length_m,
               "native inertia geometry is derived from the cylinder, not independently authored");
        const std::string clatter_json = R"({"rpm_start":800,"rpm_full":1400,"fueling_start_01":0.18,"fueling_full_01":0.55,"attack_s":0.0003,"decay_s":0.0025,"transition_s":0.04,"coherent_gain":1,"stochastic_gain":2,"highpass_hz":450,"lowpass_hz":5500,"seed":15983226,"modes":[{"frequency_hz":650,"q":3,"gain_pcm_per_n":0.00007}]})";
        auto clatter_source = inertial_source;
        replace(clatter_source, "\"inline_inertia\":",
                "\"loaded_clatter\":" + clatter_json + ",\"inline_inertia\":");
        const auto clatter_definition = crankwave::diesel::parse_sound_definition(clatter_source);
        expect(std::holds_alternative<crankwave::diesel::SoundDefinition>(clatter_definition) &&
                   std::get<crankwave::diesel::SoundDefinition>(clatter_definition).runtime_version == 15,
               "fast loaded combustion radiation explicitly selects version 15");
        for (const auto &[from, to] :
             {std::pair{"\"rpm_full\":1400", "\"rpm_full\":800"},
              std::pair{"\"fueling_full_01\":0.55", "\"fueling_full_01\":0.18"},
              std::pair{"\"attack_s\":0.0003", "\"attack_s\":0"},
              std::pair{"\"seed\":15983226", "\"seed\":0.5"},
              std::pair{"\"gain_pcm_per_n\":0.00007", "\"gain_pcm_per_n\":0.1"},
              std::pair{"\"frequency_hz\":650", "\"frequency_hz\":0"}}) {
            auto invalid = clatter_source;
            replace(invalid, from, to);
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)),
                   "malformed loaded-clatter parameters rejected before capture");
        }
        auto incomplete_clatter = mapped_source;
        replace(incomplete_clatter, "\"loaded_pressure_texture\":",
                "\"loaded_clatter\":" + clatter_json + ",\"loaded_pressure_texture\":");
        expect(std::holds_alternative<crankwave::diesel::SoundError>(
                   crankwave::diesel::parse_sound_definition(incomplete_clatter)),
               "loaded clatter requires the complete version 14 presentation");
        const auto clatter_result = crankwave::diesel::bake_sound(clatter_source, "{}");
        expect(std::holds_alternative<crankwave::diesel::BakedSound>(clatter_result),
               "loaded clatter carrier bakes");
        const auto &clatter_members = std::get<crankwave::diesel::BakedSound>(clatter_result).members;
        expect(clatter_members[3].bytes == mapped_bytes,
               "fast clatter leaves captured force, flow and motored sources byte-exact");
        const std::string clatter_manifest(reinterpret_cast<const char *>(clatter_members[2].bytes.data()),
                                           clatter_members[2].bytes.size());
        expect(clatter_manifest.find("pressure-force-modal-radiation-v15") != std::string::npos,
               "native clatter manifest binds the new runtime method");
        const auto binding_document = crankwave::authoring::parse_json(clatter_source);
        const auto native_bindings = std::get<crankwave::authoring::JsonDocument>(binding_document)
                                         .root().find("engine").find("cylinders");
        std::string coupling = "[";
        for (std::size_t i = 0; i < native_bindings.size(); ++i) {
            if (i) coupling += ",";
            coupling += "{\"id\":\"" + std::string{*native_bindings.at(i).find("id").string()} +
                        "\",\"gain\":0.75}";
        }
        coupling += "]";
        auto coupled_source = clatter_source;
        replace(coupled_source, "\"gain_pcm_per_n\":0.00007}",
                "\"gain_pcm_per_n\":0.00007,\"cylinder_coupling\":" + coupling + "}");
        const auto coupled_definition = crankwave::diesel::parse_sound_definition(coupled_source);
        expect(std::holds_alternative<crankwave::diesel::SoundDefinition>(coupled_definition) &&
                   std::get<crankwave::diesel::SoundDefinition>(coupled_definition).runtime_version == 16,
               "per-mode cylinder coupling explicitly selects version 16");
        auto unknown_coupling = coupling;
        const auto first_id = std::string{*native_bindings.at(0).find("id").string()};
        replace(unknown_coupling, first_id, "unknown-cylinder");
        auto duplicate_coupling = coupling;
        replace(duplicate_coupling, std::string{*native_bindings.at(1).find("id").string()}, first_id);
        for (const auto &bad : {std::string{"[]"}, unknown_coupling, duplicate_coupling}) {
            auto invalid = coupled_source;replace(invalid, coupling, bad);
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)),
                   "missing, unknown or duplicate mode cylinder bindings are rejected");
        }
        for (const auto bad_gain : {"2.01", "-2.01"}) {
            auto invalid = coupled_source;replace(invalid, "\"gain\":0.75", "\"gain\":" + std::string{bad_gain});
            expect(std::holds_alternative<crankwave::diesel::SoundError>(
                       crankwave::diesel::parse_sound_definition(invalid)), "coupling gains are bounded");
        }
        auto mixed_source = coupled_source;
        replace(mixed_source, "\"cylinder_coupling\":" + coupling + "}",
                "\"cylinder_coupling\":" + coupling +
                "},{\"frequency_hz\":2000,\"q\":2,\"gain_pcm_per_n\":0.00001}");
        expect(std::holds_alternative<crankwave::diesel::SoundError>(
                   crankwave::diesel::parse_sound_definition(mixed_source)), "mixed mode formats are rejected");
        const auto coupled_result = crankwave::diesel::bake_sound(coupled_source, "{}");
        expect(std::holds_alternative<crankwave::diesel::BakedSound>(coupled_result), "coupled carrier bakes");
        const auto &coupled_members = std::get<crankwave::diesel::BakedSound>(coupled_result).members;
        expect(coupled_members[3].bytes == mapped_bytes, "mode coupling preserves all native sources exactly");
        const std::string coupled_manifest(reinterpret_cast<const char *>(coupled_members[2].bytes.data()),
                                           coupled_members[2].bytes.size());
        expect(coupled_manifest.find("pressure-force-modal-radiation-v16") != std::string::npos,
               "coupled carrier binds version 16");
        for (const auto &source : {compact_ural, mapped_source}) {
            auto idle_source = source;
            replace(idle_source, "\"fueling_start_01\": 0.2", "\"fueling_start_01\": 0");
            replace(idle_source, "\"fueling_full_01\": 0.5", "\"fueling_full_01\": 0.12");
            const auto parsed_idle = crankwave::diesel::parse_sound_definition(idle_source);
            expect(std::holds_alternative<crankwave::diesel::SoundDefinition>(parsed_idle) &&
                       std::get<crankwave::diesel::SoundDefinition>(parsed_idle).runtime_version == 13,
                   "idle rounding selects version 13 with or without a boundary map");
            const auto idle_result = crankwave::diesel::bake_sound(idle_source, "{}");
            expect(std::holds_alternative<crankwave::diesel::BakedSound>(idle_result),
                   "idle rounding native sources settle");
            const auto &idle_bytes = std::get<crankwave::diesel::BakedSound>(idle_result).members[3].bytes;
            expect(idle_bytes.size() == mapped_bytes.size(), "version 13 always carries motored force");
            if (source == mapped_source)
                expect(idle_bytes == mapped_bytes, "idle radiation leaves all mapped native samples exact");
            else
                for (std::size_t i = 0; i < 4 * 2048; ++i)
                    for (std::size_t b = 0; b < 8; ++b)
                        expect(idle_bytes[i * 12 + b] == plain_bytes[i * 8 + b],
                               "idle radiation leaves legacy force and flow samples exact");
        }
        expect(mapped_bytes.size() == plain_bytes.size() / 2 * 3,
               "mapped payload carries a third force channel");
        const auto sample = [](const auto &payload, std::size_t i) {
            std::uint32_t bits = 0;
            for (unsigned b = 0; b < 4; ++b)
                bits |= static_cast<std::uint32_t>(payload[i * 4 + b]) << (8 * b);
            return std::bit_cast<float>(bits);
        };
        for (std::size_t cell = 0; cell < 4; ++cell)
            for (std::size_t phase = 0; phase < 2048; ++phase) {
                const auto at = cell * 2048 + phase;
                if (cell < 3)
                    for (std::size_t channel = 0; channel < 2; ++channel)
                        expect(sample(plain_bytes, at * 2 + channel) ==
                                   sample(mapped_bytes, at * 3 + channel),
                               "unchanged charge states preserve exact native source bytes");
                if (cell % 2 == 0)
                    expect(sample(mapped_bytes, at * 3) == sample(mapped_bytes, at * 3 + 2),
                           "fuel-cut force and matching motored force are identical");
            }
        expect(sample(mapped_bytes, 3 * 2048 * 3 + 2) >
                   1.5 * sample(mapped_bytes, 2 * 2048 * 3 + 2),
               "boosted motored baseline follows charge pressure, not ambient fuel-cut cell");
        std::cout << "diesel definition, deterministic settled capture, cancellation "
                     "and carrier binding passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

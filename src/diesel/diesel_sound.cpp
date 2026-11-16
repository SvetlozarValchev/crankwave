#include "diesel/diesel_sound.hpp"

#include "crankwave/artifacts/crankwave_package.hpp"
#include "crankwave/authoring/json.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace crankwave::diesel {
namespace {
using authoring::JsonKind;
using authoring::JsonValue;
constexpr double pi = std::numbers::pi;

void require(bool condition, std::string path, std::string message) {
    if (!condition)
        throw SoundError{std::move(path), std::move(message)};
}
void keys(JsonValue value, std::string_view path,
          std::initializer_list<std::string_view> names,
          std::initializer_list<std::string_view> optional = {}) {
    require(value.kind() == JsonKind::object, std::string{path}, "expected object");
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto key = value.member_at(i).key;
        require(std::find(names.begin(), names.end(), key) != names.end() ||
                    std::find(optional.begin(), optional.end(), key) != optional.end(),
                std::string{path} + "/" + std::string{key}, "unknown field");
    }
    for (const auto name : names) {
        require(value.find(name).valid(), std::string{path} + "/" + std::string{name},
                "required field is missing");
    }
}
double number(JsonValue object, std::string_view key, std::string_view path,
              double minimum, double maximum) {
    const auto value = object.find(key).number();
    require(value && std::isfinite(*value) && *value >= minimum && *value <= maximum,
            std::string{path} + "/" + std::string{key},
            "number outside admitted range");
    return *value;
}
std::string string(JsonValue object, std::string_view key, std::string_view path) {
    const auto value = object.find(key).string();
    require(value && !value->empty() && value->size() <= 4096,
            std::string{path} + "/" + std::string{key},
            "expected nonempty bounded string");
    return std::string{*value};
}
std::string quote(std::string_view value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 0x20U) {
            result += "\\u00";
            result += hex[c >> 4U];
            result += hex[c & 15U];
        } else
            result += static_cast<char>(c);
    }
    return result + '"';
}
std::string json(JsonValue value) {
    if (value.kind() == JsonKind::string)
        return quote(*value.string());
    if (value.kind() == JsonKind::number) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(17) << *value.number();
        return out.str();
    }
    if (value.kind() == JsonKind::boolean)
        return *value.boolean() ? "true" : "false";
    if (value.kind() == JsonKind::null_value)
        return "null";
    std::string result = value.kind() == JsonKind::array ? "[" : "{";
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i)
            result += ',';
        if (value.kind() == JsonKind::array)
            result += json(value.at(i));
        else {
            const auto member = value.member_at(i);
            result += quote(member.key) + ':' + json(member.value);
        }
    }
    return result + (value.kind() == JsonKind::array ? ']' : '}');
}
std::string hex(const contract::Sha256Digest &digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest.bytes) {
        const auto v = static_cast<unsigned>(byte);
        result += digits[v >> 4U];
        result += digits[v & 15U];
    }
    return result;
}
std::vector<std::byte> bytes(std::string_view text) {
    const auto span = std::as_bytes(std::span{text.data(), text.size()});
    return {span.begin(), span.end()};
}
simulation::DieselGasBoundary boundary(JsonValue value, std::string_view path) {
    keys(value, path, {"pressure_pa_abs", "temperature_k", "oxygen_mass_fraction"});
    return {number(value, "pressure_pa_abs", path, 1000, 300000),
            number(value, "temperature_k", path, 200, 1500),
            number(value, "oxygen_mass_fraction", path, 0, 0.3)};
}
simulation::DieselValveWindow valve(JsonValue value, std::string_view path) {
    keys(value, path,
         {"open_angle_atdc_rad", "close_angle_atdc_rad", "maximum_effective_area_m2"});
    return {number(value, "open_angle_atdc_rad", path, 0, 4 * pi),
            number(value, "close_angle_atdc_rad", path, 0, 4 * pi),
            number(value, "maximum_effective_area_m2", path, 0, 0.005)};
}
std::vector<double> axis(JsonValue value, std::string_view path, double minimum,
                         double maximum, std::size_t max_count) {
    require(value.kind() == JsonKind::array && value.size() >= 2 &&
                value.size() <= max_count,
            std::string{path},
            "expected bounded ordered axis with at least two points");
    std::vector<double> result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto v = value.at(i).number();
        require(v && *v >= minimum && *v <= maximum &&
                    (result.empty() || *v > result.back()),
                std::string{path},
                "axis must be finite, strictly increasing and in range");
        result.push_back(*v);
    }
    return result;
}
void append_float(std::vector<std::byte> &out, double value) {
    const float sample = static_cast<float>(value);
    require(std::isfinite(sample), "/capture",
            "capture cannot be represented as float32");
    const auto bits = std::bit_cast<std::uint32_t>(sample);
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((bits >> shift) & 255U));
}
} // namespace

std::variant<SoundDefinition, SoundError>
parse_sound_definition(std::string_view source) {
    try {
        auto parsed = authoring::parse_json(source);
        if (const auto *error = std::get_if<authoring::JsonParseError>(&parsed))
            return SoundError{"/", std::string{error->message()}};
        const auto root = std::get<authoring::JsonDocument>(parsed).root();
        keys(root, "",
             {"schema", "model", "engine", "presentation", "bake", "provenance"});
        require(string(root, "schema", "") == "crankwave/engine" &&
                    string(root, "model", "") == kMechanicalDieselModel,
                "/model", "unsupported engine model");
        SoundDefinition d;
        auto engine = root.find("engine");
        keys(engine, "/engine", {"id", "cylinder", "injection_pulses", "cylinders"},
             {"operating_boundaries"});
        d.engine_id = string(engine, "id", "/engine");
        require(artifacts::is_crankwave_engine_id(d.engine_id), "/engine/id",
                "expected portable engine ID");
        auto c = engine.find("cylinder");
        keys(c, "/engine/cylinder",
             {"bore_m", "stroke_m", "connecting_rod_length_m", "compression_ratio",
              "gas_constant_j_per_kg_k", "specific_heat_ratio",
              "injected_fuel_sensible_energy_j_per_kg", "wall_temperature_k",
              "wall_conductance_w_per_k", "crankcase_pressure_pa_abs", "initial_charge",
              "intake", "exhaust", "intake_valve", "exhaust_valve", "combustion"});
        auto &p = d.cylinder;
        p.gas_substeps = 32U;
        p.bore_m = number(c, "bore_m", "/engine/cylinder", 0.03, 0.3);
        p.stroke_m = number(c, "stroke_m", "/engine/cylinder", 0.03, 0.4);
        p.connecting_rod_length_m =
            number(c, "connecting_rod_length_m", "/engine/cylinder", 0.03, 1.0);
        p.compression_ratio =
            number(c, "compression_ratio", "/engine/cylinder", 10, 30);
        p.gas_constant_j_per_kg_k =
            number(c, "gas_constant_j_per_kg_k", "/engine/cylinder", 200, 400);
        p.specific_heat_ratio =
            number(c, "specific_heat_ratio", "/engine/cylinder", 1.1, 1.67);
        p.injected_fuel_sensible_energy_j_per_kg = number(
            c, "injected_fuel_sensible_energy_j_per_kg", "/engine/cylinder", 0, 1e6);
        p.wall_temperature_k =
            number(c, "wall_temperature_k", "/engine/cylinder", 250, 800);
        p.wall_conductance_w_per_k =
            number(c, "wall_conductance_w_per_k", "/engine/cylinder", 0, 100);
        p.crankcase_pressure_pa_abs =
            number(c, "crankcase_pressure_pa_abs", "/engine/cylinder", 50000, 150000);
        p.initial_charge =
            boundary(c.find("initial_charge"), "/engine/cylinder/initial_charge");
        p.intake = boundary(c.find("intake"), "/engine/cylinder/intake");
        p.exhaust = boundary(c.find("exhaust"), "/engine/cylinder/exhaust");
        p.intake_valve = valve(c.find("intake_valve"), "/engine/cylinder/intake_valve");
        p.exhaust_valve =
            valve(c.find("exhaust_valve"), "/engine/cylinder/exhaust_valve");
        const auto combustion = c.find("combustion");
        keys(combustion, "/engine/cylinder/combustion",
             {"lower_heating_value_j_per_kg", "stoichiometric_oxygen_kg_per_kg_fuel",
              "preparation_time_s", "premixed_burn_time_s",
              "reference_ignition_delay_s", "reference_temperature_k",
              "reference_pressure_pa_abs", "reference_oxygen_mass_fraction",
              "activation_temperature_k", "pressure_exponent", "oxygen_exponent"});
        auto &b = p.combustion;
        b.lower_heating_value_j_per_kg =
            number(combustion, "lower_heating_value_j_per_kg",
                   "/engine/cylinder/combustion", 1e7, 6e7);
        b.stoichiometric_oxygen_kg_per_kg_fuel =
            number(combustion, "stoichiometric_oxygen_kg_per_kg_fuel",
                   "/engine/cylinder/combustion", 1, 5);
        b.preparation_time_s = number(combustion, "preparation_time_s",
                                      "/engine/cylinder/combustion", 0.00001, 0.02);
        b.premixed_burn_time_s = number(combustion, "premixed_burn_time_s",
                                        "/engine/cylinder/combustion", 0.00001, 0.02);
        b.reference_ignition_delay_s =
            number(combustion, "reference_ignition_delay_s",
                   "/engine/cylinder/combustion", 0.00001, 0.02);
        b.reference_temperature_k = number(combustion, "reference_temperature_k",
                                           "/engine/cylinder/combustion", 300, 1500);
        b.reference_pressure_pa_abs = number(combustion, "reference_pressure_pa_abs",
                                             "/engine/cylinder/combustion", 1e5, 1e7);
        b.reference_oxygen_mass_fraction =
            number(combustion, "reference_oxygen_mass_fraction",
                   "/engine/cylinder/combustion", 0.01, 0.3);
        b.activation_temperature_k = number(combustion, "activation_temperature_k",
                                            "/engine/cylinder/combustion", 0, 20000);
        b.pressure_exponent = number(combustion, "pressure_exponent",
                                     "/engine/cylinder/combustion", 0, 3);
        b.oxygen_exponent =
            number(combustion, "oxygen_exponent", "/engine/cylinder/combustion", 0, 3);
        const auto pulses = engine.find("injection_pulses");
        require(pulses.kind() == JsonKind::array && pulses.size() >= 1 &&
                    pulses.size() <= 8,
                "/engine/injection_pulses", "expected one to eight pulses");
        for (std::size_t i = 0; i < pulses.size(); ++i) {
            const auto pulse = pulses.at(i);
            const auto path = "/engine/injection_pulses/" + std::to_string(i);
            keys(pulse, path,
                 {"id", "start_angle_atdc_rad", "duration_rad", "full_fuel_mass_kg",
                  "opening_fraction_01", "closing_fraction_01"},
                 {"minimum_duration_fraction_01"});
            const double id = number(pulse, "id", path, 1, 65535);
            require(std::floor(id) == id, path + "/id", "ID must be an integer");
            d.pulses.push_back(
                {static_cast<std::uint32_t>(id),
                 number(pulse, "start_angle_atdc_rad", path, -pi, pi),
                 number(pulse, "duration_rad", path, 0.001, pi),
                 number(pulse, "full_fuel_mass_kg", path, 0, 0.001),
                 number(pulse, "opening_fraction_01", path, 0, 1),
                 number(pulse, "closing_fraction_01", path, 0, 1),
                 pulse.find("minimum_duration_fraction_01").valid()
                     ? number(pulse, "minimum_duration_fraction_01", path, 0.02, 1)
                     : 1.0});
        }
        const auto cylinders = engine.find("cylinders");
        require(cylinders.kind() == JsonKind::array && cylinders.size() >= 1 &&
                    cylinders.size() <= 12,
                "/engine/cylinders", "expected one to twelve cylinder bindings");
        std::vector<std::string> ids;
        for (std::size_t i = 0; i < cylinders.size(); ++i) {
            const auto cylinder = cylinders.at(i);
            const auto path = "/engine/cylinders/" + std::to_string(i);
            keys(cylinder, path,
                 {"id", "firing_angle_rad", "fuel_trim", "structural_gain"});
            const auto id = string(cylinder, "id", path);
            require(artifacts::is_crankwave_engine_id(id) &&
                        std::find(ids.begin(), ids.end(), id) == ids.end(),
                    path + "/id", "duplicate or invalid cylinder ID");
            ids.push_back(id);
            number(cylinder, "firing_angle_rad", path, 0, std::nextafter(4 * pi, 0.0));
            number(cylinder, "fuel_trim", path, 0.5, 1.5);
            number(cylinder, "structural_gain", path, -2, 2);
        }
        d.cylinders_json = json(cylinders);
        const auto presentation = root.find("presentation");
        keys(presentation, "/presentation",
             {"block_modes", "block_lowpass_hz", "exhaust_gain_pcm_per_kg_s",
              "exhaust_lowpass_hz", "master_gain"},
             {"articulation", "exhaust_system", "pressure_texture", "mechanical_texture",
              "low_fuel_exhaust", "coasting_balance", "coasting_flow_texture",
              "loaded_pressure_texture", "inline_inertia", "loaded_clatter"});
        number(presentation, "block_lowpass_hz", "/presentation", 2000, 8000);
        number(presentation, "exhaust_lowpass_hz", "/presentation", 100, 3000);
        number(presentation, "exhaust_gain_pcm_per_kg_s", "/presentation", 0, 100);
        number(presentation, "master_gain", "/presentation", 0, 4);
        const auto modes = presentation.find("block_modes");
        require(modes.kind() == JsonKind::array && modes.size() >= 1 &&
                    modes.size() <= 12,
                "/presentation/block_modes", "expected one to twelve structural modes");
        for (std::size_t i = 0; i < modes.size(); ++i) {
            const auto mode = modes.at(i);
            const auto path = "/presentation/block_modes/" + std::to_string(i);
            keys(mode, path, {"frequency_hz", "q", "gain_pcm_per_n"});
            number(mode, "frequency_hz", path, 100, 8000);
            number(mode, "q", path, 0.5, 30);
            number(mode, "gain_pcm_per_n", path, -0.001, 0.001);
        }
        if (const auto articulation = presentation.find("articulation");
            articulation.valid()) {
            const std::string path = "/presentation/articulation";
            keys(articulation, path,
                 {"block_highpass_hz", "exhaust_highpass_hz", "cycle_fuel_spread_01",
                  "cycle_timing_spread_deg", "variation_seed"});
            number(articulation, "block_highpass_hz", path, 100, 1500);
            number(articulation, "exhaust_highpass_hz", path, 30, 300);
            number(articulation, "cycle_fuel_spread_01", path, 0, 0.2);
            number(articulation, "cycle_timing_spread_deg", path, 0, 2);
            const auto seed =
                number(articulation, "variation_seed", path, 0, 4294967295.0);
            require(std::floor(seed) == seed, path + "/variation_seed",
                    "expected integer");
            d.runtime_version = 2;
        }
        if (const auto system = presentation.find("exhaust_system"); system.valid()) {
            const std::string path = "/presentation/exhaust_system";
            require(d.runtime_version == 2, path,
                    "exhaust system requires articulation");
            keys(system, path,
                 {"branches", "radiation_gain_pcm_s2_per_kg",
                  "flow_noise_gain_pcm_per_kg_s", "noise_highpass_hz", "noise_seed"});
            number(system, "radiation_gain_pcm_s2_per_kg", path, 0, 0.1);
            number(system, "flow_noise_gain_pcm_per_kg_s", path, 0, 50);
            number(system, "noise_highpass_hz", path, 100, 3000);
            const auto seed = number(system, "noise_seed", path, 0, 4294967295.0);
            require(std::floor(seed) == seed, path + "/noise_seed", "expected integer");
            const auto branches = system.find("branches");
            require(branches.kind() == JsonKind::array && branches.size() >= 1 &&
                        branches.size() <= 8,
                    path + "/branches", "expected one to eight exhaust branches");
            std::vector<std::string> assigned, branch_ids;
            for (std::size_t i = 0; i < branches.size(); ++i) {
                const auto branch = branches.at(i);
                const auto bp = path + "/branches/" + std::to_string(i);
                keys(branch, bp,
                     {"id", "cylinder_ids", "delay_s", "reflection", "loss_lowpass_hz",
                      "gain"});
                const auto id = string(branch, "id", bp);
                require(artifacts::is_crankwave_engine_id(id) &&
                            std::find(branch_ids.begin(), branch_ids.end(), id) ==
                                branch_ids.end(),
                        bp + "/id", "invalid or repeated branch ID");
                branch_ids.push_back(id);
                number(branch, "delay_s", bp, 0.001, 0.04);
                number(branch, "reflection", bp, -0.9, 0.9);
                number(branch, "loss_lowpass_hz", bp, 500, 8000);
                number(branch, "gain", bp, 0, 2);
                const auto bindings = branch.find("cylinder_ids");
                require(bindings.kind() == JsonKind::array && bindings.size() > 0 &&
                            bindings.size() <= ids.size(),
                        bp + "/cylinder_ids", "expected cylinder bindings");
                for (std::size_t j = 0; j < bindings.size(); ++j) {
                    const auto id = bindings.at(j).string();
                    require(id && std::find(ids.begin(), ids.end(), *id) != ids.end() &&
                                std::find(assigned.begin(), assigned.end(), *id) ==
                                    assigned.end(),
                            bp + "/cylinder_ids",
                            "unknown or repeated cylinder binding");
                    assigned.emplace_back(*id);
                }
            }
            require(assigned.size() == ids.size(), path + "/branches",
                    "every cylinder needs one exhaust branch");
            d.runtime_version = 3;
        }
        if (const auto texture = presentation.find("pressure_texture"); texture.valid()) {
            const std::string path = "/presentation/pressure_texture";
            require(d.runtime_version == 3, path,
                    "pressure texture requires articulation and exhaust system");
            keys(texture, path,
                 {"decay_s", "coherent_gain", "compression_gain", "stochastic_gain", "seed"});
            number(texture, "decay_s", path, 0.001, 0.05);
            number(texture, "coherent_gain", path, 0, 1);
            number(texture, "compression_gain", path, 0, 1);
            number(texture, "stochastic_gain", path, 0, 32);
            const auto seed = number(texture, "seed", path, 0, 4294967295.0);
            require(std::floor(seed) == seed, path + "/seed", "expected integer");
            // Version 4 belonged to the withdrawn idle-gain experiment.
            d.runtime_version = 5;
        }
        if (const auto texture = presentation.find("mechanical_texture"); texture.valid()) {
            const std::string path = "/presentation/mechanical_texture";
            require(d.runtime_version == 5, path,
                    "mechanical texture requires pressure texture and its dependencies");
            keys(texture, path, {"valve_gain_pcm", "valve_frequency_hz", "valve_decay_s",
                                 "pumping_gain_pcm", "pumping_lowpass_hz", "seed"},
                 {"valve_attack_s"});
            number(texture, "valve_gain_pcm", path, 0, 0.5);
            number(texture, "valve_frequency_hz", path, 400, 3000);
            number(texture, "valve_decay_s", path, 0.0005, 0.008);
            number(texture, "pumping_gain_pcm", path, 0, 0.2);
            number(texture, "pumping_lowpass_hz", path, 300, 2000);
            const auto seed = number(texture, "seed", path, 0, 4294967295.0);
            require(std::floor(seed) == seed, path + "/seed", "expected integer");
            d.runtime_version = 6;
            if (texture.find("valve_attack_s").valid()) {
                number(texture, "valve_attack_s", path, 0.00025, 0.004);
                d.runtime_version = 7;
            }
        }
        if (const auto coast = presentation.find("low_fuel_exhaust"); coast.valid()) {
            const std::string path = "/presentation/low_fuel_exhaust";
            require(d.runtime_version == 7, path,
                    "low-fuel exhaust requires smooth mechanical texture and its dependencies");
            keys(coast, path, {"fueling_threshold_01", "gain", "lowpass_hz", "transition_s"});
            number(coast, "fueling_threshold_01", path, 0.01, 0.4);
            number(coast, "gain", path, 0, 1);
            number(coast, "lowpass_hz", path, 250, 1500);
            number(coast, "transition_s", path, 0.005, 0.2);
            d.runtime_version = 8;
        }
        if (const auto balance = presentation.find("coasting_balance"); balance.valid()) {
            const std::string path = "/presentation/coasting_balance";
            require(d.runtime_version == 8, path,
                    "coasting balance requires low-fuel exhaust and its dependencies");
            keys(balance, path, {"fueling_threshold_01", "bass_gain", "movement_gain",
                                 "crossover_hz", "mechanical_gain", "transition_s"});
            number(balance, "fueling_threshold_01", path, 0.01, 0.12);
            number(balance, "bass_gain", path, 0, 1);
            number(balance, "movement_gain", path, 0, 1);
            number(balance, "crossover_hz", path, 80, 400);
            number(balance, "mechanical_gain", path, 1, 2);
            number(balance, "transition_s", path, 0.005, 0.2);
            d.runtime_version = 9;
        }
        if (const auto flow = presentation.find("coasting_flow_texture"); flow.valid()) {
            const std::string path = "/presentation/coasting_flow_texture";
            require(d.runtime_version == 9, path,
                    "coasting flow texture requires coasting balance and its dependencies");
            keys(flow, path, {"gain_pcm_per_kg_s", "highpass_hz", "lowpass_hz",
                              "envelope_s", "seed"});
            number(flow, "gain_pcm_per_kg_s", path, 0, 4);
            const auto highpass = number(flow, "highpass_hz", path, 150, 800);
            const auto lowpass = number(flow, "lowpass_hz", path, 800, 3000);
            require(lowpass >= 2 * highpass, path, "coasting flow band is too narrow");
            number(flow, "envelope_s", path, 0.0005, 0.02);
            const auto seed = number(flow, "seed", path, 0, 4294967295.0);
            require(std::floor(seed) == seed, path + "/seed", "expected integer");
            d.runtime_version = 10;
        }
        if (const auto loaded = presentation.find("loaded_pressure_texture"); loaded.valid()) {
            const std::string path = "/presentation/loaded_pressure_texture";
            require(d.runtime_version == 10, path,
                    "loaded pressure texture requires coasting flow texture and its dependencies");
            keys(loaded, path, {"decay_s", "attack_s", "secondary_bank_gain",
                                "fueling_start_01", "fueling_full_01", "transition_s"});
            number(loaded, "decay_s", path, 0.005, 0.03);
            number(loaded, "attack_s", path, 0.001, 0.01);
            number(loaded, "secondary_bank_gain", path, 0.2, 1);
            const auto start = number(loaded, "fueling_start_01", path, 0, 0.4);
            const auto full = number(loaded, "fueling_full_01", path, 0.01, 1);
            require(full > start, path, "loaded fuel interval must be positive");
            number(loaded, "transition_s", path, 0.005, 0.2);
            // Common-rail combustion can need rounded radiation at idle too.
            // Preserve the old admission contract and payload for older presets.
            d.runtime_version = start < 0.12 || full < 0.2 ? 13 : 11;
        }
        if (const auto inertia = presentation.find("inline_inertia"); inertia.valid()) {
            const std::string path = "/presentation/inline_inertia";
            require(d.runtime_version == 11 || d.runtime_version == 13, path,
                    "inline inertia requires complete rounded-pressure presentation");
            keys(inertia, path, {"reciprocating_mass_kg", "frequency_hz", "q",
                                 "gain_pcm_per_n", "highpass_hz", "lowpass_hz"});
            number(inertia, "reciprocating_mass_kg", path, 0.05, 2);
            number(inertia, "frequency_hz", path, 50, 300);
            number(inertia, "q", path, 0.5, 10);
            number(inertia, "gain_pcm_per_n", path, 0, 0.001);
            const auto high = number(inertia, "highpass_hz", path, 50, 300);
            require(number(inertia, "lowpass_hz", path, 300, 1000) >= 2 * high,
                    path, "inline inertia radiation band is too narrow");
            d.runtime_version = 14;
        }
        if (const auto clatter = presentation.find("loaded_clatter"); clatter.valid()) {
            const std::string path = "/presentation/loaded_clatter";
            require(d.runtime_version == 14, path, "loaded clatter requires inline-inertia presentation");
            keys(clatter, path, {"rpm_start", "rpm_full", "fueling_start_01", "fueling_full_01",
                                "attack_s", "decay_s", "transition_s", "coherent_gain", "stochastic_gain",
                                "highpass_hz", "lowpass_hz", "seed", "modes"});
            const auto start_rpm = number(clatter, "rpm_start", path, 400, 3500);
            require(number(clatter, "rpm_full", path, 800, 4500) > start_rpm,
                    path, "clatter RPM interval must be positive");
            const auto start_fuel = number(clatter, "fueling_start_01", path, 0, 0.8);
            require(number(clatter, "fueling_full_01", path, 0.01, 1) > start_fuel,
                    path, "clatter fuel interval must be positive");
            number(clatter, "attack_s", path, 0.0001, 0.002);
            number(clatter, "decay_s", path, 0.0005, 0.008);
            number(clatter, "transition_s", path, 0.005, 0.2);
            number(clatter, "coherent_gain", path, 0, 2);
            number(clatter, "stochastic_gain", path, 0, 12);
            const auto high = number(clatter, "highpass_hz", path, 200, 1500);
            require(number(clatter, "lowpass_hz", path, 2000, 8000) >= 2 * high,
                    path, "clatter radiation band too narrow");
            const auto seed = number(clatter, "seed", path, 0, 4294967295.0);
            require(std::floor(seed) == seed, path + "/seed", "expected integer");
            const auto clatter_modes = clatter.find("modes");
            require(clatter_modes.kind() == JsonKind::array && clatter_modes.size() >= 1 &&
                        clatter_modes.size() <= 8, path, "expected one to eight clatter modes");
            const bool coupled = clatter_modes.at(0).find("cylinder_coupling").valid();
            for (std::size_t i = 0; i < clatter_modes.size(); ++i) {
                const auto mode = clatter_modes.at(i);
                const auto at = path + "/modes/" + std::to_string(i);
                keys(mode, at, {"frequency_hz", "q", "gain_pcm_per_n"}, {"cylinder_coupling"});
                number(mode, "frequency_hz", at, 400, 6000);
                number(mode, "q", at, 0.5, 15);
                number(mode, "gain_pcm_per_n", at, 0, 0.0002);
                const auto coupling = mode.find("cylinder_coupling");
                require(coupling.valid() == coupled, at, "all clatter modes must use the same coupling format");
                if (coupled) {
                    require(coupling.kind() == JsonKind::array && coupling.size() == ids.size(),
                            at, "each clatter mode must bind every cylinder");
                    std::vector<std::string> bound;
                    for (std::size_t j = 0; j < coupling.size(); ++j) {
                        const auto binding = coupling.at(j);
                        const auto where = at + "/cylinder_coupling/" + std::to_string(j);
                        keys(binding, where, {"id", "gain"});
                        const auto id = string(binding, "id", where);
                        require(std::find(ids.begin(), ids.end(), id) != ids.end() &&
                                    std::find(bound.begin(), bound.end(), id) == bound.end(),
                                where, "unknown or duplicate clatter cylinder");
                        bound.push_back(id);
                        number(binding, "gain", where, -2, 2);
                    }
                }
            }
            d.runtime_version = coupled ? 16 : 15;
        }
        d.presentation_json = json(presentation);
        const auto provenance = root.find("provenance");
        keys(provenance, "/provenance", {"physical_parameters", "presentation"});
        for (const auto name : {"physical_parameters", "presentation"}) {
            const auto record = provenance.find(name);
            const auto path = "/provenance/" + std::string{name};
            keys(record, path, {"origin", "note"});
            const auto origin = string(record, "origin", path);
            require(origin == "inferred" || origin == "artistic" ||
                        origin == "measurement" || origin == "calibrated",
                    path + "/origin", "unsupported provenance origin");
            string(record, "note", path);
        }
        d.provenance_json = json(provenance);
        const auto bake = root.find("bake");
        keys(bake, "/bake",
             {"rpms", "fueling", "samples_per_cycle", "minimum_settling_cycles",
              "maximum_settling_cycles", "settling_relative_tolerance"});
        d.rpms = axis(bake.find("rpms"), "/bake/rpms", 400, 4500, 32);
        d.fueling = axis(bake.find("fueling"), "/bake/fueling", 0, 1, 16);
        require(d.fueling.front() == 0 && d.fueling.back() == 1, "/bake/fueling",
                "fuel axis must include zero and full fueling");
        for (const auto &pair :
             {std::pair{"samples_per_cycle", &d.samples_per_cycle},
              {"minimum_settling_cycles", &d.minimum_settling_cycles},
              {"maximum_settling_cycles", &d.maximum_settling_cycles}}) {
            const bool samples = std::string_view{pair.first} == "samples_per_cycle";
            const auto v = number(bake, pair.first, "/bake", samples ? 2048 : 4,
                                  samples ? 16384 : 64);
            require(std::floor(v) == v, "/bake/" + std::string{pair.first},
                    "expected integer");
            *pair.second = static_cast<std::uint32_t>(v);
        }
        require(std::has_single_bit(d.samples_per_cycle), "/bake/samples_per_cycle",
                "sample count must be a power of two");
        require(d.minimum_settling_cycles <= d.maximum_settling_cycles, "/bake",
                "invalid settling interval");
        d.settling_relative_tolerance =
            number(bake, "settling_relative_tolerance", "/bake", 1e-7, 0.005);
        if (const auto map = engine.find("operating_boundaries"); map.valid()) {
            const std::string path = "/engine/operating_boundaries";
            require(d.runtime_version == 11 || d.runtime_version >= 13, path,
                    "boundary map requires the complete version 11 presentation");
            require(map.kind() == JsonKind::array &&
                        map.size() == d.rpms.size() * d.fueling.size(),
                    path, "expected one boundary cell for every RPM/fueling pair");
            for (std::size_t i = 0; i < map.size(); ++i) {
                const auto cell = map.at(i);
                const auto at = path + "/" + std::to_string(i);
                keys(cell, at, {"rpm", "fueling_01", "intake", "exhaust"});
                require(number(cell, "rpm", at, 400, 4500) ==
                            d.rpms[i / d.fueling.size()] &&
                            number(cell, "fueling_01", at, 0, 1) ==
                            d.fueling[i % d.fueling.size()],
                        at, "boundary cells must match RPM-major bake order");
                d.operating_boundaries.push_back({
                    boundary(cell.find("intake"), at + "/intake"),
                    boundary(cell.find("exhaust"), at + "/exhaust")});
            }
            d.runtime_version = std::max(d.runtime_version, 12u);
        }
        p.held_speed_rpm = d.rpms.front();
        if (const auto compiled =
                simulation::DieselCylinderSession::compile(p, d.pulses);
            std::holds_alternative<simulation::DieselCylinderError>(compiled))
            return SoundError{"/engine", "cylinder geometry, valve windows, or "
                                         "injection plan is not executable"};
        return d;
    } catch (const SoundError &error) {
        return error;
    }
}

std::variant<BakedSound, SoundError> bake_sound(std::string_view source_json,
                                                std::string_view backend_identity_json,
                                                std::stop_token stop) {
    try {
        auto parsed = parse_sound_definition(source_json);
        if (const auto *error = std::get_if<SoundError>(&parsed))
            return *error;
        const auto &d = std::get<SoundDefinition>(parsed);
        std::vector<std::byte> payload;
        const bool mapped = !d.operating_boundaries.empty();
        const bool motored_source = d.runtime_version >= 12;
        payload.reserve(d.rpms.size() * d.fueling.size() * d.samples_per_cycle *
                        (motored_source ? 12U : 8U));
        std::ostringstream cells;
        cells.imbue(std::locale::classic());
        cells << std::setprecision(17) << '[';
        std::size_t cell_index = 0;
        for (const double rpm : d.rpms)
            for (const double fueling : d.fueling) {
                if (stop.stop_requested())
                    return SoundError{"/bake", "diesel bake cancelled", true};
                auto p = d.cylinder;
                p.held_speed_rpm = rpm;
                if (mapped) {
                    p.intake = d.operating_boundaries[cell_index].intake;
                    p.exhaust = d.operating_boundaries[cell_index].exhaust;
                }
                // Capture a matching motored baseline at each mapped charge state.
                // Using the ambient fuel-cut cell would turn boost compression into
                // spurious combustion texture.
                const auto capture = [&](double capture_fueling) {
                    std::vector<std::array<double, 2>> samples;
                    samples.reserve(d.samples_per_cycle);
                    auto compiled =
                        simulation::DieselCylinderSession::compile(p, d.pulses, capture_fueling);
                    require(
                        std::holds_alternative<simulation::DieselCylinderSession>(compiled),
                        "/bake", "operating point cannot be compiled");
                    auto s = std::get<simulation::DieselCylinderSession>(compiled);
                    std::optional<simulation::DieselCylinderCycle> previous;
                    simulation::DieselCylinderCycle settled;
                    double settling_error = 0;
                    std::size_t consecutive = 0;
                    bool ready = false;
                    const auto advance = [&]() {
                        if (stop.stop_requested())
                            throw SoundError{"/bake", "diesel bake cancelled", true};
                        auto result = s.advance({capture_fueling, true});
                        if (const auto *error =
                                std::get_if<simulation::DieselCylinderError>(&result))
                            throw SoundError{
                                "/bake",
                                "cylinder integration failed at " + std::to_string(rpm) +
                                    " RPM, fuel " + std::to_string(capture_fueling) + ", frame " +
                                    std::to_string(s.state().frame_index) + ", error " +
                                    std::to_string(static_cast<int>(error->code))};
                        return std::get<simulation::DieselCylinderStep>(result);
                    };
                    while (s.state().cycle_index < d.maximum_settling_cycles) {
                        const auto step = advance();
                        if (!step.completed_cycle)
                            continue;
                        const auto &cycle = *step.completed_cycle;
                        if (previous &&
                            s.state().cycle_index >= d.minimum_settling_cycles) {
                            const double work = cycle.accounting.volume_work_out_j;
                            const double scale = std::max(
                                {1.0, std::abs(work),
                                 std::abs(previous->accounting.volume_work_out_j)});
                            settling_error = std::max(
                                {std::abs(work - previous->accounting.volume_work_out_j) /
                                     scale,
                                 std::abs(cycle.end_temperature_k -
                                          previous->end_temperature_k) /
                                     cycle.end_temperature_k,
                                 std::abs(cycle.end_charge_mass_kg -
                                          previous->end_charge_mass_kg) /
                                     cycle.end_charge_mass_kg,
                                 std::abs(cycle.end_oxygen_mass_kg -
                                          previous->end_oxygen_mass_kg) /
                                     cycle.end_charge_mass_kg,
                                 std::abs(cycle.end_unburned_tracer_mass_kg -
                                          previous->end_unburned_tracer_mass_kg) /
                                     cycle.end_charge_mass_kg,
                                 std::abs(cycle.peak_pressure_pa_abs -
                                          previous->peak_pressure_pa_abs) /
                                     cycle.peak_pressure_pa_abs});
                            const bool stable =
                                settling_error <= d.settling_relative_tolerance;
                            consecutive = stable ? consecutive + 1 : 0;
                            if (consecutive >= 3) {
                                settled = cycle;
                                ready = true;
                                break;
                            }
                        }
                        previous = cycle;
                    }
                    require(ready, "/bake",
                            "operating point did not settle at " + std::to_string(rpm) +
                                " RPM, fuel " + std::to_string(capture_fueling) +
                                ", relative change " + std::to_string(settling_error));
                    require(settled.peak_pressure_pa_abs <= 1e9 &&
                                std::abs(settled.accounting.volume_work_out_j) <= 1e7,
                            "/bake", "settled state exceeds diesel runtime bounds");
                    // Capture at firing TDC, independently of IVC initialization phase.
                    const double start =
                        std::ceil(s.state().unwrapped_angle_rad / (4 * pi)) * (4 * pi);
                    auto before = s.state();
                    auto step = advance();
                    for (std::uint32_t i = 0; i < d.samples_per_cycle; ++i) {
                        const double angle =
                            start + static_cast<double>(i) * (4 * pi) / d.samples_per_cycle;
                        while (s.state().unwrapped_angle_rad < angle) {
                            before = s.state();
                            step = advance();
                        }
                        const double a =
                            (angle - before.unwrapped_angle_rad) /
                            (s.state().unwrapped_angle_rad - before.unwrapped_angle_rad);
                        require(a >= -1e-9 && a <= 1 + 1e-9, "/capture",
                                "phase interpolation escaped capture bounds");
                        const double pressure =
                            before.pressure_pa_abs +
                            (s.state().pressure_pa_abs - before.pressure_pa_abs) * a;
                        const double force = (pressure - p.crankcase_pressure_pa_abs) *
                                             (pi * p.bore_m * p.bore_m / 4.0);
                        const double exhaust =
                            -step.accounting.exhaust.mass_into_cylinder_kg *
                            p.physics_rate_hz;
                        require(std::abs(force) <= 1e7 && std::abs(exhaust) <= 10,
                                "/capture",
                                "physical source exceeds diesel runtime bounds");
                        samples.push_back({force, exhaust});
                    }
                    return std::pair{settled, std::move(samples)};
                };
                auto [settled, samples] = capture(fueling);
                std::vector<std::array<double, 2>> motored;
                if (motored_source)
                    motored = fueling == 0 ? samples : capture(0).second;
                for (std::size_t i = 0; i < samples.size(); ++i) {
                    append_float(payload, samples[i][0]);
                    append_float(payload, samples[i][1]);
                    if (motored_source)
                        append_float(payload, motored[i][0]);
                }
                if (cell_index++)
                    cells << ',';
                cells << "{\"rpm\":" << rpm << ",\"fueling_01\":" << fueling
                      << ",\"manifold_pressure_pa_abs\":" << p.intake.pressure_pa_abs
                      << ",\"settled_cycles\":" << settled.index + 1
                      << ",\"indicated_work_j\":"
                      << settled.accounting.volume_work_out_j
                      << ",\"peak_pressure_pa_abs\":" << settled.peak_pressure_pa_abs
                      << '}';
            }
        cells << ']';
        std::ostringstream manifest;
        manifest.imbue(std::locale::classic());
        manifest << std::setprecision(17);
        manifest
            << "{\"schema\":\"crankwave/"
               "diesel-mechanical-audio\",\"version\":"
            << d.runtime_version << ",\"engine\":" << quote(d.engine_id)
            << ",\"model\":\"mechanical-diesel-v1\",\"sample_rate_hz\":192000,"
               "\"physics_rate_hz\":20000,\"gas_substeps\":32,\"load_coordinate\":"
               "\"normalized-fueling\""
            << ",\"source_sha256\":" << quote(hex(contract::sha256(bytes(source_json))))
            << ",\"backend\":" << backend_identity_json
            << ",\"methods\":[\"diesel-held-speed-cylinder-v1\",\"diesel-prepared-fuel-"
               "combustion-v1\",\"pressure-force-modal-radiation-v"
            << d.runtime_version << "\"]"
            << ",\"samples_per_cycle\":" << d.samples_per_cycle
            << ",\"cells\":" << cells.str() << ",\"cylinders\":" << d.cylinders_json
            << ",\"presentation\":" << d.presentation_json;
        if (d.runtime_version >= 6) {
            // Bind mechanical events to the same valve windows used by the gas
            // capture. Presentation authors do not duplicate engine timing.
            manifest << ",\"mechanical_timing\":{\"intake_open_angle_rad\":"
                     << d.cylinder.intake_valve.open_angle_atdc_rad
                     << ",\"intake_close_angle_rad\":"
                     << d.cylinder.intake_valve.close_angle_atdc_rad
                     << ",\"exhaust_close_angle_rad\":"
                     << d.cylinder.exhaust_valve.close_angle_atdc_rad << '}';
        }
        if (d.runtime_version >= 14)
            manifest << ",\"inertial_geometry\":{\"crank_radius_m\":" << d.cylinder.stroke_m / 2
                     << ",\"connecting_rod_length_m\":" << d.cylinder.connecting_rod_length_m << '}';
        manifest << ",\"provenance\":" << d.provenance_json
            << ",\"sources\":{\"path\":\"sources.f32le\",\"sha256\":"
            << quote(hex(contract::sha256(payload)))
            << ",\"layout\":" << quote(motored_source ?
                "cell-phase-force-n-exhaust-kg-s-motored-force-n" :
                "cell-phase-force-n-exhaust-kg-s") << ",\"byte_count\":"
            << payload.size() << "}}\n";
        auto runtime = bytes(manifest.str());
        std::string descriptor =
            "{\"schema\":\"crankwave/crankwave-package\",\"version\":1,\"engine_id\":" +
            quote(d.engine_id) +
            ",\"runtime\":{\"kind\":\"diesel-mechanical-audio-v1\",\"manifest_path\":"
            "\"runtime.json\",\"manifest_sha256\":" +
            quote(hex(contract::sha256(runtime))) + "}}\n";
        BakedSound result;
        result.cell_count = cell_index;
        result.members.push_back({"crankwave.json", bytes(descriptor)});
        result.members.push_back({"engine.json", bytes(source_json)});
        result.members.push_back({"runtime.json", std::move(runtime)});
        result.members.push_back({"sources.f32le", std::move(payload)});
        return result;
    } catch (const SoundError &error) {
        return error;
    }
}
} // namespace crankwave::diesel

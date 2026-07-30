#include "engine_sim_offline/authoring/parse.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline::authoring;

static_assert(noexcept(parse_engine_document(std::string_view{})));
static_assert(noexcept(parse_scenario_document(std::string_view{})));
static_assert(noexcept(validate_scenario_references(
    std::declval<const ScenarioDocument &>(),
    std::declval<const EnginePackageDocument &>())));

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string valid_scenario_json() {
    return R"json({
  "schema": "engine-sim-offline/scenario",
  "id": "dyno-listen",
  "engine": "test-engine",
  "fuel": "gasoline",
  "ambient": {
    "pressure": {"value": 101.325, "unit": "kPa"},
    "temperature": {"value": 293.15, "unit": "K"},
    "relative_humidity_01": 0.5
  },
  "initial_thermal_state": {
    "gas_temperature": {"value": 293.15, "unit": "K"},
    "wall_temperature": {"value": 293.15, "unit": "K"},
    "coolant_temperature": {"value": 363.15, "unit": "K"},
    "oil_temperature": {"value": 363.15, "unit": "K"}
  },
  "crankcase": {
    "pressure": {"value": 101.325, "unit": "kPa"},
    "temperature": {"value": 363.15, "unit": "K"}
  },
  "initial_state": {
    "engine_speed": {"value": 1500, "unit": "rpm"},
    "crank_angle": {"value": 0, "unit": "deg"},
    "ignition_enabled": true,
    "fuel_enabled": true,
    "starter_enabled": false,
    "dyno_enabled": true,
    "limiter_enabled": true
  },
  "preparation": {
    "type": "fixed_horizon",
    "preparation_duration": {"value": 250, "unit": "ms"},
    "trailing_complete_cycle_count": 2
  },
  "mode": {
    "type": "held_speed",
    "target_engine_speed": {"value": 3000, "unit": "rpm"},
    "throttle_01": {
      "interpolation": "linear",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.5},
        {"time": {"value": 2000, "unit": "ms"}, "value": 1.0}
      ]
    }
  },
  "events": [
    {
      "id": "monitor-change",
      "time": {"value": 1, "unit": "s"},
      "payload": {
        "type": "set_conditioning_monitoring",
        "jitter_scale": 0.4,
        "derivative_mix_01": 0.2,
        "air_noise_mix_01": 0.1
      }
    }
  ],
  "rates": {
    "physics": {"numerator": "20000", "denominator": "1", "unit": "Hz"},
    "capture": {"numerator": "48000", "denominator": "1", "unit": "Hz"},
    "source_processing": {
      "numerator": "48000",
      "denominator": "1",
      "unit": "Hz"
    },
    "acoustics": {"numerator": "48000", "denominator": "1", "unit": "Hz"},
    "delivery": {"numerator": "48000", "denominator": "1", "unit": "Hz"}
  },
  "quality": {
    "id": "listen",
    "process_block_capacity_frames": 4096,
    "event_queue_capacity": 64,
    "telemetry_capacity_frames": 96000
  },
  "total_duration": {"value": 2, "unit": "s"},
  "audible_start": {"value": 0, "unit": "s"},
  "audible_duration": {"value": 2000, "unit": "ms"},
  "public_seed": "18446744073709551615",
  "output": {
    "buses": ["main"],
    "telemetry_channels": []
  }
})json";
}

[[nodiscard]] std::string valid_engine_json() {
    return R"json({
  "schema": "engine-sim-offline/engine",
  "engine": {
    "identity": {
      "id": "test-engine",
      "display_name": "Minimal authoring engine"
    },
    "cycle": "four_stroke",
    "layout": "inline",
    "limits": {
      "redline": {"value": 6500, "unit": "rpm"}
    },
    "curves": [
      {
        "id": "port-flow",
        "input_dimension": "length",
        "output_dimension": "volume_flow_rate",
        "evaluation": "linear",
        "below_domain": "clamp",
        "above_domain": "clamp",
        "samples": [
          {
            "input": {"value": 0, "unit": "mm"},
            "output": {"value": 0, "unit": "cfm"}
          }
        ]
      },
      {
        "id": "lobe-lift",
        "input_dimension": "angle",
        "output_dimension": "length",
        "evaluation": "linear",
        "below_domain": "clamp",
        "above_domain": "clamp",
        "samples": [
          {
            "input": {"value": 0, "unit": "deg"},
            "output": {"value": 0, "unit": "mm"}
          }
        ]
      },
      {
        "id": "ignition-timing",
        "input_dimension": "angular_speed",
        "output_dimension": "angle",
        "evaluation": "linear",
        "below_domain": "clamp",
        "above_domain": "clamp",
        "samples": [
          {
            "input": {"value": 1000, "unit": "rpm"},
            "output": {"value": 10, "unit": "deg"}
          }
        ]
      },
      {
        "id": "flame-speed",
        "input_dimension": "dimensionless",
        "output_dimension": "speed",
        "evaluation": "linear",
        "below_domain": "clamp",
        "above_domain": "clamp",
        "samples": [
          {
            "input": {"value": 0, "unit": "1"},
            "output": {"value": 1, "unit": "m/s"}
          }
        ]
      }
    ],
    "crankshafts": [
      {
        "id": "crank",
        "throw_radius": {"value": 42, "unit": "mm"},
        "mass": {"value": 12, "unit": "kg"},
        "flywheel_mass": {"value": 8, "unit": "kg"},
        "moment_of_inertia": {"value": 0.2, "unit": "kg*m2"},
        "tdc_reference_angle": {"value": 0, "unit": "deg"},
        "journals": ["journal"]
      }
    ],
    "journals": [
      {
        "id": "journal",
        "crankshaft": "crank",
        "phase": {"value": 0, "unit": "deg"}
      }
    ],
    "connecting_rods": [
      {
        "id": "rod",
        "length": {"value": 140, "unit": "mm"},
        "mass": {"value": 600, "unit": "g"},
        "moment_of_inertia": {"value": 0.001, "unit": "kg*m2"}
      }
    ],
    "pistons": [
      {
        "id": "piston",
        "mass": {"value": 450, "unit": "g"},
        "compression_height": {"value": 30, "unit": "mm"},
        "displacement_volume": {"value": 500, "unit": "cm3"}
      }
    ],
    "banks": [
      {
        "id": "bank",
        "angle": {"value": 0, "unit": "deg"},
        "bore": {"value": 84, "unit": "mm"},
        "deck_height": {"value": 220, "unit": "mm"},
        "head": "head"
      }
    ],
    "intakes": [
      {
        "id": "intake",
        "plenum_volume": {"value": 3, "unit": "L"},
        "plenum_cross_section_area": {"value": 20, "unit": "cm2"},
        "runner_length": {"value": 300, "unit": "mm"},
        "main_restriction": {
          "type": "orifice",
          "effective_area": {"value": 10, "unit": "cm2"},
          "discharge_coefficient_01": 0.8
        },
        "idle_bypass_restriction": {
          "type": "orifice",
          "effective_area": {"value": 1, "unit": "cm2"},
          "discharge_coefficient_01": 0.8
        },
        "runner_restriction": {
          "type": "orifice",
          "effective_area": {"value": 5, "unit": "cm2"},
          "discharge_coefficient_01": 0.8
        },
        "idle_throttle_position_01": 0.05,
        "runner_velocity_decay_01": 0.1
      }
    ],
    "exhausts": [
      {
        "id": "exhaust",
        "collector_cross_section_area": {"value": 20, "unit": "cm2"},
        "collector_length": {"value": 600, "unit": "mm"},
        "primary_tube_length": {"value": 500, "unit": "mm"},
        "primary_cross_section_area": {"value": 8, "unit": "cm2"},
        "outlet_restriction": {
          "type": "orifice",
          "effective_area": {"value": 12, "unit": "cm2"},
          "discharge_coefficient_01": 0.8
        },
        "primary_restriction": {
          "type": "orifice",
          "effective_area": {"value": 8, "unit": "cm2"},
          "discharge_coefficient_01": 0.8
        },
        "velocity_decay_01": 0.1
      }
    ],
    "ports": [
      {
        "id": "intake-port",
        "head": "head",
        "kind": "intake",
        "runner_volume": {"value": 100, "unit": "cm3"},
        "runner_cross_section_area": {"value": 5, "unit": "cm2"},
        "flow_curve": "port-flow"
      },
      {
        "id": "exhaust-port",
        "head": "head",
        "kind": "exhaust",
        "runner_volume": {"value": 100, "unit": "cm3"},
        "runner_cross_section_area": {"value": 5, "unit": "cm2"},
        "flow_curve": "port-flow"
      }
    ],
    "cam_lobes": [
      {
        "id": "intake-lobe",
        "cylinder": "cylinder",
        "port_kind": "intake",
        "centerline": {"value": 110, "unit": "deg"},
        "type": "sampled",
        "lift_curve": "lobe-lift"
      },
      {
        "id": "exhaust-lobe",
        "cylinder": "cylinder",
        "port_kind": "exhaust",
        "centerline": {"value": 250, "unit": "deg"},
        "type": "sampled",
        "lift_curve": "lobe-lift"
      }
    ],
    "camshafts": [
      {
        "id": "shared-cam",
        "advance": {"value": 0, "unit": "deg"},
        "base_radius": {"value": 15, "unit": "mm"},
        "lobes": ["intake-lobe", "exhaust-lobe"]
      }
    ],
    "valvetrains": [
      {
        "id": "valvetrain",
        "type": "standard",
        "intake_camshaft": "shared-cam",
        "exhaust_camshaft": "shared-cam"
      }
    ],
    "heads": [
      {
        "id": "head",
        "chamber_volume": {"value": 50, "unit": "cm3"},
        "valvetrain": "valvetrain",
        "ports": ["intake-port", "exhaust-port"]
      }
    ],
    "fuels": [
      {
        "id": "gasoline",
        "display_name": "Gasoline",
        "molecular_mass": {"value": 100, "unit": "g/mol"},
        "lower_heating_value": {"value": 44, "unit": "MJ/kg"},
        "stoichiometric_air_fuel_molar_ratio": 14.7,
        "turbulence_to_flame_speed": "flame-speed",
        "combustion": {
          "maximum_efficiency_01": 0.35,
          "cycle_variation_01": 0.02,
          "low_efficiency_attenuation_01": 0.1,
          "maximum_turbulence_effect": 2,
          "maximum_dilution_effect": 10
        }
      }
    ],
    "default_fuel": "gasoline",
    "losses": {
      "type": "chen_flynn_cycle_mean",
      "constant_fmep": {"value": 20, "unit": "kPa"},
      "peak_pressure_coefficient": 0.01,
      "mean_piston_speed_coefficient": {
        "value": 5,
        "unit": "kPa*s/m"
      },
      "mean_piston_speed_squared_coefficient": {
        "value": 1,
        "unit": "kPa*s2/m2"
      },
      "required_oil_temperature": {"value": 90, "unit": "degC"},
      "accessory_configuration_id": "warm-stock-accessories"
    },
    "ignition": {
      "timing_curve": "ignition-timing",
      "wires": [{"id": "wire"}],
      "firing_order": [
        {
          "wire": "wire",
          "crank_angle": {"value": 0, "unit": "deg"}
        }
      ],
      "limiter": {
        "activation_speed": {"value": 6500, "unit": "rpm"},
        "cut_duration": {"value": 50, "unit": "ms"}
      }
    },
    "starter": {"type": "mechanically_disengaged"},
    "cylinders": [
      {
        "id": "cylinder",
        "bank": "bank",
        "crankshaft": "crank",
        "journal": "journal",
        "connecting_rod": "rod",
        "piston": "piston",
        "intake": "intake",
        "exhaust": "exhaust",
        "ignition_wire": "wire",
        "intake_port": "intake-port",
        "exhaust_port": "exhaust-port",
        "exhaust_header_primary_length": {"value": 500, "unit": "mm"}
      }
    ],
    "source_routes": [
      {
        "id": "exhaust-route",
        "type": "exhaust",
        "exhaust": "exhaust"
      }
    ]
  },
  "presentation": {
    "assets": [],
    "cylinder_routes": [
      {
        "cylinder": "cylinder",
        "route": "exhaust-route",
        "gain_linear": 1
      }
    ],
    "routes": [
      {
        "route": "exhaust-route",
        "source_gain_linear": 1,
        "impulse_response_gain_linear": 1,
        "wet_mix_01": 0
      }
    ],
    "conditioning": {
      "jitter_scale": 0,
      "jitter_modulation_cutoff_frequency": {"value": 800, "unit": "Hz"},
      "derivative_mix_01": 0,
      "air_noise_mix_01": 0,
      "air_noise_cutoff_frequency": {"value": 8000, "unit": "Hz"}
    },
    "buses": [
      {
        "id": "main",
        "routes": ["exhaust-route"],
        "gain_linear": 1,
        "publish": true
      }
    ],
    "audition": {
      "buses": ["main"],
      "monitoring_gain_linear": 1,
      "fade_in": {"value": 10, "unit": "ms"},
      "fade_out": {"value": 10, "unit": "ms"}
    },
    "publication_gain_linear": 1
  }
})json";
}

[[nodiscard]] ScenarioDocument require_scenario(std::string_view json) {
    auto result = parse_scenario_document(json);
    if (const auto *report = std::get_if<DiagnosticReport>(&result)) {
        std::string message = "valid scenario document was rejected";
        if (!report->diagnostics.empty()) {
            message += " at " + report->diagnostics.front().json_pointer + ": " +
                       report->diagnostics.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<ScenarioDocument>(std::move(result));
}

[[nodiscard]] EnginePackageDocument require_engine(std::string_view json) {
    auto result = parse_engine_document(json);
    if (const auto *report = std::get_if<DiagnosticReport>(&result)) {
        std::string message = "valid engine package document was rejected";
        if (!report->diagnostics.empty()) {
            message += " at " + report->diagnostics.front().json_pointer + ": " +
                       report->diagnostics.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<EnginePackageDocument>(std::move(result));
}

[[nodiscard]] const DiagnosticReport &
require_report(const ScenarioDocumentParseResult &result) {
    const auto *report = std::get_if<DiagnosticReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{"invalid scenario document was accepted"};
    }
    return *report;
}

[[nodiscard]] const DiagnosticReport &
require_engine_report(const EngineDocumentParseResult &result) {
    const auto *report = std::get_if<DiagnosticReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{"invalid engine package document was accepted"};
    }
    return *report;
}

[[nodiscard]] bool has_diagnostic(const DiagnosticReport &report,
                                  DiagnosticCode code,
                                  std::string_view pointer) {
    for (const auto &diagnostic : report.diagnostics) {
        if (diagnostic.code == code && diagnostic.json_pointer == pointer) {
            return true;
        }
    }
    return false;
}

void replace_once(std::string &text, std::string_view before,
                  std::string_view after) {
    const auto position = text.find(before);
    if (position == std::string::npos) {
        throw std::runtime_error{"test fixture replacement target was absent"};
    }
    text.replace(position, before.size(), after);
}

void test_complete_scenario_and_exact_integer_wire_values() {
    const ScenarioDocument scenario = require_scenario(valid_scenario_json());
    expect(scenario.schema == "engine-sim-offline/scenario",
           "scenario schema changed");
    expect(scenario.public_seed == 18446744073709551615ULL,
           "uint64 decimal string lost precision");
    expect(scenario.rates.physics.numerator == 20000U &&
               scenario.rates.physics.denominator == 1U,
           "exact rational rate changed");
    expect(scenario.events.size() == 1U,
           "scenario event was not retained");
    const auto *conditioning =
        std::get_if<SetConditioningMonitoringEvent>(
            &scenario.events.front().payload);
    expect(conditioning != nullptr && conditioning->jitter_scale == 0.4 &&
               conditioning->derivative_mix_01 == 0.2 &&
               conditioning->air_noise_mix_01 == 0.1,
           "conditioning monitoring event changed");
}

void test_strict_paths_and_continuous_control_authority() {
    std::string unknown = valid_scenario_json();
    replace_once(unknown, R"json("schema": "engine-sim-offline/scenario",)json",
                 R"json("schema": "engine-sim-offline/scenario",
  "legacy_version": 2,)json");
    const auto unknown_result = parse_scenario_document(unknown);
    expect(has_diagnostic(require_report(unknown_result),
                          DiagnosticCode::unknown_field, "/legacy_version"),
           "unknown root field did not retain its JSON pointer");

    std::string continuous_event = valid_scenario_json();
    replace_once(continuous_event, "set_conditioning_monitoring",
                 "set_throttle");
    const auto continuous_result =
        parse_scenario_document(continuous_event);
    expect(has_diagnostic(require_report(continuous_result),
                          DiagnosticCode::invalid_value,
                          "/events/0/payload/type"),
           "forbidden continuous-control event was accepted");
}

void test_semantic_ranges_limits_and_mixed_duration_units() {
    std::string interval = valid_scenario_json();
    replace_once(interval,
                 R"json("audible_duration": {"value": 2000, "unit": "ms"})json",
                 R"json("audible_duration": {"value": 2001, "unit": "ms"})json");
    const auto interval_result = parse_scenario_document(interval);
    expect(has_diagnostic(require_report(interval_result),
                          DiagnosticCode::inconsistent_value,
                          "/audible_duration"),
           "mixed-unit audible interval overflow was accepted");

    AuthoringParseLimits limits;
    limits.maximum_process_block_capacity_frames = 1024U;
    const auto capacity_result =
        parse_scenario_document(valid_scenario_json(), limits);
    expect(has_diagnostic(require_report(capacity_result),
                          DiagnosticCode::resource_limit,
                          "/quality/process_block_capacity_frames"),
           "quality capacity limit was not enforced");
}

void test_syntax_diagnostic_location() {
    const auto result = parse_scenario_document("{\n  \"schema\": ]");
    const auto &report = require_report(result);
    expect(!report.diagnostics.empty() &&
               report.diagnostics.front().code ==
                   DiagnosticCode::malformed_document &&
               report.diagnostics.front().source_position.has_value() &&
               report.diagnostics.front().source_position->line == 2U,
           "syntax failure did not preserve source location");
}

void test_cross_document_reference_validation() {
    ScenarioDocument scenario = require_scenario(valid_scenario_json());
    EnginePackageDocument package;
    package.engine.identity.id.value = "test-engine";
    FuelDefinition fuel;
    fuel.id.value = "gasoline";
    package.engine.fuels.push_back(std::move(fuel));
    AudioBusDefinition bus;
    bus.id.value = "main";
    package.presentation.buses.push_back(std::move(bus));

    expect(validate_scenario_references(scenario, package).ok(),
           "resolved scenario references were rejected");
    scenario.output.buses.front().value = "missing";
    const auto report = validate_scenario_references(scenario, package);
    expect(has_diagnostic(report, DiagnosticCode::dangling_reference,
                          "/output/buses/0"),
           "dangling output bus did not retain its JSON pointer");
}

void test_engine_schema_identifier_is_strict() {
    const auto result = parse_engine_document(
        R"json({"schema":"engine-sim-offline/engine-v1","engine":{},"presentation":{}})json");
    const auto *report = std::get_if<DiagnosticReport>(&result);
    expect(report != nullptr &&
               has_diagnostic(*report, DiagnosticCode::unsupported_schema,
                              "/schema"),
           "legacy-like engine schema identifier was accepted");
}

void test_complete_engine_package() {
    const EnginePackageDocument package = require_engine(valid_engine_json());
    expect(package.schema == "engine-sim-offline/engine",
           "engine package schema changed");
    expect(package.engine.identity.id.value == "test-engine",
           "engine identity was not retained");
    expect(package.engine.cylinders.size() == 1U &&
               package.engine.source_routes.size() == 1U,
           "engine graph definitions were not retained");
    expect(std::holds_alternative<ChenFlynnLossDefinition>(
               package.engine.losses),
           "Chen-Flynn loss discriminator changed");
    expect(std::holds_alternative<MechanicallyDisengagedStarter>(
               package.engine.starter),
           "mechanically-disengaged starter discriminator changed");
    expect(!package.engine.throttle_controllers &&
               !package.engine.throttle_controller,
           "omitted future throttle-controller capability became authored");
}

void test_engine_duplicate_id_and_dangling_reference_paths() {
    std::string duplicate = valid_engine_json();
    replace_once(duplicate, R"json("id": "exhaust-port")json",
                 R"json("id": "intake-port")json");
    const auto duplicate_result = parse_engine_document(duplicate);
    expect(has_diagnostic(require_engine_report(duplicate_result),
                          DiagnosticCode::duplicate_id,
                          "/engine/ports/1/id"),
           "duplicate engine object ID did not retain its JSON pointer");

    std::string dangling = valid_engine_json();
    replace_once(dangling, R"json("piston": "piston")json",
                 R"json("piston": "missing-piston")json");
    const auto dangling_result = parse_engine_document(dangling);
    expect(has_diagnostic(require_engine_report(dangling_result),
                          DiagnosticCode::dangling_reference,
                          "/engine/cylinders/0/piston"),
           "dangling engine reference did not retain its JSON pointer");
}

} // namespace

int main() {
    try {
        test_complete_scenario_and_exact_integer_wire_values();
        test_strict_paths_and_continuous_control_authority();
        test_semantic_ranges_limits_and_mixed_duration_units();
        test_syntax_diagnostic_location();
        test_cross_document_reference_validation();
        test_engine_schema_identifier_is_strict();
        test_complete_engine_package();
        test_engine_duplicate_id_and_dangling_reference_paths();
        std::cout << "authoring document parser tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "authoring document parser test failure: "
                  << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}

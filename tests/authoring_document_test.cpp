#include "engine_sim_offline/authoring/parse.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
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
static_assert(noexcept(
    validate_scenario_references(std::declval<const ScenarioDocument &>(),
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
        "tdc_reference_angle": {"value": 0, "unit": "deg"}
      }
    ],
    "output_crankshaft": "crank",
    "journals": [
      {
        "id": "journal",
        "type": "crankshaft",
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
        "runner_velocity_decay_01": 0.1,
        "main_mixture_lambda": 0.8
      }
    ],
    "exhausts": [
      {
        "id": "exhaust",
        "collector_cross_section_area": {"value": 20, "unit": "cm2"},
        "collector_length": {"value": 600, "unit": "mm"},
        "primary_tube_length": {"value": 500, "unit": "mm"},
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
    "accessory_configurations": [
      {
        "id": "warm-stock-accessories",
        "uri": "accessories/warm-stock-accessories.json",
        "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
      }
    ],
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

[[nodiscard]] bool has_diagnostic(const DiagnosticReport &report, DiagnosticCode code,
                                  std::string_view pointer) {
    for (const auto &diagnostic : report.diagnostics) {
        if (diagnostic.code == code && diagnostic.json_pointer == pointer) {
            return true;
        }
    }
    return false;
}

void replace_once(std::string &text, std::string_view before, std::string_view after) {
    const auto position = text.find(before);
    if (position == std::string::npos) {
        throw std::runtime_error{"test fixture replacement target was absent"};
    }
    text.replace(position, before.size(), after);
}

[[nodiscard]] std::string valid_master_rod_engine_json() {
    std::string json = valid_engine_json();
    replace_once(json,
                 R"json(        "phase": {"value": 0, "unit": "deg"}
      }
    ],
    "connecting_rods")json",
                 R"json(        "phase": {"value": 0, "unit": "deg"}
      },
      {
        "id": "slave-journal",
        "type": "master_rod",
        "master_cylinder": "cylinder",
        "throw_radius": {"value": 30, "unit": "mm"},
        "phase": {"value": 72, "unit": "deg"}
      }
    ],
    "connecting_rods")json");
    replace_once(
        json,
        R"json(        "exhaust_header_primary_length": {"value": 500, "unit": "mm"}
      }
    ],
    "source_routes")json",
        R"json(        "exhaust_header_primary_length": {"value": 500, "unit": "mm"}
      },
      {
        "id": "slave-cylinder",
        "bank": "bank",
        "journal": "slave-journal",
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
    "source_routes")json");
    return json;
}

[[nodiscard]] std::string valid_engine_with_vehicle_rig_json() {
    std::string json = valid_engine_json();
    replace_once(json, "\n}", R"json(,
  "rig": {
    "id": "road-rig",
    "vehicle": {
      "id": "test-car",
      "mass": {"value": 1395, "unit": "kg"},
      "drag_coefficient": 0.29,
      "frontal_area": {"value": 2.05, "unit": "m2"},
      "differential_ratio": 2.93,
      "tire_radius": {"value": 0.315, "unit": "m"},
      "rolling_resistance_force": {"value": 165, "unit": "N"},
      "maximum_service_brake_force": {"value": 14500, "unit": "N"}
    },
    "transmission": {
      "id": "five-speed",
      "maximum_clutch_torque": {"value": 380, "unit": "N*m"},
      "gears": [
        {"id": "gear-1", "ratio": 4.21},
        {"id": "gear-2", "ratio": 2.49}
      ]
    }
  }
})json");
    return json;
}

[[nodiscard]] std::string valid_free_vehicle_scenario_json() {
    std::string json = valid_scenario_json();
    replace_once(json,
                 R"json(    "type": "held_speed",
    "target_engine_speed": {"value": 3000, "unit": "rpm"},
    "throttle_01": {
      "interpolation": "linear",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.5},
        {"time": {"value": 2000, "unit": "ms"}, "value": 1.0}
      ]
    })json",
                 R"json(    "type": "free_vehicle",
    "rig": "road-rig",
    "initial_gear": null,
    "initial_vehicle_speed": {"value": 36, "unit": "km/h"},
    "initial_clutch_engagement_01": 0.25,
    "initial_service_brake_application_01": 0.6,
    "throttle_01": {
      "interpolation": "right_continuous_hold",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.2},
        {"time": {"value": 1, "unit": "s"}, "value": 0.8}
      ]
    })json");
    return json;
}

void replace_monitoring_event_payload(std::string &json, std::string_view replacement) {
    replace_once(json,
                 R"json(      "payload": {
        "type": "set_conditioning_monitoring",
        "jitter_scale": 0.4,
        "derivative_mix_01": 0.2,
        "air_noise_mix_01": 0.1
      })json",
                 replacement);
}

[[nodiscard]] std::string valid_vtec_engine_json() {
    std::string json = valid_engine_json();
    replace_once(json,
                 R"json(      {
        "id": "valvetrain",
        "type": "standard",
        "intake_camshaft": "shared-cam",
        "exhaust_camshaft": "shared-cam"
      })json",
                 R"json(      {
        "id": "valvetrain",
        "type": "vtec",
        "base_intake_camshaft": "shared-cam",
        "base_exhaust_camshaft": "shared-cam",
        "alternate_intake_camshaft": "shared-cam",
        "alternate_exhaust_camshaft": "shared-cam",
        "activation": {
          "minimum_engine_speed": {"value": 5800, "unit": "rpm"},
          "minimum_manifold_pressure_abs": {"value": 84, "unit": "kPa"},
          "minimum_throttle_linkage_opening_01": 0.3
        }
      })json");
    return json;
}

void test_complete_scenario_and_exact_integer_wire_values() {
    const ScenarioDocument scenario = require_scenario(valid_scenario_json());
    expect(scenario.schema == "engine-sim-offline/scenario", "scenario schema changed");
    expect(scenario.public_seed == 18446744073709551615ULL,
           "uint64 decimal string lost precision");
    expect(scenario.rates.physics.numerator == 20000U &&
               scenario.rates.physics.denominator == 1U,
           "exact rational rate changed");
    expect(scenario.quality.process_block_capacity_frames == 4096U &&
               scenario.quality.event_queue_capacity == 64U &&
               scenario.quality.telemetry_capacity_frames == 96000U,
           "session process/control/telemetry capacities changed during parsing");
    expect(scenario.events.size() == 1U, "scenario event was not retained");
    const auto *conditioning =
        std::get_if<SetConditioningMonitoringEvent>(&scenario.events.front().payload);
    expect(conditioning != nullptr && conditioning->jitter_scale == 0.4 &&
               conditioning->derivative_mix_01 == 0.2 &&
               conditioning->air_noise_mix_01 == 0.1,
           "conditioning monitoring event changed");
}

void test_free_engine_requires_and_retains_throttle_trajectory() {
    std::string json = valid_scenario_json();
    replace_once(json,
                 R"json(    "type": "held_speed",
    "target_engine_speed": {"value": 3000, "unit": "rpm"},
    "throttle_01": {
      "interpolation": "linear",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.5},
        {"time": {"value": 2000, "unit": "ms"}, "value": 1.0}
      ]
    })json",
                 R"json(    "type": "free_engine",
    "attached_inertia": {"value": 0.25, "unit": "kg*m2"},
    "throttle_01": {
      "interpolation": "right_continuous_hold",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.2},
        {"time": {"value": 1, "unit": "s"}, "value": 0.8}
      ]
    },
    "external_resisting_torque": {
      "value_dimension": "torque",
      "interpolation": "right_continuous_hold",
      "points": [
        {
          "time": {"value": 0, "unit": "s"},
          "value": {"value": 12, "unit": "N*m"}
        }
      ]
    })json");

    const ScenarioDocument scenario = require_scenario(json);
    const auto *free_engine = std::get_if<FreeEngineMode>(&scenario.mode);
    expect(free_engine != nullptr && free_engine->attached_inertia.has_value() &&
               free_engine->attached_inertia->value == 0.25 &&
               free_engine->external_resisting_torque.has_value() &&
               free_engine->external_resisting_torque->points.front().value.value ==
                   12.0 &&
               free_engine->throttle_01.points.size() == 2U &&
               free_engine->throttle_01.points.front().value == 0.2 &&
               free_engine->throttle_01.points.back().value == 0.8,
           "free-engine attachment, resistance, or throttle was not retained");

    std::string omitted = valid_scenario_json();
    replace_once(omitted,
                 R"json(    "type": "held_speed",
    "target_engine_speed": {"value": 3000, "unit": "rpm"},
    "throttle_01": {
      "interpolation": "linear",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.5},
        {"time": {"value": 2000, "unit": "ms"}, "value": 1.0}
      ]
    })json",
                 R"json(    "type": "free_engine",
    "throttle_01": {
      "interpolation": "right_continuous_hold",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.2}
      ]
    })json");
    const auto omitted_scenario = require_scenario(omitted);
    const auto &omitted_free_engine = std::get<FreeEngineMode>(omitted_scenario.mode);
    expect(!omitted_free_engine.attached_inertia.has_value() &&
               !omitted_free_engine.external_resisting_torque.has_value(),
           "omitted free-engine attachment or resistance gained authored values");

    std::string legacy = omitted;
    replace_once(legacy, R"json(    "type": "free_engine",)json",
                 R"json(    "type": "free_engine",
    "equivalent_inertia": {"value": 0.25, "unit": "kg*m2"},
    "resisting_torque": {"value_dimension": "torque", "interpolation": "right_continuous_hold", "points": [{"time": {"value": 0, "unit": "s"}, "value": {"value": 0, "unit": "N*m"}}]},)json");
    const auto legacy_result = parse_scenario_document(legacy);
    const auto &legacy_report = require_report(legacy_result);
    expect(has_diagnostic(legacy_report, DiagnosticCode::unknown_field,
                          "/mode/equivalent_inertia") &&
               has_diagnostic(legacy_report, DiagnosticCode::unknown_field,
                              "/mode/resisting_torque"),
           "legacy free-engine inertia or torque names were accepted");

    replace_once(json, R"json(    "throttle_01": {
      "interpolation": "right_continuous_hold",
      "points": [
        {"time": {"value": 0, "unit": "s"}, "value": 0.2},
        {"time": {"value": 1, "unit": "s"}, "value": 0.8}
      ]
    },
)json",
                 "");
    const auto missing = parse_scenario_document(json);
    expect(has_diagnostic(require_report(missing), DiagnosticCode::missing_value,
                          "/mode/throttle_01"),
           "free-engine mode without a throttle trajectory was accepted");
}

void test_free_vehicle_vocabulary_is_explicit_and_greenfield() {
    const EnginePackageDocument package =
        require_engine(valid_engine_with_vehicle_rig_json());
    expect(package.rig.has_value() && package.rig->vehicle.has_value() &&
               package.rig->vehicle->maximum_service_brake_force.has_value() &&
               package.rig->vehicle->maximum_service_brake_force->value == 14500.0 &&
               package.rig->transmission.has_value() &&
               package.rig->transmission->gears.size() == 2U,
           "vehicle service-brake capability or forward gears were not retained");

    const ScenarioDocument neutral =
        require_scenario(valid_free_vehicle_scenario_json());
    const auto *mode = std::get_if<FreeVehicleMode>(&neutral.mode);
    expect(mode != nullptr && !mode->initial_gear.has_value() &&
               mode->initial_vehicle_speed.value == 36.0 &&
               mode->initial_vehicle_speed.unit == "km/h" &&
               mode->initial_clutch_engagement_01 == 0.25 &&
               mode->initial_service_brake_application_01 == 0.6 &&
               mode->throttle_01.points.size() == 2U,
           "neutral FreeVehicle initial state changed during parsing");
    expect(validate_scenario_references(neutral, package).ok(),
           "nullable neutral initial gear was treated as a dangling reference");

    std::string selected = valid_free_vehicle_scenario_json();
    replace_once(selected, R"json("initial_gear": null)json",
                 R"json("initial_gear": "gear-2")json");
    const ScenarioDocument selected_scenario = require_scenario(selected);
    const auto &selected_mode = std::get<FreeVehicleMode>(selected_scenario.mode);
    expect(selected_mode.initial_gear.has_value() &&
               selected_mode.initial_gear->value == "gear-2" &&
               validate_scenario_references(selected_scenario, package).ok(),
           "selected forward gear was not retained or resolved");

    std::string dangling = selected;
    replace_once(dangling, R"json("initial_gear": "gear-2")json",
                 R"json("initial_gear": "missing-gear")json");
    const auto dangling_report =
        validate_scenario_references(require_scenario(dangling), package);
    expect(has_diagnostic(dangling_report, DiagnosticCode::dangling_reference,
                          "/mode/initial_gear"),
           "dangling nullable initial gear lost its diagnostic path");

    std::string neutral_event = valid_free_vehicle_scenario_json();
    replace_monitoring_event_payload(
        neutral_event,
        R"json(      "payload": {"type": "select_gear", "gear": null})json");
    const ScenarioDocument neutral_event_scenario = require_scenario(neutral_event);
    const auto *select =
        std::get_if<SelectGearEvent>(&neutral_event_scenario.events.front().payload);
    expect(select != nullptr && !select->gear.has_value() &&
               validate_scenario_references(neutral_event_scenario, package).ok(),
           "nullable neutral gear event was not retained or reference-safe");

    std::string selected_event = neutral_event;
    replace_once(selected_event, R"json("gear": null)json",
                 R"json("gear": "gear-1")json");
    const ScenarioDocument selected_event_scenario = require_scenario(selected_event);
    const auto *selected_event_payload =
        std::get_if<SelectGearEvent>(&selected_event_scenario.events.front().payload);
    expect(selected_event_payload != nullptr &&
               selected_event_payload->gear.has_value() &&
               selected_event_payload->gear->value == "gear-1" &&
               validate_scenario_references(selected_event_scenario, package).ok(),
           "selected forward-gear event was not retained or resolved");

    std::string clutch = valid_free_vehicle_scenario_json();
    replace_monitoring_event_payload(clutch,
                                     R"json(      "payload": {
        "type": "set_clutch_engagement",
        "engagement_01": 0.75
      })json");
    const ScenarioDocument clutch_scenario = require_scenario(clutch);
    const auto &clutch_event =
        std::get<SetClutchEngagementEvent>(clutch_scenario.events.front().payload);
    expect(clutch_event.engagement_01 == 0.75,
           "clutch engagement event changed during parsing");

    std::string service_brake = valid_free_vehicle_scenario_json();
    replace_monitoring_event_payload(service_brake,
                                     R"json(      "payload": {
        "type": "set_service_brake_application",
        "application_01": 0.9
      })json");
    const ScenarioDocument service_brake_scenario = require_scenario(service_brake);
    const auto &brake_event = std::get<SetServiceBrakeApplicationEvent>(
        service_brake_scenario.events.front().payload);
    expect(brake_event.application_01 == 0.9,
           "service-brake application event changed during parsing");

    std::string invalid_brake = service_brake;
    replace_once(invalid_brake, R"json("application_01": 0.9)json",
                 R"json("application_01": 1.1)json");
    expect(has_diagnostic(require_report(parse_scenario_document(invalid_brake)),
                          DiagnosticCode::out_of_range,
                          "/events/0/payload/application_01"),
           "out-of-range service-brake application was accepted");

    std::string legacy_mode = valid_free_vehicle_scenario_json();
    replace_once(legacy_mode, "initial_clutch_engagement_01",
                 "initial_clutch_position_01");
    const auto legacy_mode_result = parse_scenario_document(legacy_mode);
    const auto &legacy_mode_report = require_report(legacy_mode_result);
    expect(has_diagnostic(legacy_mode_report, DiagnosticCode::unknown_field,
                          "/mode/initial_clutch_position_01") &&
               has_diagnostic(legacy_mode_report, DiagnosticCode::missing_value,
                              "/mode/initial_clutch_engagement_01"),
           "retired clutch-position mode vocabulary was accepted");

    std::string legacy_event = clutch;
    replace_once(legacy_event, "set_clutch_engagement", "set_clutch");
    expect(has_diagnostic(require_report(parse_scenario_document(legacy_event)),
                          DiagnosticCode::invalid_value, "/events/0/payload/type"),
           "retired set_clutch event vocabulary was accepted");

    std::string nullable_brake = valid_engine_with_vehicle_rig_json();
    replace_once(
        nullable_brake,
        R"json("maximum_service_brake_force": {"value": 14500, "unit": "N"})json",
        R"json("maximum_service_brake_force": null)json");
    const auto null_brake_package = require_engine(nullable_brake);
    expect(null_brake_package.rig->vehicle.has_value() &&
               !null_brake_package.rig->vehicle->maximum_service_brake_force,
           "null service-brake capability did not remain unavailable");

    std::string zero_brake = valid_engine_with_vehicle_rig_json();
    replace_once(zero_brake, R"json({"value": 14500, "unit": "N"})json",
                 R"json({"value": 0, "unit": "N"})json");
    expect(has_diagnostic(require_engine_report(parse_engine_document(zero_brake)),
                          DiagnosticCode::out_of_range,
                          "/rig/vehicle/maximum_service_brake_force/value"),
           "zero service-brake capacity was accepted");

    std::string reverse = valid_engine_with_vehicle_rig_json();
    replace_once(reverse, R"json({"id": "gear-2", "ratio": 2.49})json",
                 R"json({"id": "gear-2", "ratio": -2.49})json");
    expect(has_diagnostic(require_engine_report(parse_engine_document(reverse)),
                          DiagnosticCode::out_of_range,
                          "/rig/transmission/gears/1/ratio"),
           "negative reverse ratio was accepted as a forward gear");

    std::string zero_differential = valid_engine_with_vehicle_rig_json();
    replace_once(zero_differential, R"json("differential_ratio": 2.93)json",
                 R"json("differential_ratio": 0)json");
    expect(
        has_diagnostic(require_engine_report(parse_engine_document(zero_differential)),
                       DiagnosticCode::out_of_range, "/rig/vehicle/differential_ratio"),
        "zero differential ratio was accepted by the forward drivetrain");
}

void test_strict_paths_and_continuous_control_authority() {
    std::string unknown = valid_scenario_json();
    replace_once(unknown, R"json("schema": "engine-sim-offline/scenario",)json",
                 R"json("schema": "engine-sim-offline/scenario",
  "legacy_version": 2,)json");
    const auto unknown_result = parse_scenario_document(unknown);
    expect(has_diagnostic(require_report(unknown_result), DiagnosticCode::unknown_field,
                          "/legacy_version"),
           "unknown root field did not retain its JSON pointer");

    std::string continuous_event = valid_scenario_json();
    replace_once(continuous_event, "set_conditioning_monitoring", "set_throttle");
    const auto continuous_result = parse_scenario_document(continuous_event);
    expect(has_diagnostic(require_report(continuous_result),
                          DiagnosticCode::invalid_value, "/events/0/payload/type"),
           "forbidden continuous-control event was accepted");
}

void test_semantic_ranges_limits_and_mixed_duration_units() {
    std::string interval = valid_scenario_json();
    replace_once(interval,
                 R"json("audible_duration": {"value": 2000, "unit": "ms"})json",
                 R"json("audible_duration": {"value": 2001, "unit": "ms"})json");
    const auto interval_result = parse_scenario_document(interval);
    expect(has_diagnostic(require_report(interval_result),
                          DiagnosticCode::inconsistent_value, "/audible_duration"),
           "mixed-unit audible interval overflow was accepted");

    AuthoringParseLimits limits;
    limits.maximum_process_block_capacity_frames = 1024U;
    const auto capacity_result = parse_scenario_document(valid_scenario_json(), limits);
    expect(has_diagnostic(require_report(capacity_result),
                          DiagnosticCode::resource_limit,
                          "/quality/process_block_capacity_frames"),
           "quality capacity limit was not enforced");

    std::string empty_control_queue = valid_scenario_json();
    replace_once(empty_control_queue, R"json("event_queue_capacity": 64)json",
                 R"json("event_queue_capacity": 0)json");
    const auto empty_control_queue_result =
        parse_scenario_document(empty_control_queue);
    expect(has_diagnostic(require_report(empty_control_queue_result),
                          DiagnosticCode::out_of_range,
                          "/quality/event_queue_capacity"),
           "zero caller control-command capacity was accepted");

    std::string empty_telemetry = valid_scenario_json();
    replace_once(empty_telemetry, R"json("telemetry_capacity_frames": 96000)json",
                 R"json("telemetry_capacity_frames": 0)json");
    const auto empty_telemetry_result = parse_scenario_document(empty_telemetry);
    expect(has_diagnostic(require_report(empty_telemetry_result),
                          DiagnosticCode::out_of_range,
                          "/quality/telemetry_capacity_frames"),
           "zero returned telemetry capacity was accepted");
}

void test_syntax_diagnostic_location() {
    const auto result = parse_scenario_document("{\n  \"schema\": ]");
    const auto &report = require_report(result);
    expect(!report.diagnostics.empty() &&
               report.diagnostics.front().code == DiagnosticCode::malformed_document &&
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
    expect(
        has_diagnostic(report, DiagnosticCode::dangling_reference, "/output/buses/0"),
        "dangling output bus did not retain its JSON pointer");
}

void test_engine_schema_identifier_is_strict() {
    const auto result = parse_engine_document(
        R"json({"schema":"engine-sim-offline/engine-v1","engine":{},"presentation":{}})json");
    const auto *report = std::get_if<DiagnosticReport>(&result);
    expect(report != nullptr &&
               has_diagnostic(*report, DiagnosticCode::unsupported_schema, "/schema"),
           "legacy-like engine schema identifier was accepted");
}

void test_complete_engine_package() {
    const EnginePackageDocument package = require_engine(valid_engine_json());
    expect(package.schema == "engine-sim-offline/engine",
           "engine package schema changed");
    expect(package.engine.identity.id.value == "test-engine",
           "engine identity was not retained");
    expect(package.engine.crankshafts.size() == 1U &&
               package.engine.crankshafts.front().id.value == "crank" &&
               package.engine.output_crankshaft.value == "crank",
           "ordered crankshaft definitions or explicit output reference were not "
           "retained");
    expect(package.engine.cylinders.size() == 1U &&
               package.engine.source_routes.size() == 1U,
           "engine graph definitions were not retained");
    expect(
        package.engine.accessory_configurations.size() == 1U &&
            package.engine.accessory_configurations.front().id.value ==
                "warm-stock-accessories" &&
            package.engine.accessory_configurations.front().uri ==
                "accessories/warm-stock-accessories.json" &&
            package.engine.accessory_configurations.front().sha256 ==
                std::optional<std::string>{
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
        "accessory-configuration definition was not retained");
    expect(std::holds_alternative<ChenFlynnLossDefinition>(package.engine.losses),
           "Chen-Flynn loss discriminator changed");
    expect(
        std::holds_alternative<MechanicallyDisengagedStarter>(package.engine.starter),
        "mechanically-disengaged starter discriminator changed");
    expect(!package.engine.throttle_controllers && !package.engine.throttle_controller,
           "omitted future throttle-controller capability became authored");
}

void test_output_crankshaft_reference_is_explicit_and_ordered() {
    std::string missing = valid_engine_json();
    replace_once(missing, R"json(    "output_crankshaft": "crank",
)json",
                 "");
    expect(has_diagnostic(require_engine_report(parse_engine_document(missing)),
                          DiagnosticCode::missing_value, "/engine/output_crankshaft"),
           "engine without an explicit output crankshaft was accepted");

    std::string dangling = valid_engine_json();
    replace_once(dangling, R"json("output_crankshaft": "crank")json",
                 R"json("output_crankshaft": "missing-crank")json");
    expect(has_diagnostic(require_engine_report(parse_engine_document(dangling)),
                          DiagnosticCode::dangling_reference,
                          "/engine/output_crankshaft"),
           "dangling output crankshaft reference was accepted");

    std::string multiple = valid_engine_json();
    replace_once(multiple,
                 R"json(        "tdc_reference_angle": {"value": 0, "unit": "deg"}
      }
    ],
    "output_crankshaft": "crank")json",
                 R"json(        "tdc_reference_angle": {"value": 0, "unit": "deg"}
      },
      {
        "id": "crank-secondary",
        "throw_radius": {"value": 42, "unit": "mm"},
        "mass": {"value": 12, "unit": "kg"},
        "flywheel_mass": {"value": 8, "unit": "kg"},
        "moment_of_inertia": {"value": 0.2, "unit": "kg*m2"},
        "tdc_reference_angle": {"value": 0, "unit": "deg"}
      }
    ],
    "output_crankshaft": "crank-secondary")json");
    const auto package = require_engine(multiple);
    expect(package.engine.crankshafts.size() == 2U &&
               package.engine.crankshafts[0].id.value == "crank" &&
               package.engine.crankshafts[1].id.value == "crank-secondary" &&
               package.engine.output_crankshaft.value == "crank-secondary",
           "authoring parser collapsed crankshaft identity, order, or output "
           "selection");
}

void test_intake_main_mixture_lambda_is_required_positive_and_greenfield() {
    const auto package = require_engine(valid_engine_json());
    expect(package.engine.intakes.size() == 1U &&
               package.engine.intakes.front().main_mixture_lambda == 0.8,
           "intake main-mixture lambda was not retained");

    std::string missing = valid_engine_json();
    replace_once(missing,
                 R"json("runner_velocity_decay_01": 0.1,
        "main_mixture_lambda": 0.8)json",
                 R"json("runner_velocity_decay_01": 0.1)json");
    expect(has_diagnostic(require_engine_report(parse_engine_document(missing)),
                          DiagnosticCode::missing_value,
                          "/engine/intakes/0/main_mixture_lambda"),
           "intake without a main-mixture lambda was accepted");

    std::string zero = valid_engine_json();
    replace_once(zero, R"json("main_mixture_lambda": 0.8)json",
                 R"json("main_mixture_lambda": 0)json");
    expect(has_diagnostic(require_engine_report(parse_engine_document(zero)),
                          DiagnosticCode::out_of_range,
                          "/engine/intakes/0/main_mixture_lambda"),
           "nonpositive intake main-mixture lambda was accepted");

    std::string duplicated_fuel_authority = valid_engine_json();
    replace_once(duplicated_fuel_authority,
                 R"json("main_mixture_lambda": 0.8)json",
                 R"json("main_mixture_lambda": 0.8,
        "molecular_afr": 12.5)json");
    expect(has_diagnostic(
               require_engine_report(
                   parse_engine_document(duplicated_fuel_authority)),
               DiagnosticCode::unknown_field,
               "/engine/intakes/0/molecular_afr"),
           "duplicate intake molecular-AFR fuel authority was accepted");
}

void test_vtec_activation_contract_is_greenfield_and_strict() {
    const auto package = require_engine(valid_vtec_engine_json());
    const auto *vtec =
        std::get_if<VtecValvetrain>(&package.engine.valvetrains.front().kind);
    expect(vtec != nullptr && vtec->base_intake_camshaft.value == "shared-cam" &&
               vtec->base_exhaust_camshaft.value == "shared-cam" &&
               vtec->alternate_intake_camshaft.value == "shared-cam" &&
               vtec->alternate_exhaust_camshaft.value == "shared-cam" &&
               vtec->activation.minimum_engine_speed.value == 5800.0 &&
               vtec->activation.minimum_engine_speed.unit == "rpm" &&
               vtec->activation.minimum_manifold_pressure_abs.value == 84.0 &&
               vtec->activation.minimum_manifold_pressure_abs.unit == "kPa" &&
               vtec->activation.minimum_throttle_linkage_opening_01 == 0.3,
           "VTEC cam references or activation thresholds were not retained");

    std::string retired_vehicle_speed = valid_vtec_engine_json();
    replace_once(
        retired_vehicle_speed,
        R"json(          "minimum_engine_speed": {"value": 5800, "unit": "rpm"},)json",
        R"json(          "minimum_engine_speed": {"value": 5800, "unit": "rpm"},
          "minimum_vehicle_speed": {"value": 10, "unit": "mph"},)json");
    expect(has_diagnostic(
               require_engine_report(parse_engine_document(retired_vehicle_speed)),
               DiagnosticCode::unknown_field,
               "/engine/valvetrains/0/activation/minimum_vehicle_speed"),
           "pristine's stored-but-unused VTEC vehicle-speed input was accepted");

    std::string retired_manifold_name = valid_vtec_engine_json();
    replace_once(retired_manifold_name, "minimum_manifold_pressure_abs",
                 "minimum_manifold_vacuum");
    expect(has_diagnostic(
               require_engine_report(parse_engine_document(retired_manifold_name)),
               DiagnosticCode::unknown_field,
               "/engine/valvetrains/0/activation/minimum_manifold_vacuum"),
           "retired VTEC manifold-vacuum field name was accepted");

    std::string retired_throttle_name = valid_vtec_engine_json();
    replace_once(retired_throttle_name, "minimum_throttle_linkage_opening_01",
                 "minimum_throttle_01");
    expect(has_diagnostic(
               require_engine_report(parse_engine_document(retired_throttle_name)),
               DiagnosticCode::unknown_field,
               "/engine/valvetrains/0/activation/minimum_throttle_01"),
           "retired ambiguous VTEC throttle field name was accepted");

    std::string wrong_speed_dimension = valid_vtec_engine_json();
    replace_once(wrong_speed_dimension, R"json({"value": 5800, "unit": "rpm"})json",
                 R"json({"value": 5800, "unit": "km/h"})json");
    expect(has_diagnostic(
               require_engine_report(parse_engine_document(wrong_speed_dimension)),
               DiagnosticCode::invalid_unit,
               "/engine/valvetrains/0/activation/minimum_engine_speed/unit"),
           "VTEC engine-speed threshold accepted a linear-speed unit");

    std::string wrong_pressure_dimension = valid_vtec_engine_json();
    replace_once(wrong_pressure_dimension, R"json({"value": 84, "unit": "kPa"})json",
                 R"json({"value": 84, "unit": "rpm"})json");
    expect(has_diagnostic(
               require_engine_report(parse_engine_document(wrong_pressure_dimension)),
               DiagnosticCode::invalid_unit,
               "/engine/valvetrains/0/activation/minimum_manifold_pressure_abs/unit"),
           "VTEC manifold-pressure threshold accepted an angular-speed unit");

    std::string negative_speed = valid_vtec_engine_json();
    replace_once(negative_speed, R"json("value": 5800, "unit": "rpm")json",
                 R"json("value": -1, "unit": "rpm")json");
    expect(
        has_diagnostic(require_engine_report(parse_engine_document(negative_speed)),
                       DiagnosticCode::out_of_range,
                       "/engine/valvetrains/0/activation/minimum_engine_speed/value"),
        "negative VTEC engine-speed threshold was accepted");

    std::string zero_pressure = valid_vtec_engine_json();
    replace_once(zero_pressure, R"json("value": 84, "unit": "kPa")json",
                 R"json("value": 0, "unit": "kPa")json");
    expect(has_diagnostic(
               require_engine_report(parse_engine_document(zero_pressure)),
               DiagnosticCode::out_of_range,
               "/engine/valvetrains/0/activation/minimum_manifold_pressure_abs/value"),
           "nonpositive VTEC absolute manifold-pressure threshold was accepted");

    std::string invalid_throttle = valid_vtec_engine_json();
    replace_once(invalid_throttle,
                 R"json("minimum_throttle_linkage_opening_01": 0.3)json",
                 R"json("minimum_throttle_linkage_opening_01": 1.1)json");
    expect(
        has_diagnostic(require_engine_report(parse_engine_document(invalid_throttle)),
                       DiagnosticCode::out_of_range,
                       "/engine/valvetrains/0/activation/"
                       "minimum_throttle_linkage_opening_01"),
        "out-of-range VTEC throttle-linkage threshold was accepted");
}

void test_cranking_starter_contract_is_minimal_and_strict() {
    std::string cranking = valid_engine_json();
    replace_once(
        cranking, R"json("starter": {"type": "mechanically_disengaged"})json",
        R"json("starter": {"type": "cranking", "torque": {"value": 110, "unit": "lb*ft"}, "target_speed": {"value": 240, "unit": "rpm"}})json");
    const auto package = require_engine(cranking);
    const auto *starter = std::get_if<CrankingStarter>(&package.engine.starter);
    expect(starter != nullptr && starter->torque.value == 110.0 &&
               starter->torque.unit == "lb*ft" &&
               starter->target_speed.value == 240.0 &&
               starter->target_speed.unit == "rpm",
           "cranking starter torque or target speed was not retained");

    std::string retired_release = cranking;
    replace_once(
        retired_release, R"json("target_speed": {"value": 240, "unit": "rpm"})json",
        R"json("target_speed": {"value": 240, "unit": "rpm"}, "release_speed": {"value": 500, "unit": "rpm"})json");
    const auto retired_release_result = parse_engine_document(retired_release);
    expect(has_diagnostic(require_engine_report(retired_release_result),
                          DiagnosticCode::unknown_field,
                          "/engine/starter/release_speed"),
           "retired starter release_speed field was accepted");

    std::string zero_torque = cranking;
    replace_once(zero_torque, R"json("torque": {"value": 110, "unit": "lb*ft"})json",
                 R"json("torque": {"value": 0, "unit": "N*m"})json");
    const auto zero_torque_result = parse_engine_document(zero_torque);
    expect(has_diagnostic(require_engine_report(zero_torque_result),
                          DiagnosticCode::out_of_range, "/engine/starter/torque/value"),
           "zero-torque cranking starter was accepted");
}

void test_direct_journal_attachment_contract_is_unambiguous() {
    const auto package = require_engine(valid_engine_json());
    expect(package.engine.journals.size() == 1U &&
               std::get<CrankshaftJournalAttachment>(
                   package.engine.journals.front().attachment)
                       .crankshaft.value == "crank" &&
               package.engine.cylinders.size() == 1U &&
               package.engine.cylinders.front().journal.value == "journal",
           "direct journal ownership or cylinder attachment changed during parsing");

    std::string missing_type = valid_engine_json();
    replace_once(missing_type, R"json(        "type": "crankshaft",
)json",
                 "");
    expect(has_diagnostic(require_engine_report(parse_engine_document(missing_type)),
                          DiagnosticCode::missing_value, "/engine/journals/0/type"),
           "direct journal without its required type discriminator was accepted");

    const auto master_package = require_engine(valid_master_rod_engine_json());
    const auto *master = std::get_if<MasterRodJournalAttachment>(
        &master_package.engine.journals[1].attachment);
    expect(master != nullptr && master->master_cylinder.value == "cylinder" &&
               master->throw_radius.value == 30.0 &&
               master_package.engine.journals[1].phase.value == 72.0 &&
               master_package.engine.cylinders[1].journal.value == "slave-journal",
           "master_rod journal shape or graph references changed during parsing");

    std::string wrong_type = valid_engine_json();
    replace_once(wrong_type, R"json("type": "crankshaft")json",
                 R"json("type": "gearbox")json");
    expect(has_diagnostic(require_engine_report(parse_engine_document(wrong_type)),
                          DiagnosticCode::invalid_value, "/engine/journals/0/type"),
           "unknown journal attachment variant was accepted");

    const auto expect_retired_field = [](std::string json, std::string_view before,
                                         std::string_view after,
                                         std::string_view pointer) {
        replace_once(json, before, after);
        expect(has_diagnostic(require_engine_report(parse_engine_document(json)),
                              DiagnosticCode::unknown_field, pointer),
               std::string{"retired direct-mechanism field was accepted at "} +
                   std::string{pointer});
    };

    expect_retired_field(
        valid_engine_json(),
        R"json("tdc_reference_angle": {"value": 0, "unit": "deg"})json",
        R"json("tdc_reference_angle": {"value": 0, "unit": "deg"}, "journals": ["journal"])json",
        "/engine/crankshafts/0/journals");
    expect_retired_field(valid_engine_json(), R"json("crankshaft": "crank",
        "phase")json",
                         R"json("crankshaft": "crank", "master_journal": "journal",
        "phase")json",
                         "/engine/journals/0/master_journal");
    expect_retired_field(
        valid_engine_json(), R"json("crankshaft": "crank",
        "phase")json",
        R"json("crankshaft": "crank", "slave_throw": {"value": 10, "unit": "mm"},
        "phase")json",
        "/engine/journals/0/slave_throw");
    expect_retired_field(
        valid_engine_json(),
        R"json("moment_of_inertia": {"value": 0.001, "unit": "kg*m2"})json",
        R"json("moment_of_inertia": {"value": 0.001, "unit": "kg*m2"}, "slave_throw": {"value": 10, "unit": "mm"})json",
        "/engine/connecting_rods/0/slave_throw");
    expect_retired_field(valid_engine_json(), R"json("bank": "bank",
        "journal")json",
                         R"json("bank": "bank", "crankshaft": "crank",
        "journal")json",
                         "/engine/cylinders/0/crankshaft");
    expect_retired_field(valid_engine_json(), R"json("journal": "journal",
        "connecting_rod")json",
                         R"json("journal": "journal", "slave_journal": "journal",
        "connecting_rod")json",
                         "/engine/cylinders/0/slave_journal");
}

void test_exhaust_primary_area_has_single_greenfield_owner() {
    std::string duplicate_area = valid_engine_json();
    replace_once(duplicate_area,
                 R"json("primary_tube_length": {"value": 500, "unit": "mm"},)json",
                 R"json("primary_tube_length": {"value": 500, "unit": "mm"},
        "primary_cross_section_area": {"value": 8, "unit": "cm2"},)json");
    expect(has_diagnostic(require_engine_report(parse_engine_document(duplicate_area)),
                          DiagnosticCode::unknown_field,
                          "/engine/exhausts/0/primary_cross_section_area"),
           "retired exhaust-system primary area compatibility field was accepted");
}

void test_engine_duplicate_id_and_dangling_reference_paths() {
    std::string duplicate = valid_engine_json();
    replace_once(duplicate, R"json("id": "exhaust-port")json",
                 R"json("id": "intake-port")json");
    const auto duplicate_result = parse_engine_document(duplicate);
    expect(has_diagnostic(require_engine_report(duplicate_result),
                          DiagnosticCode::duplicate_id, "/engine/ports/1/id"),
           "duplicate engine object ID did not retain its JSON pointer");

    std::string dangling = valid_engine_json();
    replace_once(dangling, R"json("piston": "piston")json",
                 R"json("piston": "missing-piston")json");
    const auto dangling_result = parse_engine_document(dangling);
    expect(has_diagnostic(require_engine_report(dangling_result),
                          DiagnosticCode::dangling_reference,
                          "/engine/cylinders/0/piston"),
           "dangling engine reference did not retain its JSON pointer");

    std::string dangling_accessory = valid_engine_json();
    replace_once(dangling_accessory,
                 R"json("accessory_configuration_id": "warm-stock-accessories")json",
                 R"json("accessory_configuration_id": "missing-accessories")json");
    const auto dangling_accessory_result = parse_engine_document(dangling_accessory);
    expect(has_diagnostic(require_engine_report(dangling_accessory_result),
                          DiagnosticCode::dangling_reference,
                          "/engine/losses/accessory_configuration_id"),
           "dangling accessory-configuration reference did not retain its JSON "
           "pointer");
}

} // namespace

int main() {
    try {
        test_complete_scenario_and_exact_integer_wire_values();
        test_free_engine_requires_and_retains_throttle_trajectory();
        test_free_vehicle_vocabulary_is_explicit_and_greenfield();
        test_strict_paths_and_continuous_control_authority();
        test_semantic_ranges_limits_and_mixed_duration_units();
        test_syntax_diagnostic_location();
        test_cross_document_reference_validation();
        test_engine_schema_identifier_is_strict();
        test_complete_engine_package();
        test_output_crankshaft_reference_is_explicit_and_ordered();
        test_intake_main_mixture_lambda_is_required_positive_and_greenfield();
        test_vtec_activation_contract_is_greenfield_and_strict();
        test_cranking_starter_contract_is_minimal_and_strict();
        test_direct_journal_attachment_contract_is_unambiguous();
        test_exhaust_primary_area_has_single_greenfield_owner();
        test_engine_duplicate_id_and_dangling_reference_paths();
        std::cout << "authoring document parser tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "authoring document parser test failure: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}

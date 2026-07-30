#include "parity_driver.h"

#include "engine_sim_offline/c_api.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define ESO_WASM_PARITY_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define ESO_WASM_PARITY_EXPORT
#endif

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr std::array<std::uint8_t, 8> kBundleMagic{'E', 'S', 'O', 'W',
                                                   'P', 'A', 'R', '\0'};
constexpr std::uint32_t kBundleVersion = 1;
constexpr std::uint32_t kNumericStride = 21;

enum class DriverStatus : std::uint32_t {
    success = 0,
    invalid_input = 1000,
    context = 1001,
    engine = 1002,
    engine_id = 1003,
    scenario = 1004,
    scenario_id = 1005,
    session = 1006,
    descriptor = 1007,
    identity = 1008,
    bus = 1009,
    preparation_control = 1010,
    live_control = 1011,
    process = 1012,
    terminal_control = 1013,
    transcript = 1014,
    output_capacity = 1015,
    unexpected_exception = 1016,
};

class Context {
  public:
    Context() = default;
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    ~Context() {
        if (value_ != nullptr) {
            (void)eso_context_destroy(value_);
        }
    }

    [[nodiscard]] eso_context_t **output() noexcept {
        return &value_;
    }

    [[nodiscard]] eso_context_t *get() const noexcept {
        return value_;
    }

  private:
    eso_context_t *value_ = nullptr;
};

class Handles {
  public:
    explicit Handles(eso_context_t *context) : context_(context) {}
    Handles(const Handles &) = delete;
    Handles &operator=(const Handles &) = delete;

    ~Handles() {
        if (session != ESO_INVALID_HANDLE) {
            (void)eso_destroy_session(context_, session);
        }
        if (scenario != ESO_INVALID_HANDLE) {
            (void)eso_destroy_scenario(context_, scenario);
        }
        if (engine != ESO_INVALID_HANDLE) {
            (void)eso_destroy_engine(context_, engine);
        }
    }

    eso_engine_handle_t engine = ESO_INVALID_HANDLE;
    eso_scenario_handle_t scenario = ESO_INVALID_HANDLE;
    eso_session_handle_t session = ESO_INVALID_HANDLE;

  private:
    eso_context_t *context_ = nullptr;
};

[[nodiscard]] bool valid_input(const std::uint8_t *data,
                               const std::uint32_t size) noexcept {
    return data != nullptr || size == 0U;
}

[[nodiscard]] eso_utf8_view_t utf8(const std::uint8_t *data,
                                   const std::uint32_t size) noexcept {
    return {reinterpret_cast<const char *>(data), static_cast<std::size_t>(size)};
}

[[nodiscard]] eso_byte_view_t bytes(const std::uint8_t *data,
                                    const std::uint32_t size) noexcept {
    return {data, static_cast<std::size_t>(size)};
}

void append_json_string(std::string &output, const std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    output.push_back('"');
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20U) {
                output += "\\u00";
                output.push_back(digits[character >> 4U]);
                output.push_back(digits[character & 0x0fU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

template <class Integer> void append_integer(std::string &output, const Integer value) {
    static_assert(std::is_integral_v<Integer>);
    std::array<char, 32> buffer{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        throw DriverStatus::transcript;
    }
    output.append(buffer.data(), result.ptr);
}

void append_boolean(std::string &output, const std::uint32_t value) {
    output += value == 0U ? "false" : "true";
}

void append_torque_state(std::string &output, const eso_torque_value_nm_t &value) {
    output.push_back('[');
    append_integer(output, value.availability);
    output.push_back(',');
    append_integer(output, value.completeness);
    output.push_back(',');
    append_integer(output, value.unavailable_reason);
    output.push_back(',');
    append_integer(output, value.included_terms);
    output.push_back(',');
    append_integer(output, value.omitted_terms);
    output.push_back(']');
}

void append_quantity_state(std::string &output, const eso_quantity_value_t &value) {
    output.push_back('[');
    append_integer(output, value.availability);
    output.push_back(',');
    append_integer(output, value.completeness);
    output.push_back(',');
    append_integer(output, value.unavailable_reason);
    output.push_back(']');
}

void append_telemetry_state(std::string &output, const eso_engine_telemetry_t &value) {
    output.push_back('[');
    append_integer(output, value.physics_step_end);
    output.push_back(',');
    append_integer(output, value.engine_step_end_index);
    output.push_back(',');
    append_integer(output, value.validity_mask);
    output.push_back(',');
    append_integer(output, value.ignition_enabled);
    output.push_back(',');
    append_integer(output, value.fuel_enabled);
    output.push_back(',');
    append_integer(output, value.starter_enabled);
    output.push_back(',');
    append_integer(output, value.dyno_enabled);
    output.push_back(',');
    append_integer(output, value.limiter_enabled);
    output.push_back(',');
    append_integer(output, value.limiter_cut_active);

    const std::array<const eso_torque_value_nm_t *, 8> torque{
        &value.torque.instantaneous_indicated_gas,
        &value.torque.pumping_partition,
        &value.torque.friction_pump_and_accessory,
        &value.torque.starter,
        &value.torque.instantaneous_net_shaft,
        &value.torque.cycle_mean_net_shaft,
        &value.torque.actuator,
        &value.torque.dyno_reaction,
    };
    for (const auto *item : torque) {
        output.push_back(',');
        append_torque_state(output, *item);
    }

    const std::array<const eso_quantity_value_t *, 4> quantity{
        &value.torque.cycle_work_j,
        &value.torque.net_bmep_pa,
        &value.torque.instantaneous_power_w,
        &value.torque.cycle_mean_power_w,
    };
    for (const auto *item : quantity) {
        output.push_back(',');
        append_quantity_state(output, *item);
    }
    output.push_back(']');
}

void append_telemetry_numeric(std::vector<double> &output,
                              const eso_engine_telemetry_t &value) {
    output.insert(output.end(), {
                                    value.theta_rad,
                                    value.theta_cycle_rad,
                                    value.angular_speed_rad_s,
                                    value.angular_acceleration_rad_s2,
                                    value.engine_speed_rpm,
                                    value.requested_throttle_01,
                                    value.resolved_engine_throttle_01,
                                    value.intake_plate_position_01,
                                    value.main_flow_multiplier_01,
                                    value.requested_external_resisting_torque_nm,
                                    value.torque.instantaneous_indicated_gas.value_nm,
                                    value.torque.pumping_partition.value_nm,
                                    value.torque.friction_pump_and_accessory.value_nm,
                                    value.torque.starter.value_nm,
                                    value.torque.instantaneous_net_shaft.value_nm,
                                    value.torque.cycle_mean_net_shaft.value_nm,
                                    value.torque.actuator.value_nm,
                                    value.torque.dyno_reaction.value_nm,
                                    value.torque.cycle_work_j.value,
                                    value.torque.net_bmep_pa.value,
                                    value.torque.instantaneous_power_w.value,
                                    value.torque.cycle_mean_power_w.value,
                                });
}

void append_bus(std::string &output, const eso_audio_bus_descriptor_t &bus,
                const std::string_view id) {
    output += "{\"kind\":";
    append_integer(output, bus.kind);
    output += ",\"channels\":";
    append_integer(output, bus.channel_count);
    output += ",\"rate\":[";
    append_integer(output, bus.sample_rate_numerator_hz);
    output.push_back(',');
    append_integer(output, bus.sample_rate_denominator);
    output += "],\"route\":";
    if (bus.has_route_id != 0U) {
        append_integer(output, bus.route_id);
    } else {
        output += "null";
    }
    output += ",\"id\":";
    append_json_string(output, id);
    output.push_back('}');
}

void append_block(std::string &output, const eso_process_info_t &process,
                  const eso_engine_telemetry_t &telemetry) {
    output.push_back('[');
    append_integer(output, process.block_phase);
    output.push_back(',');
    append_integer(output, process.block_ordinal);
    output.push_back(',');
    append_integer(output, process.first_physics_frame);
    output.push_back(',');
    append_integer(output, process.physics_frame_count);
    output.push_back(',');
    append_integer(output, process.first_delivery_frame);
    output.push_back(',');
    append_integer(output, process.delivery_frame_count);
    output.push_back(',');
    append_integer(output, process.telemetry_written);
    output.push_back(',');
    append_telemetry_state(output, telemetry);
    output.push_back(']');
}

template <class Value>
void append_binary(std::vector<std::uint8_t> &output, const Value value) {
    static_assert(std::is_trivially_copyable_v<Value>);
    const auto previous = output.size();
    output.resize(previous + sizeof(Value));
    std::memcpy(output.data() + previous, &value, sizeof(Value));
}

void append_binary_bytes(std::vector<std::uint8_t> &output, const void *data,
                         const std::size_t size) {
    const auto previous = output.size();
    output.resize(previous + size);
    if (size != 0U) {
        std::memcpy(output.data() + previous, data, size);
    }
}

[[nodiscard]] std::string copy_engine_id(eso_context_t *context,
                                         const eso_engine_handle_t engine) {
    std::size_t size = 0;
    if (eso_engine_copy_id(context, engine, {nullptr, 0U}, &size) != ESO_STATUS_OK) {
        throw DriverStatus::engine_id;
    }
    std::string result(size + 1U, '\0');
    if (eso_engine_copy_id(context, engine, {result.data(), result.size()}, &size) !=
        ESO_STATUS_OK) {
        throw DriverStatus::engine_id;
    }
    result.resize(size);
    return result;
}

[[nodiscard]] std::string copy_scenario_id(eso_context_t *context,
                                           const eso_scenario_handle_t scenario) {
    std::size_t size = 0;
    if (eso_scenario_copy_id(context, scenario, {nullptr, 0U}, &size) !=
        ESO_STATUS_OK) {
        throw DriverStatus::scenario_id;
    }
    std::string result(size + 1U, '\0');
    if (eso_scenario_copy_id(context, scenario, {result.data(), result.size()},
                             &size) != ESO_STATUS_OK) {
        throw DriverStatus::scenario_id;
    }
    result.resize(size);
    return result;
}

[[nodiscard]] std::string copy_bus_id(eso_context_t *context,
                                      const eso_session_handle_t session,
                                      const std::uint32_t bus_index,
                                      const std::size_t size) {
    std::string result(size + 1U, '\0');
    if (eso_session_copy_audio_bus_id(context, session, bus_index,
                                      {result.data(), result.size()}) !=
        ESO_STATUS_OK) {
        throw DriverStatus::bus;
    }
    result.resize(size);
    return result;
}

[[nodiscard]] DriverStatus run(const std::uint8_t *engine_json,
                               const std::uint32_t engine_json_size,
                               const std::uint8_t *scenario_json,
                               const std::uint32_t scenario_json_size,
                               const std::uint8_t *impulse_response_id,
                               const std::uint32_t impulse_response_id_size,
                               const std::uint8_t *impulse_response_bytes,
                               const std::uint32_t impulse_response_byte_count,
                               const std::uint8_t *accessory_configuration_id,
                               const std::uint32_t accessory_configuration_id_size,
                               const std::uint8_t *accessory_configuration_bytes,
                               const std::uint32_t accessory_configuration_byte_count,
                               std::vector<std::uint8_t> &bundle) {
    eso_abi_layout_t abi{};
    if (eso_api_version() != ESO_C_API_VERSION ||
        eso_get_abi_layout(&abi) != ESO_STATUS_OK ||
        abi.api_version != ESO_C_API_VERSION || abi.little_endian != 1U) {
        return DriverStatus::context;
    }

    Context owner;
    if (eso_context_create(ESO_C_API_VERSION, owner.output()) != ESO_STATUS_OK ||
        owner.get() == nullptr) {
        return DriverStatus::context;
    }
    Handles handles{owner.get()};

    const std::array assets{
        eso_asset_payload_t{
            ESO_ASSET_AUDIO,
            utf8(impulse_response_id, impulse_response_id_size),
            bytes(impulse_response_bytes, impulse_response_byte_count),
        },
        eso_asset_payload_t{
            ESO_ASSET_ACCESSORY_CONFIGURATION,
            utf8(accessory_configuration_id, accessory_configuration_id_size),
            bytes(accessory_configuration_bytes, accessory_configuration_byte_count),
        },
    };
    if (eso_compile_engine_json(owner.get(), utf8(engine_json, engine_json_size),
                                assets.data(), assets.size(),
                                &handles.engine) != ESO_STATUS_OK) {
        return DriverStatus::engine;
    }
    const auto engine_id = copy_engine_id(owner.get(), handles.engine);

    if (eso_compile_scenario_json(owner.get(), handles.engine,
                                  utf8(scenario_json, scenario_json_size),
                                  &handles.scenario) != ESO_STATUS_OK) {
        return DriverStatus::scenario;
    }
    const auto scenario_id = copy_scenario_id(owner.get(), handles.scenario);

    if (eso_create_session(owner.get(), handles.scenario, &handles.session) !=
        ESO_STATUS_OK) {
        return DriverStatus::session;
    }

    eso_session_descriptor_t descriptor{};
    if (eso_session_get_descriptor(owner.get(), handles.session, &descriptor) !=
            ESO_STATUS_OK ||
        descriptor.maximum_telemetry_frames_per_process_call == 0U ||
        descriptor.maximum_delivery_frames_per_process_call == 0U ||
        descriptor.audio_bus_count == 0U) {
        return DriverStatus::descriptor;
    }

    std::string identity_engine(descriptor.engine_id_utf8_bytes + 1U, '\0');
    std::string identity_scenario(descriptor.scenario_id_utf8_bytes + 1U, '\0');
    eso_session_identity_buffers_t identity{
        {identity_engine.data(), identity_engine.size()},
        {identity_scenario.data(), identity_scenario.size()},
    };
    if (eso_session_copy_identity(owner.get(), handles.session, &identity) !=
        ESO_STATUS_OK) {
        return DriverStatus::identity;
    }
    identity_engine.resize(descriptor.engine_id_utf8_bytes);
    identity_scenario.resize(descriptor.scenario_id_utf8_bytes);

    std::vector<eso_audio_bus_descriptor_t> buses(descriptor.audio_bus_count);
    std::vector<std::string> bus_ids;
    bus_ids.reserve(descriptor.audio_bus_count);
    std::uint32_t audition_bus_index = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t index = 0; index < descriptor.audio_bus_count; ++index) {
        if (eso_session_get_audio_bus_descriptor(owner.get(), handles.session, index,
                                                 &buses[index]) != ESO_STATUS_OK) {
            return DriverStatus::bus;
        }
        bus_ids.push_back(copy_bus_id(owner.get(), handles.session, index,
                                      buses[index].id_utf8_bytes));
        if (buses[index].kind == ESO_AUDIO_BUS_ENGINE_AUDITION_MASTER) {
            if (audition_bus_index != std::numeric_limits<std::uint32_t>::max()) {
                return DriverStatus::bus;
            }
            audition_bus_index = index;
        }
    }
    if (audition_bus_index == std::numeric_limits<std::uint32_t>::max() ||
        buses[audition_bus_index].channel_count != 1U) {
        return DriverStatus::bus;
    }

    eso_control_rejection_t preparation_rejection{};
    const eso_control_command_t preparation_command{0U, 1U,  ESO_CONTROL_THROTTLE,
                                                    0U, 0.5, 0U};
    const auto preparation_status = eso_session_enqueue_controls(
        owner.get(), handles.session, &preparation_command, 1U, &preparation_rejection);
    if (preparation_status != ESO_STATUS_CONTROL_REJECTED ||
        preparation_rejection.code !=
            ESO_ERROR_CONTROL_UNAVAILABLE_DURING_PREPARATION ||
        preparation_rejection.command_index != 0U) {
        return DriverStatus::preparation_control;
    }

    if (descriptor.preparation_block_count > std::numeric_limits<std::uint64_t>::max() /
                                                 descriptor.delivery_frames_per_block) {
        return DriverStatus::live_control;
    }
    const auto first_audible_frame =
        descriptor.preparation_block_count * descriptor.delivery_frames_per_block;
    const std::array live_commands{
        eso_control_command_t{first_audible_frame, 1U, ESO_CONTROL_THROTTLE, 0U, 0.5,
                              0U},
        eso_control_command_t{first_audible_frame, 2U, ESO_CONTROL_IGNITION_ENABLED, 1U,
                              0.0, 0U},
        eso_control_command_t{first_audible_frame, 3U, ESO_CONTROL_FUEL_ENABLED, 1U,
                              0.0, 0U},
    };
    eso_control_rejection_t live_rejection{};
    const auto live_status =
        eso_session_enqueue_controls(owner.get(), handles.session, live_commands.data(),
                                     live_commands.size(), &live_rejection);
    if (live_status != ESO_STATUS_OK || live_rejection.code != ESO_ERROR_NONE) {
        return DriverStatus::live_control;
    }

    std::string metadata;
    metadata.reserve(16384U);
    metadata += "{\"format\":\"engine-sim-offline-wasm-parity-v1\",";
    metadata += "\"abi\":{\"api_version\":";
    append_integer(metadata, abi.api_version);
    metadata += ",\"pointer_size\":";
    append_integer(metadata, abi.pointer_size_bytes);
    metadata += ",\"size_type_size\":";
    append_integer(metadata, abi.size_type_size_bytes);
    metadata += ",\"float_size\":";
    append_integer(metadata, abi.float_size_bytes);
    metadata += ",\"double_size\":";
    append_integer(metadata, abi.double_size_bytes);
    metadata += ",\"little_endian\":";
    append_boolean(metadata, abi.little_endian);
    metadata += ",\"control_size\":";
    append_integer(metadata, abi.control_command_size_bytes);
    metadata += ",\"descriptor_size\":";
    append_integer(metadata, abi.session_descriptor_size_bytes);
    metadata += ",\"bus_descriptor_size\":";
    append_integer(metadata, abi.audio_bus_descriptor_size_bytes);
    metadata += ",\"telemetry_size\":";
    append_integer(metadata, abi.engine_telemetry_size_bytes);
    metadata += "},\"semantic\":{\"engine_id\":";
    append_json_string(metadata, engine_id);
    metadata += ",\"scenario_id\":";
    append_json_string(metadata, scenario_id);
    metadata += ",\"session_identity\":[";
    append_json_string(metadata, identity_engine);
    metadata.push_back(',');
    append_json_string(metadata, identity_scenario);
    metadata += "],\"descriptor\":[";
    append_integer(metadata, descriptor.maximum_delivery_frames_per_process_call);
    metadata.push_back(',');
    append_integer(metadata, descriptor.control_command_queue_capacity);
    metadata.push_back(',');
    append_integer(metadata, descriptor.maximum_telemetry_frames_per_process_call);
    metadata.push_back(',');
    append_integer(metadata, descriptor.physics_rate_numerator_hz);
    metadata.push_back(',');
    append_integer(metadata, descriptor.physics_rate_denominator);
    metadata.push_back(',');
    append_integer(metadata, descriptor.delivery_rate_numerator_hz);
    metadata.push_back(',');
    append_integer(metadata, descriptor.delivery_rate_denominator);
    metadata.push_back(',');
    append_integer(metadata, descriptor.physics_frames_per_block);
    metadata.push_back(',');
    append_integer(metadata, descriptor.delivery_frames_per_block);
    metadata.push_back(',');
    append_integer(metadata, descriptor.total_block_count);
    metadata.push_back(',');
    append_integer(metadata, descriptor.preparation_block_count);
    metadata.push_back(',');
    append_integer(metadata, descriptor.audio_bus_count);
    metadata.push_back(',');
    append_integer(metadata, descriptor.live_control_capabilities);
    metadata += "],\"buses\":[";
    for (std::size_t index = 0; index < buses.size(); ++index) {
        if (index != 0U) {
            metadata.push_back(',');
        }
        append_bus(metadata, buses[index], bus_ids[index]);
    }
    metadata += "],\"controls\":{\"preparation\":[";
    append_integer(metadata, preparation_status);
    metadata.push_back(',');
    append_integer(metadata, preparation_rejection.code);
    metadata.push_back(',');
    append_integer(metadata, preparation_rejection.command_index);
    metadata += "],\"live\":[";
    append_integer(metadata, live_status);
    metadata.push_back(',');
    append_integer(metadata, live_rejection.code);
    metadata.push_back(',');
    append_integer(metadata, live_rejection.command_index);
    metadata += "]},\"numeric_stride\":";
    append_integer(metadata, kNumericStride);
    metadata += ",\"blocks\":[";

    const auto samples_per_block =
        static_cast<std::size_t>(descriptor.delivery_frames_per_block) *
        buses[audition_bus_index].channel_count;
    std::vector<float> block_audio(samples_per_block);
    std::vector<float> audible_pcm;
    const auto audible_blocks =
        descriptor.total_block_count - descriptor.preparation_block_count;
    if (audible_blocks <= std::numeric_limits<std::size_t>::max() / samples_per_block) {
        audible_pcm.reserve(static_cast<std::size_t>(audible_blocks) *
                            samples_per_block);
    }
    std::vector<eso_engine_telemetry_t> telemetry(
        descriptor.maximum_telemetry_frames_per_process_call);
    std::vector<double> numeric;
    numeric.reserve(static_cast<std::size_t>(descriptor.total_block_count) *
                    descriptor.maximum_telemetry_frames_per_process_call *
                    kNumericStride);

    bool first_block = true;
    eso_process_info_t completed{};
    for (;;) {
        eso_audio_copy_buffer_t audio{audition_bus_index, block_audio.data(),
                                      block_audio.size(), 0U};
        eso_process_info_t process{};
        const auto status =
            eso_session_process(owner.get(), handles.session, &audio, 1U,
                                telemetry.data(), telemetry.size(), &process);
        if (status != ESO_STATUS_OK) {
            return DriverStatus::process;
        }
        if (process.kind == ESO_PROCESS_COMPLETED) {
            completed = process;
            break;
        }
        if (process.kind != ESO_PROCESS_BLOCK || process.telemetry_written != 1U ||
            audio.samples_written != samples_per_block) {
            return DriverStatus::process;
        }
        if (!first_block) {
            metadata.push_back(',');
        }
        first_block = false;
        append_block(metadata, process, telemetry[0]);
        append_telemetry_numeric(numeric, telemetry[0]);
        if (process.block_phase == ESO_BLOCK_AUDIBLE) {
            audible_pcm.insert(audible_pcm.end(), block_audio.begin(),
                               block_audio.end());
        }
    }

    metadata += "],\"completion\":[";
    append_integer(metadata, completed.completed_physics_frame_count);
    metadata.push_back(',');
    append_integer(metadata, completed.completed_delivery_frame_count);
    metadata.push_back(',');
    append_integer(metadata, completed.completed_block_count);
    metadata.push_back(',');
    append_integer(metadata, completed.live_controls_accepted);
    metadata.push_back(',');
    append_integer(metadata, completed.has_held_speed_operating_point);
    metadata.push_back(',');
    append_integer(metadata, completed.has_inertial_dyno_result);
    metadata += ']';

    eso_control_rejection_t terminal_rejection{};
    const eso_control_command_t terminal_command{
        first_audible_frame, 4U, ESO_CONTROL_THROTTLE, 0U, 0.5, 0U};
    const auto terminal_status = eso_session_enqueue_controls(
        owner.get(), handles.session, &terminal_command, 1U, &terminal_rejection);
    if (terminal_status != ESO_STATUS_CONTROL_REJECTED ||
        terminal_rejection.code != ESO_ERROR_CONTROL_SESSION_TERMINAL ||
        terminal_rejection.command_index != 0U) {
        return DriverStatus::terminal_control;
    }
    metadata += ",\"terminal_control\":[";
    append_integer(metadata, terminal_status);
    metadata.push_back(',');
    append_integer(metadata, terminal_rejection.code);
    metadata.push_back(',');
    append_integer(metadata, terminal_rejection.command_index);
    metadata += "]}}";

    if (metadata.size() > std::numeric_limits<std::uint32_t>::max() ||
        numeric.size() > std::numeric_limits<std::uint32_t>::max() ||
        audible_pcm.size() > std::numeric_limits<std::uint32_t>::max()) {
        return DriverStatus::transcript;
    }

    bundle.reserve(kBundleMagic.size() + sizeof(std::uint32_t) * 4U + metadata.size() +
                   numeric.size() * sizeof(double) +
                   audible_pcm.size() * sizeof(float));
    append_binary_bytes(bundle, kBundleMagic.data(), kBundleMagic.size());
    append_binary(bundle, kBundleVersion);
    append_binary(bundle, static_cast<std::uint32_t>(metadata.size()));
    append_binary(bundle, static_cast<std::uint32_t>(numeric.size()));
    append_binary(bundle, static_cast<std::uint32_t>(audible_pcm.size()));
    append_binary_bytes(bundle, metadata.data(), metadata.size());
    append_binary_bytes(bundle, numeric.data(), numeric.size() * sizeof(double));
    append_binary_bytes(bundle, audible_pcm.data(), audible_pcm.size() * sizeof(float));
    return DriverStatus::success;
}

} // namespace

extern "C" ESO_WASM_PARITY_EXPORT uint32_t eso_wasm_parity_run(
    const uint8_t *const engine_json, const uint32_t engine_json_size,
    const uint8_t *const scenario_json, const uint32_t scenario_json_size,
    const uint8_t *const impulse_response_id, const uint32_t impulse_response_id_size,
    const uint8_t *const impulse_response_bytes,
    const uint32_t impulse_response_byte_count,
    const uint8_t *const accessory_configuration_id,
    const uint32_t accessory_configuration_id_size,
    const uint8_t *const accessory_configuration_bytes,
    const uint32_t accessory_configuration_byte_count, uint8_t *const output,
    const uint32_t output_capacity, uint32_t *const out_output_size) {
    if (out_output_size == nullptr) {
        return static_cast<std::uint32_t>(DriverStatus::invalid_input);
    }
    *out_output_size = 0U;
    if (!valid_input(engine_json, engine_json_size) ||
        !valid_input(scenario_json, scenario_json_size) ||
        !valid_input(impulse_response_id, impulse_response_id_size) ||
        !valid_input(impulse_response_bytes, impulse_response_byte_count) ||
        !valid_input(accessory_configuration_id, accessory_configuration_id_size) ||
        !valid_input(accessory_configuration_bytes,
                     accessory_configuration_byte_count) ||
        (output == nullptr && output_capacity != 0U)) {
        return static_cast<std::uint32_t>(DriverStatus::invalid_input);
    }

    try {
        std::vector<std::uint8_t> bundle;
        const auto status =
            run(engine_json, engine_json_size, scenario_json, scenario_json_size,
                impulse_response_id, impulse_response_id_size, impulse_response_bytes,
                impulse_response_byte_count, accessory_configuration_id,
                accessory_configuration_id_size, accessory_configuration_bytes,
                accessory_configuration_byte_count, bundle);
        if (status != DriverStatus::success) {
            return static_cast<std::uint32_t>(status);
        }
        if (bundle.size() > std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(DriverStatus::transcript);
        }
        *out_output_size = static_cast<std::uint32_t>(bundle.size());
        if (bundle.size() > output_capacity) {
            return static_cast<std::uint32_t>(DriverStatus::output_capacity);
        }
        if (!bundle.empty()) {
            std::memcpy(output, bundle.data(), bundle.size());
        }
        return static_cast<std::uint32_t>(DriverStatus::success);
    } catch (const DriverStatus status) {
        return static_cast<std::uint32_t>(status);
    } catch (...) {
        return static_cast<std::uint32_t>(DriverStatus::unexpected_exception);
    }
}

#undef ESO_WASM_PARITY_EXPORT

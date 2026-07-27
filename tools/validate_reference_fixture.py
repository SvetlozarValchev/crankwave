#!/usr/bin/env python3
"""Validate the frozen BMW M52B28 P1.8 two-lane reference fixture.

The validator intentionally has no dependency on engine-sim code.  It parses the
three little-endian fixture files, validates their closed schema, and independently
replays every redundant audit value from the physical/reference-parity lane.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import hashlib
import json
import math
import mmap
from pathlib import Path
import stat
import struct
import sys
from typing import Iterable, Mapping, Sequence


PARITY_FILENAME = "reference-parity.bin"
AUDIT_FILENAME = "reference-audit.bin"
SEED_FILENAME = "component-seeds.bin"

PARITY_MAGIC = b"ESOPAR01"
AUDIT_MAGIC = b"ESOAUD01"
SEED_MAGIC = b"ESOSEED1"
SCHEMA_VERSION = 1

EXPECTED_CYLINDER_COUNT = 6
EXPECTED_BUS_COUNT = 2
EXPECTED_PHYSICS_RATE_HZ = 10_000
EXPECTED_RECORD_COUNT = 170_000
EXPECTED_AUDIBLE_START = 20_000
EXPECTED_AUDIBLE_END = 170_000

PARITY_BASE_HEADER = struct.Struct("<8s6I3Q9d")
PARITY_CYLINDER = struct.Struct("<6I7d")
PARITY_RECORD_PREFIX = struct.Struct("<QQ7d4BI")
PARITY_PRESSURES = struct.Struct("<3d")
AUDIT_HEADER = struct.Struct("<8s6I4Q")
SEED_HEADER = struct.Struct("<8s8I")
SEED_PAIR = struct.Struct("<QQ")

EXPECTED_RANDOM_KEY = (
    "272121adec1fe448dd149b05990e477a816a7e6191352854d7a1dafd92183f5d"
)
PUBLIC_SEED = 12_648_430
SEED_DERIVATION = "sha256_length_prefixed_capture_component_pcg32_v1"
RANDOM_KEY_DOMAIN = "engine-sim-offline-capture-random-key-v1"
COMPONENT_SEED_DOMAIN = "engine-sim-offline-capture-component-seed-v1"
CAPTURE_ID = "baked.loaded_acceleration"
MAX_CANONICAL_STREAM = (1 << 63) - 1

# Runtime cylinder order is authoring order 1..6.  These values lock the narrow
# BMW oracle rather than accepting a merely schema-compatible, different engine.
EXPECTED_FIRING_RANKS = (0, 4, 2, 5, 1, 3)
EXPECTED_ROUTES = (1, 0, 1, 0, 1, 0)
EXPECTED_FIRING_ANGLE_BITS = (
    0x0000000000000000,
    0x4020C152382D749C,
    0x4010C152382D749C,
    0x4024F1A6C638D1C3,
    0x4000C152382D749C,
    0x401921FB54442EEA,
)
EXPECTED_EXHAUST_LENGTH_BITS = 0x4018AB47E0B3FFC9
EXPECTED_GAS_PRIMARY_LENGTH_BITS = 0x3FE04189374BC6A8

HASH_ALIASES = {
    "parity": PARITY_FILENAME,
    PARITY_FILENAME: PARITY_FILENAME,
    "audit": AUDIT_FILENAME,
    AUDIT_FILENAME: AUDIT_FILENAME,
    "seed": SEED_FILENAME,
    "seeds": SEED_FILENAME,
    SEED_FILENAME: SEED_FILENAME,
}


class ValidationError(Exception):
    """The fixture does not satisfy its frozen contract."""


@dataclass(frozen=True)
class ParityHeader:
    header_bytes: int
    cylinder_count: int
    bus_count: int
    record_bytes: int
    rate_hz: int
    record_count: int
    audible_start: int
    audible_end: int
    atmosphere_pa: float
    propagation_speed_m_s: float
    excitation_scale: float
    speed_threshold_rpm: float
    static_pressure_gain: float
    forward_pressure_gain: float
    reverse_pressure_gain: float
    cylinder_divisor: float
    inverse_length_exponent: float


@dataclass(frozen=True)
class CylinderDescriptor:
    runtime_index: int
    stable_id: int
    firing_rank: int
    route: int
    delay_samples: int
    firing_angle_rad: float
    header_length_m: float
    exhaust_length_m: float
    total_length_m: float
    gas_primary_length_not_delay_m: float
    sound_attenuation: float
    route_audio_volume: float


@dataclass(frozen=True)
class AuditHeader:
    header_bytes: int
    cylinder_count: int
    bus_count: int
    record_bytes: int
    rate_hz: int
    record_count: int
    start: int
    end: int


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def double_bits(value: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def require_finite(value: float, field: str) -> None:
    require(math.isfinite(value), f"{field}: non-finite binary64 value")


def require_same_bits(actual: float, expected: float, field: str) -> None:
    actual_bits = double_bits(actual)
    expected_bits = double_bits(expected)
    require(
        actual_bits == expected_bits,
        (
            f"{field}: binary64 mismatch "
            f"(actual={actual!r}/0x{actual_bits:016x}, "
            f"expected={expected!r}/0x{expected_bits:016x})"
        ),
    )


def require_bits(actual: float, expected_bits: int, field: str) -> None:
    actual_bits = double_bits(actual)
    require(
        actual_bits == expected_bits,
        (
            f"{field}: binary64 mismatch "
            f"(actual={actual!r}/0x{actual_bits:016x}, "
            f"expected_bits=0x{expected_bits:016x})"
        ),
    )


def require_regular_file(path: Path) -> int:
    try:
        info = path.stat()
    except OSError as error:
        raise ValidationError(f"{path}: cannot stat fixture file: {error}") from error
    require(stat.S_ISREG(info.st_mode), f"{path}: not a regular file")
    require(info.st_size > 0, f"{path}: empty fixture file")
    return info.st_size


def require_file_size(path: Path, actual: int, expected: int) -> None:
    require(
        actual == expected,
        f"{path}: size is {actual} bytes, expected exactly {expected}",
    )


def parse_parity_header(
    data: mmap.mmap,
    path: Path,
    file_size: int,
) -> tuple[ParityHeader, list[CylinderDescriptor]]:
    require(
        file_size >= PARITY_BASE_HEADER.size,
        f"{path}: truncated parity base header",
    )
    values = PARITY_BASE_HEADER.unpack_from(data, 0)
    (
        magic,
        version,
        header_bytes,
        cylinder_count,
        bus_count,
        record_bytes,
        rate_hz,
        record_count,
        audible_start,
        audible_end,
        atmosphere,
        propagation_speed,
        excitation_scale,
        speed_threshold,
        static_gain,
        forward_gain,
        reverse_gain,
        cylinder_divisor,
        inverse_length_exponent,
    ) = values

    require(magic == PARITY_MAGIC, f"{path}: bad parity magic {magic!r}")
    require(version == SCHEMA_VERSION, f"{path}: unsupported parity version {version}")
    require(
        cylinder_count == EXPECTED_CYLINDER_COUNT,
        f"{path}: cylinder count {cylinder_count}, expected 6",
    )
    require(
        bus_count == EXPECTED_BUS_COUNT,
        f"{path}: bus count {bus_count}, expected 2",
    )
    expected_header_bytes = 128 + 80 * cylinder_count
    expected_record_bytes = 80 + 24 * cylinder_count
    require(
        header_bytes == expected_header_bytes,
        f"{path}: parity header_bytes {header_bytes}, expected {expected_header_bytes}",
    )
    require(
        record_bytes == expected_record_bytes,
        f"{path}: parity record_bytes {record_bytes}, expected {expected_record_bytes}",
    )
    require(rate_hz == EXPECTED_PHYSICS_RATE_HZ, f"{path}: physics rate is {rate_hz}")
    require(
        record_count == EXPECTED_RECORD_COUNT,
        f"{path}: record count {record_count}, expected 170000",
    )
    require(
        (audible_start, audible_end)
        == (EXPECTED_AUDIBLE_START, EXPECTED_AUDIBLE_END),
        (
            f"{path}: audible interval [{audible_start},{audible_end}), "
            "expected [20000,170000)"
        ),
    )
    require(
        0 <= audible_start <= audible_end <= record_count,
        f"{path}: invalid audible interval",
    )
    require_file_size(
        path,
        file_size,
        header_bytes + record_bytes * record_count,
    )

    named_constants = (
        ("atmosphere_pa", atmosphere, 101_325.0),
        ("propagation_speed_m_s", propagation_speed, 343.0),
        ("excitation_scale", excitation_scale, 1600.0),
        ("speed_threshold_rpm", speed_threshold, 40.0),
        ("static_pressure_gain", static_gain, 1.0),
        ("forward_pressure_gain", forward_gain, 0.1),
        ("reverse_pressure_gain", reverse_gain, 0.1),
        ("cylinder_divisor", cylinder_divisor, 6.0),
        ("inverse_length_exponent", inverse_length_exponent, 2.0),
    )
    for name, actual, expected in named_constants:
        require_finite(actual, f"{path}:{name}")
        require_same_bits(actual, expected, f"{path}:{name}")

    header = ParityHeader(
        header_bytes=header_bytes,
        cylinder_count=cylinder_count,
        bus_count=bus_count,
        record_bytes=record_bytes,
        rate_hz=rate_hz,
        record_count=record_count,
        audible_start=audible_start,
        audible_end=audible_end,
        atmosphere_pa=atmosphere,
        propagation_speed_m_s=propagation_speed,
        excitation_scale=excitation_scale,
        speed_threshold_rpm=speed_threshold,
        static_pressure_gain=static_gain,
        forward_pressure_gain=forward_gain,
        reverse_pressure_gain=reverse_gain,
        cylinder_divisor=cylinder_divisor,
        inverse_length_exponent=inverse_length_exponent,
    )

    descriptors: list[CylinderDescriptor] = []
    for cylinder in range(cylinder_count):
        offset = 128 + cylinder * PARITY_CYLINDER.size
        (
            runtime_index,
            stable_id,
            firing_rank,
            route,
            delay_samples,
            reserved,
            firing_angle,
            header_length,
            exhaust_length,
            total_length,
            gas_primary_length,
            sound_attenuation,
            audio_volume,
        ) = PARITY_CYLINDER.unpack_from(data, offset)
        prefix = f"{path}:cylinder[{cylinder}]"

        require(runtime_index == cylinder, f"{prefix}: wrong runtime index {runtime_index}")
        require(stable_id == cylinder + 1, f"{prefix}: wrong stable ID {stable_id}")
        require(
            firing_rank == EXPECTED_FIRING_RANKS[cylinder],
            f"{prefix}: wrong firing rank {firing_rank}",
        )
        require(route == EXPECTED_ROUTES[cylinder], f"{prefix}: wrong route {route}")
        require(route < bus_count, f"{prefix}: route {route} is out of range")
        require(delay_samples == 180, f"{prefix}: delay is {delay_samples}, expected 180")
        require(reserved == 0, f"{prefix}: reserved field is nonzero")

        static_floats = (
            ("firing_angle_rad", firing_angle),
            ("header_length_m", header_length),
            ("exhaust_length_m", exhaust_length),
            ("total_length_m", total_length),
            ("gas_primary_length_not_delay_m", gas_primary_length),
            ("sound_attenuation", sound_attenuation),
            ("route_audio_volume", audio_volume),
        )
        for name, value in static_floats:
            require_finite(value, f"{prefix}:{name}")

        require_bits(
            firing_angle,
            EXPECTED_FIRING_ANGLE_BITS[cylinder],
            f"{prefix}:firing_angle_rad",
        )
        require_same_bits(header_length, 0.0, f"{prefix}:header_length_m")
        require_bits(
            exhaust_length,
            EXPECTED_EXHAUST_LENGTH_BITS,
            f"{prefix}:exhaust_length_m",
        )
        require_bits(
            gas_primary_length,
            EXPECTED_GAS_PRIMARY_LENGTH_BITS,
            f"{prefix}:gas_primary_length_not_delay_m",
        )
        require_same_bits(sound_attenuation, 1.0, f"{prefix}:sound_attenuation")
        expected_volume = 1.0 if route == 1 else 0.5
        require_same_bits(audio_volume, expected_volume, f"{prefix}:route_audio_volume")

        replayed_total = header_length + exhaust_length
        require_same_bits(total_length, replayed_total, f"{prefix}:total_length_m")
        require(total_length > 0.0, f"{prefix}: total length is not positive")
        delay_argument = (
            total_length / header.propagation_speed_m_s
        ) * float(header.rate_hz)
        require_finite(delay_argument, f"{prefix}:resolved_delay_argument")
        # std::round is halfway away from zero.  Length and rate are positive.
        replayed_delay = math.floor(delay_argument + 0.5)
        require(
            delay_samples == replayed_delay,
            (
                f"{prefix}: delay {delay_samples} does not equal "
                f"round(total_length / propagation_speed * rate)={replayed_delay}"
            ),
        )

        descriptors.append(
            CylinderDescriptor(
                runtime_index=runtime_index,
                stable_id=stable_id,
                firing_rank=firing_rank,
                route=route,
                delay_samples=delay_samples,
                firing_angle_rad=firing_angle,
                header_length_m=header_length,
                exhaust_length_m=exhaust_length,
                total_length_m=total_length,
                gas_primary_length_not_delay_m=gas_primary_length,
                sound_attenuation=sound_attenuation,
                route_audio_volume=audio_volume,
            )
        )

    require(
        sorted(item.firing_rank for item in descriptors) == list(range(cylinder_count)),
        f"{path}: firing ranks are not a permutation",
    )
    require(
        sorted(item.stable_id for item in descriptors)
        == list(range(1, cylinder_count + 1)),
        f"{path}: stable cylinder IDs are not a permutation",
    )
    require(
        {item.route for item in descriptors} == set(range(bus_count)),
        f"{path}: route descriptors do not cover both buses",
    )
    return header, descriptors


def parse_audit_header(
    data: mmap.mmap,
    path: Path,
    file_size: int,
    parity: ParityHeader,
) -> AuditHeader:
    require(file_size >= AUDIT_HEADER.size, f"{path}: truncated audit header")
    (
        magic,
        version,
        header_bytes,
        cylinder_count,
        bus_count,
        record_bytes,
        rate_hz,
        record_count,
        start,
        end,
        reserved,
    ) = AUDIT_HEADER.unpack_from(data, 0)

    require(magic == AUDIT_MAGIC, f"{path}: bad audit magic {magic!r}")
    require(version == SCHEMA_VERSION, f"{path}: unsupported audit version {version}")
    require(header_bytes == 64, f"{path}: audit header_bytes is {header_bytes}")
    require(
        cylinder_count == parity.cylinder_count,
        f"{path}: cylinder count does not match parity lane",
    )
    require(bus_count == parity.bus_count, f"{path}: bus count does not match parity lane")
    expected_record_bytes = 16 + 16 * cylinder_count + 8 * bus_count
    require(
        record_bytes == expected_record_bytes,
        f"{path}: audit record_bytes {record_bytes}, expected {expected_record_bytes}",
    )
    require(rate_hz == parity.rate_hz, f"{path}: rate does not match parity lane")
    require(
        record_count == parity.record_count,
        f"{path}: record count does not match parity lane",
    )
    require(
        (start, end) == (0, parity.record_count),
        f"{path}: audit interval [{start},{end}) is not [0,{parity.record_count})",
    )
    require(reserved == 0, f"{path}: reserved audit-header field is nonzero")
    require_file_size(path, file_size, header_bytes + record_bytes * record_count)
    return AuditHeader(
        header_bytes=header_bytes,
        cylinder_count=cylinder_count,
        bus_count=bus_count,
        record_bytes=record_bytes,
        rate_hz=rate_hz,
        record_count=record_count,
        start=start,
        end=end,
    )


def append_length_prefixed(hasher: "hashlib._Hash", value: str) -> None:
    encoded = value.encode("utf-8")
    hasher.update(len(encoded).to_bytes(8, "big"))
    hasher.update(encoded)


def digest_components(domain: str, components: Mapping[str, str]) -> bytes:
    hasher = hashlib.sha256()
    append_length_prefixed(hasher, domain)
    for key, value in sorted(components.items()):
        append_length_prefixed(hasher, key)
        append_length_prefixed(hasher, value)
    return hasher.digest()


def expected_component_seeds(
    cylinder_count: int,
    channel_count: int,
) -> tuple[str, list[tuple[str, int, int, int]]]:
    random_key = digest_components(
        RANDOM_KEY_DOMAIN,
        {
            "capture_id": CAPTURE_ID,
            "public_seed": str(PUBLIC_SEED),
            "seed_derivation": SEED_DERIVATION,
        },
    ).hex()
    require(
        random_key == EXPECTED_RANDOM_KEY,
        "internal error: frozen capture random-key derivation changed",
    )

    inventory: list[tuple[str, int, int, int]] = []
    domains: list[tuple[str, int]] = [
        *[("combustion", index) for index in range(cylinder_count)],
        *[("synth_air_noise", index) for index in range(channel_count)],
        *[("synth_jitter", index) for index in range(channel_count)],
        ("starter", 0),
    ]
    for component_domain, component_index in domains:
        digest = digest_components(
            COMPONENT_SEED_DOMAIN,
            {
                "capture_random_key_sha256": random_key,
                "component_domain": component_domain,
                "component_index": str(component_index),
            },
        )
        initial_state = int.from_bytes(digest[0:8], "big")
        stream = int.from_bytes(digest[8:16], "big") & MAX_CANONICAL_STREAM
        inventory.append(
            (component_domain, component_index, initial_state, stream)
        )
    return random_key, inventory


def parse_and_validate_seeds(
    data: mmap.mmap,
    path: Path,
    file_size: int,
    parity: ParityHeader,
) -> str:
    require(file_size >= SEED_HEADER.size, f"{path}: truncated seed header")
    (
        magic,
        version,
        cylinder_count,
        channel_count,
        pair_bytes,
        combustion_count,
        air_count,
        jitter_count,
        reserved,
    ) = SEED_HEADER.unpack_from(data, 0)

    require(magic == SEED_MAGIC, f"{path}: bad seed magic {magic!r}")
    require(version == SCHEMA_VERSION, f"{path}: unsupported seed version {version}")
    require(
        cylinder_count == parity.cylinder_count,
        f"{path}: cylinder count does not match parity lane",
    )
    require(
        channel_count == parity.bus_count,
        f"{path}: channel count does not match reference buses",
    )
    require(pair_bytes == SEED_PAIR.size, f"{path}: pair_bytes is {pair_bytes}")
    require(
        combustion_count == cylinder_count,
        f"{path}: combustion seed count is {combustion_count}",
    )
    require(air_count == channel_count, f"{path}: air seed count is {air_count}")
    require(jitter_count == channel_count, f"{path}: jitter seed count is {jitter_count}")
    require(reserved == 0, f"{path}: reserved seed-header field is nonzero")

    pair_count = combustion_count + air_count + jitter_count + 1
    require_file_size(path, file_size, SEED_HEADER.size + pair_count * pair_bytes)
    random_key, expected = expected_component_seeds(cylinder_count, channel_count)
    require(len(expected) == pair_count, f"{path}: internal seed inventory shape mismatch")

    streams: list[int] = []
    for index, (domain, component_index, expected_state, expected_stream) in enumerate(
        expected
    ):
        offset = SEED_HEADER.size + index * pair_bytes
        actual_state, actual_stream = SEED_PAIR.unpack_from(data, offset)
        prefix = f"{path}:{domain}[{component_index}]"
        require(
            actual_state == expected_state,
            (
                f"{prefix}: initial state 0x{actual_state:016x}, "
                f"expected 0x{expected_state:016x}"
            ),
        )
        require(
            actual_stream == expected_stream,
            (
                f"{prefix}: stream 0x{actual_stream:016x}, "
                f"expected 0x{expected_stream:016x}"
            ),
        )
        require(
            actual_stream <= MAX_CANONICAL_STREAM,
            f"{prefix}: noncanonical PCG32 stream",
        )
        streams.append(actual_stream)
    require(len(set(streams)) == len(streams), f"{path}: component streams are not unique")
    return random_key


def expected_control(sample_index: int) -> tuple[float, int, int, int, int]:
    if sample_index < 8_000:
        return 0.18, 0, 1, 1, 0
    if sample_index < 9_000:
        return 0.18, 0, 1, 1, 1
    if sample_index < 10_000:
        return 0.12, 1, 1, 0, 1
    return 0.85, 1, 1, 0, 1


def validate_records(
    parity_data: mmap.mmap,
    audit_data: mmap.mmap,
    parity_path: Path,
    audit_path: Path,
    parity: ParityHeader,
    audit: AuditHeader,
    descriptors: Sequence[CylinderDescriptor],
) -> None:
    audit_values = struct.Struct(
        "<QQ" + "d" * (2 * parity.cylinder_count + parity.bus_count)
    )
    require(
        audit_values.size == audit.record_bytes,
        "internal error: audit record format does not match header",
    )
    expected_dt = 1.0 / float(parity.rate_hz)
    expected_time = 0.0
    previous_filtered_rpm = 0.0
    delay_queues = [deque() for _ in range(parity.cylinder_count)]
    cycle_radians = 4.0 * 3.14159265359

    for sample_index in range(parity.record_count):
        parity_offset = parity.header_bytes + sample_index * parity.record_bytes
        audit_offset = audit.header_bytes + sample_index * audit.record_bytes
        (
            parity_index,
            parity_step_end,
            time_s,
            dt_s,
            engine_rpm,
            filtered_rpm,
            crank_angle,
            requested_throttle,
            resolved_intake_throttle,
            ignition,
            fuel,
            starter,
            dyno,
            parity_reserved,
        ) = PARITY_RECORD_PREFIX.unpack_from(parity_data, parity_offset)
        audit_record = audit_values.unpack_from(audit_data, audit_offset)
        audit_index = audit_record[0]
        audit_step_end = audit_record[1]
        prefix = f"record[{sample_index}]"

        require(
            parity_index == sample_index,
            f"{parity_path}:{prefix}: sample_index is {parity_index}",
        )
        require(
            parity_step_end == sample_index + 1,
            f"{parity_path}:{prefix}: step_end is {parity_step_end}",
        )
        require(
            (audit_index, audit_step_end) == (parity_index, parity_step_end),
            (
                f"{audit_path}:{prefix}: index/step "
                f"({audit_index},{audit_step_end}) does not match parity lane"
            ),
        )

        global_floats = (
            ("time_s", time_s),
            ("dt_s", dt_s),
            ("engine_rpm", engine_rpm),
            ("filtered_rpm", filtered_rpm),
            ("crank_angle_rad", crank_angle),
            ("requested_throttle_control", requested_throttle),
            ("resolved_intake_throttle", resolved_intake_throttle),
        )
        for name, value in global_floats:
            require_finite(value, f"{parity_path}:{prefix}:{name}")
        require_same_bits(dt_s, expected_dt, f"{parity_path}:{prefix}:dt_s")
        expected_time = expected_time + expected_dt
        require_same_bits(time_s, expected_time, f"{parity_path}:{prefix}:time_s")
        require(engine_rpm >= 0.0, f"{parity_path}:{prefix}: negative engine RPM")
        require(
            0.0 <= crank_angle < cycle_radians,
            f"{parity_path}:{prefix}: crank angle is outside wrapped four-stroke cycle",
        )
        require(
            0.0 <= requested_throttle <= 1.0,
            f"{parity_path}:{prefix}: requested throttle is outside [0,1]",
        )
        require(
            0.0 <= resolved_intake_throttle <= 1.0,
            f"{parity_path}:{prefix}: resolved intake throttle is outside [0,1]",
        )
        require(
            all(flag in (0, 1) for flag in (ignition, fuel, starter, dyno)),
            f"{parity_path}:{prefix}: control flag is not 0 or 1",
        )
        require(parity_reserved == 0, f"{parity_path}:{prefix}: reserved field is nonzero")

        (
            expected_throttle,
            expected_ignition,
            expected_fuel,
            expected_starter,
            expected_dyno,
        ) = expected_control(sample_index)
        require_same_bits(
            requested_throttle,
            expected_throttle,
            f"{parity_path}:{prefix}:requested_throttle_control",
        )
        require(
            (ignition, fuel, starter, dyno)
            == (
                expected_ignition,
                expected_fuel,
                expected_starter,
                expected_dyno,
            ),
            (
                f"{parity_path}:{prefix}: flags "
                f"{(ignition, fuel, starter, dyno)} do not match "
                f"{(expected_ignition, expected_fuel, expected_starter, expected_dyno)}"
            ),
        )

        alpha = dt_s / (100.0 + dt_s)
        replayed_filtered_rpm = (
            alpha * previous_filtered_rpm
            + (1.0 - alpha) * engine_rpm
        )
        require_same_bits(
            filtered_rpm,
            replayed_filtered_rpm,
            f"{parity_path}:{prefix}:filtered_rpm replay",
        )
        previous_filtered_rpm = replayed_filtered_rpm

        pressure_values: list[tuple[float, float, float]] = []
        for cylinder in range(parity.cylinder_count):
            offset = (
                parity_offset
                + PARITY_RECORD_PREFIX.size
                + cylinder * PARITY_PRESSURES.size
            )
            absolute_pressure, dynamic_forward, dynamic_reverse = (
                PARITY_PRESSURES.unpack_from(parity_data, offset)
            )
            pressure_values.append(
                (absolute_pressure, dynamic_forward, dynamic_reverse)
            )
            cylinder_prefix = f"{parity_path}:{prefix}:cylinder[{cylinder}]"
            require_finite(
                absolute_pressure,
                f"{cylinder_prefix}:static_pressure_pa_abs",
            )
            require_finite(
                dynamic_forward,
                f"{cylinder_prefix}:dynamic_pressure_forward_pa",
            )
            require_finite(
                dynamic_reverse,
                f"{cylinder_prefix}:dynamic_pressure_reverse_pa",
            )
            require(
                absolute_pressure > 0.0,
                f"{cylinder_prefix}: absolute pressure is not positive",
            )
            require(
                dynamic_forward >= 0.0 and dynamic_reverse >= 0.0,
                f"{cylinder_prefix}: directional dynamic pressure is negative",
            )

        audit_float_values = audit_record[2:]
        for value_index, value in enumerate(audit_float_values):
            require_finite(value, f"{audit_path}:{prefix}:value[{value_index}]")
        pre_delay = audit_float_values[: parity.cylinder_count]
        post_delay = audit_float_values[
            parity.cylinder_count : 2 * parity.cylinder_count
        ]
        captured_buses = audit_float_values[2 * parity.cylinder_count :]

        attenuation = (
            min(abs(filtered_rpm), parity.speed_threshold_rpm)
            / parity.speed_threshold_rpm
        )
        attenuation_cubed = attenuation * attenuation * attenuation
        replayed_buses = [0.0 for _ in range(parity.bus_count)]
        for cylinder, descriptor in enumerate(descriptors):
            absolute_pressure, dynamic_forward, dynamic_reverse = pressure_values[cylinder]
            gauge_pressure = absolute_pressure - parity.atmosphere_pa
            pressure_mix = (
                parity.static_pressure_gain * gauge_pressure
                + parity.forward_pressure_gain * dynamic_forward
            )
            pressure_mix = (
                pressure_mix
                + parity.reverse_pressure_gain * dynamic_reverse
            )
            replayed_pre_delay = (
                attenuation_cubed * parity.excitation_scale
            ) * pressure_mix
            require_same_bits(
                pre_delay[cylinder],
                replayed_pre_delay,
                f"{audit_path}:{prefix}:cylinder[{cylinder}]:pre_delay replay",
            )

            queue = delay_queues[cylinder]
            queue.append(replayed_pre_delay)
            if len(queue) <= descriptor.delay_samples:
                replayed_post_delay = 0.0
            else:
                replayed_post_delay = queue.popleft()
            require_same_bits(
                post_delay[cylinder],
                replayed_post_delay,
                f"{audit_path}:{prefix}:cylinder[{cylinder}]:post_delay replay",
            )

            # Preserve the exact P1.8 grouping and runtime-cylinder accumulation
            # order.  The frozen exponent is two because the implementation uses
            # total_length * total_length; no generic pow() participates.
            contribution = descriptor.sound_attenuation * (
                (
                    descriptor.route_audio_volume
                    * replayed_post_delay
                )
                / parity.cylinder_divisor
            ) * (
                1.0
                / (
                    descriptor.total_length_m
                    * descriptor.total_length_m
                )
            )
            route = descriptor.route
            replayed_buses[route] = replayed_buses[route] + contribution

        for bus in range(parity.bus_count):
            require_same_bits(
                captured_buses[bus],
                replayed_buses[bus],
                f"{audit_path}:{prefix}:bus[{bus}] replay",
            )


def normalize_sha256(value: str, source: str) -> str:
    normalized = value.strip().lower()
    require(
        len(normalized) == 64
        and all(character in "0123456789abcdef" for character in normalized),
        f"{source}: expected a 64-digit SHA-256",
    )
    return normalized


def canonical_hash_name(name: str, source: str) -> str:
    normalized = name.strip()
    normalized = Path(normalized).name
    require(normalized in HASH_ALIASES, f"{source}: unknown fixture hash name {name!r}")
    return HASH_ALIASES[normalized]


def merge_expected_hash(
    result: dict[str, str],
    name: str,
    value: str,
    source: str,
) -> None:
    canonical_name = canonical_hash_name(name, source)
    digest = normalize_sha256(value, source)
    if canonical_name in result:
        require(
            result[canonical_name] == digest,
            f"{source}: conflicting expected hash for {canonical_name}",
        )
    result[canonical_name] = digest


def parse_hash_file(path: Path) -> dict[str, str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise ValidationError(f"{path}: cannot read expected hashes: {error}") from error
    result: dict[str, str] = {}
    stripped = text.lstrip()
    if stripped.startswith("{"):
        try:
            document = json.loads(text)
        except json.JSONDecodeError as error:
            raise ValidationError(f"{path}: invalid JSON hash file: {error}") from error
        require(isinstance(document, dict), f"{path}: JSON hash document must be an object")
        candidates = document.get(
            "sha256",
            document.get("hashes", document.get("files", document)),
        )
        require(isinstance(candidates, dict), f"{path}: JSON hashes must be an object")
        for name, value in candidates.items():
            if isinstance(value, dict):
                value = value.get("sha256")
            if name in HASH_ALIASES or Path(str(name)).name in HASH_ALIASES:
                require(isinstance(value, str), f"{path}: hash for {name!r} is not a string")
                merge_expected_hash(result, str(name), value, str(path))
    else:
        for line_number, line in enumerate(text.splitlines(), 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split(maxsplit=1)
            require(
                len(fields) == 2,
                f"{path}:{line_number}: expected SHA256SUMS-style line",
            )
            digest, name = fields
            name = name.lstrip("*").strip()
            if Path(name).name in HASH_ALIASES:
                merge_expected_hash(
                    result,
                    name,
                    digest,
                    f"{path}:{line_number}",
                )
    require(
        set(result) == {PARITY_FILENAME, AUDIT_FILENAME, SEED_FILENAME},
        f"{path}: expected hashes for all three fixture files",
    )
    return result


def parse_cli_hashes(
    entries: Iterable[str],
    hash_file: Path | None,
) -> dict[str, str]:
    result = parse_hash_file(hash_file) if hash_file is not None else {}
    for entry in entries:
        require("=" in entry, f"--expect-sha256 {entry!r}: expected NAME=HEX")
        name, value = entry.split("=", 1)
        merge_expected_hash(result, name, value, "--expect-sha256")
    require(
        set(result) == {PARITY_FILENAME, AUDIT_FILENAME, SEED_FILENAME},
        (
            "expected hashes are required for all three fixture files; "
            "provide manifest.json, --sha256-file, or three "
            "--expect-sha256 arguments"
        ),
    )
    return result


def validate_fixture(
    parity_path: Path,
    audit_path: Path,
    seed_path: Path,
    expected_hashes: Mapping[str, str],
) -> dict[str, object]:
    parity_size = require_regular_file(parity_path)
    audit_size = require_regular_file(audit_path)
    seed_size = require_regular_file(seed_path)

    try:
        with (
            parity_path.open("rb") as parity_file,
            audit_path.open("rb") as audit_file,
            seed_path.open("rb") as seed_file,
        ):
            parity_data = mmap.mmap(parity_file.fileno(), 0, access=mmap.ACCESS_READ)
            audit_data = mmap.mmap(audit_file.fileno(), 0, access=mmap.ACCESS_READ)
            seed_data = mmap.mmap(seed_file.fileno(), 0, access=mmap.ACCESS_READ)
            try:
                computed_hashes = {
                    PARITY_FILENAME: hashlib.sha256(parity_data).hexdigest(),
                    AUDIT_FILENAME: hashlib.sha256(audit_data).hexdigest(),
                    SEED_FILENAME: hashlib.sha256(seed_data).hexdigest(),
                }
                for name, expected in expected_hashes.items():
                    require(
                        computed_hashes[name] == expected,
                        (
                            f"{name}: SHA-256 {computed_hashes[name]}, "
                            f"expected {expected}"
                        ),
                    )

                parity, descriptors = parse_parity_header(
                    parity_data,
                    parity_path,
                    parity_size,
                )
                audit = parse_audit_header(
                    audit_data,
                    audit_path,
                    audit_size,
                    parity,
                )
                random_key = parse_and_validate_seeds(
                    seed_data,
                    seed_path,
                    seed_size,
                    parity,
                )
                validate_records(
                    parity_data,
                    audit_data,
                    parity_path,
                    audit_path,
                    parity,
                    audit,
                    descriptors,
                )
            finally:
                seed_data.close()
                audit_data.close()
                parity_data.close()
    except OSError as error:
        raise ValidationError(f"could not read fixture: {error}") from error

    return {
        "ok": True,
        "schemaVersion": SCHEMA_VERSION,
        "recordCount": parity.record_count,
        "physicsRateHz": parity.rate_hz,
        "cylinderCount": parity.cylinder_count,
        "busCount": parity.bus_count,
        "audibleInterval": [parity.audible_start, parity.audible_end],
        "sha256": computed_hashes,
        "expectedHashesChecked": sorted(expected_hashes),
        "randomKeySha256": random_key,
        "replay": {
            "filteredRpm": "bit_exact",
            "excitationPreDelay": "bit_exact",
            "delayWriteThenRead": "bit_exact",
            "cylinderOrderBusAccumulation": "bit_exact",
        },
    }


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Strictly validate the frozen BMW M52B28 P1.8 reference fixture "
            "and replay its redundant audit lane."
        )
    )
    parser.add_argument(
        "fixture_dir",
        nargs="?",
        default=".",
        help="directory containing the three fixture files (default: current directory)",
    )
    parser.add_argument("--parity", type=Path, help="override reference-parity.bin path")
    parser.add_argument("--audit", type=Path, help="override reference-audit.bin path")
    parser.add_argument("--seeds", type=Path, help="override component-seeds.bin path")
    parser.add_argument(
        "--expect-sha256",
        action="append",
        default=[],
        metavar="NAME=HEX",
        help=(
            "check a file hash; NAME is parity, audit, seeds, or its fixture "
            "filename (repeatable)"
        ),
    )
    parser.add_argument(
        "--sha256-file",
        type=Path,
        help="JSON or SHA256SUMS file containing expected hashes for all three files",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="emit a machine-readable JSON result",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    fixture_dir = Path(args.fixture_dir)
    parity_path = args.parity or fixture_dir / PARITY_FILENAME
    audit_path = args.audit or fixture_dir / AUDIT_FILENAME
    seed_path = args.seeds or fixture_dir / SEED_FILENAME
    hash_file = args.sha256_file
    if hash_file is None:
        fixture_manifest = fixture_dir / "manifest.json"
        if fixture_manifest.is_file():
            hash_file = fixture_manifest

    try:
        expected_hashes = parse_cli_hashes(args.expect_sha256, hash_file)
        summary = validate_fixture(
            parity_path,
            audit_path,
            seed_path,
            expected_hashes,
        )
    except ValidationError as error:
        if args.json:
            print(json.dumps({"ok": False, "error": str(error)}, sort_keys=True))
        else:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(summary, sort_keys=True))
    else:
        hashes = summary["sha256"]
        print(
            "OK: "
            f"{summary['recordCount']} records, "
            f"{summary['cylinderCount']} cylinders, "
            f"{summary['busCount']} buses at "
            f"{summary['physicsRateHz']} Hz; "
            "filtered RPM, excitation, delay, and buses are bit-exact"
        )
        print(
            "SHA-256: "
            f"parity={hashes[PARITY_FILENAME]} "
            f"audit={hashes[AUDIT_FILENAME]} "
            f"seeds={hashes[SEED_FILENAME]}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

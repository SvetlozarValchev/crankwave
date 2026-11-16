#!/usr/bin/env python3
"""Audit native graph values against actual Crankwave render-manifest values.

The manifest contains the compiler's resolved binary64 values, not a second read
of the exported engine JSON. Output also compares the independently run RPM tapes.
"""
import argparse
import json
import math
from pathlib import Path
import statistics
import struct
import sys


def decode(value, field=""):
    if isinstance(value, dict):
        if set(value) == {"value", "resolution_id"}:
            v = value["value"]
            if (isinstance(v, str) and v.startswith("0x") and len(v) == 18
                    and not field.endswith(("_terms", "_mask"))):
                return struct.unpack(">d", bytes.fromhex(v[2:]))[0]
            return decode(v)
        return {key: decode(v, key) for key, v in value.items()}
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


def audit(native, compiled):
    checks = []
    def check(path, a, b):
        exact = a == b
        close = exact or (isinstance(a, (int, float)) and isinstance(b, (int, float))
                          and math.isclose(a, b, rel_tol=2e-13, abs_tol=1e-15))
        checks.append(dict(path=path, native=a, crankwave=b, exact=exact, passed=close))

    e = compiled["engine"]
    profile = e["physics_profile"]["value"]
    core = profile["core"]
    banks = {b["id"]: b for b in native["banks"]}
    native_cylinders = {c["id"]: c for c in native["cylinders"]}
    native_functions = {f["id"]: f for f in native["functions"]}
    bank_ids = {b["id"]: b["semantic_id"] for b in e["banks"]}
    cylinder_ids = {c["id"]: c["semantic_id"] for c in e["cylinders"]}
    intake_ids = {v["id"]: v["semantic_id"] for v in e["intakes"]}
    route_ids = {v["id"]: v["semantic_id"] for v in e["routes"]}
    check("cylinder_count", len(native["cylinders"]), len(e["cylinders"]))
    check("bank_count", len(native["banks"]), len(e["banks"]))
    check("exhaust_count", len(native["exhausts"]), len(e["routes"]))
    crank = core["mechanism"]["cranks"][0]
    for a, b in [("tdc_rad", "crank_tdc_reference_rad"), ("mass_kg", "crankshaft_mass_kg"),
                 ("flywheel_mass_kg", "flywheel_mass_kg"), ("inertia_kg_m2", "authored_crank_inertia_kg_m2"),
                 ("friction_nm", "running_friction_torque_magnitude_nm")]:
        check("crank." + a, native["crank"][a], crank[b])
    check("throttle.gamma", native["throttle_gamma"], core["throttle_controller"]["value"]["gamma"])
    check("starter.torque", native["starter_torque_nm"], profile["starter"]["maximum_torque_nm"])
    check("starter.speed", native["starter_speed_rad_s"], profile["starter"]["target_speed_rad_s"])
    for c in core["mechanism"]["cylinders"]:
        t, p = c["topology"], c["parameters"]
        name = cylinder_ids[t["cylinder_id"]]
        n = native_cylinders[name]; b = banks[n["bank"]]
        for field, source in {
            "bore_m": b["bore_m"], "crank_radius_m": native["crank"]["throw_m"],
            "deck_height_m": b["deck_height_m"], "connecting_rod_length_m": n["rod"]["length_m"],
            "connecting_rod_center_of_mass_from_crank_pin_m": n["rod"]["length_m"] / 2 + n["rod"]["center_of_mass_midpoint_offset_m"],
            "connecting_rod_mass_kg": n["rod"]["mass_kg"], "connecting_rod_inertia_kg_m2": n["rod"]["inertia_kg_m2"],
            "piston_mass_kg": n["piston"]["mass_kg"], "piston_compression_height_m": n["piston"]["compression_height_m"],
            "piston_wrist_pin_position_m": n["piston"]["wrist_pin_m"], "piston_displacement_term_m3": n["piston"]["displacement_m3"],
            "header_primary_length_m": n["header_length_m"],
            # Crankwave resolves direct journals into the cylinder-axis frame.
            "journal_angle_rad": next(j["angle_rad"] for j in native["journals"] if j["id"] == n["journal"]) - b["angle_rad"],
            "ignition_wire_angle_rad": next(j["angle_rad"] for j in native["ignition"]["firing"] if j["cylinder"] == name),
        }.items():
            check(name + "." + field, source, p[field])
        check(name + ".blowby_k", n["piston"]["blowby_k"], p["piston_blowby"]["resolved_k"])
        check(name + ".intake", n["intake"], intake_ids[t["intake_id"]])
        check(name + ".exhaust", n["exhaust"], route_ids[t["exhaust_route_id"]])

    def curve(name, source, samples, x_key, y_key, radius):
        f = native_functions[source]
        check(name + ".radius", f["filter_radius"] / f["input_scale"], radius)
        check(name + ".sample_count", len(f["samples"]), len(samples))
        for i, ((x, y), sample) in enumerate(zip(f["samples"], samples, strict=True)):
            check(f"{name}.{i}.x", x / f["input_scale"], sample[x_key])
            check(f"{name}.{i}.y", y * f["output_scale"], sample[y_key])

    for h in core["gas_path"]["heads"]:
        name = bank_ids[h["bank_id"]]; b = banks[name]
        check(name + ".chamber", b["chamber_m3"], h["chamber_volume_m3"])
        for role in ["intake", "exhaust"]:
            check(name + "." + role + ".volume", b[role + "_runner_m3"], h[role + "_runner_base_volume_m3"])
            check(name + "." + role + ".area", b[role + "_runner_m2"], h[role + "_runner_cross_section_area_m2"])
            curve(name + "." + role + ".flow", b[role + "_flow"], h[role + "_flow"], "lift_m", "resolved_k", h[role + "_flow_triangle_radius_m"])
    for role in ["intake", "exhaust"]:
        cams = core["valvetrain"][role]
        for lobe in cams["lobes"]:
            n = native_cylinders[cylinder_ids[lobe["cylinder_id"]]]
            cam = banks[n["bank"]][role + "_cam"]
            name = n["id"] + "." + role + ".cam"
            target = cams["profiles"][lobe["profile_index"]]
            check(name + ".center", cam["lobe_centers_cam_rad"][n["bank_local_index"]] * 2, lobe["crank_center_rad"])
            check(name + ".advance", cam["advance_rad"], target["advance_rad"])
            check(name + ".base_radius", cam["base_radius_m"], target["base_radius_m"])
            curve(name, cam["profile"], target["samples"], "angle_rad", "lift_m", target["triangle_radius_rad"])
    for v in core["gas_path"]["intakes"]:
        name = intake_ids[v["topology"]["intake_id"]]
        n = next(i for i in native["intakes"] if i["id"] == name); p = v["parameters"]
        for a, b in [("volume_m3", "plenum_volume_m3"), ("area_m2", "plenum_cross_section_area_m2"),
                     ("runner_length_m", "runner_length_m"), ("idle_plate", "idle_throttle_plate_position_01"), ("velocity_decay", "velocity_decay")]:
            check(name + "." + a, n[a], p[b])
        for a, b in [("input_k", "main_throttle"), ("idle_k", "idle_bypass"), ("runner_k", "plenum_to_runner")]:
            check(name + "." + a, n[a], p[b]["resolved_k"])
        check(name + ".mixture", n["molecular_afr"], p["main_mixture_lambda"] * core["fuel"]["molecular_afr"])
    for v in core["gas_path"]["exhaust_routes"]:
        name = route_ids[v["topology"]["route_id"]]
        n = next(i for i in native["exhausts"] if i["id"] == name); p = v["parameters"]
        for a, b in [("length_m", "exhaust_system_length_m"), ("area_m2", "collector_cross_section_area_m2"),
                     ("primary_length_m", "primary_tube_length_m"), ("velocity_decay", "velocity_decay"), ("audio_volume", "audio_volume_linear")]:
            check(name + "." + a, n[a], p[b])
        for a, b in [("outlet_k", "collector_outlet"), ("primary_k", "primary_to_collector")]:
            check(name + "." + a, n[a], p[b]["resolved_k"])
        route = next(r for r in compiled["presentation"]["routes"] if r["route_id"] == v["topology"]["route_id"])
        check(name + ".IR_gain", n["ir_volume"], route["impulse_response_gain_linear"])
    f = core["fuel"]; n = native["fuel"]
    check("fuel.lbv_multiplier", n.get("lbv_multiplier", 1), f["lbv_multiplier"])
    two_stage = n.get("source_two_stage")
    if two_stage and two_stage["auto_ignition_temperature_k"] > 0:
        resolved = f.get("source_two_stage_combustion", {})
        for field, value in two_stage.items():
            check("fuel.source_two_stage." + field, value, resolved.get(field))
    for a, b in [("molecular_mass_kg_mol", "molecular_mass_kg_per_mol"), ("energy_j_kg", "energy_density_j_per_kg"),
                 ("molecular_afr", "molecular_afr"), ("efficiency", "maximum_burning_efficiency_01"),
                 ("variation", "burning_efficiency_randomness_01"), ("low_efficiency_attenuation", "low_efficiency_attenuation_01"),
                 ("max_turbulence", "maximum_turbulence_effect"), ("max_dilution", "maximum_dilution_effect")]:
        check("fuel." + a, n[a], f[b])
    curve("fuel.turbulence", n["turbulence"], f["turbulence_to_flame_speed_ratio"], "turbulence", "flame_speed_ratio", f["turbulence_to_flame_speed_ratio_triangle_radius"])
    ign = core["ignition"]
    curve("ignition.timing", native["ignition"]["timing"], ign["timing_curve"], "angular_speed_rad_s", "timing_advance_rad", ign["timing_curve_triangle_radius_rad_s"])
    check("ignition.limiter", native["ignition"]["limiter_rad_s"], ign["limiter_speed_rpm"] * .104719755)
    check("ignition.cut_duration", native["ignition"]["cut_seconds"], ign["limiter_hold_s"])
    for a, b in [("jitter", "jitter_scale"), ("noise", "air_noise_mix_01"), ("hf_gain", "derivative_mix_01")]:
        check("presentation." + a, native[a], compiled["presentation"]["conditioning"][b])
    return checks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--comparison", type=Path, required=True)
    a = p.parse_args(); root = a.comparison
    reports = []
    for folder in sorted(root.iterdir()):
        if not folder.is_dir():
            continue
        manifest = json.loads((folder / "crankwave/manifest/render-manifest.v10.json").read_text())
        compiled = decode(manifest["content"]["inputs"]["value"]["resolved"])
        native = json.loads((folder / "import/resolved-graph.json").read_text())
        checks = audit(native, compiled)
        cw = [json.loads(line) for line in (folder / "crankwave/telemetry/engine-telemetry.v1.ndjson").read_text().splitlines()]
        cw = [r for r in cw if r["record_type"] == "block"]
        es = [json.loads(line) for line in (folder / "native/telemetry.jsonl").read_text().splitlines()][1:]
        assert len(cw) == len(es) == 1350
        errors = []
        for c, n in zip(cw, es, strict=True):
            assert abs(c["endpoint_scenario_time_s"] - n["simulationSeconds"]) < 1e-8
            errors.append(c["telemetry"]["engine"]["engine_speed_rpm"] - n["rpm"])
        stages = []
        for name, begin, end in [("idle", 5, 7), ("rev", 8, 10), ("limiter", 16, 18), ("coast", 21, 23)]:
            indices = [i for i, n in enumerate(es) if begin < n["simulationSeconds"] <= end + 1e-8]
            stages.append(dict(name=name, native_median_rpm=statistics.median(es[i]["rpm"] for i in indices),
                crankwave_median_rpm=statistics.median(cw[i]["telemetry"]["engine"]["engine_speed_rpm"] for i in indices)))
        cuts_file = folder / "native/limiter-events.jsonl"
        native_cuts = None
        if cuts_file.exists():
            events = [json.loads(line) for line in cuts_file.read_text().splitlines()]
            assert events[0]["kind"] == "metadata"
            assert events[0]["physics_hz"] == native["simulation_hz"]
            native_cuts = sum(r["kind"] == "cut_begin" for r in events)
        report = dict(engine=folder.name, scalar_checks=len(checks), exact_checks=sum(c["exact"] for c in checks),
            failed_checks=[c for c in checks if not c["passed"]],
            graph_check_scope="Geometry, topology routing, sampled cams/flow/timing/turbulence, fuel, throttle, starter, limiter and authored acoustic gains; not a complete proof of backend equivalence",
            rpm_mae=statistics.mean(abs(v) for v in errors), rpm_max_error=max(map(abs, errors)),
            crankwave_limiter_activations=sum(int(r["event_counters"]["limiter_activation_count"]) for r in cw),
            native_limiter_activations=native_cuts,
            stages=stages, parity_accepted=False)
        (folder / "compiled-decoded.json").write_text(json.dumps(compiled, indent=2) + "\n")
        (folder / "graph-audit.json").write_text(json.dumps(checks, indent=2) + "\n")
        (folder / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
        reports.append(report)
        print(json.dumps(report), flush=True)
    (root / "comparison.json").write_text(json.dumps(reports, indent=2) + "\n")
    return 0 if reports and all(not r["failed_checks"] for r in reports) else 1


if __name__ == "__main__":
    sys.exit(main())

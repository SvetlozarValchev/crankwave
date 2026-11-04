#!/usr/bin/env python3
"""Deterministically lower an evaluated engine-sim graph into Crankwave JSON.

This is an experimental offline compiler front end for an explicit subset. It
does not claim acoustic parity or emit a production-ready .crankwave carrier.
All native quantities are preserved or converted algebraically, without tuning.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def q(value, unit):
    return {"value": value, "unit": unit}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")


def lower(g, output, engine_id, graph_directory, diagnostic_loss_policy=False):
    if g["schema"] != "crankwave/engine-sim-resolved-graph/v1":
        raise ValueError("Unsupported resolved graph schema")
    curves = []
    functions = {f["id"]: f for f in g["functions"]}
    conversions = []
    curve_ids = {}

    def curve(source, role):
        key = (source, role)
        if key in curve_ids:
            return curve_ids[key]
        f = functions[source]
        units = {"flow": ("m", "cfm", "length", "volume_flow_rate"),
                 "cam": ("rad", "m", "angle", "length"),
                 "ignition": ("rad/s", "rad", "angular_speed", "angle"),
                 "turbulence": ("1", "m/s", "dimensionless", "speed")}[role]
        iu, ou, idim, odim = units
        scale = f["output_scale"]
        if role == "flow":
            scale /= g["flow_calibration"]["port_k_per_cfm"]
        name = source + "-" + role
        samples = []
        for x, y in f["samples"]:
            value = q(y * scale, ou)
            if role == "flow":
                value["standard"] = "port_28_inh2o"
            samples.append({"input": q(x / f["input_scale"], iu), "output": value})
        curves.append(dict(id=name, input_dimension=idim, output_dimension=odim,
                           evaluation="triangle_weighted_samples", below_domain="clamp", above_domain="clamp",
                           triangle_filter_radius=q(f["filter_radius"] / f["input_scale"], iu), samples=samples))
        curve_ids[key] = name
        conversions.append({"source": source, "target": name, "role": role,
                            "input_scale": f["input_scale"], "output_scale_applied": scale})
        return name

    def flow(k, port=False):
        calibration = "port" if port else "carb"
        return dict(type="flow_bench", rated_flow=dict(q(k / g["flow_calibration"][calibration + "_k_per_cfm"], "cfm"),
                    standard="port_28_inh2o" if port else "carburetor_1p5_inhg"),
                    pressure_drop=q(28 if port else 1.5, "inH2O" if port else "inHg"))

    crank = g["crank"]
    e = dict(identity=dict(id=engine_id, display_name=g["name"],
                          description="Deterministic evaluated-MR import. See import-report.json for scope and parity status."),
             cycle="four_stroke", layout="inline" if len(g["banks"]) == 1 else "v_engine",
             limits=dict(redline=q(g["redline_rad_s"], "rad/s")), curves=curves,
             crankshafts=[dict(id="crank", throw_radius=q(crank["throw_m"], "m"), mass=q(crank["mass_kg"], "kg"),
                              flywheel_mass=q(crank["flywheel_mass_kg"], "kg"), moment_of_inertia=q(crank["inertia_kg_m2"], "kg*m2"),
                              friction_torque=q(crank["friction_nm"], "N*m"), tdc_reference_angle=q(crank["tdc_rad"], "rad"))],
             output_crankshaft="crank",
             journals=[dict(id=j["id"], type="crankshaft", crankshaft="crank", phase=q(j["angle_rad"], "rad")) for j in g["journals"]],
             connecting_rods=[], pistons=[], cylinders=[], banks=[], heads=[], ports=[], cam_lobes=[], camshafts=[], valvetrains=[],
             intakes=[], exhausts=[], source_routes=[])
    for c in g["cylinders"]:
        name, p, r = c["id"], c["piston"], c["rod"]
        e["connecting_rods"].append(dict(id=name + "-rod", length=q(r["length_m"], "m"), mass=q(r["mass_kg"], "kg"),
            moment_of_inertia=q(r["inertia_kg_m2"], "kg*m2"),
            center_of_mass_from_crank_pin=q(r["length_m"] / 2 + r["center_of_mass_midpoint_offset_m"], "m")))
        e["pistons"].append(dict(id=name + "-piston", mass=q(p["mass_kg"], "kg"), compression_height=q(p["compression_height_m"], "m"),
            displacement_volume=q(p["displacement_m3"], "m3"), wrist_pin_position=q(p["wrist_pin_m"], "m"), blowby=flow(p["blowby_k"], True)))
        e["cylinders"].append(dict(id=name, bank=c["bank"], journal=c["journal"], connecting_rod=name + "-rod", piston=name + "-piston",
            intake=c["intake"], exhaust=c["exhaust"], ignition_wire=name + "-wire", intake_port=c["bank"] + "-intake-port",
            exhaust_port=c["bank"] + "-exhaust-port", exhaust_header_primary_length=q(c["header_length_m"], "m")))
    for b in g["banks"]:
        name = b["id"]
        e["banks"].append(dict(id=name, angle=q(b["angle_rad"], "rad"), bore=q(b["bore_m"], "m"),
                               deck_height=q(b["deck_height_m"], "m"), head=name + "-head"))
        e["heads"].append(dict(id=name + "-head", chamber_volume=q(b["chamber_m3"], "m3"), valvetrain=name + "-valvetrain",
                               ports=[name + "-intake-port", name + "-exhaust-port"]))
        e["valvetrains"].append(dict(id=name + "-valvetrain", type="standard", intake_camshaft=name + "-intake-cam", exhaust_camshaft=name + "-exhaust-cam"))
        bank_cylinders = sorted([c for c in g["cylinders"] if c["bank"] == name], key=lambda c: c["bank_local_index"])
        for role in ["intake", "exhaust"]:
            cam = b[role + "_cam"]
            profile = curve(cam["profile"], "cam")
            lobes = []
            for c, center in zip(bank_cylinders, cam["lobe_centers_cam_rad"], strict=True):
                lobe = c["id"] + "-" + role + "-lobe"
                lobes.append(lobe)
                e["cam_lobes"].append(dict(id=lobe, cylinder=c["id"], port_kind=role, centerline=q(center * 2, "rad"), type="sampled", lift_curve=profile))
            e["camshafts"].append(dict(id=name + "-" + role + "-cam", advance=q(cam["advance_rad"], "rad"),
                                       base_radius=q(cam["base_radius_m"], "m"), lobes=lobes))
            e["ports"].append(dict(id=name + "-" + role + "-port", head=name + "-head", kind=role,
                runner_volume=q(b[role + "_runner_m3"], "m3"), runner_cross_section_area=q(b[role + "_runner_m2"], "m2"), flow_curve=curve(b[role + "_flow"], "flow")))
    for v in g["intakes"]:
        e["intakes"].append(dict(id=v["id"], plenum_volume=q(v["volume_m3"], "m3"), plenum_cross_section_area=q(v["area_m2"], "m2"),
            runner_length=q(v["runner_length_m"], "m"), main_restriction=flow(v["input_k"]), idle_bypass_restriction=flow(v["idle_k"]),
            runner_restriction=flow(v["runner_k"]), idle_throttle_position_01=v["idle_plate"], runner_velocity_decay_01=v["velocity_decay"],
            main_mixture_lambda=v["molecular_afr"] / g["fuel"]["molecular_afr"]))
    assets, routes, seen_assets = [], [], {}
    asset_dir = output / "assets"
    asset_dir.mkdir()
    for v in g["exhausts"]:
        e["exhausts"].append(dict(id=v["id"], collector_length=q(v["length_m"], "m"), collector_cross_section_area=q(v["area_m2"], "m2"),
            primary_tube_length=q(v["primary_length_m"], "m"), outlet_restriction=flow(v["outlet_k"]), primary_restriction=flow(v["primary_k"]), velocity_decay_01=v["velocity_decay"]))
        e["source_routes"].append(dict(id=v["id"], type="exhaust", exhaust=v["id"]))
        source_ir = Path(v["ir_path"])
        if not source_ir.is_absolute():
            source_ir = graph_directory / source_ir
        ir_hash = digest(source_ir)
        if ir_hash not in seen_assets:
            asset_id = "ir-" + ir_hash[:16]
            dest = asset_dir / (ir_hash + ".wav")
            shutil.copy2(source_ir, dest)
            seen_assets[ir_hash] = asset_id
            assets.append(dict(id=asset_id, kind="impulse_response", uri="assets/" + dest.name, sha256=ir_hash))
        routes.append(dict(route=v["id"], source_gain_linear=v["audio_volume"], impulse_response=seen_assets[ir_hash],
                           impulse_response_gain_linear=v["ir_volume"], wet_mix_01=1))
    f = g["fuel"]
    e["fuels"] = [dict(id="fuel", display_name=f["name"] or "Resolved engine-sim fuel", molecular_mass=q(f["molecular_mass_kg_mol"], "kg/mol"),
        lower_heating_value=q(f["energy_j_kg"], "J/kg"), stoichiometric_air_fuel_molar_ratio=f["molecular_afr"], turbulence_to_flame_speed=curve(f["turbulence"], "turbulence"),
        combustion=dict(maximum_efficiency_01=f["efficiency"], cycle_variation_01=f["variation"], low_efficiency_attenuation_01=f["low_efficiency_attenuation"],
                        maximum_turbulence_effect=f["max_turbulence"], maximum_dilution_effect=f["max_dilution"]))]
    e["default_fuel"] = "fuel"
    e["ignition"] = dict(timing_curve=curve(g["ignition"]["timing"], "ignition"), wires=[dict(id=c["id"] + "-wire") for c in g["cylinders"]],
        firing_order=[dict(wire=v["cylinder"] + "-wire", crank_angle=q(v["angle_rad"], "rad")) for v in sorted(g["ignition"]["firing"], key=lambda v: v["angle_rad"])],
        limiter=dict(activation_speed=q(g["ignition"]["limiter_rad_s"], "rad/s"), cut_duration=q(g["ignition"]["cut_seconds"], "s")))
    e["throttle_controllers"] = [dict(id="throttle", type="direct", gamma=g["throttle_gamma"])]
    e["throttle_controller"] = "throttle"
    e["starter"] = dict(type="cranking", torque=q(g["starter_torque_nm"], "N*m"), target_speed=q(g["starter_speed_rad_s"], "rad/s"))
    # Supplementary policy is explicit diagnostic scaffolding. Crank friction
    # remains authored; this prior is not a quantity supplied by engine-sim.
    accessory = output / "accessory-configuration.json"
    write(accessory, dict(schema_id="crankwave.accessory-configuration.v1", engine_id=engine_id,
                          configuration_id="native-import-diagnostic-loss" if diagnostic_loss_policy else "native-import-no-additional-loss",
                          claim="Engine-sim supplies no aggregate loss coefficients. Diagnostic mode supplies a generic Crankwave prior; native mechanical crank friction is preserved.",
                          diagnostic_policy=diagnostic_loss_policy))
    e["accessory_configurations"] = [dict(id="native-import-loss-policy", uri=accessory.name, sha256=digest(accessory))]
    e["losses"] = dict(type="chen_flynn_cycle_mean", constant_fmep=q(40000 if diagnostic_loss_policy else 0, "Pa"),
                       peak_pressure_coefficient=.005 if diagnostic_loss_policy else 0,
                       mean_piston_speed_coefficient=q(9000 if diagnostic_loss_policy else 0, "Pa*s/m"),
                       mean_piston_speed_squared_coefficient=q(90 if diagnostic_loss_policy else 0, "Pa*s2/m2"),
                       required_oil_temperature=q(363.15, "K"), accessory_configuration_id="native-import-loss-policy")
    route_ids = [v["id"] for v in g["exhausts"]]
    presentation = dict(assets=assets, routes=routes,
        cylinder_routes=[dict(cylinder=c["id"], route=c["exhaust"], gain_linear=c["sound_attenuation"]) for c in g["cylinders"]],
        conditioning=dict(jitter_scale=g["jitter"], jitter_modulation_cutoff_frequency=q(10000, "Hz"), derivative_mix_01=g["hf_gain"],
                          air_noise_mix_01=g["noise"], air_noise_cutoff_frequency=q(2000, "Hz")),
        buses=[dict(id=n, routes=route_ids, gain_linear=1, publish=True) for n in ["master-engine-raw", "master-engine-audition"]],
        audition=dict(buses=["master-engine-audition"], volume_linear=1, fade_in=q(0, "s"), fade_out=q(0, "s")), publication_gain_linear=2**-26)
    r = g["rig"]
    rig = dict(id="native-rig", vehicle=dict(id="native-vehicle", mass=q(r["mass_kg"], "kg"), drag_coefficient=r["drag_coefficient"],
        frontal_area=q(r["area_m2"], "m2"), differential_ratio=r["diff_ratio"], tire_radius=q(r["tire_radius_m"], "m"), rolling_resistance_force=q(r["rolling_resistance_n"], "N")),
        transmission=dict(id="native-transmission", maximum_clutch_torque=q(r["clutch_torque_nm"], "N*m"), gears=[dict(id=f"gear-{i}", ratio=v) for i, v in enumerate(r["gears"])]),
        dyno_defaults=dict(minimum_engine_speed=q(r["dyno_min_rad_s"], "rad/s"), maximum_engine_speed=q(r["dyno_max_rad_s"], "rad/s"), hold_step=q(r["dyno_step_rad_s"], "rad/s")))
    write(output / "engine.json", dict(schema="crankwave/engine", engine=e, presentation=presentation, rig=rig))
    return conversions


def scenario(g, engine_id):
    rate = lambda hz: dict(numerator=str(hz), denominator="1", unit="Hz")
    return dict(schema="crankwave/scenario", id=engine_id + "-native-audition", engine=engine_id, fuel="fuel",
        ambient=dict(pressure=q(101325, "Pa"), temperature=q(298.15, "K"), relative_humidity_01=0),
        initial_thermal_state=dict(gas_temperature=q(298.15, "K"), wall_temperature=q(363.15, "K"), coolant_temperature=q(363.15, "K"), oil_temperature=q(363.15, "K")),
        crankcase=dict(pressure=q(101325, "Pa"), temperature=q(298.15, "K")),
        initial_state=dict(engine_speed=q(0, "rpm"), crank_angle=q(g["crank"]["tdc_rad"], "rad"), ignition_enabled=True, fuel_enabled=True,
                           starter_enabled=True, dyno_enabled=False, limiter_enabled=True),
        preparation=dict(type="fixed_settling", warm_up_duration=q(0, "s"), settling_duration=q(0, "s")),
        mode=dict(type="free_engine", throttle_01=dict(interpolation="right_continuous_hold",
            points=[dict(time=q(t, "s"), value=v) for t, v in [(0, 0), (7, .12), (10, 0), (14, 1), (18, 0)]])),
        events=[dict(id="starter-release", time=q(2, "s"), payload=dict(type="operating_state_patch", starter_enabled=False)),
                dict(id="key-off", time=q(23, "s"), payload=dict(type="operating_state_patch", ignition_enabled=False))],
        rates=dict(physics=rate(int(g["simulation_hz"])), capture=rate(int(g["simulation_hz"])), source_processing=rate(192000), acoustics=rate(192000), delivery=rate(192000)),
        quality=dict(id="listening", process_block_capacity_frames=3840, event_queue_capacity=64, telemetry_capacity_frames=1),
        total_duration=q(27, "s"), audible_start=q(0, "s"), audible_duration=q(27, "s"), public_seed="20261104",
        output=dict(buses=["master-engine-raw", "master-engine-audition"], telemetry_channels=[]))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--script", type=Path, required=True)
    p.add_argument("--adapter", type=Path, required=True)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--id", required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--diagnostic-loss-policy", action="store_true", help="Explicitly attach Crankwave's generic aggregate-loss prior for comparison; not native MR semantics")
    a = p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,100}", a.id):
        p.error("Invalid engine id")
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    work = out / "native-work"
    work.mkdir()
    (work / "es").symlink_to(a.library.resolve(), target_is_directory=True)
    graph = out / "resolved-graph.json"
    result = subprocess.run([str(a.adapter.resolve()), str(a.script.resolve()), str(graph)], cwd=work, capture_output=True, text=True, timeout=60)
    (out / "native-compile.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"Native graph export failed: {result.stderr.strip()}; see {work / 'error_log.log'}")
    g = json.loads(graph.read_text())
    if not a.diagnostic_loss_policy:
        write(out / "import-report.json", dict(status="unsupported-policy", source_sha256=digest(a.script),
            graph_sha256=digest(graph), code="native-aggregate-loss-policy-not-representable",
            message="Crankwave requires a positive supplementary aggregate-loss policy absent from engine-sim. No engine JSON was emitted. Use --diagnostic-loss-policy only for explicitly marked comparisons."))
        p.exit(2, "Native graph extracted; strict import stopped: Crankwave requires an aggregate-loss policy absent from MR. See import-report.json.\n")
    conversions = lower(g, out, a.id, work, a.diagnostic_loss_policy)
    shutil.copy2(a.script, out / "source.mr")
    write(out / "scenario.json", scenario(g, a.id))
    write(out / "import-report.json", dict(source_sha256=digest(a.script), graph_sha256=digest(graph), adapter_sha256=digest(a.adapter),
        engine_sha256=digest(out / "engine.json"), subset=g["subset"], conversions=conversions,
        preserved="Evaluated physical quantities, graph routing, sampled curves, ignition phases, IR bytes and authored gains; no engine-specific tuning",
        policy=dict(additional_aggregate_loss="generic-diagnostic-prior" if a.diagnostic_loss_policy else "zero-not-currently-admitted-by-Crankwave", publication_gain_linear=2**-26, output_sample_rate=192000, physics_hz=g["simulation_hz"]),
        parity_status="unverified; diagnostic import only, no production carrier",
        known_backend_differences=["192 kHz Crankwave presentation versus native 44.1 kHz", "Different seed expansion policies despite identical public seed", "Native synthesizer presentation defaults and Crankwave canonical publication policy require comparison"],
        excluded_nonexecuted_metadata=["Native GUI geometry/display options", "Native fuel density used for volume display"] ))
    print(out / "engine.json")


if __name__ == "__main__":
    main()

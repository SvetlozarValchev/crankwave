export function mechanicalFixture() {
  return {
    schema: "crankwave/mechanical-texture-v1", engine: "unit-engine",
    parameters: {valve_gain_pcm: .1, valve_frequency_hz: 850, valve_decay_s: .0022,
      valve_attack_s: .0016, pumping_gain_pcm: .01, pumping_lowpass_hz: 1200, seed: 469765},
    timing: {intake_open_angle_rad: 6, intake_close_angle_rad: 10.4, exhaust_close_angle_rad: 6.6},
    cylinders: [{id: "cylinder-1", firing_angle_rad: 0, structural_gain: 1}],
    intake_reference_pressure_pa_abs: 100000, intake_pressure_exponent: .75, gain_linear: 1,
    provenance: {engine_identity_sha256: "a".repeat(64), authoring_sha256: "b".repeat(64),
      base_carrier_sha256: "c".repeat(64), note: "Synthetic acoustic adapter test"}
  };
}

# Responsive delivery profiles v1

Responsive authoring and physics remain at 10 kHz. Delivery profiles select the
serialized presentation sample rate and transfer geometry:

| ID | Sample rate | Batch | FFT | Spectrum coefficients |
| --- | ---: | ---: | ---: | ---: |
| `canonical-192k-v1` | 192,000 Hz | 3,840 | 65,536 | 30,071 |
| `game-48k-v1` | 48,000 Hz | 8,192 | 16,384 | 7,518 |

The canonical profile is the accepted native bake baseline. `game-48k-v1` is a
delivery profile for a separately converted package: it preserves the phase
texture grid and changes the time-domain presentation rate, block size, and
fixed impulse-response transfer. A noncanonical profile must bind its ID into
the native bake identity and must carry the same geometry in `runtime.json`.

The compact `pcm16le-scale-v1` payload encoding is allowed for the game
profile's time-domain lifecycle, starter, and limiter recordings. Each such
artifact stores a positive linear scale and decodes signed little-endian PCM16
as `sample = int16 / 32768 * scale`. Phase-domain banks remain float32 in
this profile until a separately validated phase quantization profile is
admitted.

Host playback scheduling is separate from this serialized geometry. The JavaScript
bridge can prepare the 48 kHz fixed spectrum as complete 1,024-frame partitions,
using 2,048-point FFTs, without changing the carrier. At this setting the internal
streaming delay is 21.33 ms rather than 170.67 ms. The original 8,192 warm-up
frames are retained to preserve initial phase and filter/master state. The
standalone harness uses this setting for supported 48 kHz responsive carriers;
the consumer API selects it with `playbackBlockFrames: 1024`. See
[the audio bridge](../CRANKWAVE_AUDIO_BRIDGE.md) for its bounds and sharing contract.

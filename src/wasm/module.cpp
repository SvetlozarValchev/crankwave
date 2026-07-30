// The Emscripten final link retains the public C ABI through its explicit export
// list. This translation unit gives CMake one final executable target without
// adding a second simulation or DSP entry point.
namespace engine_sim_offline::wasm {

[[maybe_unused]] constexpr bool kCAbiOnlyModule = true;

} // namespace engine_sim_offline::wasm

#pragma once

#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/randomness.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

#include "compile/engine_resolver.hpp"
#include "compile/scenario_resolver.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine_sim_offline::test::bmw_m52b28_migration {

struct ExactAtom {
    enum class Kind : std::uint8_t {
        unsigned_integer,
        binary64,
        text,
    };

    Kind kind = Kind::unsigned_integer;
    std::uint64_t bits = 0;
    std::string text;

    friend bool operator==(const ExactAtom &, const ExactAtom &) = default;
};

struct ExactField {
    std::string path;
    ExactAtom value;
};

class ExecutionProjection {
  public:
    void count(std::string path, std::size_t value);
    void integer(std::string path, std::uint64_t value);
    void boolean(std::string path, bool value);

    template <class Enum>
        requires std::is_enum_v<Enum>
    void enumeration(std::string path, const Enum value) {
        using Underlying = std::underlying_type_t<Enum>;
        integer(std::move(path),
                static_cast<std::uint64_t>(static_cast<Underlying>(value)));
    }

    void binary64(std::string path, double value);
    void text(std::string path, std::string value);
    void digest(std::string path, const contract::Sha256Digest &value);
    void rate(const std::string &path, const contract::RationalRateHz &value);
    void method(const std::string &path, const contract::MethodIdentity &value);

    [[nodiscard]] const std::vector<ExactField> &fields() const noexcept;

  private:
    std::vector<ExactField> fields_;
};

void compare(const ExecutionProjection &compiled,
             const ExecutionProjection &oracle);

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

[[nodiscard]] std::string read_text(const std::filesystem::path &path);
[[nodiscard]] std::vector<OwnedAsset>
load_referenced_assets(const authoring::EnginePackageDocument &document,
                       const std::filesystem::path &engine_json);
[[nodiscard]] std::vector<compile::AssetPayloadView>
asset_views(const std::vector<OwnedAsset> &assets);

class ExecutionNames {
  public:
    explicit ExecutionNames(const contract::EngineSpec &engine);

    [[nodiscard]] std::string bank(contract::BankId id) const;
    [[nodiscard]] std::string cylinder(contract::CylinderId id) const;
    [[nodiscard]] std::string port(contract::PortId id) const;
    [[nodiscard]] std::string volume(contract::GasVolumeId id) const;
    [[nodiscard]] std::string edge(contract::FlowEdgeId id) const;
    [[nodiscard]] std::string route(contract::RouteId id) const;

  private:
    using Names = std::unordered_map<std::uint32_t, std::string>;

    static void insert(Names &names, std::uint32_t id, std::string name);
    [[nodiscard]] static std::string
    lookup(const Names &names, std::uint32_t id, std::string_view kind);
    static void require_complete(std::string_view kind, const Names &names,
                                 std::size_t expected);

    Names banks_;
    Names cylinders_;
    Names ports_;
    Names volumes_;
    Names edges_;
    Names routes_;
};

template <class Range, class Name>
[[nodiscard]] auto ordered_by_name(const Range &range, Name name) {
    using Value = typename Range::value_type;
    std::vector<const Value *> result;
    result.reserve(range.size());
    for (const auto &value : range) {
        result.push_back(&value);
    }
    std::ranges::sort(result, {}, [&](const Value *value) {
        return name(*value);
    });
    return result;
}

void add_mechanism(ExecutionProjection &out, const std::string &path,
                   const contract::LegacyMechanismProfile &value,
                   const ExecutionNames &names);
void add_gas_path(ExecutionProjection &out, const std::string &path,
                  const contract::LegacyGasPathProfile &value,
                  const ExecutionNames &names);
void add_camshaft(ExecutionProjection &out, const std::string &path,
                  const contract::LegacyCamshaftProfile &value,
                  const ExecutionNames &names);
void add_ignition_and_fuel(ExecutionProjection &out, const std::string &path,
                           const contract::LowOrderEngineCoreV1 &value,
                           const ExecutionNames &names);
void add_excitation(
    ExecutionProjection &out, const std::string &path,
    const contract::LegacyReferenceExcitationProfile &value,
    const ExecutionNames &names);
void add_torque_capability(ExecutionProjection &out, const std::string &path,
                           const contract::TorqueCapability &value);

[[nodiscard]] ExecutionProjection
project_engine(const contract::EngineSpec &engine);
[[nodiscard]] ExecutionProjection
project_presentation(const contract::PresentationCalibration &presentation,
                     const contract::EngineSpec &engine);
[[nodiscard]] ExecutionProjection
project_scenario(const contract::RenderScenario &scenario);
[[nodiscard]] ExecutionProjection
project_randomness(const contract::ResolvedRandomnessPolicy &policy,
                   const contract::RandomPlan &plan,
                   const contract::EngineSpec &engine);

} // namespace engine_sim_offline::test::bmw_m52b28_migration

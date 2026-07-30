#include "bmw_m52b28_migration_test_support.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <span>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::test::bmw_m52b28_migration {
namespace {

[[nodiscard]] std::string show(const ExactAtom &value) {
    if (value.kind == ExactAtom::Kind::text) {
        return "'" + value.text + "'";
    }
    std::ostringstream output;
    output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value.bits;
    return output.str();
}

[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

} // namespace

void ExecutionProjection::count(std::string path, const std::size_t value) {
    integer(std::move(path), static_cast<std::uint64_t>(value));
}

void ExecutionProjection::integer(std::string path,
                                  const std::uint64_t value) {
    fields_.push_back(
        {std::move(path), {ExactAtom::Kind::unsigned_integer, value, {}}});
}

void ExecutionProjection::boolean(std::string path, const bool value) {
    integer(std::move(path), value ? 1U : 0U);
}

void ExecutionProjection::binary64(std::string path, const double value) {
    fields_.push_back(
        {std::move(path),
         {ExactAtom::Kind::binary64, std::bit_cast<std::uint64_t>(value), {}}});
}

void ExecutionProjection::text(std::string path, std::string value) {
    fields_.push_back(
        {std::move(path), {ExactAtom::Kind::text, 0U, std::move(value)}});
}

void ExecutionProjection::digest(std::string path,
                                 const contract::Sha256Digest &value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.bytes.size() * 2U);
    for (const std::uint8_t byte : value.bytes) {
        encoded.push_back(digits[byte >> 4U]);
        encoded.push_back(digits[byte & UINT8_C(0x0f)]);
    }
    text(std::move(path), std::move(encoded));
}

void ExecutionProjection::rate(const std::string &path,
                               const contract::RationalRateHz &value) {
    integer(path + ".numerator", value.numerator);
    integer(path + ".denominator", value.denominator);
}

void ExecutionProjection::method(const std::string &path,
                                 const contract::MethodIdentity &value) {
    text(path + ".id", value.id);
    integer(path + ".version", value.version);
    digest(path + ".configuration_sha256", value.configuration_sha256);
}

const std::vector<ExactField> &ExecutionProjection::fields() const noexcept {
    return fields_;
}

void compare(const ExecutionProjection &compiled,
             const ExecutionProjection &oracle) {
    const auto &actual = compiled.fields();
    const auto &expected = oracle.fields();
    const std::size_t common = std::min(actual.size(), expected.size());
    for (std::size_t index = 0; index < common; ++index) {
        if (actual[index].path != expected[index].path) {
            throw std::runtime_error{
                "execution projection shape first differs at field " +
                std::to_string(index) + ": compiled path '" + actual[index].path +
                "', oracle path '" + expected[index].path + "'"};
        }
        if (actual[index].value != expected[index].value) {
            throw std::runtime_error{
                "first exact migration mismatch at " + actual[index].path +
                ": compiled=" + show(actual[index].value) +
                ", oracle=" + show(expected[index].value)};
        }
    }
    if (actual.size() != expected.size()) {
        const auto &extra =
            actual.size() > common ? actual[common] : expected[common];
        throw std::runtime_error{
            "execution projection field count differs after " +
            std::to_string(common) + " fields; first unmatched path is '" +
            extra.path + "'"};
    }
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
}

std::vector<OwnedAsset>
load_referenced_assets(const authoring::EnginePackageDocument &document,
                       const std::filesystem::path &engine_json) {
    std::vector<OwnedAsset> result;
    result.reserve(document.presentation.assets.size() +
                   document.engine.accessory_configurations.size());
    const auto base = engine_json.parent_path();
    for (const auto &asset : document.presentation.assets) {
        result.push_back({
            compile::AssetKind::audio,
            asset.id.value,
            read_bytes(base / asset.uri),
        });
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        result.push_back({
            compile::AssetKind::accessory_configuration,
            asset.id.value,
            read_bytes(base / asset.uri),
        });
    }
    return result;
}

std::vector<compile::AssetPayloadView>
asset_views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> result;
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

} // namespace engine_sim_offline::test::bmw_m52b28_migration

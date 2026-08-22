#include "crankwave/compile.hpp"

#include "compile/compiled_model_storage.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace crankwave::compile {

CompiledEngine::CompiledEngine(
    std::shared_ptr<const detail::CompiledEngineStorage> storage) noexcept
    : storage_(std::move(storage)) {}

CompiledEngine::CompiledEngine(CompiledEngine &&other) noexcept
    : storage_(other.storage_) {}

CompiledEngine &CompiledEngine::operator=(CompiledEngine &&other) noexcept {
    storage_ = other.storage_;
    return *this;
}

std::string_view CompiledEngine::id() const noexcept {
    return storage_->id;
}

std::span<const StableIdAssignment>
CompiledEngine::stable_id_assignments() const noexcept {
    return storage_->resolved.stable_id_assignments;
}

const contract::ProvenanceLedger &CompiledEngine::provenance() const noexcept {
    return storage_->resolved.provenance;
}

std::size_t CompiledEngine::asset_count() const noexcept {
    return storage_->resolved.assets.size();
}

std::optional<CompiledAssetView>
CompiledEngine::asset(const std::size_t index) const noexcept {
    if (index >= storage_->resolved.assets.size()) {
        return std::nullopt;
    }
    const auto &asset = storage_->resolved.assets[index];
    return CompiledAssetView{
        asset.kind,
        asset.asset_id,
        asset.bytes,
    };
}

CompiledScenario::CompiledScenario(
    std::shared_ptr<const detail::CompiledScenarioStorage> storage) noexcept
    : storage_(std::move(storage)) {}

CompiledScenario::CompiledScenario(CompiledScenario &&other) noexcept
    : storage_(other.storage_) {}

CompiledScenario &CompiledScenario::operator=(CompiledScenario &&other) noexcept {
    storage_ = other.storage_;
    return *this;
}

std::string_view CompiledScenario::id() const noexcept {
    return storage_->id;
}

CompiledEngine CompiledScenario::engine() const noexcept {
    return CompiledEngine{storage_->engine};
}

std::span<const StableIdAssignment>
CompiledScenario::stable_id_assignments() const noexcept {
    return storage_->resolved.stable_id_assignments;
}

const contract::ProvenanceLedger &CompiledScenario::provenance() const noexcept {
    return storage_->resolved.combined_provenance;
}

const CompiledSessionCapacities &CompiledScenario::session_capacities() const noexcept {
    return storage_->resolved.request_input.session_capacities;
}

} // namespace crankwave::compile

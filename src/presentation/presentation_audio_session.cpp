#include "presentation/presentation_audio_session.hpp"

#include "dsp/source_conditioning_primitives.hpp"
#include "presentation/master_dynamics.hpp"
#include "presentation/overlap_save_convolver.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace crankwave::presentation {
namespace {

using SourceBlock = std::array<double, kSourceFramesPerMethodBlock>;
using PublishedBlock = std::array<float, kSourceFramesPerMethodBlock>;

[[nodiscard]] constexpr std::size_t stem_count(std::size_t route_count) noexcept {
    return route_count * kPresentationAudioStemsPerRoute;
}

[[nodiscard]] constexpr std::size_t
stem_offset(std::size_t route_index, PresentationAudioStemRole role) noexcept {
    return route_index * kPresentationAudioStemsPerRoute +
           static_cast<std::size_t>(role);
}

[[nodiscard]] constexpr bool
dry_only(const PresentationAudioPlan &plan) noexcept {
    return plan.processing_projection ==
           PresentationAudioProcessingProjection::source_route_dry_only;
}

[[nodiscard]] bool
valid_kernel(const dsp::RuntimeConvolutionKernel &kernel) noexcept {
    return kernel.valid();
}

void validate_plan(const PresentationAudioPlan &plan) {
    if (plan.processing_projection !=
            PresentationAudioProcessingProjection::complete &&
        plan.processing_projection !=
            PresentationAudioProcessingProjection::source_route_dry_only) {
        throw std::invalid_argument{
            "presentation audio processing projection is unknown"};
    }
    if (plan.routes.empty()) {
        throw std::invalid_argument{"presentation audio requires at least one route"};
    }
    if (plan.routes.size() >
        std::numeric_limits<std::size_t>::max() / kPresentationAudioStemsPerRoute) {
        throw std::overflow_error{"presentation audio stem count overflowed"};
    }
    if (!valid_route_conditioning_calibration(plan.conditioning)) {
        throw std::invalid_argument{
            "presentation audio conditioning is outside the executable domain"};
    }
    if (!std::isfinite(plan.publication_calibration_gain_linear) ||
        plan.publication_calibration_gain_linear <= 0.0) {
        throw std::invalid_argument{
            "presentation audio publication gain must be finite and positive"};
    }
    if (!std::isfinite(plan.audition_volume_linear) ||
        plan.audition_volume_linear <= 0.0F) {
        throw std::invalid_argument{
            "presentation audio listening volume must be finite and positive"};
    }
    const auto expected_input_frames = plan.excitation_rate == kPreviewExcitationRateHz
                                           ? kPreviewExcitationFramesPerMethodBlock
                                       : plan.excitation_rate == kExcitationRateHz
                                           ? kExcitationFramesPerMethodBlock
                                           : 0U;
    if (expected_input_frames == 0U ||
        plan.excitation_frames_per_block != expected_input_frames) {
        throw std::invalid_argument{
            "presentation audio requires one exact 20 ms block at 10000/1 or "
            "20000/1 Hz"};
    }

    for (std::size_t route = 0; route < plan.routes.size(); ++route) {
        const auto &configured = plan.routes[route];
        const bool exhaust =
            configured.source_route_kind == contract::SourceRouteKind::exhaust_outlet;
        if (!configured.route_id.valid() || !exhaust ||
            (!dry_only(plan) && !valid_kernel(configured.configured_ir)) ||
            !configured.conditioning_seeds.has_value() ||
            !configured.exhaust_valve_reference_mass_flow_kg_s.has_value() ||
            !std::isfinite(*configured.exhaust_valve_reference_mass_flow_kg_s) ||
            *configured.exhaust_valve_reference_mass_flow_kg_s <= 0.0 ||
            !std::isfinite(configured.wet_mix_01) || configured.wet_mix_01 < 0.0 ||
            configured.wet_mix_01 > 1.0) {
            throw std::invalid_argument{
                "presentation audio route requires a valid exhaust identity, "
                "conditioning, flow reference, IR, and wet mix"};
        }
        if (configured.wet_mix_01 == 0.0 && std::signbit(configured.wet_mix_01)) {
            throw std::invalid_argument{
                "presentation audio wet mix requires canonical positive zero"};
        }
        for (std::size_t prior = 0; prior < route; ++prior) {
            if (configured.route_id == plan.routes[prior].route_id) {
                throw std::invalid_argument{
                    "presentation audio route identities must be distinct"};
            }
        }
    }
    if (dry_only(plan) && !plan.audition_route_ids.empty()) {
        throw std::invalid_argument{
            "dry-only presentation projection cannot select audition routes"};
    }
    if (!dry_only(plan) &&
        plan.audition_route_ids.size() != plan.routes.size()) {
        throw std::invalid_argument{
            "presentation audio audition must select every active exhaust route"};
    }
    for (std::size_t selected = 0; selected < plan.audition_route_ids.size();
         ++selected) {
        const auto selected_id = plan.audition_route_ids[selected];
        bool found = false;
        for (const auto &route : plan.routes) {
            found = found || route.route_id == selected_id;
        }
        if (!found) {
            throw std::invalid_argument{
                "presentation audio audition route is absent from the active plan"};
        }
        for (std::size_t prior = 0; prior < selected; ++prior) {
            if (selected_id == plan.audition_route_ids[prior]) {
                throw std::invalid_argument{
                    "presentation audio audition routes must be distinct"};
            }
        }
    }
}

[[nodiscard]] PresentationAudioPlan validated_plan(PresentationAudioPlan plan) {
    validate_plan(plan);
    return plan;
}

[[nodiscard]] std::vector<contract::RouteId>
exhaust_route_ids(const PresentationAudioPlan &plan) {
    std::vector<contract::RouteId> result;
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(route.route_id);
    }
    return result;
}

[[nodiscard]] std::vector<RouteConditioningSeeds>
exhaust_route_seeds(const PresentationAudioPlan &plan) {
    std::vector<RouteConditioningSeeds> result;
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(*route.conditioning_seeds);
    }
    return result;
}

[[nodiscard]] std::vector<double>
exhaust_route_reference_mass_flows(const PresentationAudioPlan &plan) {
    std::vector<double> result;
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(*route.exhaust_valve_reference_mass_flow_kg_s);
    }
    return result;
}

[[nodiscard]] ExhaustSourceStage make_source_stage(const PresentationAudioPlan &plan) {
    const auto route_ids = exhaust_route_ids(plan);
    const auto route_seeds = exhaust_route_seeds(plan);
    const auto reference_mass_flows = exhaust_route_reference_mass_flows(plan);
    return ExhaustSourceStage{
        route_ids,         route_seeds,          reference_mass_flows,
        plan.conditioning, plan.excitation_rate, plan.excitation_frames_per_block};
}

[[nodiscard]] std::vector<std::unique_ptr<CausalConfiguredIrConvolver>>
make_convolvers(const PresentationAudioPlan &plan) {
    std::vector<std::unique_ptr<CausalConfiguredIrConvolver>> result;
    if (dry_only(plan)) {
        return result;
    }
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(
            std::make_unique<CausalConfiguredIrConvolver>(route.configured_ir));
    }
    return result;
}

[[nodiscard]] std::vector<std::size_t>
make_audition_route_indices(const PresentationAudioPlan &plan) {
    std::vector<std::size_t> result;
    result.reserve(plan.audition_route_ids.size());
    for (const auto selected_id : plan.audition_route_ids) {
        for (std::size_t route = 0; route < plan.routes.size(); ++route) {
            if (plan.routes[route].route_id == selected_id) {
                result.push_back(route);
                break;
            }
        }
    }
    return result;
}

[[nodiscard]] std::vector<contract::RouteId>
all_route_ids(const PresentationAudioPlan &plan) {
    std::vector<contract::RouteId> result;
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(route.route_id);
    }
    return result;
}

struct AudioScratch {
    AudioScratch(std::size_t route_count, std::size_t exhaust_route_count,
                 const PresentationAudioProcessingProjection projection)
        : conditioned_exhaust(kSourceFramesPerMethodBlock * exhaust_route_count),
          dry(route_count),
          configured_ir(projection == PresentationAudioProcessingProjection::complete
                            ? route_count
                            : 0U),
          selected(projection == PresentationAudioProcessingProjection::complete
                       ? route_count
                       : 0U),
          stems(projection == PresentationAudioProcessingProjection::complete
                    ? stem_count(route_count)
                    : route_count),
          stem_views(stem_count(route_count)) {
        if (projection == PresentationAudioProcessingProjection::complete) {
            for (std::size_t stem = 0; stem < stems.size(); ++stem) {
                stem_views[stem] = std::span<const float>{stems[stem]};
            }
        } else {
            for (std::size_t route = 0; route < route_count; ++route) {
                stem_views[stem_offset(route, PresentationAudioStemRole::dry)] =
                    std::span<const float>{stems[route]};
            }
        }
    }

    std::vector<double> conditioned_exhaust;
    std::vector<SourceBlock> dry;
    std::vector<SourceBlock> configured_ir;
    std::vector<SourceBlock> selected;
    std::vector<PublishedBlock> stems;
    std::vector<std::span<const float>> stem_views;
    PublishedBlock raw_master{};
    PublishedBlock audition_master{};
};

} // namespace

PresentationAudioBlockView::PresentationAudioBlockView(
    SourceBlockExtent extent, std::span<const contract::RouteId> route_ids,
    std::span<const contract::RouteId> audition_route_ids,
    std::span<const StemBlock> stems, std::span<const float> raw_master,
    std::span<const float> audition_master) noexcept
    : extent_(extent), route_ids_(route_ids), audition_route_ids_(audition_route_ids),
      stems_(stems), raw_master_(raw_master), audition_master_(audition_master) {}

std::uint64_t PresentationAudioBlockView::first_input_frame_index() const noexcept {
    return extent_.first_input_frame_index;
}

std::uint64_t PresentationAudioBlockView::first_source_frame_index() const noexcept {
    return extent_.first_source_frame_index;
}

std::size_t PresentationAudioBlockView::input_frame_count() const noexcept {
    return extent_.input_frame_count;
}

std::size_t PresentationAudioBlockView::frame_count() const noexcept {
    return extent_.source_frame_count;
}

contract::RationalRateHz PresentationAudioBlockView::sample_rate() const noexcept {
    return kPresentationAudioRateHz;
}

std::span<const contract::RouteId>
PresentationAudioBlockView::route_ids() const noexcept {
    return route_ids_;
}

std::span<const contract::RouteId>
PresentationAudioBlockView::audition_route_ids() const noexcept {
    return audition_route_ids_;
}

std::size_t PresentationAudioBlockView::route_count() const noexcept {
    return route_ids_.size();
}

std::span<const float>
PresentationAudioBlockView::route_stem(std::size_t route_index,
                                       PresentationAudioStemRole role) const {
    const auto role_index = static_cast<std::size_t>(role);
    if (route_index >= route_count() || role_index >= kPresentationAudioStemsPerRoute) {
        throw std::out_of_range{
            "presentation audio stem route or role is out of range"};
    }
    return stems_[stem_offset(route_index, role)];
}

std::span<const float> PresentationAudioBlockView::raw_master() const noexcept {
    return raw_master_;
}

std::span<const float> PresentationAudioBlockView::audition_master() const noexcept {
    return audition_master_;
}

class PresentationAudioSession::Implementation final {
  public:
    explicit Implementation(PresentationAudioPlan plan)
        : plan_(validated_plan(std::move(plan))), route_ids_(all_route_ids(plan_)),
          source_stage_(make_source_stage(plan_)),
          convolvers_(make_convolvers(plan_)),
          audition_route_indices_(make_audition_route_indices(plan_)),
          master_dynamics_(dry_only(plan_)
                               ? std::optional<MasterDynamics>{}
                               : std::optional<MasterDynamics>{
                                     std::in_place,
                                     plan_.audition_volume_linear}),
          scratch_(plan_.routes.size(), source_stage_.route_count(),
                   plan_.processing_projection) {}

    [[nodiscard]] PresentationAudioBlockView
    process(ExhaustExcitationBlockView exhaust) {
        if (terminal_failed_) {
            throw std::logic_error{
                "presentation audio cannot resume after an arithmetic failure"};
        }

        bool source_stage_advanced = false;
        try {
            const auto extent =
                source_stage_.process(exhaust, scratch_.conditioned_exhaust);
            source_stage_advanced = true;

            const auto route_count = plan_.routes.size();
            for (std::size_t route = 0; route < route_count; ++route) {
                for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock;
                     ++frame) {
                    scratch_.dry[route][frame] =
                        scratch_
                            .conditioned_exhaust[frame * source_stage_.route_count() +
                                                 route];
                }

                if (dry_only(plan_)) {
                    continue;
                }

                convolvers_[route]->process(scratch_.dry[route],
                                            scratch_.configured_ir[route]);
                const double wet_mix = plan_.routes[route].wet_mix_01;
                for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock;
                     ++frame) {
                    scratch_.selected[route][frame] =
                        wet_mix * scratch_.configured_ir[route][frame] +
                        (1.0 - wet_mix) * scratch_.dry[route][frame];
                }
            }

            if (dry_only(plan_)) {
                publish_dry_stems();
            } else {
                publish_stems();
                mix_masters();
            }
            return {
                extent,
                route_ids_,
                plan_.audition_route_ids,
                scratch_.stem_views,
                dry_only(plan_) ? std::span<const float>{}
                                : std::span<const float>{scratch_.raw_master},
                dry_only(plan_) ? std::span<const float>{}
                                : std::span<const float>{scratch_.audition_master},
            };
        } catch (...) {
            if (source_stage_advanced || source_stage_.terminal_failed()) {
                terminal_failed_ = true;
            }
            throw;
        }
    }

    [[nodiscard]] std::span<const contract::RouteId> route_ids() const noexcept {
        return route_ids_;
    }

    [[nodiscard]] std::span<const contract::RouteId>
    audition_route_ids() const noexcept {
        return plan_.audition_route_ids;
    }

    [[nodiscard]] std::uint64_t next_input_frame_index() const noexcept {
        return source_stage_.next_input_frame_index();
    }

    [[nodiscard]] std::uint64_t next_source_frame_index() const noexcept {
        return source_stage_.next_source_frame_index();
    }

    [[nodiscard]] bool terminal_failed() const noexcept {
        return terminal_failed_ || source_stage_.terminal_failed();
    }

  private:
    void publish_dry_stems() {
        for (std::size_t route = 0; route < plan_.routes.size(); ++route) {
            auto &published = scratch_.stems[route];
            for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
                published[frame] = dsp::publish_calibrated_float32(
                    scratch_.dry[route][frame],
                    plan_.publication_calibration_gain_linear);
            }
        }
    }

    void publish_stems() {
        for (std::size_t route = 0; route < plan_.routes.size(); ++route) {
            const auto dry = stem_offset(route, PresentationAudioStemRole::dry);
            const auto configured_ir =
                stem_offset(route, PresentationAudioStemRole::configured_transfer);
            const auto selected =
                stem_offset(route, PresentationAudioStemRole::selected);
            for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
                scratch_.stems[dry][frame] = dsp::publish_calibrated_float32(
                    scratch_.dry[route][frame],
                    plan_.publication_calibration_gain_linear);
                scratch_.stems[configured_ir][frame] = dsp::publish_calibrated_float32(
                    scratch_.configured_ir[route][frame],
                    plan_.publication_calibration_gain_linear);
                scratch_.stems[selected][frame] = dsp::publish_calibrated_float32(
                    scratch_.selected[route][frame],
                    plan_.publication_calibration_gain_linear);
            }
        }
    }

    void mix_masters() {
        for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock; ++frame) {
            const auto first_route = audition_route_indices_.front();
            float raw = scratch_.stems[stem_offset(
                first_route, PresentationAudioStemRole::selected)][frame];
            float leveler_mix =
                static_cast<float>(scratch_.selected[first_route][frame]);
            if (!std::isfinite(raw)) {
                throw std::domain_error{
                    "presentation audio mastering input was non-finite"};
            }
            if (!std::isfinite(leveler_mix)) {
                throw std::domain_error{
                    "presentation audio leveler input was non-finite"};
            }
            for (std::size_t selected = 1; selected < audition_route_indices_.size();
                 ++selected) {
                const auto route = audition_route_indices_[selected];
                const float sample = scratch_.stems[stem_offset(
                    route, PresentationAudioStemRole::selected)][frame];
                if (!std::isfinite(sample)) {
                    throw std::domain_error{
                        "presentation audio mastering input was non-finite"};
                }
                raw = raw + sample;
                if (!std::isfinite(raw)) {
                    throw std::domain_error{
                        "presentation audio raw master sum was non-finite"};
                }
                const float source_sample =
                    static_cast<float>(scratch_.selected[route][frame]);
                if (!std::isfinite(source_sample)) {
                    throw std::domain_error{
                        "presentation audio leveler input was non-finite"};
                }
                leveler_mix = leveler_mix + source_sample;
                if (!std::isfinite(leveler_mix)) {
                    throw std::domain_error{
                        "presentation audio leveler sum was non-finite"};
                }
            }
            const float audition = master_dynamics_->process(leveler_mix);
            if (!std::isfinite(audition)) {
                throw std::domain_error{
                    "presentation audio master dynamics produced non-finite output"};
            }
            scratch_.raw_master[frame] = raw;
            scratch_.audition_master[frame] = audition;
        }
    }

    PresentationAudioPlan plan_;
    std::vector<contract::RouteId> route_ids_;
    ExhaustSourceStage source_stage_;
    std::vector<std::unique_ptr<CausalConfiguredIrConvolver>> convolvers_;
    std::vector<std::size_t> audition_route_indices_;
    std::optional<MasterDynamics> master_dynamics_;
    AudioScratch scratch_;
    bool terminal_failed_ = false;
};

PresentationAudioSession::PresentationAudioSession(PresentationAudioPlan plan)
    : implementation_{std::make_unique<Implementation>(std::move(plan))} {}

PresentationAudioSession::~PresentationAudioSession() = default;

PresentationAudioBlockView
PresentationAudioSession::process(ExhaustExcitationBlockView exhaust) {
    return implementation_->process(exhaust);
}

std::span<const contract::RouteId>
PresentationAudioSession::route_ids() const noexcept {
    return implementation_->route_ids();
}

std::span<const contract::RouteId>
PresentationAudioSession::audition_route_ids() const noexcept {
    return implementation_->audition_route_ids();
}

std::uint64_t PresentationAudioSession::next_input_frame_index() const noexcept {
    return implementation_->next_input_frame_index();
}

std::uint64_t PresentationAudioSession::next_source_frame_index() const noexcept {
    return implementation_->next_source_frame_index();
}

bool PresentationAudioSession::terminal_failed() const noexcept {
    return implementation_->terminal_failed();
}

} // namespace crankwave::presentation

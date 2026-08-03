#include "presentation/presentation_audio_session.hpp"

#include "dsp/source_conditioning_primitives.hpp"
#include "presentation/overlap_save_convolver.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine_sim_offline::presentation {
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

void validate_plan(const PresentationAudioPlan &plan) {
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
    if (!std::isfinite(plan.audition_monitoring_gain_linear) ||
        plan.audition_monitoring_gain_linear <= 0.0F) {
        throw std::invalid_argument{
            "presentation audio monitoring gain must be finite and positive"};
    }
    if (plan.excitation_rate != kExcitationRateHz ||
        plan.excitation_frames_per_block != kExcitationFramesPerMethodBlock) {
        throw std::invalid_argument{
            "presentation audio requires one exact 400-frame, 20 ms block at "
            "20000/1 Hz"};
    }

    for (std::size_t route = 0; route < plan.routes.size(); ++route) {
        const auto &configured = plan.routes[route];
        if (!configured.route_id.valid() || !configured.configured_ir ||
            !std::isfinite(configured.wet_mix_01) || configured.wet_mix_01 < 0.0 ||
            configured.wet_mix_01 > 1.0) {
            throw std::invalid_argument{
                "presentation audio route requires a valid identity, IR, and "
                "wet mix in [0, 1]"};
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

    if (plan.audition_route_ids.size() != plan.routes.size()) {
        throw std::invalid_argument{
            "presentation audio audition must select every route"};
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
                "presentation audio audition route is absent from the route plan"};
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
source_route_ids(const PresentationAudioPlan &plan) {
    std::vector<contract::RouteId> result;
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(route.route_id);
    }
    return result;
}

[[nodiscard]] std::vector<RouteConditioningSeeds>
source_route_seeds(const PresentationAudioPlan &plan) {
    std::vector<RouteConditioningSeeds> result;
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(route.conditioning_seeds);
    }
    return result;
}

[[nodiscard]] ExhaustSourceStage make_source_stage(const PresentationAudioPlan &plan) {
    const auto route_ids = source_route_ids(plan);
    const auto route_seeds = source_route_seeds(plan);
    return ExhaustSourceStage{route_ids, route_seeds, plan.conditioning,
                              plan.excitation_rate, plan.excitation_frames_per_block};
}

[[nodiscard]] std::vector<std::unique_ptr<CausalOverlapSaveConvolver>>
make_convolvers(const PresentationAudioPlan &plan) {
    std::vector<std::unique_ptr<CausalOverlapSaveConvolver>> result;
    result.reserve(plan.routes.size());
    for (const auto &route : plan.routes) {
        result.push_back(
            std::make_unique<CausalOverlapSaveConvolver>(route.configured_ir));
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

struct AudioScratch {
    explicit AudioScratch(std::size_t route_count)
        : conditioned(kSourceFramesPerMethodBlock * route_count), dry(route_count),
          configured_ir(route_count), selected(route_count),
          stems(stem_count(route_count)), stem_views(stem_count(route_count)) {
        for (std::size_t stem = 0; stem < stems.size(); ++stem) {
            stem_views[stem] = std::span<const float>{stems[stem]};
        }
    }

    std::vector<double> conditioned;
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
        : plan_(validated_plan(std::move(plan))),
          source_stage_(make_source_stage(plan_)), convolvers_(make_convolvers(plan_)),
          audition_route_indices_(make_audition_route_indices(plan_)),
          scratch_(plan_.routes.size()) {}

    [[nodiscard]] PresentationAudioBlockView process(ExhaustExcitationBlockView input) {
        if (terminal_failed_) {
            throw std::logic_error{
                "presentation audio cannot resume after an arithmetic failure"};
        }

        bool source_stage_advanced = false;
        try {
            const auto extent = source_stage_.process(input, scratch_.conditioned);
            source_stage_advanced = true;

            const auto route_count = plan_.routes.size();
            for (std::size_t route = 0; route < route_count; ++route) {
                for (std::size_t frame = 0; frame < kSourceFramesPerMethodBlock;
                     ++frame) {
                    scratch_.dry[route][frame] =
                        scratch_.conditioned[frame * route_count + route];
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

            publish_stems();
            mix_masters();
            return {
                extent,
                source_stage_.expected_route_ids(),
                plan_.audition_route_ids,
                scratch_.stem_views,
                scratch_.raw_master,
                scratch_.audition_master,
            };
        } catch (...) {
            if (source_stage_advanced || source_stage_.terminal_failed()) {
                terminal_failed_ = true;
            }
            throw;
        }
    }

    [[nodiscard]] std::span<const contract::RouteId> route_ids() const noexcept {
        return source_stage_.expected_route_ids();
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
    void publish_stems() {
        for (std::size_t route = 0; route < plan_.routes.size(); ++route) {
            const auto dry = stem_offset(route, PresentationAudioStemRole::dry);
            const auto configured_ir =
                stem_offset(route, PresentationAudioStemRole::configured_ir);
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
            if (!std::isfinite(raw)) {
                throw std::domain_error{
                    "presentation audio mastering input was non-finite"};
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
            }
            const float audition = raw * plan_.audition_monitoring_gain_linear;
            if (!std::isfinite(audition)) {
                throw std::domain_error{
                    "presentation audio monitoring gain produced non-finite "
                    "output"};
            }
            scratch_.raw_master[frame] = raw;
            scratch_.audition_master[frame] = audition;
        }
    }

    PresentationAudioPlan plan_;
    ExhaustSourceStage source_stage_;
    std::vector<std::unique_ptr<CausalOverlapSaveConvolver>> convolvers_;
    std::vector<std::size_t> audition_route_indices_;
    AudioScratch scratch_;
    bool terminal_failed_ = false;
};

PresentationAudioSession::PresentationAudioSession(PresentationAudioPlan plan)
    : implementation_{std::make_unique<Implementation>(std::move(plan))} {}

PresentationAudioSession::~PresentationAudioSession() = default;

PresentationAudioBlockView
PresentationAudioSession::process(ExhaustExcitationBlockView input) {
    return implementation_->process(input);
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

} // namespace engine_sim_offline::presentation

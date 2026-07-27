#include "engine_sim_offline/contract/common.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

namespace engine_sim_offline::contract {

bool Sha256Digest::is_zero() const noexcept {
    return std::ranges::all_of(bytes, [](std::uint8_t byte) { return byte == 0; });
}

void ValidationReport::add(ContractIssueCode code, std::string path,
                           std::string message) {
    issues.push_back({code, std::move(path), std::move(message)});
}

void ValidationReport::append(ValidationReport other) {
    issues.insert(issues.end(), std::make_move_iterator(other.issues.begin()),
                  std::make_move_iterator(other.issues.end()));
}

bool is_valid_semantic_id(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }

    const auto ascii_lower = [](char character) {
        return character >= 'a' && character <= 'z';
    };
    const auto ascii_digit = [](char character) {
        return character >= '0' && character <= '9';
    };
    if (!(ascii_lower(value.front()) || ascii_digit(value.front()))) {
        return false;
    }

    return std::ranges::all_of(value, [&](char character) {
        return ascii_lower(character) || ascii_digit(character) || character == '.' ||
               character == '_' || character == '-' || character == '/';
    });
}

ValidationReport validate(const RationalRateHz &rate) {
    ValidationReport report;
    if (rate.numerator == 0) {
        report.add(ContractIssueCode::invalid_value, "numerator",
                   "rate numerator must be positive");
    }
    if (rate.denominator == 0) {
        report.add(ContractIssueCode::invalid_value, "denominator",
                   "rate denominator must be positive");
    }
    if (rate.numerator != 0 && rate.denominator != 0 &&
        std::gcd(rate.numerator, rate.denominator) != 1) {
        report.add(ContractIssueCode::invalid_value, "",
                   "rate must be stored as a reduced rational");
    }
    return report;
}

ValidationReport validate(const RenderRates &rates) {
    ValidationReport report;
    const auto append = [&report](const RationalRateHz &rate, std::string_view path) {
        auto nested = validate(rate);
        for (auto &issue : nested.issues) {
            issue.path = issue.path.empty() ? std::string(path)
                                            : std::string(path) + "." + issue.path;
            report.issues.push_back(std::move(issue));
        }
    };
    append(rates.physics, "physics");
    append(rates.capture, "capture");
    append(rates.source_processing, "source_processing");
    append(rates.acoustic, "acoustic");
    append(rates.delivery, "delivery");
    return report;
}

ValidationReport validate(const MethodIdentity &method) {
    ValidationReport report;
    if (!is_valid_semantic_id(method.id)) {
        report.add(ContractIssueCode::invalid_value, "id",
                   "method ID must be a canonical semantic ID");
    }
    if (method.version == 0) {
        report.add(ContractIssueCode::invalid_value, "version",
                   "method version must be positive");
    }
    if (method.configuration_sha256.is_zero()) {
        report.add(ContractIssueCode::invalid_value, "configuration_sha256",
                   "method configuration digest must be nonzero");
    }
    return report;
}

} // namespace engine_sim_offline::contract

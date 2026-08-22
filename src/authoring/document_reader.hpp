#pragma once

#include "crankwave/authoring/diagnostic.hpp"
#include "crankwave/authoring/json.hpp"
#include "crankwave/authoring/parse.hpp"
#include "crankwave/authoring/quantity.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace crankwave::authoring::detail {

[[nodiscard]] std::string pointer_member(std::string_view parent,
                                         std::string_view member);
[[nodiscard]] std::string pointer_index(std::string_view parent, std::size_t index);
[[nodiscard]] DiagnosticReport syntax_diagnostic(const JsonParseError &error);
[[nodiscard]] DiagnosticReport internal_diagnostic(std::string message);

class DocumentReader final {
  public:
    explicit DocumentReader(AuthoringParseLimits limits) noexcept;

    bool object(JsonValue value, std::string_view path,
                const std::optional<DiagnosticSubject> &subject = {});
    bool array(JsonValue value, std::string_view path,
               const std::optional<DiagnosticSubject> &subject = {});

    [[nodiscard]] JsonValue
    required(JsonValue object, std::string_view key, std::string_view object_path,
             const std::optional<DiagnosticSubject> &subject = {});
    [[nodiscard]] JsonValue optional(JsonValue object, std::string_view key) const;
    void reject_unknown(JsonValue object, std::string_view object_path,
                        std::initializer_list<std::string_view> allowed,
                        const std::optional<DiagnosticSubject> &subject = {});

    bool string(JsonValue value, std::string_view path, std::string &output,
                const std::optional<DiagnosticSubject> &subject = {});
    bool boolean(JsonValue value, std::string_view path, bool &output,
                 const std::optional<DiagnosticSubject> &subject = {});
    bool number(JsonValue value, std::string_view path, double &output,
                const std::optional<DiagnosticSubject> &subject = {});
    bool fraction(JsonValue value, std::string_view path, double &output,
                  const std::optional<DiagnosticSubject> &subject = {});
    bool nonnegative_number(JsonValue value, std::string_view path, double &output,
                            const std::optional<DiagnosticSubject> &subject = {});
    bool uint32(JsonValue value, std::string_view path, std::uint32_t &output,
                const std::optional<DiagnosticSubject> &subject = {});
    bool uint64(JsonValue value, std::string_view path, std::uint64_t &output,
                const std::optional<DiagnosticSubject> &subject = {});
    bool quantity(JsonValue value, std::string_view path, QuantityDimension dimension,
                  Quantity &output,
                  const std::optional<DiagnosticSubject> &subject = {});
    bool rational_rate(JsonValue value, std::string_view path, RationalRate &output,
                       const std::optional<DiagnosticSubject> &subject = {});

    template <class Tag>
    bool id(JsonValue value, std::string_view path, StableId<Tag> &output,
            const std::optional<DiagnosticSubject> &subject = {}) {
        std::string text;
        if (!string(value, path, text, subject)) {
            return false;
        }
        if (!valid_stable_id(text)) {
            add(DiagnosticCode::invalid_value, path,
                "stable ID must contain 1..128 ASCII letters, digits, '.', '_', or "
                "'-' and begin with a letter or digit",
                subject);
            return false;
        }
        output.value = std::move(text);
        return true;
    }

    template <class Tag>
    bool ref(JsonValue value, std::string_view path, StableRef<Tag> &output,
             const std::optional<DiagnosticSubject> &subject = {}) {
        std::string text;
        if (!string(value, path, text, subject)) {
            return false;
        }
        if (!valid_stable_id(text)) {
            add(DiagnosticCode::invalid_value, path,
                "stable reference must contain 1..128 ASCII letters, digits, '.', "
                "'_', or '-' and begin with a letter or digit",
                subject);
            return false;
        }
        output.value = std::move(text);
        return true;
    }

    void add(DiagnosticCode code, std::string_view path, std::string message,
             const std::optional<DiagnosticSubject> &subject = {});

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const AuthoringParseLimits &limits() const noexcept;
    [[nodiscard]] DiagnosticReport finish() &&;

  private:
    [[nodiscard]] static bool valid_stable_id(std::string_view value) noexcept;
    AuthoringParseLimits limits_;
    DiagnosticReport report_;
};

[[nodiscard]] std::optional<DiagnosticSubject> subject(std::string kind,
                                                       std::string id);

} // namespace crankwave::authoring::detail

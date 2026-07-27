#include <array>
#include <numbers>
#include <span>

int main() {
    static_assert(__cplusplus >= 202002L);

    static constexpr std::array values{1, 2, 3};
    constexpr std::span<const int> view{values};
    static_assert(view.size() == values.size());
    static_assert(std::numbers::pi_v<double> > 3.14);

    return view.front() == 1 ? 0 : 1;
}

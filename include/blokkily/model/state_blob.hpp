#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace blokkily {

// A plugin's saved state: opaque bytes, read-only once made. Copies share the
// bytes, so a song copied for every step of undo does not copy every
// plugin's state with it; assigning new bytes replaces them for this copy
// alone. It reads as the std::vector<std::byte> it holds.
class StateBlob {
public:
    StateBlob() = default;
    StateBlob(std::vector<std::byte> bytes) // NOLINT: implicit, as the vector it stands for
        : bytes_(bytes.empty() ? nullptr : std::make_shared<const std::vector<std::byte>>(std::move(bytes))) {}
    StateBlob(std::initializer_list<std::byte> bytes) : StateBlob(std::vector<std::byte>(bytes)) {}

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept {
        static const std::vector<std::byte> none;
        return bytes_ ? *bytes_ : none;
    }
    operator const std::vector<std::byte>&() const noexcept { return bytes(); } // NOLINT

    [[nodiscard]] bool empty() const noexcept { return bytes().empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return bytes().size(); }
    [[nodiscard]] const std::byte* data() const noexcept { return bytes().data(); }
    [[nodiscard]] const std::byte& front() const { return bytes().front(); }
    [[nodiscard]] const std::byte& operator[](std::size_t i) const { return bytes()[i]; }
    [[nodiscard]] auto begin() const noexcept { return bytes().begin(); }
    [[nodiscard]] auto end() const noexcept { return bytes().end(); }
    void clear() noexcept { bytes_.reset(); }

    friend bool operator==(const StateBlob& a, const StateBlob& b) noexcept {
        return a.bytes_ == b.bytes_ || a.bytes() == b.bytes();
    }
    friend bool operator==(const StateBlob& a, const std::vector<std::byte>& b) noexcept { return a.bytes() == b; }

private:
    std::shared_ptr<const std::vector<std::byte>> bytes_;
};

} // namespace blokkily

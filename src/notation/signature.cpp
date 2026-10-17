#include "blokkily/notation/signature.hpp"

namespace blokkily::notation {

std::string KeySignature::label() const {
    const SpelledPitch tonic = major_tonic();
    std::string name(step_name(tonic.step));
    name += accidental_name(tonic.accidental);
    name += " major";
    return name;
}

} // namespace blokkily::notation

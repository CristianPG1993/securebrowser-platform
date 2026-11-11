#include "security/security_context.h"

namespace sbp {

// Conserva los datos recibidos; PolicyManager será responsable de crear la nueva generación.
SecurityContext::SecurityContext(
    std::uint64_t generation,
    std::int64_t deviceId,
    std::int64_t companyId)
    : generation_(generation), deviceId_(deviceId), companyId_(companyId) {}

std::uint64_t SecurityContext::Generation() const noexcept {
    return generation_;
}

std::int64_t SecurityContext::DeviceId() const noexcept {
    return deviceId_;
}

std::int64_t SecurityContext::CompanyId() const noexcept {
    return companyId_;
}

// Exige la misma generación e instalación para considerar que ambos contextos coinciden.
bool SecurityContext::operator==(const SecurityContext& other) const noexcept {
    return generation_ == other.generation_
        && deviceId_ == other.deviceId_
        && companyId_ == other.companyId_;
}

}  // namespace sbp

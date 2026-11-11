#include "security/policy_snapshot.h"

#include <utility>

namespace sbp {

// Conserva los metadatos y transfiere los objetos recibidos por valor al snapshot.
PolicySnapshot::PolicySnapshot(
    std::int64_t id,
    std::int64_t companyId,
    std::string name,
    UrlFilteringConfig urlFiltering,
    DownloadControlConfig downloadControl,
    Timestamp createdAt,
    Timestamp updatedAt)
    : id_(id),
      companyId_(companyId),
      name_(std::move(name)),
      urlFiltering_(std::move(urlFiltering)),
      downloadControl_(std::move(downloadControl)),
      createdAt_(createdAt),
      updatedAt_(updatedAt) {}

// Consulta el identificador de la política sin modificar el snapshot.
std::int64_t PolicySnapshot::Id() const noexcept {
    return id_;
}

// Consulta el identificador de la empresa propietaria de la política sin modificar el snapshot.
std::int64_t PolicySnapshot::CompanyId() const noexcept {
    return companyId_;
}

// Consulta el nombre de la política sin modificar el snapshot.
const std::string& PolicySnapshot::Name() const noexcept {
    return name_;
}

// Consulta la configuración de filtrado de URLs sin modificar el snapshot.
const UrlFilteringConfig& PolicySnapshot::UrlFiltering() const noexcept {
    return urlFiltering_;
}

// Consulta la configuración de control de descargas sin modificar el snapshot.
const DownloadControlConfig& PolicySnapshot::DownloadControl() const noexcept {
    return downloadControl_;
}

// Consulta la fecha de creación de la política en el backend sin modificar el snapshot.
PolicySnapshot::Timestamp PolicySnapshot::CreatedAt() const noexcept {
    return createdAt_;
}

// Consulta la fecha de modificación de la política en el backend, no su vigencia local.
PolicySnapshot::Timestamp PolicySnapshot::UpdatedAt() const noexcept {
    return updatedAt_;
}

}  // namespace sbp

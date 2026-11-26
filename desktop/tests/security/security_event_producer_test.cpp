#include "security/security_event_producer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sbp {

class SecurityEventProducerTest : public ::testing::Test {
protected:
    const SecurityContext validOrigin{1, 10, 20};
    std::string nextUuid = "10ea1c30-82a3-4590-9fec-66d778ea21cf";
    int uuidRequests = 0;
    std::vector<SecurityEvent> deliveries;

    // Cada prueba puede elegir el siguiente UUID y comprobar si llegó a pedirse.
    SecurityEventProducer::UuidGenerator generate = [this] {
        ++uuidRequests;
        return nextUuid;
    };
    // Comprobamos el contexto recibido y guardamos una copia de cada evento entregado.
    SecurityEventSink record = [this](const SecurityContext& receivedOrigin, const SecurityEvent& event) {
        EXPECT_EQ(validOrigin, receivedOrigin);
        deliveries.push_back(event);
        return true;
    };
};

// Un bloqueo de dominio genera un UUID y entrega el evento una vez, con el contexto y la hora originales.
TEST_F(SecurityEventProducerTest, DeliversUrlBlockedWithOriginalContext) {
    const std::chrono::sys_time<std::chrono::microseconds> occurredAt{std::chrono::microseconds{1'234'567}};
    const SecurityEventProducer producer(generate, record);
    EXPECT_TRUE(producer.ProduceUrlBlocked(validOrigin, occurredAt, SecurityReason::DomainDenied, "blocked.example", 7));

    EXPECT_EQ(1, uuidRequests);
    ASSERT_EQ(1u, deliveries.size());
    const auto& event = deliveries.front();
    EXPECT_EQ(nextUuid, event.eventUuid);
    EXPECT_EQ(SecurityEventType::UrlBlocked, event.type);
    EXPECT_EQ(occurredAt, event.occurredAt);
    ASSERT_TRUE(event.details.has_value());
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"DOMAIN_DENIED","policyId":7,"host":"blocked.example"})", *event.details);
}

// El bloqueo conserva la hora, la política y la extensión UTF-8 ya evaluada.
TEST_F(SecurityEventProducerTest, DeliversDownloadBlockedWithEvaluatedExtension) {
    const std::chrono::sys_time<std::chrono::microseconds> occurredAt{std::chrono::microseconds{123456}};
    const SecurityEventProducer producer(generate, record);
    const std::string extension = "\xC3\xA9xe";
    ASSERT_TRUE(producer.ProduceDownloadBlocked(validOrigin, occurredAt, SecurityReason::ExtensionDenied, extension, 7));
    ASSERT_EQ(1u, deliveries.size());
    const auto& event = deliveries.front();
    EXPECT_EQ(1, uuidRequests);
    EXPECT_EQ(nextUuid, event.eventUuid);
    EXPECT_EQ(occurredAt, event.occurredAt);
    EXPECT_EQ(SecurityEventType::DownloadBlocked, event.type);
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"EXTENSION_DENIED","policyId":7,"extension":")" + extension + R"("})",
              event.details);
}

// No se incluyen rutas, nombres completos ni datos mal formados aunque lleguen como extensión.
TEST_F(SecurityEventProducerTest, OmitsUnsafeDownloadExtensionAndKeepsReason) {
    const SecurityEventProducer producer(generate, record);
    for (const char* input : {"informe.zip", "carpeta/zip", R"(carpeta\zip)", "a\"b", "\n", "\xFF"}) {
        ASSERT_TRUE(producer.ProduceDownloadBlocked(validOrigin, {}, SecurityReason::ExtensionNotAllowed, input, {}));
        EXPECT_EQ(R"({"schemaVersion":1,"reason":"EXTENSION_NOT_ALLOWED"})", deliveries.back().details);
    }
    ASSERT_TRUE(producer.ProduceDownloadBlocked(validOrigin, {}, SecurityReason::MissingExtension, "privado", {}));
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"MISSING_EXTENSION"})", deliveries.back().details);
    EXPECT_FALSE(producer.ProduceDownloadBlocked(validOrigin, {}, SecurityReason::Allowed, "zip", 7));
    EXPECT_FALSE(producer.ProduceDownloadBlocked(validOrigin, {}, SecurityReason::DomainDenied, "zip", 7));
}

// Cuenta puntos de código Unicode, aunque una letra de la extensión ocupe varios bytes.
TEST_F(SecurityEventProducerTest, BoundsDownloadDetailsByUnicodeCodePoints) {
    const SecurityEventProducer producer(generate, record);
    const std::string prefix = R"({"schemaVersion":1,"reason":"EXTENSION_DENIED","extension":")";
    const std::string suffix = R"("})";
    std::string extension;
    for (std::size_t i = 0; i < 2000 - prefix.size() - suffix.size(); ++i) {
        extension += "\xC3\xA9";
    }
    ASSERT_TRUE(producer.ProduceDownloadBlocked(validOrigin, {}, SecurityReason::ExtensionDenied, extension, {}));
    EXPECT_EQ(prefix + extension + suffix, deliveries.back().details);
    extension += "\xC3\xA9";
    ASSERT_TRUE(producer.ProduceDownloadBlocked(validOrigin, {}, SecurityReason::ExtensionDenied, extension, {}));
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"EXTENSION_DENIED"})", deliveries.back().details);
}

// Cada intento real contra el mismo dominio conserva su propio UUID.
TEST_F(SecurityEventProducerTest, DeliversSeparateEventsForTwoBlocksOfTheSameHost) {
    const SecurityEventProducer producer(generate, record);
    const std::string firstUuid = nextUuid;
    ASSERT_TRUE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, "blocked.example", 7));
    nextUuid = "47A98DE0-214A-438D-AF9F-B52999E9E8B0";
    ASSERT_TRUE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, "blocked.example", 7));

    EXPECT_EQ(2, uuidRequests);
    ASSERT_EQ(2u, deliveries.size());
    EXPECT_EQ(firstUuid, deliveries[0].eventUuid);
    EXPECT_EQ(nextUuid, deliveries[1].eventUuid);
}

// El productor necesita ambas funciones al construirse.
TEST_F(SecurityEventProducerTest, RequiresGeneratorAndSink) {
    EXPECT_THROW(SecurityEventProducer({}, record), std::invalid_argument);
    EXPECT_THROW(SecurityEventProducer(generate, {}), std::invalid_argument);
}

// Un contexto incompleto, un permiso o un motivo de descarga no producen un evento de navegación.
TEST_F(SecurityEventProducerTest, RejectsInvalidOriginOrReasonBeforeRequestingUuid) {
    const SecurityEventProducer producer(generate, record);
    for (const auto& origin : {SecurityContext(0, 10, 20), SecurityContext(1, 0, 20), SecurityContext(1, 10, -1)}) {
        EXPECT_FALSE(producer.ProduceUrlBlocked(origin, {}, SecurityReason::DomainDenied, "blocked.example", 7));
    }
    for (const auto reason : {SecurityReason::Allowed, SecurityReason::ExtensionDenied}) {
        EXPECT_FALSE(producer.ProduceUrlBlocked(validOrigin, {}, reason, "blocked.example", 7));
    }
    EXPECT_EQ(0, uuidRequests);
    EXPECT_TRUE(deliveries.empty());
}

// Los detalles conservan el motivo sin exponer una URL completa ni inventar un identificador de política.
TEST_F(SecurityEventProducerTest, OmitsUnsafeHostAndUnavailablePolicyId) {
    const SecurityEventProducer producer(generate, record);
    ASSERT_TRUE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::InvalidUrl, "blocked.example", std::nullopt));
    ASSERT_TRUE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, "https://example.test/path?q=1", 0));
    ASSERT_TRUE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::IpBlocked, "[2001:db8::1]", std::nullopt));

    ASSERT_EQ(3u, deliveries.size());
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"INVALID_URL"})", deliveries[0].details);
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"DOMAIN_DENIED"})", deliveries[1].details);
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"IP_BLOCKED","host":"[2001:db8::1]"})", deliveries[2].details);
}

// Un detalle de 2000 caracteres cabe exactamente; con un carácter más se omite el host completo.
TEST_F(SecurityEventProducerTest, PreservesReasonAtDetailsSizeLimit) {
    const SecurityEventProducer producer(generate, record);
    const std::string prefix = R"({"schemaVersion":1,"reason":"DOMAIN_DENIED","policyId":7,"host":")";
    const std::string suffix = R"("})";
    std::string host(2000 - prefix.size() - suffix.size(), 'a');
    ASSERT_TRUE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, host, 7));
    ASSERT_EQ(1u, deliveries.size());
    EXPECT_EQ(prefix + host + suffix, deliveries[0].details);

    host += 'a';
    ASSERT_TRUE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, host, 7));
    ASSERT_EQ(2u, deliveries.size());
    EXPECT_EQ(R"({"schemaVersion":1,"reason":"DOMAIN_DENIED","policyId":7})", deliveries[1].details);
}

// Un UUID vacío, con un separador incorrecto o con un carácter no hexadecimal nunca alcanza al receptor.
TEST_F(SecurityEventProducerTest, RejectsMalformedUuidsBeforeDelivery) {
    const SecurityEventProducer producer(generate, record);
    for (const char* invalid : {"", "10ea1c30_82a3-4590-9fec-66d778ea21cf", "10ea1c30-82a3-4590-9fec-66d778ea21cg"}) {
        SCOPED_TRACE(invalid);
        nextUuid = invalid;
        EXPECT_FALSE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, "blocked.example", 7));
    }
    EXPECT_TRUE(deliveries.empty());
}

// Si el generador lanza una excepción, el productor comunica el fallo antes de entregar ningún evento.
TEST_F(SecurityEventProducerTest, ReportsGeneratorExceptionWithoutDelivery) {
    // Simula un fallo al obtener el UUID del sistema.
    const auto failingGenerator = []() -> std::string { throw std::runtime_error("UUID generation failed"); };
    const SecurityEventProducer producer(failingGenerator, record);
    EXPECT_FALSE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, "blocked.example", 7));
    EXPECT_TRUE(deliveries.empty());
}

// Tanto el rechazo explícito como la excepción del receptor devuelven false tras un único intento de entrega.
TEST_F(SecurityEventProducerTest, ReportsSinkRejectionAndExceptionWithoutRetry) {
    int attempts = 0;
    // La primera entrega se rechaza y la segunda llamada del test simula un error en el receptor.
    const auto failingSink = [&attempts](const SecurityContext&, const SecurityEvent&) {
        ++attempts;
        if (attempts == 1) {
            return false;
        }
        throw std::runtime_error("Event delivery failed");
    };
    const SecurityEventProducer producer(generate, failingSink);
    EXPECT_FALSE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, "blocked.example", 7));
    ASSERT_EQ(1, attempts);
    EXPECT_FALSE(producer.ProduceUrlBlocked(validOrigin, {}, SecurityReason::DomainDenied, "blocked.example", 7));
    EXPECT_EQ(2, attempts);
    EXPECT_EQ(2, uuidRequests);
}

}  // namespace sbp

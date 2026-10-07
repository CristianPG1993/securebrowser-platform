-- Conserva las instalaciones y sus referencias de origen, incluso al desactivarlas.
CREATE TABLE devices (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    device_identifier VARCHAR(36) NOT NULL,
    name VARCHAR(150),
    company_id BIGINT NOT NULL,
    user_id BIGINT NOT NULL,
    license_id BIGINT NOT NULL,
    policy_id BIGINT NOT NULL,
    enrollment_token_id BIGINT NOT NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL,
    last_seen_at TIMESTAMP WITH TIME ZONE,

    CONSTRAINT uq_devices_device_identifier
        UNIQUE (device_identifier),

    CONSTRAINT uq_devices_enrollment_token
        UNIQUE (enrollment_token_id),

    CONSTRAINT ck_devices_device_identifier
        CHECK (device_identifier COLLATE "C" ~
            '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'),

    CONSTRAINT ck_devices_name_not_blank
        CHECK (name IS NULL OR name ~ '[^[:space:]]'),

    CONSTRAINT fk_devices_company
        FOREIGN KEY (company_id) REFERENCES companies (id) ON DELETE RESTRICT,

    CONSTRAINT fk_devices_user
        FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE RESTRICT,

    CONSTRAINT fk_devices_license
        FOREIGN KEY (license_id) REFERENCES licenses (id) ON DELETE RESTRICT,

    CONSTRAINT fk_devices_policy
        FOREIGN KEY (policy_id) REFERENCES policies (id) ON DELETE RESTRICT,

    CONSTRAINT fk_devices_enrollment_token
        FOREIGN KEY (enrollment_token_id) REFERENCES enrollment_tokens (id) ON DELETE RESTRICT
);

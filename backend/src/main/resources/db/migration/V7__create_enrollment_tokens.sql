-- Conserva hashes de enrollment, sus referencias de origen y su ciclo de vida.
CREATE TABLE enrollment_tokens (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    license_id BIGINT NOT NULL,
    user_id BIGINT NOT NULL,
    policy_id BIGINT NOT NULL,
    token_hash VARCHAR(64) NOT NULL,
    expires_at TIMESTAMP WITH TIME ZONE NOT NULL,
    used_at TIMESTAMP WITH TIME ZONE,
    revoked_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,

    CONSTRAINT uq_enrollment_tokens_token_hash
        UNIQUE (token_hash),

    CONSTRAINT ck_enrollment_tokens_hash_format
        CHECK (token_hash COLLATE "C" ~ '^[0-9a-f]{64}$'),

    CONSTRAINT ck_enrollment_tokens_expiry
        CHECK (expires_at > created_at),

    CONSTRAINT ck_enrollment_tokens_usage
        CHECK (used_at IS NULL OR (used_at >= created_at AND used_at < expires_at)),

    CONSTRAINT ck_enrollment_tokens_revocation
        CHECK (revoked_at IS NULL OR revoked_at >= created_at),

    CONSTRAINT ck_enrollment_tokens_lifecycle
        CHECK (used_at IS NULL OR revoked_at IS NULL),

    -- Las referencias históricas impiden borrar la licencia, usuario o política.
    CONSTRAINT fk_enrollment_tokens_license
        FOREIGN KEY (license_id) REFERENCES licenses (id) ON DELETE RESTRICT,

    CONSTRAINT fk_enrollment_tokens_user
        FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE RESTRICT,

    CONSTRAINT fk_enrollment_tokens_policy
        FOREIGN KEY (policy_id) REFERENCES policies (id) ON DELETE RESTRICT
);

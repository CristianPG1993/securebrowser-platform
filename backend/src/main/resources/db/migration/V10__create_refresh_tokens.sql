-- Agrupa la renovación de un usuario y admite revocar registros ya consumidos.
CREATE TABLE refresh_tokens (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    user_id BIGINT NOT NULL,
    token_hash VARCHAR(64) NOT NULL,
    family_id UUID NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,
    expires_at TIMESTAMP WITH TIME ZONE NOT NULL,
    used_at TIMESTAMP WITH TIME ZONE,
    revoked_at TIMESTAMP WITH TIME ZONE,

    CONSTRAINT uq_refresh_tokens_token_hash
        UNIQUE (token_hash),

    CONSTRAINT ck_refresh_tokens_token_hash
        CHECK (token_hash COLLATE "C" ~ '^[0-9a-f]{64}$'),

    CONSTRAINT ck_refresh_tokens_expiry
        CHECK (expires_at > created_at),

    CONSTRAINT ck_refresh_tokens_usage
        CHECK (used_at IS NULL OR (used_at >= created_at AND used_at < expires_at)),

    CONSTRAINT ck_refresh_tokens_revocation
        CHECK (revoked_at IS NULL OR revoked_at >= created_at),

    CONSTRAINT fk_refresh_tokens_user
        FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
);

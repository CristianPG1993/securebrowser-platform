-- Crea los dominios de cada política y evita duplicados dentro de ella.
CREATE TABLE url_rules (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    policy_id BIGINT NOT NULL,
    domain VARCHAR(253) NOT NULL,

    CONSTRAINT uq_url_rules_policy_domain
        UNIQUE (policy_id, domain),

    -- La intercalación C limita los rangos del patrón a caracteres ASCII.
    CONSTRAINT ck_url_rules_domain
        CHECK (domain COLLATE "C" ~
            '^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?([.][a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?)*$'),

    CONSTRAINT fk_url_rules_policy
        FOREIGN KEY (policy_id)
        REFERENCES policies (id)
        ON DELETE CASCADE
);

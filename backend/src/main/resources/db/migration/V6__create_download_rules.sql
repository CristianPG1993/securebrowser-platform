-- Crea las extensiones de cada política y evita duplicados dentro de ella.
CREATE TABLE download_rules (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    policy_id BIGINT NOT NULL,
    extension VARCHAR(20) NOT NULL,

    CONSTRAINT uq_download_rules_policy_extension
        UNIQUE (policy_id, extension),

    -- Rechaza espacios Unicode, controles, puntos y separadores de rutas.
    -- Los escapes Unicode hacen explícitos los espacios, incluso con locale C.
    CONSTRAINT ck_download_rules_extension
        CHECK (extension <> ''
            AND extension = lower(extension)
            AND extension COLLATE "C" !~
                U&'[\0001-\0020\007F-\009F\00A0\1680\2000-\200A\2028\2029\202F\205F\3000./\\\\:]'),

    CONSTRAINT fk_download_rules_policy
        FOREIGN KEY (policy_id)
        REFERENCES policies (id)
        ON DELETE CASCADE
);

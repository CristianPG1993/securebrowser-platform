-- Crea la configuración base de políticas y sus modos permitidos.
CREATE TABLE policies (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name VARCHAR(150) NOT NULL,
    company_id BIGINT NOT NULL,
    url_filtering_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    url_filtering_mode VARCHAR(9) NOT NULL DEFAULT 'DENYLIST',
    download_control_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    download_control_mode VARCHAR(9) NOT NULL DEFAULT 'DENYLIST',
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL,

    CONSTRAINT ck_policies_name_not_blank
        CHECK (name ~ '[^[:space:]]'),

    CONSTRAINT ck_policies_url_filtering_mode
        CHECK (url_filtering_mode IN ('DENYLIST', 'ALLOWLIST')),

    CONSTRAINT ck_policies_download_control_mode
        CHECK (download_control_mode IN ('DENYLIST', 'ALLOWLIST')),

    CONSTRAINT fk_policies_company
        FOREIGN KEY (company_id)
        REFERENCES companies (id)
        ON DELETE RESTRICT
);

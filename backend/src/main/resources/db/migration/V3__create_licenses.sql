-- Crea la tabla de licencias con su compañía y capacidad mínima.
CREATE TABLE licenses (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    company_id BIGINT NOT NULL,
    max_installations INTEGER NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL,

    CONSTRAINT ck_licenses_max_installations
        CHECK (max_installations >= 1),

    CONSTRAINT fk_licenses_company
        FOREIGN KEY (company_id)
        REFERENCES companies (id)
        ON DELETE RESTRICT
);

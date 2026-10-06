-- Crea la tabla de usuarios con sus relaciones y restricciones.
CREATE TABLE users (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name VARCHAR(150) NOT NULL,
    last_name VARCHAR(150) NOT NULL,
    email VARCHAR(254) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    role VARCHAR(5) NOT NULL,
    company_id BIGINT NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL,

    CONSTRAINT ck_users_name_not_blank
        CHECK (name ~ '[^[:space:]]'),

    CONSTRAINT ck_users_last_name_not_blank
        CHECK (last_name ~ '[^[:space:]]'),

    CONSTRAINT ck_users_email_normalized
        CHECK (
            email <> ''
            AND email = lower(email)
            AND email !~ '[[:space:]]'
        ),

    CONSTRAINT ck_users_password_hash_bcrypt
        CHECK (
            password_hash ~
            '^\$2[aby]\$(0[4-9]|[12][0-9]|3[01])\$[./A-Za-z0-9]{53}$'
        ),

    CONSTRAINT ck_users_role
        CHECK (role IN ('ADMIN', 'USER')),

    CONSTRAINT uq_users_email
        UNIQUE (email),

    CONSTRAINT fk_users_company
        FOREIGN KEY (company_id)
        REFERENCES companies (id)
        ON DELETE RESTRICT
);
-- Conserva los eventos y su instalación de origen, sin borrar historial en cascada.
CREATE TABLE security_events (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_uuid UUID NOT NULL,
    device_id BIGINT NOT NULL,
    type VARCHAR(16) NOT NULL,
    details VARCHAR(2000),
    occurred_at TIMESTAMP WITH TIME ZONE NOT NULL,
    received_at TIMESTAMP WITH TIME ZONE NOT NULL,

    CONSTRAINT uq_security_events_event_uuid
        UNIQUE (event_uuid),

    CONSTRAINT ck_security_events_type
        CHECK (type IN ('URL_BLOCKED', 'DOWNLOAD_BLOCKED', 'POLICY_UPDATED')),

    CONSTRAINT fk_security_events_device
        FOREIGN KEY (device_id) REFERENCES devices (id) ON DELETE RESTRICT
);

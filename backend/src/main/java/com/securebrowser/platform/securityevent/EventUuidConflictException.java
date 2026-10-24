package com.securebrowser.platform.securityevent;

/** No revela el contenido ni el propietario del evento que ya utiliza el UUID. */
public class EventUuidConflictException extends RuntimeException {
    public EventUuidConflictException() { super("Event UUID conflict"); }
}

package com.securebrowser.platform.securityevent;

/** Señala una entrada inválida sin conservar detalles ni valores del cliente. */
public class InvalidSecurityEventRequestException extends RuntimeException {
    public InvalidSecurityEventRequestException() { super("Invalid security event request"); }
}

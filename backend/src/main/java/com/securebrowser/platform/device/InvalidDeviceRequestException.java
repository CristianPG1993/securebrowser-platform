package com.securebrowser.platform.device;

/** Identifica errores de entrada del caso de uso sin reflejar valores recibidos. */
public class InvalidDeviceRequestException extends RuntimeException {
    public InvalidDeviceRequestException() { super("Invalid device request"); }
}

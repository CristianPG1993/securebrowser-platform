package com.securebrowser.platform.device;

/** Solo se evalúa después de comprobar Company y propietario. */
public class DeviceInactiveException extends RuntimeException {
    public DeviceInactiveException() { super("Device is inactive"); }
}

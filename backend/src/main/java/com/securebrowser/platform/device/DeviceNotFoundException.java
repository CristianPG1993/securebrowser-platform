package com.securebrowser.platform.device;

/** Conserva el mismo error para referencias inexistentes y fuera del ámbito permitido. */
public class DeviceNotFoundException extends RuntimeException {
    public DeviceNotFoundException() { super("Device not found"); }
}

package com.securebrowser.platform.policy;

/** Identifica exclusivamente entradas no válidas del caso de uso de Policy. */
public class InvalidPolicyRequestException extends RuntimeException {
    public InvalidPolicyRequestException() {
        super("Invalid policy request");
    }
}

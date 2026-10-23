package com.securebrowser.platform.policy;

/** No distingue entre un id inexistente y uno fuera de la compañía autorizada. */
public class PolicyNotFoundException extends RuntimeException {
    public PolicyNotFoundException() {
        super("Policy not found");
    }
}

package com.securebrowser.platform.company;

/** Indica que no se encontró la compañía dentro del ámbito autorizado. */
public class CompanyNotFoundException extends RuntimeException {

    // Identifica la versión de esta excepción para la serialización de Java.
    private static final long serialVersionUID = 1L;

    /** Crea el error sin incluir datos de otras compañías. */
    public CompanyNotFoundException() {
        super("Company not found");
    }
}
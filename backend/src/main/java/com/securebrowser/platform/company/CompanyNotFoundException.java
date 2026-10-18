package com.securebrowser.platform.company;

import org.springframework.http.HttpStatus;
import org.springframework.web.bind.annotation.ResponseStatus;

/** Indica que no se encontró la compañía autorizada y corresponde a HTTP 404. */
@ResponseStatus(HttpStatus.NOT_FOUND)
public class CompanyNotFoundException extends RuntimeException {

    // Identifica la versión de esta excepción para la serialización de Java.
    private static final long serialVersionUID = 1L;

    /** Crea el error sin incluir datos de otras compañías. */
    public CompanyNotFoundException() {
        super("Company not found");
    }
}

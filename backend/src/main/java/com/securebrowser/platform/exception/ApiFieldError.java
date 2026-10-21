package com.securebrowser.platform.exception;

/** Describe un error de validación sin incluir el valor rechazado. */
public record ApiFieldError(String field, String message) {
}

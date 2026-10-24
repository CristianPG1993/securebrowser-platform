package com.securebrowser.platform.exception;

import org.springframework.http.HttpStatus;

/** Reúne los códigos del contrato necesarios para los errores comunes de la API. */
public enum ApiErrorCode {

    VALIDATION_ERROR(HttpStatus.BAD_REQUEST, "La petición contiene datos no válidos."),
    AUTHENTICATION_REQUIRED(HttpStatus.UNAUTHORIZED, "Se requiere una autenticación válida."),
    INVALID_CREDENTIALS(HttpStatus.UNAUTHORIZED, "Las credenciales no son válidas."),
    INVALID_REFRESH_TOKEN(HttpStatus.UNAUTHORIZED, "El token de renovación no es válido."),
    ACCESS_DENIED(HttpStatus.FORBIDDEN, "No tienes permiso para realizar esta operación."),
    RESOURCE_NOT_FOUND(HttpStatus.NOT_FOUND, "No se ha encontrado el recurso solicitado."),
    DEVICE_INACTIVE(HttpStatus.CONFLICT, "El dispositivo está inactivo."),
    EVENT_UUID_CONFLICT(HttpStatus.CONFLICT, "El identificador del evento ya se utiliza con otros datos."),
    METHOD_NOT_ALLOWED(HttpStatus.METHOD_NOT_ALLOWED, "El método HTTP no está permitido para esta ruta."),
    NOT_ACCEPTABLE(HttpStatus.NOT_ACCEPTABLE, "El formato de respuesta solicitado no está disponible."),
    UNSUPPORTED_MEDIA_TYPE(HttpStatus.UNSUPPORTED_MEDIA_TYPE, "El tipo de contenido enviado no está admitido."),
    SERVICE_UNAVAILABLE(HttpStatus.SERVICE_UNAVAILABLE, "El servicio no está disponible temporalmente."),
    INTERNAL_ERROR(HttpStatus.INTERNAL_SERVER_ERROR, "Se ha producido un error interno.");

    // Estado HTTP asociado al código estable que recibirán los clientes.
    private final HttpStatus status;

    // Explicación pública que no incorpora mensajes internos ni valores de entrada.
    private final String detail;

    /** Asocia cada código con su estado HTTP y una explicación genérica. */
    ApiErrorCode(HttpStatus status, String detail) {
        this.status = status;
        this.detail = detail;
    }

    /** Devuelve el estado HTTP correspondiente al error. */
    public HttpStatus getStatus() {
        return status;
    }

    /** Devuelve la explicación que puede mostrarse en la respuesta pública. */
    public String getDetail() {
        return detail;
    }

    /** Traduce los estados del procesamiento interno de errores al catálogo público. */
    public static ApiErrorCode fromStatus(int status) {
        return switch (status) {
            case 400 -> VALIDATION_ERROR;
            case 401 -> AUTHENTICATION_REQUIRED;
            case 403 -> ACCESS_DENIED;
            case 404 -> RESOURCE_NOT_FOUND;
            case 405 -> METHOD_NOT_ALLOWED;
            case 406 -> NOT_ACCEPTABLE;
            case 415 -> UNSUPPORTED_MEDIA_TYPE;
            case 503 -> SERVICE_UNAVAILABLE;
            default -> INTERNAL_ERROR;
        };
    }
}

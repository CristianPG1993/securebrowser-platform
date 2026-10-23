package com.securebrowser.platform.exception;

import java.util.List;

import com.securebrowser.platform.company.CompanyNotFoundException;
import com.securebrowser.platform.auth.InvalidCredentialsException;
import com.securebrowser.platform.auth.InvalidRefreshTokenException;
import com.securebrowser.platform.policy.PolicyNotFoundException;
import com.securebrowser.platform.policy.InvalidPolicyRequestException;
import com.securebrowser.platform.device.DeviceNotFoundException;
import com.securebrowser.platform.device.DeviceInactiveException;
import com.securebrowser.platform.device.InvalidDeviceRequestException;

import jakarta.servlet.http.HttpServletRequest;

import org.springframework.context.annotation.Import;
import org.springframework.beans.TypeMismatchException;
import org.springframework.dao.DataAccessResourceFailureException;
import org.springframework.dao.TransientDataAccessException;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatusCode;
import org.springframework.http.MediaType;
import org.springframework.http.ProblemDetail;
import org.springframework.http.ResponseEntity;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.http.converter.HttpMessageNotWritableException;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.security.core.AuthenticationException;
import org.springframework.transaction.CannotCreateTransactionException;
import org.springframework.validation.FieldError;
import org.springframework.web.bind.MethodArgumentNotValidException;
import org.springframework.web.bind.MissingServletRequestParameterException;
import org.springframework.web.HttpRequestMethodNotSupportedException;
import org.springframework.web.HttpMediaTypeNotAcceptableException;
import org.springframework.web.HttpMediaTypeNotSupportedException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.springframework.web.context.request.ServletWebRequest;
import org.springframework.web.context.request.WebRequest;
import org.springframework.web.servlet.mvc.method.annotation.ResponseEntityExceptionHandler;
import org.springframework.web.servlet.NoHandlerFoundException;
import org.springframework.web.servlet.resource.NoResourceFoundException;
import org.springframework.web.method.annotation.HandlerMethodValidationException;

/** Traduce las excepciones de los controllers al formato público común. */
@RestControllerAdvice
@Import(ApiProblemDetailFactory.class)
public class ApiExceptionHandler extends ResponseEntityExceptionHandler {

    // Comparte la construcción del cuerpo con los mecanismos de seguridad.
    private final ApiProblemDetailFactory problemFactory;

    /** Recibe la fábrica por constructor, también en los contextos HTTP de prueba. */
    public ApiExceptionHandler(ApiProblemDetailFactory problemFactory) {
        this.problemFactory = problemFactory;
    }

    /** Convierte la ausencia de la compañía en un 404 genérico. */
    @ExceptionHandler(CompanyNotFoundException.class)
    public ResponseEntity<ProblemDetail> handleCompanyNotFound(HttpServletRequest request) {
        ProblemDetail problem = problemFactory.create(ApiErrorCode.RESOURCE_NOT_FOUND, request);
        return ResponseEntity.status(problem.getStatus())
                .contentType(MediaType.APPLICATION_PROBLEM_JSON)
                .body(problem);
    }

    @ExceptionHandler(PolicyNotFoundException.class)
    public ResponseEntity<ProblemDetail> handlePolicyNotFound(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.RESOURCE_NOT_FOUND, request);
    }

    @ExceptionHandler(InvalidPolicyRequestException.class)
    public ResponseEntity<ProblemDetail> handleInvalidPolicyRequest(HttpServletRequest request) {
        ProblemDetail problem = problemFactory.create(ApiErrorCode.VALIDATION_ERROR, request);
        problem.setProperty("errors", List.of(new ApiFieldError("request", "Los datos de la política no son válidos.")));
        return ResponseEntity.status(problem.getStatus()).contentType(MediaType.APPLICATION_PROBLEM_JSON).body(problem);
    }

    @ExceptionHandler(DeviceNotFoundException.class)
    public ResponseEntity<ProblemDetail> handleDeviceNotFound(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.RESOURCE_NOT_FOUND, request);
    }

    @ExceptionHandler(DeviceInactiveException.class)
    public ResponseEntity<ProblemDetail> handleDeviceInactive(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.DEVICE_INACTIVE, request);
    }

    @ExceptionHandler(InvalidDeviceRequestException.class)
    public ResponseEntity<ProblemDetail> handleInvalidDeviceRequest(HttpServletRequest request) {
        ProblemDetail problem = problemFactory.create(ApiErrorCode.VALIDATION_ERROR, request);
        problem.setProperty("errors", List.of(new ApiFieldError("request", "Los datos del dispositivo no son válidos.")));
        return ResponseEntity.status(problem.getStatus()).contentType(MediaType.APPLICATION_PROBLEM_JSON).body(problem);
    }

    /** Añade los campos inválidos al error 400 generado por la validación del DTO. */
    @Override
    protected ResponseEntity<Object> handleMethodArgumentNotValid(
            MethodArgumentNotValidException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        HttpServletRequest servletRequest = ((ServletWebRequest) request).getRequest();
        ProblemDetail problem = problemFactory.create(ApiErrorCode.VALIDATION_ERROR, servletRequest);

        List<ApiFieldError> errors = exception.getBindingResult().getAllErrors().stream()
                .map(error -> error instanceof FieldError fieldError ? toFieldError(fieldError)
                        : new ApiFieldError("body", "El cuerpo no cumple las restricciones de validación."))
                .distinct()
                .toList();
        problem.setProperty("errors", errors);

        return ResponseEntity.status(status)
                .headers(headers)
                .contentType(MediaType.APPLICATION_PROBLEM_JSON)
                .body(problem);
    }

    /** Rechaza cuerpos que no se pueden leer como el DTO, sin exponer detalles de Jackson. */
    @Override
    protected ResponseEntity<Object> handleHttpMessageNotReadable(
            HttpMessageNotReadableException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        HttpServletRequest servletRequest = ((ServletWebRequest) request).getRequest();
        ProblemDetail problem = problemFactory.create(ApiErrorCode.VALIDATION_ERROR, servletRequest);
        problem.setProperty("errors", List.of(new ApiFieldError(
                "body", "El cuerpo debe contener un JSON válido con los campos y tipos admitidos.")));

        return ResponseEntity.status(status)
                .headers(headers)
                .contentType(MediaType.APPLICATION_PROBLEM_JSON)
                .body(problem);
    }

    /** Conserva 405 y las cabeceras de Spring, incluido Allow. */
    @Override
    protected ResponseEntity<Object> handleHttpRequestMethodNotSupported(
            HttpRequestMethodNotSupportedException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.METHOD_NOT_ALLOWED, headers, request);
    }

    /** Explica el formato de salida no disponible sin publicar detalles internos. */
    @Override
    protected ResponseEntity<Object> handleHttpMediaTypeNotAcceptable(
            HttpMediaTypeNotAcceptableException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.NOT_ACCEPTABLE, headers, request);
    }

    /** Conserva el rechazo 415 y las cabeceras de tipos soportados. */
    @Override
    protected ResponseEntity<Object> handleHttpMediaTypeNotSupported(
            HttpMediaTypeNotSupportedException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.UNSUPPORTED_MEDIA_TYPE, headers, request);
    }

    /** Un parámetro ausente es un error de entrada, sin copiar su valor. */
    @Override
    protected ResponseEntity<Object> handleMissingServletRequestParameter(
            MissingServletRequestParameterException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.VALIDATION_ERROR, headers, request);
    }

    /** Rechaza conversiones de parámetros sin incluir el texto recibido. */
    @Override
    protected ResponseEntity<Object> handleTypeMismatch(
            TypeMismatchException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.VALIDATION_ERROR, headers, request);
    }

    /** La validación de un retorno incorrecto conserva 500, no se atribuye al cliente. */
    @Override
    protected ResponseEntity<Object> handleHandlerMethodValidationException(
            HandlerMethodValidationException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(status.is5xxServerError()
                ? ApiErrorCode.INTERNAL_ERROR : ApiErrorCode.VALIDATION_ERROR, headers, request);
    }

    /** Un recurso o una ruta que MVC no encuentra producen el mismo 404 público. */
    @Override
    protected ResponseEntity<Object> handleNoResourceFoundException(
            NoResourceFoundException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.RESOURCE_NOT_FOUND, headers, request);
    }

    @Override
    protected ResponseEntity<Object> handleNoHandlerFoundException(
            NoHandlerFoundException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.RESOURCE_NOT_FOUND, headers, request);
    }

    /** Un fallo al construir la salida es un error interno, no una entrada inválida. */
    @Override
    protected ResponseEntity<Object> handleHttpMessageNotWritable(
            HttpMessageNotWritableException exception, HttpHeaders headers,
            HttpStatusCode status, WebRequest request) {
        return frameworkProblem(ApiErrorCode.INTERNAL_ERROR, headers, request);
    }

    /** También conserva 403 cuando el servicio o la seguridad de métodos deniegan acceso. */
    @ExceptionHandler(AccessDeniedException.class)
    public ResponseEntity<ProblemDetail> handleAccessDenied(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.ACCESS_DENIED, request);
    }

    @ExceptionHandler(InvalidCredentialsException.class)
    public ResponseEntity<ProblemDetail> handleInvalidCredentials(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.INVALID_CREDENTIALS, request);
    }

    @ExceptionHandler(InvalidRefreshTokenException.class)
    public ResponseEntity<ProblemDetail> handleInvalidRefreshToken(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.INVALID_REFRESH_TOKEN, request);
    }

    @ExceptionHandler(AuthenticationException.class)
    public ResponseEntity<ProblemDetail> handleAuthentication(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.AUTHENTICATION_REQUIRED, request);
    }

    /** Solo las excepciones pertinentes de disponibilidad se clasifican como 503. */
    @ExceptionHandler({TransientDataAccessException.class, DataAccessResourceFailureException.class,
            CannotCreateTransactionException.class})
    public ResponseEntity<ProblemDetail> handleTemporaryUnavailability(HttpServletRequest request) {
        return publicProblem(ApiErrorCode.SERVICE_UNAVAILABLE, request);
    }

    /** Los fallos de programación y de integridad imprevistos conservan 500 y detalle genérico. */
    @ExceptionHandler(Exception.class)
    public ResponseEntity<ProblemDetail> handleUnexpectedException(HttpServletRequest request) {
        logger.error("Se ha producido un error interno al procesar una petición de la API.");
        return publicProblem(ApiErrorCode.INTERNAL_ERROR, request);
    }

    /** Construye respuestas de MVC manteniendo las cabeceras de protocolo recibidas. */
    private ResponseEntity<Object> frameworkProblem(ApiErrorCode code, HttpHeaders headers, WebRequest request) {
        HttpServletRequest servletRequest = ((ServletWebRequest) request).getRequest();
        ProblemDetail problem = problemFactory.create(code, servletRequest);
        if (code == ApiErrorCode.VALIDATION_ERROR) {
            problem.setProperty("errors", List.of(new ApiFieldError("request", "Los parámetros no son válidos.")));
        }
        return ResponseEntity.status(code.getStatus()).headers(headers)
                .contentType(MediaType.APPLICATION_PROBLEM_JSON).body(problem);
    }

    private ResponseEntity<ProblemDetail> publicProblem(ApiErrorCode code, HttpServletRequest request) {
        HttpHeaders headers = new HttpHeaders();
        if (code.getStatus().value() == 401) {
            headers.set(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\"");
        }
        if (request.getRequestURI().startsWith("/api/v1/auth/")) {
            headers.set(HttpHeaders.CACHE_CONTROL, "no-store");
            headers.set(HttpHeaders.PRAGMA, "no-cache");
        }
        return ResponseEntity.status(code.getStatus()).headers(headers)
                .contentType(MediaType.APPLICATION_PROBLEM_JSON).body(problemFactory.create(code, request));
    }

    /** Utiliza mensajes controlados, sin copiar valores rechazados ni mensajes internos. */
    private ApiFieldError toFieldError(FieldError error) {
        String message = "El campo no cumple las restricciones de validación.";
        if ("NotBlank".equals(error.getCode())) {
            message = "El campo es obligatorio y no puede estar en blanco.";
        } else if ("CodePointLength".equals(error.getCode())) {
            message = "El campo supera la longitud permitida.";
        }
        return new ApiFieldError(error.getField(), message);
    }
}

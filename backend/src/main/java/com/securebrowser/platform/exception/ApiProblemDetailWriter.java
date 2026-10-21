package com.securebrowser.platform.exception;

import java.io.IOException;
import java.nio.charset.StandardCharsets;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import tools.jackson.databind.json.JsonMapper;

import org.springframework.context.annotation.Import;
import org.springframework.http.MediaType;
import org.springframework.http.ProblemDetail;
import org.springframework.stereotype.Component;

/** Escribe el formato común cuando el error se produce fuera del Controller. */
@Component
@Import(ApiProblemDetailFactory.class)
public final class ApiProblemDetailWriter {

    private final ApiProblemDetailFactory problemFactory;
    private final JsonMapper jsonMapper;

    /** Recibe la fábrica y el mapper configurado por Spring Boot. */
    public ApiProblemDetailWriter(ApiProblemDetailFactory problemFactory, JsonMapper jsonMapper) {
        this.problemFactory = problemFactory;
        this.jsonMapper = jsonMapper;
    }

    /** Prepara estado, tipo de contenido y cuerpo sin borrar cabeceras ya establecidas. */
    public void write(ApiErrorCode errorCode, HttpServletRequest request,
            HttpServletResponse response) throws IOException {
        ProblemDetail problem = problemFactory.create(errorCode, request);

        response.setStatus(errorCode.getStatus().value());
        response.setContentType(MediaType.APPLICATION_PROBLEM_JSON_VALUE);
        response.setCharacterEncoding(StandardCharsets.UTF_8.name());
        jsonMapper.writeValue(response.getOutputStream(), problem);
    }
}

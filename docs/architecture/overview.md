# Arquitectura general de SecureBrowser Platform

## Estado

Este documento describe la arquitectura prevista para el MVP del TFG.

## Diagrama general

![Arquitectura general de SBP](../../diagrams/architecture-overview.svg)

[Fuente editable del diagrama (Mermaid)](../../diagrams/architecture-overview.mmd).


## Desktop

Aplicación de navegación en C++ con Chromium Embedded Framework (CEF).

- Aplica las políticas de filtrado de URL y control de descargas.
- Registra eventos de seguridad.
- Utiliza SQLite para persistencia local y eventos pendientes de sincronización.
- Genera un identificador único por evento para evitar duplicados al sincronizar.

## Backend

API en Java con Spring Boot y PostgreSQL.

- Gestiona autenticación, compañías y usuarios.
- Gestiona licencias, tokens de enrollment y dispositivos.
- Gestiona políticas y eventos de seguridad.
- Limita la administración de cada ADMIN a su compañía.

## Android

Aplicación en Kotlin con Jetpack Compose y arquitectura MVVM.

- Permite administrar y monitorizar la plataforma.
- Incluye pantallas de dispositivos, eventos y políticas.
- Se comunica con la API del backend mediante Retrofit.

## Decisiones pendientes

La tecnología de interfaz desktop se decidirá tras un prototipo técnico
que permita comparar Qt 6 Widgets y CEF Views.

Las decisiones arquitectónicas relevantes se documentarán mediante ADR
en `docs/adr/`.
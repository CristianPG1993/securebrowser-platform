# Compila desde el código fuente con Java 25 y el Maven Wrapper del proyecto.
FROM eclipse-temurin:25-jdk-noble@sha256:ba0f0e1a33864105e1d454e88105c617ab40f71062db1f306bf58be32a3f8e5e AS build
WORKDIR /workspace
COPY .mvn/wrapper/maven-wrapper.properties .mvn/wrapper/maven-wrapper.properties
COPY mvnw pom.xml ./
COPY src/main/ src/main/
# Las pruebas con PostgreSQL y Docker se ejecutan mediante mvn verify fuera de esta construcción.
RUN sh ./mvnw --batch-mode --no-transfer-progress -DskipTests package

# La imagen de ejecución contiene el JRE y el JAR generado, sin herramientas de construcción.
FROM eclipse-temurin:25-jre-noble@sha256:3f2223fe04b750aa5a8179e29b5c9a1ddf7665fc9bb08bfe11ad01d143bc13a5
WORKDIR /app
RUN groupadd --system --gid 10001 sbp \
    && useradd --system --uid 10001 --gid sbp --no-create-home sbp
COPY --from=build --chown=10001:10001 /workspace/target/securebrowser-backend-0.0.1-SNAPSHOT.jar app.jar
USER 10001:10001
EXPOSE 8080
ENTRYPOINT ["java", "-jar", "/app/app.jar"]

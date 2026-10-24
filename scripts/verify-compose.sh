#!/usr/bin/env bash
# Comprueba la entrega con datos desechables en un proyecto Compose independiente.
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p backend/target/compose-check
run_dir="$(mktemp -d backend/target/compose-check/run-XXXXXX)"
project="sbp-delivery-check-$(node -e 'process.stdout.write(require("crypto").randomBytes(6).toString("hex"))')"
compose=(docker compose --project-name "$project" --file compose.yaml --env-file "$run_dir/empty.env")
started=false

cleanup() {
    result=$?
    trap - EXIT
    if [ "$started" = true ]; then
        "${compose[@]}" logs --no-color > "$run_dir/services.log" 2>&1 || true
        # El volumen pertenece únicamente al proyecto temporal creado por esta ejecución.
        if ! "${compose[@]}" down --volumes --remove-orphans > "$run_dir/cleanup.log" 2>&1; then
            result=1
            echo "No se pudieron retirar todos los recursos de $project; consulta $run_dir/cleanup.log."
        fi
    fi
    echo "Logs de la comprobación: $run_dir"
    exit "$result"
}
trap cleanup EXIT

# No se carga el .env del usuario ni se reutilizan sus contraseñas.
: > "$run_dir/empty.env"
export SBP_HTTP_PORT="${SBP_HTTP_PORT:-8080}"
export SBP_DB_USERNAME=sbp_app
export SBP_DB_PASSWORD=sbp_compose_disposable_password
export SBP_JWT_SECRET_BASE64="$(node -e 'process.stdout.write(require("crypto").randomBytes(32).toString("base64"))')"
export SBP_JWT_ISSUER=sbp-compose-check
export SBP_JWT_AUDIENCE=sbp-compose-api
export SBP_JWT_MAX_LIFETIME_SECONDS=900
export SBP_REFRESH_TOKEN_LIFETIME_SECONDS=604800
export SBP_DEMO_ADMIN_PASSWORD='Admin compose verification 2026!'
export SBP_DEMO_USER_PASSWORD='User compose verification 2026!'

"${compose[@]}" config --format json > "$run_dir/config.json"
node - "$run_dir/config.json" <<'JS'
const fs = require('fs');
const config = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const ports = config.services['sbp-backend'].ports;
if (ports.length !== 1 || ports[0].host_ip !== '127.0.0.1' || ports[0].target !== 8080
    || String(ports[0].published) !== process.env.SBP_HTTP_PORT) throw Error('Publicación de API incorrecta');
if (config.services['sbp-postgres'].ports?.length) throw Error('PostgreSQL publica un puerto al host');
JS

node - "$run_dir/postman-environment.json" <<'JS'
const fs = require('fs');
fs.writeFileSync(process.argv[2], JSON.stringify({name: 'sbp-compose-temporal', values: [
    {key: 'baseUrl', value: `http://127.0.0.1:${process.env.SBP_HTTP_PORT}/api/v1`, enabled: true},
    {key: 'demoAdminPassword', value: process.env.SBP_DEMO_ADMIN_PASSWORD, enabled: true},
    {key: 'demoUserPassword', value: process.env.SBP_DEMO_USER_PASSWORD, enabled: true}
]}));
JS

wait_demo() {
    for attempt in {1..60}; do
        accounts="$("${compose[@]}" exec -T sbp-postgres psql -U sbp_app -d securebrowser_db -Atqc \
            "select count(*) from users where email in ('admin@demo.local','user@demo.local')" 2>/dev/null || true)"
        if [ "${accounts//$'\r'/}" = 2 ]; then return; fi
        sleep 1
    done
    echo 'La fixture demo no se confirmó a tiempo.' >&2
    return 1
}

snapshot() {
    "${compose[@]}" exec -T sbp-postgres psql -U sbp_app -d securebrowser_db -At -v ON_ERROR_STOP=1 <<'SQL'
SELECT jsonb_build_object(
    'companies', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM companies t),
    'users', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM users t),
    'licenses', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM licenses t),
    'policies', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM policies t),
    'urlRules', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM url_rules t),
    'downloadRules', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM download_rules t),
    'enrollments', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM enrollment_tokens t),
    'devices', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM devices t),
    'events', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM security_events t),
    'refreshTokens', (SELECT jsonb_agg(to_jsonb(t) ORDER BY id) FROM refresh_tokens t)
);
SQL
}

postman() {
    if ! npx --yes newman@6.2.2 run docs/postman/auth-demo.postman_collection.json \
            --environment "$run_dir/postman-environment.json" --reporters cli \
            --reporter-cli-no-console --color off > "$run_dir/postman-$1.log" 2>&1; then
        tail -n 45 "$run_dir/postman-$1.log"
        return 1
    fi
    tail -n 19 "$run_dir/postman-$1.log"
}

echo "Arranque limpio en http://127.0.0.1:$SBP_HTTP_PORT ($project)."
started=true
"${compose[@]}" up --build --detach --wait --wait-timeout 180 > "$run_dir/start.log" 2>&1
wait_demo
postman first
snapshot > "$run_dir/before-recreate.json"
"${compose[@]}" logs --no-color > "$run_dir/first-start.log" 2>&1

echo 'Recreación de los contenedores conservando el volumen de PostgreSQL.'
"${compose[@]}" down > "$run_dir/down.log" 2>&1
"${compose[@]}" up --no-build --detach --wait --wait-timeout 180 > "$run_dir/recreate.log" 2>&1
wait_demo
snapshot > "$run_dir/after-recreate.json"
if ! cmp -s "$run_dir/before-recreate.json" "$run_dir/after-recreate.json"; then
    echo 'Los datos han cambiado durante la recreación de los contenedores.' >&2
    exit 1
fi
echo 'Credenciales, políticas, referencias, auditoría y eventos conservados íntegramente.'
postman second
echo 'Entrega comprobada: ambos recorridos Postman correctos y datos persistentes.'

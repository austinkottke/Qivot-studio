#!/bin/bash
# Load the sample scripts into the SQL Server container (see docker-compose.yml).
set -euo pipefail
sqlcmd() { /opt/mssql-tools18/bin/sqlcmd -C -S sqlserver -U sa -P "$MSSQL_SA_PASSWORD" "$@"; }

for f in /sql/*.sql.gz; do
    db=$(basename "$f" .sql.gz)
    db=${db#*-}                      # 02-university -> university
    there=$(sqlcmd -h -1 -W -Q "SET NOCOUNT ON; SELECT COUNT(*) FROM sys.databases WHERE name = '$db'")
    if [ "$there" = "1" ]; then
        echo "$db: already loaded"
        continue
    fi
    echo "$db: loading"
    gunzip -c "$f" > /tmp/load.sql
    if ! sqlcmd -b -i /tmp/load.sql > /tmp/load.log; then
        tail -20 /tmp/load.log
        sqlcmd -Q "IF DB_ID('$db') IS NOT NULL DROP DATABASE $db" || true   # so the next run tries again
        exit 1
    fi
done
echo "SQL Server samples ready"

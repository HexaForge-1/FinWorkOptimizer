# FinWorkOptimizer

**FinWorkOptimizer** is a modular, REST-enabled C++17+ platform designed to integrate **Finance**, **Workday**, **CRM**, and **Oracle Fusion-style** workflows. It helps businesses automate operations like payroll, expenses, invoicing, CRM data management, budget tracking, and General Ledger exports.

---

## Features Overview

### Core Finance / Workday
- **Employee Management** – GET/POST `/employees`
- **Timesheets** – GET/POST `/timesheets`
- **Expenses** – POST `/expenses`
- **Payroll Compute** – GET `/payroll?id=E1`
- **Invoicing & AR/AP** – CSV export endpoints

### CRM
- **Customer Management** – GET/POST `/crm/customers`  
  *(New in v9)* – Add or update `{id, name, segment}`

### Timesheet Approval
- **Approve Work Entries** – POST `/timesheets/approve`  
  *(New in v9)* – Approve an entry by `{emp, project, date}`

### Budget & Forecasting
- **Budget Management** – POST `/budget` *(New in v9)*
- **Budget Variance Report** – GET `/budget/variance` *(New in v9)*
- **Cashflow Forecast** – GET `/forecast/cashflow?window=N` *(New in v9)*

### Oracle Fusion–style GL Export
- **GL Export** – POST `/gl/export` → Generates a CSV output for Oracle Fusion import *(New in v9)*

### Security
- API key authentication via `X-API-Key` header
- Configurable via `FWO_API_KEY` environment variable

---

## API Quick Test (via curl)

All requests require:  
`-H "X-API-Key: changeme"`

```bash
# Customers
curl -H "X-API-Key: changeme" http://localhost:8080/crm/customers
curl -X POST -H "X-API-Key: changeme" -H "Content-Type: application/json"   -d '{"id":"C1","name":"Acme","segment":"Enterprise"}'   http://localhost:8080/crm/customers

# Approve timesheet
curl -X POST -H "X-API-Key: changeme" -H "Content-Type: application/json"   -d '{"emp":"E1","project":"Alpha","date":"2025-08-13"}'   http://localhost:8080/timesheets/approve

# Budget & variance
curl -X POST -H "X-API-Key: changeme" -H "Content-Type: application/json"   -d '{"category":"Travel","amount":1000}' http://localhost:8080/budget
curl -H "X-API-Key: changeme" http://localhost:8080/budget/variance

# Forecast
curl -H "X-API-Key: changeme" "http://localhost:8080/forecast/cashflow?window=3"

# GL export
curl -X POST -H "X-API-Key: changeme" -H "Content-Type: application/json"   -d '{"account":"5000","costCenter":"CC-10","amount":1500,"desc":"Cloud spend"}'   http://localhost:8080/gl/export
```

---

## Getting Started

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

# CLI mode
./build/FinWorkOptimizer --server 8080
# or standalone
./build/fwo_rest_server 8080
```

---

## Contributing
Pull requests are welcome! Ensure code passes all builds & tests.

---

## License
MIT License – see LICENSE file for details.

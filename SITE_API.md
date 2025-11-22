# Site Data API

REST API for querying IoT site data from DynamoDB. Similar to OpenWeather API style.

## Quick Start

### Deploy
```bash
./deploy_api.sh deploy
```

### Get API URL
```bash
./deploy_api.sh url
```

### Test
```bash
./test_api.sh
```

## API Reference

### Endpoint

```
GET /site
```

### Query Parameters

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `site_name` | Yes | - | Name of the site to query |
| `count` | No | 5 | Number of records to return (max: 100) |

### Available Sites

| Site | Type |
|------|------|
| Sakti | air |
| Igoo | air |
| Ayee | air |
| Stakmo | air |
| Tuna | air |
| Likir | air |
| Baroo | air |
| Chanigund | air |
| Lingshed | air |

## Usage Examples

### Basic Request
```bash
curl "https://<api-id>.execute-api.us-east-1.amazonaws.com/prod/site?site_name=Sakti"
```

### With Custom Count
```bash
curl "https://<api-id>.execute-api.us-east-1.amazonaws.com/prod/site?site_name=Ayee&count=10"
```

### Python
```python
import requests

API_URL = "https://<api-id>.execute-api.us-east-1.amazonaws.com/prod/site"

# Get latest data for Sakti
response = requests.get(API_URL, params={"site_name": "Sakti", "count": 5})
data = response.json()

print(f"Site: {data['site_name']}")
print(f"Latest reading: {data['current']['timestamp']}")
print(f"Temperature: {data['current']['temperature']}")
```

### JavaScript
```javascript
const API_URL = "https://<api-id>.execute-api.us-east-1.amazonaws.com/prod/site";

fetch(`${API_URL}?site_name=Sakti&count=5`)
  .then(response => response.json())
  .then(data => {
    console.log(`Site: ${data.site_name}`);
    console.log(`Latest: ${data.current.timestamp}`);
    console.log(`Temperature: ${data.current.temperature}`);
  });
```

## Response Format

### Success Response (200)

```json
{
  "site_name": "Sakti",
  "site_type": "air",
  "active": true,
  "timezone": "Asia/Kolkata",
  "timezone_offset": 19800,
  "query_time": 1700000000,
  "query_time_ist": "2024-11-14 10:30:00",
  "count": 5,
  "current": {
    "timestamp": "2024-11-14 10:25",
    "dt": 1699999500,
    "temperature": 25.5,
    "water_temp": 20.3,
    "pressure": 1013,
    "voltage": 12.4,
    "counter": 1234
  },
  "readings": [
    {
      "timestamp": "2024-11-14 10:25",
      "dt": 1699999500,
      "temperature": 25.5,
      "water_temp": 20.3,
      "pressure": 1013,
      "voltage": 12.4,
      "counter": 1234
    }
  ]
}
```

### Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `site_name` | string | Name of the queried site |
| `site_type` | string | Type of site: "air" or "drip" |
| `active` | boolean | Whether the site is active |
| `timezone` | string | Timezone identifier |
| `timezone_offset` | integer | Timezone offset in seconds (IST = 19800) |
| `query_time` | integer | Unix timestamp of the query (UTC) |
| `query_time_ist` | string | Query time in IST (human readable) |
| `count` | integer | Number of readings returned |
| `current` | object | Most recent reading |
| `readings` | array | Array of readings (newest first) |

### Reading Fields (AIR type)

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | string | Reading timestamp (IST) |
| `dt` | integer | Unix timestamp |
| `temperature` | number | Ambient temperature |
| `water_temp` | number | Water temperature |
| `pressure` | number | Pressure reading |
| `voltage` | number | Battery/supply voltage |
| `counter` | integer | Reading counter |

### Reading Fields (Drip type)

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | string | Reading timestamp (IST) |
| `dt` | integer | Unix timestamp |
| `soil_1` | number | Soil moisture sensor 1 |
| `soil_2` | number | Soil moisture sensor 2 |
| `temperature` | number | Ambient temperature |
| `pressure` | number | Pressure reading |
| `voltage` | number | Battery/supply voltage |
| `counter` | integer | Reading counter |

## Error Responses

### Missing site_name (400)
```json
{
  "error": "Missing required parameter: site_name",
  "available_sites": ["Sakti", "Igoo", "Ayee", ...]
}
```

### Site Not Found (404)
```json
{
  "error": "Site not found: InvalidSite",
  "available_sites": ["Sakti", "Igoo", "Ayee", ...]
}
```

### Server Error (500)
```json
{
  "error": "Internal server error",
  "message": "Error details..."
}
```

## Management Commands

```bash
# Full deployment (Lambda + API Gateway)
./deploy_api.sh deploy

# Update Lambda code only
./deploy_api.sh update

# Test the API
./deploy_api.sh test

# Show API URL
./deploy_api.sh url

# Remove all resources
./deploy_api.sh cleanup
```

## Architecture

```
Client Request
     │
     ▼
┌─────────────────┐
│  API Gateway    │
│  (HTTP API)     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Lambda         │
│  (site_api.py)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  DynamoDB       │
│  AIRTable /     │
│  DripTable      │
└─────────────────┘
```

## AWS Resources Created

| Resource | Name |
|----------|------|
| Lambda Function | `site-data-api` |
| IAM Role | `site-api-lambda-role` |
| API Gateway | `SiteDataAPI` |
| Stage | `prod` |

## Rate Limits

- API Gateway default: 10,000 requests/second
- Lambda concurrent executions: Account default (1,000)

## CORS

CORS is enabled for all origins (`*`). Allowed methods: GET, OPTIONS.

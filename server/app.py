from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional
from fastapi import FastAPI, Query
from fastapi.responses import HTMLResponse

import database

@asynccontextmanager
async def lifespan(app: FastAPI):
    database.init_db()
    yield

app = FastAPI(
    title="Soil Moisture Monitor API",
    lifespan=lifespan
)

HTML_FILE_PATH = Path(__file__).resolve().parent / "index.html"

@app.get("/api/latest")
def api_get_latest(sensor_id: Optional[str] = None):
    latest = database.get_latest_measurement(sensor_id=sensor_id)
    if not latest:
        return {"status": "no_data", "message": "No records found"}
    return {"status": "ok", "data": latest}

@app.get("/api/history")
def api_get_history(limit: int = Query(50, ge=1, le=500), sensor_id: Optional[str] = None):
    history = database.get_history(limit=limit, sensor_id=sensor_id)
    return {
        "status": "ok",
        "count": len(history),
        "data": history
    }

@app.get("/", response_class=HTMLResponse)
def get_dashboard():
    if not HTML_FILE_PATH.exists():
        return HTMLResponse("<h1>index.html not found</h1>", status_code=404)
    return HTMLResponse(content=HTML_FILE_PATH.read_text(encoding="utf-8"))

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("app:app", host="0.0.0.0", port=8000, reload=True)

from fastapi import FastAPI, Query
from fastapi.responses import HTMLResponse
from typing import Optional
from pathlib import Path

# Importujemy nasze funkcje bazy danych
import database

app = FastAPI(
    title="Czujnik Wilgotności Gleby - Serwer",
    description="API i panel WWW do monitorowania odczytów z czujnika LoRa"
)

# Ścieżka do pliku HTML z panelem kontrolnym
HTML_FILE_PATH = Path(__file__).resolve().parent / "index.html"


@app.on_event("startup")
def startup_event():
    """Uruchamiane jednorazowo przy starcie serwera - upewnia się, że baza istnieje."""
    database.init_db()


@app.get("/api/latest")
def api_get_latest(sensor_id: Optional[str] = None):
    """
    Endpoint zwracający najświeższy odczyt w formacie JSON.
    Można opcjonalnie przefiltrować po sensor_id, np. /api/latest?sensor_id=plant_pot_1
    """
    latest = database.get_latest_measurement(sensor_id=sensor_id)
    if not latest:
        return {"status": "no_data", "message": "Brak danych w bazie"}
    return {"status": "ok", "data": latest}


@app.get("/api/history")
def api_get_history(limit: int = Query(50, ge=1, le=500), sensor_id: Optional[str] = None):
    """
    Endpoint zwracający historię pomiarów do wykresu w formacie JSON.
    Parametr limit określa ile ostatnich próbek pobrać (domyślnie 50, max 500).
    """
    history = database.get_history(limit=limit, sensor_id=sensor_id)
    return {
        "status": "ok",
        "count": len(history),
        "data": history
    }


@app.get("/", response_class=HTMLResponse)
def get_dashboard():
    """Serwuje główną stronę z panelem kontrolnym i wykresem."""
    if not HTML_FILE_PATH.exists():
        return HTMLResponse("<h1>Błąd: Brak pliku index.html</h1>", status_code=404)
    return HTMLResponse(content=HTML_FILE_PATH.read_text(encoding="utf-8"))


if __name__ == "__main__":
    import uvicorn
    # host="0.0.0.0" oznacza, że serwer jest dostępny nie tylko na localhost,
    # ale też dla innych urządzeń w sieci lokalnej (np. telefon po Wi-Fi)
    uvicorn.run("app:app", host="0.0.0.0", port=8000, reload=True)


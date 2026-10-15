import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional, List, Dict, Any

DB_PATH = Path(__file__).resolve().parent / "sensors.db"

def _get_connection() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    # Enable Write-Ahead Logging to support concurrent reads during bridge writes
    conn.execute("PRAGMA journal_mode=WAL;")
    return conn

def init_db() -> None:
    with _get_connection() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS measurements (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                sensor_id TEXT NOT NULL,
                humidity REAL NOT NULL,
                battery_voltage REAL NOT NULL
            )
        """)
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_timestamp 
            ON measurements (timestamp)
        """)

def add_measurement(
    sensor_id: str,
    humidity: float,
    battery_voltage: float,
    timestamp: Optional[str] = None
) -> None:
    if timestamp is None:
        timestamp = datetime.now(timezone.utc).isoformat()

    with _get_connection() as conn:
        conn.execute("""
            INSERT INTO measurements (timestamp, sensor_id, humidity, battery_voltage)
            VALUES (?, ?, ?, ?)
        """, (timestamp, sensor_id, humidity, battery_voltage))

def get_latest_measurement(sensor_id: Optional[str] = None) -> Optional[Dict[str, Any]]:
    with _get_connection() as conn:
        if sensor_id:
            cursor = conn.execute("""
                SELECT * FROM measurements 
                WHERE sensor_id = ? 
                ORDER BY timestamp DESC 
                LIMIT 1
            """, (sensor_id,))
        else:
            cursor = conn.execute("""
                SELECT * FROM measurements 
                ORDER BY timestamp DESC 
                LIMIT 1
            """)
        row = cursor.fetchone()
        return dict(row) if row else None

def get_history(limit: int = 100, sensor_id: Optional[str] = None) -> List[Dict[str, Any]]:
    with _get_connection() as conn:
        if sensor_id:
            query = """
                SELECT * FROM (
                    SELECT * FROM measurements 
                    WHERE sensor_id = ? 
                    ORDER BY timestamp DESC 
                    LIMIT ?
                ) ORDER BY timestamp ASC
            """
            cursor = conn.execute(query, (sensor_id, limit))
        else:
            query = """
                SELECT * FROM (
                    SELECT * FROM measurements 
                    ORDER BY timestamp DESC 
                    LIMIT ?
                ) ORDER BY timestamp ASC
            """
            cursor = conn.execute(query, (limit,))
        return [dict(row) for row in cursor.fetchall()]

if __name__ == "__main__":
    init_db()
    add_measurement("test_node", 45.0, 3.95)
    print(f"Latest record: {get_latest_measurement('test_node')}")

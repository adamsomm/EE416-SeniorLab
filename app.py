from flask import Flask, request, jsonify, render_template
from datetime import datetime
import hashlib
import json
import csv
import os

app = Flask(__name__)

# --- CSV File Paths ---
ROSTER_FILE = 'student_roster.csv'
LOG_FILE = 'attendance_log.csv'


# --- Load Student Map from CSV ---
def load_student_map_from_csv():
    """Reads the student_roster.csv and builds the secure map."""
    student_map = {}
    try:
        with open(ROSTER_FILE, mode='r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                hashed_mac = row['hashed_mac'].strip()
                student_id = row['student_id'].strip()
                student_name = row['student_name'].strip()

                student_map[hashed_mac] = {
                    "student_id": student_id,
                    "student_name": student_name
                }
        print(f"Successfully loaded {len(student_map)} students from {ROSTER_FILE}")
    except FileNotFoundError:
        print(f"ERROR: '{ROSTER_FILE}' not found. Please create it.")
        return {}
    except Exception as e:
        print(f"Error reading CSV: {e}")
        return {}
    return student_map


# Load the map on startup
SECURE_STUDENT_MAP = load_student_map_from_csv()


def initialize_attendance_db():
    """Initializes the in-memory DB from the loaded student map."""
    db = {}
    if not SECURE_STUDENT_MAP:
        print("Warning: SECURE_STUDENT_MAP is empty. Dashboard will be empty.")
        return {}

    for hash_key, info in SECURE_STUDENT_MAP.items():
        student_id = info['student_id']
        db[student_id] = {
            "student_name": info['student_name'],
            "is_present": False,  # Default to Absent
            "timestamp": "N/A"
        }
    return db


# Initialize the 'database' when the app starts
attendance_status_db = initialize_attendance_db()


# --- Function to log attendance events to CSV ---
def log_attendance_to_csv(hashed_mac, student_info, presence_status, timestamp):
    """Appends a new attendance event to the log file."""
    file_exists = os.path.isfile(LOG_FILE)

    try:
        with open(LOG_FILE, mode='a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            if not file_exists:
                writer.writerow(["timestamp", "hashed_mac", "student_id", "student_name", "status"])

            status_text = "PRESENT" if presence_status else "ABSENT"
            writer.writerow([
                timestamp,
                hashed_mac,
                student_info['student_id'],
                student_info['student_name'],
                status_text
            ])
    except Exception as e:
        print(f"Error writing to log file: {e}")


# ---------------------- Flask Routes ----------------------

@app.route('/')
def index():
    """Renders the web dashboard with the *full list* of student statuses."""
    student_list = sorted(attendance_status_db.values(), key=lambda x: x['student_name'])
    return render_template('dashboard.html', students=list(student_list))


@app.route('/receive_data', methods=['POST'])
def receive_data():
    """
    Handles incoming POST request.
    UPDATED LOGIC: Once a student is marked PRESENT, they remain PRESENT.
    Future updates for that student are ignored to 'finalize' attendance.
    """
    global attendance_status_db

    try:
        gateway_data = request.json
        if not gateway_data or 'tag_id' not in gateway_data or 'is_present' not in gateway_data:
            return jsonify({"status": "error", "message": "Missing required fields"}), 400

        raw_tag_id = gateway_data.get('tag_id').strip()
        incoming_presence_status = gateway_data.get('is_present', False)

        # 1. HASH the raw Tag ID
        hashed_mac = hashlib.sha256(raw_tag_id.encode('utf-8')).hexdigest()

        # 2. Look up the student
        student_info = SECURE_STUDENT_MAP.get(hashed_mac)

        if student_info:
            student_id_to_update = student_info['student_id']
            timestamp_str = datetime.now().strftime("%Y-m-%d %H:%M:%S")

            if student_id_to_update in attendance_status_db:

                # --- NEW LOGIC: CHECK IF ALREADY PRESENT ---
                current_db_status = attendance_status_db[student_id_to_update]['is_present']

                if current_db_status == True:
                    # Student is ALREADY finalized as Present. Do NOT update.
                    # We return 200 OK so the ESP32 knows we got the msg, but we ignore the data.
                    print(f"Student {student_info['student_name']} is already PRESENT. Ignoring update.")
                    return jsonify({"status": "success", "message": "Attendance already finalized"}), 200

                # --- IF NOT ALREADY PRESENT, CHECK IF THIS UPDATE MAKES THEM PRESENT ---
                if incoming_presence_status == True:
                    # Update DB to Present
                    attendance_status_db[student_id_to_update]['is_present'] = True
                    attendance_status_db[student_id_to_update]['timestamp'] = timestamp_str

                    # Log ONLY the first arrival to CSV
                    log_attendance_to_csv(hashed_mac, student_info, True, timestamp_str)

                    print(f"--- LOGGED EVENT ---")
                    print(f"Student: {student_info['student_name']} | Status: PRESENT (Finalized)")

                    return jsonify(
                        {"status": "success", "message": f"Marked {student_info['student_name']} Present"}), 200

                else:
                    # Incoming status is False, and Current status is False.
                    # The student is still absent. No need to flood the CSV or console.
                    return jsonify({"status": "ignored", "message": "Student remains Absent"}), 200

            else:
                print(f"Error: Student {student_info['student_name']} found in map but not in status DB.")
                return jsonify({"status": "error", "message": "DB mismatch"}), 500
        else:
            print(f"--- ERROR: Unregistered Tag: {hashed_mac} ---")
            return jsonify({"status": "warning", "message": "Unregistered Tag ID received"}), 202

    except Exception as e:
        print(f"Error processing data: {e}")
        return jsonify({"status": "error", "message": "Internal Server Error"}), 500


if __name__ == '__main__':
    if not SECURE_STUDENT_MAP:
        print("Halting server: Student roster file could not be loaded.")
    else:
        app.run(host='0.0.0.0', port=5000, debug=True)
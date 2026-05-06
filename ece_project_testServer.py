from flask import Flask, request
import csv
from datetime import datetime

LOG_FILE = "sensor_data.csv"

app = Flask(__name__)
@app.route('/log')
def log_data():
    temp = request.args.get('temperature')
    turb = request.args.get('turbidity')
    timestamp = datetime.now()
    print(f"Timestamp: {timestamp} | Temperature: {temp} Celsius | Turbidity: {turb} %")

    with open(LOG_FILE, 'a') as file:
        writer = csv.writer(file)
        writer.writerow([timestamp, temp, turb])
        

    return "hi"
app.run(host='0.0.0.0', port = 5000)
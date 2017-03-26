#!/usr/bin/python3
"""
    Tool to asynchronously fetch weather data from OpenWeatherMap.org
"""
import json
import subprocess
import os
import sys

with open('/etc/weather.json','r') as f:
    config = json.loads(f.read())

key = config['key']
city = config['city']
units = config['units']

def write_out(data):
    with open('/tmp/weather.json','w') as f:
        f.write(data)
    os.chmod('/tmp/weather.json',0o666) # Ensure users can write this, too, for now.
    # Obviously a better approach would be a per-user file, but whatever.

with open('/proc/netif','r') as f:
    lines = f.readlines()
    if len(lines) < 4 or "no network" in lines[0]:
        with open('/tmp/weather.json','w') as f:
            f.write("")
        sys.exit(1)

try:
    url = f"http://api.openweathermap.org/data/2.5/weather?q={city}&appid={key}&units={units}"
    data = subprocess.check_output(['fetch',url]).decode('utf-8').strip()
    weather = json.loads(data)
    if 'weather' in weather and len(weather['weather']) >= 1:
        conditions = weather['weather'][0]
    else:
        conditions = None
    temp = round(weather['main']['temp'])

    output = {
        'temp': weather['main']['temp'],
        'temp_r': round(weather['main']['temp']),
        'conditions': conditions['main'] if conditions else None,
        'icon': conditions['icon'] if conditions else None,
        'humidity': weather['main']['humidity'],
        'clouds': weather['clouds']['all'] if 'all' in weather['clouds'] else None,
    }

    with open('/tmp/weather.json','w') as f:
        f.write(json.dumps(output))
except:
    with open('/tmp/weather.json','w') as f:
        f.write("")
    sys.exit(1)

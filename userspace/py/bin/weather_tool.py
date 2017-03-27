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

home = os.environ['HOME']
if os.path.exists(f'{home}/.weather.json'):
    with open(f'{home}/.weather.json','r') as f:
        x_config = json.loads(f.read())
        for k in x_config:
            config[k] = x_config[k]
else:
    x_config = {}


key = config['key']
city = config['city']
units = config['units']

def write_config():
    with open(f'{home}/.weather.json','w') as f:
        f.write(json.dumps(x_config))

def write_out(data):
    with open('/tmp/weather.json','w') as f:
        f.write(data)
    try:
        os.chmod('/tmp/weather.json',0o666) # Ensure users can write this, too, for now.
        # Obviously a better approach would be a per-user file, but whatever.
    except:
        pass

def update_weather():

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
        return True
    except:
        with open('/tmp/weather.json','w') as f:
            f.write("")
        return False

if __name__ == "__main__":

    if "--config" in sys.argv:
        import yutani
        import yutani_mainloop
        from input_box import TextInputWindow

        if __name__ == '__main__':
            yutani.Yutani()
            d = yutani.Decor()

            def quit():
                sys.exit(0)

            def set_units(inputbox):
                global units
                x_config['units'] = inputbox.text()
                units = x_config['units']
                inputbox.close()
                write_config()
                update_weather()
                quit()

            def set_city(inputbox):
                global city
                x_config['city'] = inputbox.text()
                city = x_config['city']
                inputbox.close()
                TextInputWindow(d,"What units would you like? (metric, imperial, kelvin)","",text=units,callback=set_units, cancel_callback=quit)

            TextInputWindow(d,"What city are you in?","",text=city,callback=set_city,cancel_callback=quit)

            yutani_mainloop.mainloop()
            sys.exit(0)

    else:
        if not update_weather():
            sys.exit(1)


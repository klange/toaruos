import subprocess

for i in ['01','02','03','04','09','10','11','13','50']:
    for j in ['d','n']:
        subprocess.check_output(['wget',f"http://openweathermap.org/themes/openweathermap/assets/vendor/owm/img/widgets/{i}{j}.png"])

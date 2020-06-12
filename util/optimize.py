#!/usr/bin/env python3

import glob
import os
import subprocess

bmps = glob.glob("*.bmp")

for i in bmps:
    subprocess.run(["convert",i,"png32:"+i.replace(".bmp",".png")])
    bmp_size = os.stat(i).st_size
    png_size = os.stat(i.replace(".bmp",".png")).st_size

    if bmp_size >= png_size:
        print(f"{i}: keeping png")
        os.remove(i)
    else:
        print(f"{i}: keeping bmp")
        os.remove(i.replace(".bmp",".png"))



#!/usr/bin/python3
"""
    Install Vim if it's not installed, otherwise run it.
"""
import os
import subprocess

def run_vim():
    subprocess.call(["vim"])

def install_vim():
    subprocess.call(["sudo","get-py","vim"])

if not os.path.exists("/usr/bin/vim"):
    print("Vim is not installed. Would you like to install it?")
    response = input("Y/n? ")
    if not response or response == "Y" or response == "y" or response == "yes":
        install_vim()

run_vim()

#!/usr/bin/env python
import subprocess, sys

q = subprocess.Popen(['qemu-system-i386', '-kernel', 'toaruos-kernel', '-m', '256',
                      '-serial', 'stdio', '-vga', 'std', '-hda', 'toaruos-disk.img',
                      '-vnc', ':1', '-append', 'vgaterm hdd start=/bin/core-tests logtoserial=10'],
                      stdout=subprocess.PIPE)

passes   = 0
failures = 0
result   = 0

def process_line(line):
	global passes, failures
	data = line.strip().split(" : ")
	if data[1] == "FAIL":
		color = "1;31"
		text  = "fail"
		failures += 1
	elif data[1] == "WARN":
		color = "1;33"
		text  = "warn"
	elif data[1] == "PASS":
		color = "1;32"
		text  = "pass"
		passes += 1
	elif data[1] == "INFO":
		color = "1;34"
		text  = "info"
	elif data[1] == "DONE":
		color = "1;36"
		text  = "Done!"
	elif data[1] == "FATAL":
		color = "1;37;41"
		text  = "FATAL ERROR ECOUNTERED"
	print "\033[%sm%s\033[0m %s" % (color, text, data[2])
	if data[1] == "FATAL":
		return 2
	elif data[1] == "DONE":
		return 1
	return 0


def log_line(line):
	print >>sys.stderr, line.strip()

while q.poll() == None:
	line = q.stdout.readline()
	if line:
		if line.startswith("core-tests :"):
			result = process_line(line)
			if result > 0:
				q.kill()
				try:
					subprocess.call(["stty","echo"])
				except:
					pass
				if result == 2:
					sys.exit(2)
		else:
			log_line(line)

print "\033[1mTest completed. \033[1;32m%d passes\033[0m, \033[1;31m%d failures\033[0m." % (passes, failures)

if failures > 0:
	sys.exit(1)

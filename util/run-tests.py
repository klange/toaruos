#!/usr/bin/env python2
import subprocess
import sys
import time

class TestRunner(object):

    def __init__(self):
        self.passes = 0
        self.fails = 0

    def run(self):
        self.qemu = subprocess.Popen(['make','-s','headless'], stdout=subprocess.PIPE, stdin=subprocess.PIPE)

        time.sleep(2)

        self.qemu.stdin.write("shell\n")
        self.qemu.stdin.write("core-tests\n");

        line = ""
        while self.qemu.poll() == None:
            charin = self.qemu.stdout.read(1)
            if charin == '\n':
                self.parse_line(line)
                line = ""
            else:
                line += charin

        print "\033[1mTest completed. \033[1;32m%d passes\033[0m, \033[1;31m%d failures\033[0m." % (self.passes, self.fails)

        if self.fails > 0:
            sys.exit(1)

    def parse_line(self, line):
        if line.startswith("core-tests :"):
            output, result = self.process_line(line)
            print output
            if result > 0:
                self.qemu.kill()
        else:
            self.log_line(line)

    def process_line(self, line):
        data = line.strip().split(" : ")
        text = ""
        color = ""
        result = 0
        if data[1] == "FAIL":
            color = "1;31"
            text  = "fail"
            self.fails += 1
        elif data[1] == "WARN":
            color = "1;33"
            text  = "warn"
        elif data[1] == "PASS":
            color = "1;32"
            text  = "pass"
            self.passes += 1
        elif data[1] == "INFO":
            color = "1;34"
            text  = "info"
        elif data[1] == "DONE":
            color = "1;36"
            text  = "Done!"
            result = 1
        elif data[1] == "FATAL":
            color = "1;37;41"
            text  = "FATAL ERROR ECOUNTERED"
            result = 2
        return "\033[%sm%s\033[0m %s" % (color, text, data[2]), result

    def log_line(self, line):
        print >>sys.stderr, line.strip()


if __name__ == "__main__":
    TestRunner().run()

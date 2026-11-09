# Reporting Vulnerabilities and Exploits

As ToaruOS is not intended for serious real-world use, responsible disclosure should not typically be necessary: Issue reports for security vulnerabilities should be filed [directly on Github as regular issues](https://github.com/klange/toaruos/issues). There may be exceptions to this, eg. if you discover a remote exploit that could affect casual users or impacts the host system during the build process.

Reports are greatly appreciated, but my bandwidth to work on the OS is limited. While I will generally try to spend some time on quick fixes for issues that adversely affect the behavior of benign software, I may never get around to addressing vulnerabilities which require more careful exploits - but, please do still report these. As an exception to my general contribution guidelines, I am open to accepting unprompted code contributions related to resolving security issues.

## For Users

Beyond the usual boilerplate about the software being provided "as is" and "without warranty", potential users should understand that ToaruOS is not meant to be "used" at all. ToaruOS is intended as an educational tool - it is meant to be studied. While users are encouraged to run the OS in a virtual machine to that end, proper precautions should be taken. If the OS is exposed to untrusted users, it should be properly isolated and firewalled. The use of virtual machine hosts which employ tunnel devices when on an untrusted network is highly discouraged.

## For CTF Operators

ToaruOS has been used in a handful of CTF competitions, which I find quite neat. If you are operating a CTF and have identified an existing vulnerability you hope competitors will find and exploit, I am happy to be informed ahead of time and won't spoil things.

Additionally, as a recommendation to CTF operators, there are many known TOCTOU vulnerabilities in ToaruOS that are only exploitable when SMP is enabled. These kinds of issues are likely to stick around for a while, so consider disabling SMP to make the attack surface smaller and more interesting.

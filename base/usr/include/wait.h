#pragma once

int waitpid(int pid, int *status, int options);
int wait(int *status);

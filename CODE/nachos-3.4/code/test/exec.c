//
// Created by sduda on 2019/11/9.
//

#include "syscall.h"

int
main() {
    int pid;
    pid = Exec("../test/halt.noff");
    Halt()
}
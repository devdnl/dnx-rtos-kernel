# dnx RTOS

The dnx RTOS  is  a  general  purpose operating  system  based  on  the FreeRTOS
kernel. The dnx layer is modeled on  well-known Unix architecture. A destination
of  the  system  are  small  microcontrollers supported by the FreeRTOS kernel,
especially  32-bit. The system  is easy scalable to the user's needs,  the user
can write own drivers,  virtual devices, programs and so on. The program layer
is mostly compatible with the C standard. You can say that the dnx RTOS is a
FreeRTOS kernel distribution.

This repository contains only operating system. To run applications user should
download so called "application set" (appset) repository that contains all applications
that can be used by user, it can be found in dnx-rtos-workspace repository.

## The minimal hardware requirements
There are minimal hardware requirements that allows to start system but with
very minimalistic software base:

- 64 KiB Flash memory,
- 20 KiB RAM memory.

The hardware requirements depends on the user's configuration.

## The system features
- user's terminal (via UART interface in example configuration),
- possibility to run many copies of the same program,
- simple porting of PC programs for the dnx (due to the C standard compatibility)
- simple Drivers Layer,
- support many file systems (the VFS Layer),
- interrupts are not masked by the system (the RTOS feature),
- dynamic memory allocation (simple garbage collector for programs),
- supports many CPU architectures (the FreeRTOS feature),
- modular design.

## The project configuration
To configure the project, type in the terminal:
```
make config
```
or
```
./configure
```

To compile the project, type in the terminal:
```
make
```

## Folder structure
- BSP    - Board Support Packages (configurations)
- build  - the project's binaries (created after compilation)
- config - the project's default configuration
- doc    - documentation
- src    - the project's sources
- tools  - the project's tools (scripts, etc)



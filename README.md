# dpdk-azure-portinfo
Diagnostic tool to dump DPDK information on Azure

This is a simple program to look at DPDK ports while another
primary process is using them.

# Usage

The program uses DPDK arguments to set process type and
bind to CPU.

```
dpdk-portinfo -l 3 --proc-type=secondary


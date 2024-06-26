# CPUUsage Object

* `percentCPUUsage` number - Percentage of CPU used since the last call to getCPUUsage.
  First call returns 0.
* `cumulativeCPUUsage` number (optional) - Total seconds of CPU time used since process
  startup.
* `idleWakeupsPerSecond` number - The number of average idle CPU wakeups per second
  since the last call to getCPUUsage. First call returns 0. Will always return 0 on
  Windows.

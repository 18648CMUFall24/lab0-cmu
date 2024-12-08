# 2.1 Writeup (10 points)

### Q1. (1 point)
>Your hardware device, like any other, has a very limited number of hardware timers. Does
that limit extend to the number of hrtimers that can be created? Why or why not?

No, hardware timer limit doesn't extend to the number of hrtimers. Hrtimers are a software abstraction leveraged by the kernel that doesn't require a dedicated hardware timer.

### Q2. (1 point)
>Assume a thread makes a blocking file read I/O call and the OS does not have the data
handy in memory. The OS blocks and deschedules the thread until the data arrives from disk. When
the data does arrive, how does the OS know which thread to wake up? Which kernel mechanism is
used?

The OS can use context like file descriptor stored in the kernel to identify and retrieve that previous thread. A wait queue is used to wake the blocked thread.

### Q3. (1 point)
>Periodic work in the kernel can be performed by adding it to a work queue or to an hrtimer
callback. What is a work queue handler allowed to do that an hrtimer handler is not?

The work queue handler can blocking operations like sleep as it operates in process context, while hrtimer callback handlers operates in interrupt context, and prohibits sleeping or blocking operations.

### Q4. (1 point)
>What is the difference between regular signals and real-time signals?
 
Real time signals are queued up and have a higher range of identifiers, while regular signals are not. When multiple regular signals are sent before handled, they may be merged or dropped. Thus each instance of real time signals is delivered without loss.

### Q4. (1 point)
>What kernel subsystem decides which sleep state the processor enters and what parameters
does it use to make the decision?

Power management system such as CPUIdle in linux. Various parameters could be considered based on system design, for example, user space instructions, CPU utilization, latency constraints and energy goals, etc.

![2.5.3](https://raw.githubusercontent.com/18648CMUFall24/lab0-cmu/refs/heads/lab-3/writeups/Untitled.png)

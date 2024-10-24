# 2.1 Writeup (10 points)

### Q1. (1 point)

> What is the difference between concurrent execution and parallel execution?

Parallel execution is when processes are literally running at the same time, on multiple cores, whereas concurrent execution is when processes are being run in an interleaved manner, but not necessarily at the same time.

### Q2. Imagine Dexter needs to instrument his kernel to collect some data points.

> The points are generated within the kernel at some memory address and need to be visualized in a userspace application. Suppose each point appears periodically at one memory address in the kernel, for example corresponding to a memory-mapped register of a device. If a point is not read before the next one is ready, then the point is lost.
> Suppose that on Dexter’s platform:
>
> - the computation overhead of reading the point from the initial memory address is negligible
> - it takes 100µs to complete a round-trip between user-space and kernel space
> - it takes 10ns to copy one data point value from kernel memory to user memory.

### Q2. (a) (2 points)

> As a trained syscall writer, Dexter creates a syscall to retrieve the one data value and call the syscall in a tight while loop from a userspace application. Suppose the data points are generated at a rate of 1,000 points per second. What fraction of points, if any, are lost due to the overhead delay?

1,000 points per second so 1sec/1000points = 0.001s/point = 1000µs/point. The overhead to copy a point is 100µs+10ns = 100.01µs, which is smaller than the time it takes to generate a point. Therefore, no points are lost. 0% of points are lost.

### Q2. (b) (2 points)

> Dexter is not satisfied with a slow sampling rate, so from now on, suppose the data points are generated at a rate of 100,000 points per second. Assuming the same implementation approach, what fraction of points, if any, are lost due to the overhead delay under the faster sampling rate?

100,000 points per second is 1sec/100,000points = 0.00001s/point = 10µs/point. 10µs/point is much faster than the time it takes to copy the point (100.01µs), therefore all points are lost. 100% of points are lost.

### Q2. (c) (2 points)

> Dexter abandons the naive approach and changes his implementation to amortize the delay of kernel-user crossing over many points by buffering the points in kernel memory. For a buffer size of 1000 points, what fraction of points, if any, are lost due to the overhead delay? (Assume Dexter reads the points once the buffer is full)

When the rate is 100,000 points per second is 1sec/100,000points = 0.00001s/point = 10µs/point. Then the first 1000 points will not be lost and will be saved into the buffer. Once the buffer is full, it will take (100µs + 10ns/point*1000points) = 1.1E-4sec to copy the entire buffer. During this time, (1.1E-4sec * 100,000points/sec) = 11 points will be lost. Therefore, 11/1000 = 1.1% of points are lost.

### Q2. (d) (3 points)

> Dexter is not satisfied with losing any points at all. How can he improve his implementation to achieve this?

He can improve this implementation to avoid losing any points by using a circular buffer such that new data points overwrite the oldest data points. This way, the buffer is always full and no points are lost. When the data is being transferred, the buffer can still be filled with new data points, so no points are lost.

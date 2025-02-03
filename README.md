In this project, I am implementing a multithreaded matrix multiplication machine that accelerates matrix multiplication by fully utilizing CPU cores.

To prevent race conditions, I use `pthread_mutex_t` to lock the critical section. Additionally, I use `pthread_cond_t` to manage the lock, avoiding busy waiting.

Homework Project Spec [2024 NTUSP Programming HW4 - Matrix Multiplication Machine](https://hackmd.io/@kcwayne/sp2024_hw4)

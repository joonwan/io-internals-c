## Sync + Blocking IO

## Result 
```bash
=== sync + blocking io example ===
create pipe: read_end = 3, write_end = 4
[parent = 33086] 10초 후 데이터를 전송
[parent = 33086] child 는 그 10초 동안 sleep 상태임.

[parent = 33086] 10 초 후 전송 ...
[child PID=33087] read() 호출 직전 - 지금부터 blocking.
[child pid=33087] data 가 올때까지 child process sleep ... 

[parent = 33086] 9 초 후 전송 ...
[parent = 33086] 8 초 후 전송 ...
[parent = 33086] 7 초 후 전송 ...
[parent = 33086] 6 초 후 전송 ...
[parent = 33086] 5 초 후 전송 ...
[parent = 33086] 4 초 후 전송 ...
[parent = 33086] 3 초 후 전송 ...
[parent = 33086] 2 초 후 전송 ...
[parent = 33086] 1 초 후 전송 ...

[Parent PID=33086] write() 완료: 20 bytes 전송
[Child PID=33087] read() 반환! 블록된 시간: 10초
[Child PID=33087] 수신한 데이터: "blocking io 완료!!" (20 bytes)

[결론] read()는 데이터가 올 때까지 프로세스를 잠재웠다.
CPU 낭비 없음. 하지만 그 동안 이 스레드로 다른 I/O 불가.
```

## Blocking 인 이유

**do_child()**

```c
void do_child(int pipefd[2])
{
     // close write end
        close(pipefd[1]);

        char buf[256];
        // buffer 내용 null 로 초기화
        memset(buf, 0, sizeof(buf));

        printf("[child PID=%d] read() 호출 직전 - 지금부터 blocking.\n", getpid());
        printf("[child pid=%d] data 가 올때까지 child process sleep ... \n\n", getpid());
        fflush(stdout);

        time_t t_before = time(NULL);

        /**
            pipefd[0] 에 data 없으면 OS 가 child process 를 sleep 상태로 전환
            데이터가 들어오는 순간 OS 가 process 를 깨우고 read() 가 반환됨.
            CPU는 child process 가 기다리는 동안 다른 process 에게 넘어감
        */

        // 읽을 데이터가 없을 경우 blocking
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        time_t t_after = time(NULL);

       if (n < 0) {
            perror("[Child] read 실패");
            exit(EXIT_FAILURE);
        }

        
        buf[n] = '\0';
        printf("[Child PID=%d] read() 반환! 블록된 시간: %ld초\n", getpid(), t_after - t_before);
        printf("[Child PID=%d] 수신한 데이터: \"%s\" (%zd bytes)\n", getpid(), buf, n);

        close(pipefd[0]);
        exit(EXIT_SUCCESS);
}
```

```c
// 읽을 데이터가 없을 경우 blocking
ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
```

child process 가 `read()` 를 호출하면, 부모 process 가 pipe 에 데이터를 쓸 때까지 대기하게 된다. 그동안 child process 는 다음 코드를 실행하지 못하고 `read()` 가 반환될 때까지 멈춰 있는다.

즉 이 예제에서 `read()` 는 호출한 실행 흐름을 잠시 멈추게 만들기 때문에 **blocking** 이다.


## synchronous 인 이유

```c
buf[n] = '\0';
printf("[Child PID=%d] read() 반환! 블록된 시간: %ld초\n", getpid(), t_after - t_before);
printf("[Child PID=%d] 수신한 데이터: \"%s\" (%zd bytes)\n", getpid(), buf, n);
```

child process 는 `read()` 의 결과가 준비되어야만 그 다음 코드를 실행할 수 있다. 즉 `read()` 작업이 끝나서 `n` 과 `buf` 가 채워진 뒤에야 이후 로직이 이어진다.

다시 말해, 다음 코드의 실행 시점이 `read()` 의 완료 시점에 맞춰져 있기 때문에 이 코드는 **synchronous** 하다.

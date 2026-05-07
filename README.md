# io-internals-c

Hands-on C examples for mastering sync/async and blocking/non-blocking I/O from scratch.

강의나 책에서 "비동기는 콜백으로 완료를 통지받는 방식"이라고 읽어도 체감이 안 된다면, 직접 raw C로 구현해서 `strace`로 syscall을 눈으로 보는 게 제일 빠르다. 이 레포는 그 목적으로 만들었다.

---

## 개념 정리

동기/비동기와 블로킹/논블로킹은 자주 같이 언급되지만 **완전히 다른 축**이다.

**블로킹/논블로킹** → I/O 호출 시 프로세스 상태의 문제

| | 블로킹 | 논블로킹 |
|---|---|---|
| 데이터 없을 때 | 프로세스를 SLEEP 상태로 전환 | `EAGAIN` 반환 후 즉시 리턴 |
| CPU 사용 | 0% (잠든 동안) | 폴링 시 100% (busy-wait) |
| 설정 | 기본값 | `fcntl(fd, F_SETFL, O_NONBLOCK)` |

**동기/비동기** → 실행 흐름이 이어지느냐, 분리되느냐의 문제

| | 동기 | 비동기 |
|---|---|---|
| 실행 흐름 | 호출자와 작업이 같은 타임라인 위에 있다 | 호출자와 작업이 분리된 타임라인으로 진행된다 |
| 완료 확인 | 호출자가 직접 기다리거나 확인 | 완료 시 콜백/시그널/이벤트로 통지 |

> 동기/비동기는 I/O에 국한된 개념이 아니다. 메시지 큐, HTTP 통신, 함수 호출, OS 시그널 모두 이 축으로 분류할 수 있다. I/O에서 가장 극적으로 드러나기 때문에 같이 설명될 뿐이다.

**2×2 매트릭스**

```
                  블로킹(Blocking)             논블로킹(Non-blocking)
               ┌────────────────────────┬──────────────────────────────┐
    동기       │  일반 read(), write()  │  O_NONBLOCK + polling loop   │
  (Sync)       │  가장 단순한 형태      │  CPU 낭비, 실제론 거의 안씀  │
               ├────────────────────────┼──────────────────────────────┤
    비동기     │  select(), poll()      │  epoll ET + O_NONBLOCK       │
  (Async)      │  epoll LT              │  aio_read(), io_uring        │
               │  I/O 멀티플렉싱        │  고성능 서버의 표준 패턴     │
               └────────────────────────┴──────────────────────────────┘
```

> select/epoll은 "비동기"라고 부르기도 하지만 정확히는 **I/O 멀티플렉싱**이다. `epoll_wait` 자체는 블로킹이고, 이후 `read()`는 호출자가 직접 한다. 진짜 비동기는 I/O 요청만 던지고 완료 시 시스템이 알려주는 것(`aio_read`, `io_uring`).

---

## 실습 목록

| # | 파일 | 패턴 | 핵심 |
|---|------|------|------|
| 01 | `01_sync_blocking.c` | Sync + Blocking | `read()`가 데이터 올 때까지 프로세스를 SLEEP |
| 02 | `02_sync_nonblocking.c` | Sync + Non-blocking | `EAGAIN` 폴링 루프, CPU 낭비를 숫자로 확인 |
| 03 | `03_io_multiplexing_select.c` | I/O 멀티플렉싱 | `select()`로 N개 fd를 스레드 하나로 감시 |
| 04 | `04_epoll_nonblocking.c` | epoll ET + Non-blocking | Edge Trigger의 동작과 `O_NONBLOCK` 필수 이유 |
| 05 | `05_posix_aio.c` | True Async | `aio_read()` 제출 후 `SIGUSR1`로 완료 통지 |
| 06 | `06_thread_async_simulation.c` | Thread-based Async | 스레드 풀 + 콜백. `CompletableFuture`의 내부 원리 |
| 07 | `07_compare_all.c` | 4가지 비교 | 동일 조건에서 4패턴 소요시간 + EAGAIN 횟수 비교 |

---

## 빌드 및 실행

```bash
# 전체 빌드
make all

# 개별 실행 (순서대로 보는 게 효과적)
./01   # Sync + Blocking
./02   # Sync + Non-blocking  ← EAGAIN 수백만 번 나오는 것 직접 확인
./03   # select()
./04   # epoll
./05   # POSIX AIO
./06   # Thread Async
./07   # 비교 (가장 중요)
```

Linux 전용. `04`는 epoll 사용으로 macOS 미지원.

---

## 실행 결과 요약

`./07` 실행 시 4가지 패턴을 동일 조건(pipe 5개)에서 비교:

```
패턴                  소요시간    EAGAIN
--------------------  ---------  ---------------------
Sync  + Blocking       1000ms    0회     (SLEEP으로 대기)
Sync  + Non-blocking   1003ms    638만 회 (CPU 100% 낭비)
Async + Blocking       1000ms    0회     (epoll_wait SLEEP)
Async + Non-blocking   1000ms    0회     (epoll ET, 최적)
```

Sync+NonBlocking이 3ms 더 걸린 이유는 수백만 번의 syscall 오버헤드.

---

## strace로 syscall 레벨 확인

실습과 함께 보면 훨씬 체감이 된다.

```bash
# 01: read()가 실제로 얼마나 블록되는지 확인 (-T: 각 syscall 소요시간)
strace -T ./01 2>&1 | grep read

# 02: EAGAIN이 syscall 레벨에서 반환되는 것 확인
strace -e trace=read ./02 2>&1 | grep EAGAIN | wc -l

# 04: epoll syscall 흐름 확인
strace -e trace=epoll_create1,epoll_ctl,epoll_wait,read ./04

# CPU 사용률 비교 (터미널 2개 열고)
./02 &  top -p $!   # CPU ~100%
./04 &  top -p $!   # CPU ~0%
```

---

## Java/Spring과의 대응

| C 실습 | Java |
|--------|------|
| `read()` 블로킹 | `InputStream.read()`, `RestTemplate` |
| `select()` / `epoll` LT | `Java NIO Selector` |
| `epoll` ET + Non-blocking | `Netty EventLoop` |
| 스레드 풀 + 콜백 (실습 06) | `CompletableFuture.supplyAsync()` |
| `aio_read()` + 시그널 | `Spring WebFlux` (Reactor Netty) |

---

## 참고

- [Linux man pages: epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html)
- [Linux man pages: aio(7)](https://man7.org/linux/man-pages/man7/aio.7.html)
- [The C10K problem](http://www.kegel.com/c10k.html) — select의 한계와 epoll 등장 배경
- [io_uring](https://kernel.dk/io_uring.pdf) — Linux 5.1+의 차세대 비동기 I/O
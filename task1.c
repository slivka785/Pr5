#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>

#define TOTAL_ROUNDS 20

volatile sig_atomic_t got_signal = 0;
volatile sig_atomic_t latest_guess = 0;
volatile sig_atomic_t guess_correct = 0;

void handle_guess(int sig, siginfo_t *info, void *context) {
    if (sig == SIGRTMIN) {
        latest_guess = info->si_value.sival_int;
        got_signal = 1;
    }
}

void handle_feedback(int sig) {
    guess_correct = (sig == SIGUSR1) ? 1 : 0;
}

void run_host(pid_t guesser_pid, int max_val, int round) {
    srand(time(NULL) ^ getpid());
    int secret = 1 + rand() % max_val;
    printf("[Раунд %d] Ведущий загадал число: %d\n", round, secret);
    fflush(stdout);

    struct sigaction sa = {0};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_guess;
    sigaction(SIGRTMIN, &sa, NULL);

    sigset_t wait_mask;
    sigemptyset(&wait_mask);
    sigaddset(&wait_mask, SIGRTMIN);
    sigprocmask(SIG_BLOCK, &wait_mask, NULL);

    int attempts = 0;
    siginfo_t info;

    while (1) {
        got_signal = 0;
        sigwaitinfo(&wait_mask, &info);

        int guess = info.si_value.sival_int;
        attempts++;

        printf("Ведущий получил предположение: %d\n", guess);
        fflush(stdout);

        if (guess == secret) {
            kill(guesser_pid, SIGUSR1);
            break;
        } else {
            kill(guesser_pid, SIGUSR2);
        }
    }

    printf("Число отгадано за %d попыток!\n\n", attempts);
}

void run_guesser(pid_t host_pid, int max_val) {
    struct sigaction sa = {0};
    sa.sa_handler = handle_feedback;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    sigset_t wait_mask;
    sigemptyset(&wait_mask);
    sigaddset(&wait_mask, SIGUSR1);
    sigaddset(&wait_mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &wait_mask, NULL);

    for (int round = 1; round <= TOTAL_ROUNDS; ++round) {
        int attempts = 0;

        do {
            int guess = 1 + rand() % max_val;
            printf("Игрок предполагает: %d\n", guess);
            fflush(stdout);

            union sigval value;
            value.sival_int = guess;
            sigqueue(host_pid, SIGRTMIN, value);

            int sig;
            sigwait(&wait_mask, &sig);
            attempts++;
        } while (!guess_correct);

        printf("Угадал за %d попыток!\n\n", attempts);
        guess_correct = 0;
        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <максимальное число>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int max_value = atoi(argv[1]);
    if (max_value < 1) {
        fprintf(stderr, "Число должно быть больше 0.\n");
        return EXIT_FAILURE;
    }

    pid_t child_pid = fork();

    if (child_pid < 0) {
        perror("Ошибка fork");
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        srand(time(NULL) ^ getpid());
        run_guesser(getppid(), max_value);
        exit(EXIT_SUCCESS);
    } else {
        for (int round = 1; round <= TOTAL_ROUNDS; ++round) {
            run_host(child_pid, max_value, round);
        }

        kill(child_pid, SIGTERM);
        wait(NULL);
        printf("Игра завершена. Все процессы корректно завершены.\n");
    }

    return EXIT_SUCCESS;
}

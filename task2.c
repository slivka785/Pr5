#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>

#define ROUNDS 25
#define MSG_SIZE (sizeof(msg_t) - sizeof(long))

typedef struct {
    long mtype;
    int payload;
} msg_t;

enum MsgTypes {
    MSG_SECRET = 1,
    MSG_ATTEMPT,
    MSG_FEEDBACK
};

void send_message(int queue_id, long type, int data) {
    msg_t msg = { .mtype = type, .payload = data };
    if (msgsnd(queue_id, &msg, MSG_SIZE, 0) == -1) {
        perror(" Ошибка отправки сообщения");
        exit(EXIT_FAILURE);
    }
}

int receive_message(int queue_id, long expected_type) {
    msg_t msg;
    if (msgrcv(queue_id, &msg, MSG_SIZE, expected_type, 0) == -1) {
        perror(" Ошибка при получении сообщения");
        exit(EXIT_FAILURE);
    }
    return msg.payload;
}

void host_process(int queue_id, int max_val) {
    srand(time(NULL) ^ getpid());

    for (int round = 1; round <= ROUNDS; ++round) {
        int secret = rand() % max_val + 1;
        int attempt, tries = 0;

        printf(" Раунд %d: Загадано число %d\n", round, secret);

        send_message(queue_id, MSG_SECRET, secret);

        while (1) {
            attempt = receive_message(queue_id, MSG_ATTEMPT);
            tries++;
            printf(" Получено предположение: %d\n", attempt);

            int feedback = (attempt == secret) ? 1 : 0;
            send_message(queue_id, MSG_FEEDBACK, feedback);

            if (feedback == 1) {
                printf(" Угадано с %d попытки(ок)!\n\n", tries);
                break;
            }
        }
    }


    send_message(queue_id, MSG_SECRET, -1);
}

void guesser_process(int queue_id, int max_val) {
    while (1) {
        int mystery = receive_message(queue_id, MSG_SECRET);
        if (mystery == -1) {
            printf(" Игра окончена. Получен сигнал завершения.\n");
            break;
        }

        int low = 1, high = max_val, guess, verdict;

        printf(" Начинаю угадывать...\n");

        while (1) {
            guess = (low + high) / 2;
            printf(" Предположение: %d\n", guess);

            send_message(queue_id, MSG_ATTEMPT, guess);
            verdict = receive_message(queue_id, MSG_FEEDBACK);

            if (verdict == 1) {
                printf(" Успех! Число — %d\n\n", guess);
                break;
            }

            if (guess < mystery)
                low = guess + 1;
            else
                high = guess - 1;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <макс_число>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int upper_limit = atoi(argv[1]);
    if (upper_limit <= 1) {
        fprintf(stderr, "Максимальное число должно быть больше 1.\n");
        exit(EXIT_FAILURE);
    }

    key_t key = ftok(".", 'M');
    if (key == -1) {
        perror(" ftok");
        exit(EXIT_FAILURE);
    }

    int msg_queue_id = msgget(key, IPC_CREAT | 0666);
    if (msg_queue_id == -1) {
        perror(" msgget");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror(" fork");
        msgctl(msg_queue_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        guesser_process(msg_queue_id, upper_limit);
        exit(EXIT_SUCCESS);
    } else {
        host_process(msg_queue_id, upper_limit);
        wait(NULL);
        msgctl(msg_queue_id, IPC_RMID, NULL);
        printf(" Очередь сообщений удалена. Все завершено корректно.\n");
    }

    return 0;
}

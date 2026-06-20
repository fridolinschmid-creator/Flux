#include "worker.h"
#include "actions.h"
#include "provider.h"
#include "proactive.h"
#include "journal.h"
#include "habits.h"
#include "exec.h"
#include "../../common/flux_protocol.h"
#include "../../common/flux_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* ---- Job-Warteschlange ----------------------------------------------- */

enum { JOB_CLIENT, JOB_PROACTIVE };

typedef struct {
    int type;
    int cfd;   /* nur fuer JOB_CLIENT */
} job_t;

#define QUEUE_CAP 32

static job_t          queue[QUEUE_CAP];
static int            q_head = 0, q_tail = 0, q_count = 0;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  q_not_full  = PTHREAD_COND_INITIALIZER;

static void enqueue(job_t job) {
    pthread_mutex_lock(&q_lock);
    while (q_count == QUEUE_CAP)
        pthread_cond_wait(&q_not_full, &q_lock);
    queue[q_tail] = job;
    q_tail = (q_tail + 1) % QUEUE_CAP;
    q_count++;
    pthread_cond_signal(&q_not_empty);
    pthread_mutex_unlock(&q_lock);
}

static job_t dequeue(void) {
    pthread_mutex_lock(&q_lock);
    while (q_count == 0)
        pthread_cond_wait(&q_not_empty, &q_lock);
    job_t job = queue[q_head];
    q_head = (q_head + 1) % QUEUE_CAP;
    q_count--;
    pthread_cond_signal(&q_not_full);
    pthread_mutex_unlock(&q_lock);
    return job;
}

/* ---- Request-Bearbeitung (unveraendert aus main.c uebernommen) -------- */

static void handle_client(int cfd) {
    char line[FLUX_MAX_LINE];
    ssize_t n = read(cfd, line, sizeof(line) - 1);
    if (n <= 0) { close(cfd); return; }
    line[n] = '\0';

    char answer[FLUX_MAX_LINE];
    if (strncmp(line, "Q:", 2) == 0) {
        /* Eine Frage ist eine einzelne Zeile -- am ersten Newline
         * abschneiden, falls noch einer mitgesendet wurde. */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        const char *question = line + 2;
        if (!flux_actions_try(question, answer, sizeof(answer)))
            flux_provider_ask(question, answer, sizeof(answer));
        /* Nutzungsgewohnheiten loggen (ersten 80 Zeichen der Frage) */
        char topic[84];
        snprintf(topic, sizeof(topic), "%.80s", question);
        flux_habits_log("assistant", topic);
    } else if (strncmp(line, "X:", 2) == 0) {
        /* Eine bestaetigte Aktion ist mehrzeilig (TO:/SUBJECT:/BODY:)
         * -- NICHT am ersten Newline abschneiden. */
        flux_exec_action(line + 2, answer, sizeof(answer));
    } else {
        const char *msg = "ERR:Unbekanntes Protokoll, erwarte 'Q:<frage>' oder 'X:<aktion>'\nEND\n";
        if (write(cfd, msg, strlen(msg)) < 0) { /* Client schon weg, egal */ }
        close(cfd);
        return;
    }

    char resp[FLUX_MAX_RESPONSE];
    snprintf(resp, sizeof(resp), "A:%s\nEND\n", answer);
    if (write(cfd, resp, strlen(resp)) < 0) { /* Client schon weg, egal */ }
    close(cfd);
}

/* ---- Proaktive Pruefungen (unveraendert aus main.c uebernommen) ------- */

static void run_proactive(void) {
    char key_buf[256] = {0};
    flux_config_get("api_key", key_buf, sizeof(key_buf));
    const char *k = key_buf[0] ? key_buf : getenv("FLUX_AI_API_KEY");
    const char *m = getenv("FLUX_AI_MODEL");
    if (!m || !*m) m = "claude-haiku-4-5-20251001";
    if (k && *k) {
        flux_proactive_check(k, m);
        flux_journal_check(k, m);
        flux_habits_morning_briefing(k, m);
    }
}

/* ---- Arbeiterthread --------------------------------------------------- */

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        job_t job = dequeue();
        switch (job.type) {
            case JOB_CLIENT:    handle_client(job.cfd); break;
            case JOB_PROACTIVE: run_proactive();        break;
        }
    }
    return NULL;
}

/* ---- Oeffentliche API ------------------------------------------------- */

void flux_worker_start(void) {
    pthread_t tid;
    pthread_create(&tid, NULL, worker_main, NULL);
    pthread_detach(tid);
}

void flux_worker_submit_client(int cfd) {
    job_t job = { .type = JOB_CLIENT, .cfd = cfd };
    enqueue(job);
}

void flux_worker_submit_proactive(void) {
    job_t job = { .type = JOB_PROACTIVE, .cfd = -1 };
    enqueue(job);
}

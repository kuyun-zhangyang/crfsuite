/*
 *        Learn command for CRFsuite frontend.
 *
 * Copyright (c) 2007-2010, Naoaki Okazaki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of the authors nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $Id$ */

#include <os.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <crfsuite.h>
#include "option.h"
#include "readdata.h"

#define    SAFE_RELEASE(obj)    if ((obj) != NULL) { (obj)->release(obj); (obj) = NULL; }
#define    MAX(a, b)    ((a) < (b) ? (b) : (a))


typedef struct {
    char *model;
    char *algorithm;
    char *type;

    int holdout;
    int help;

    int num_params;
    char **params;
} learn_option_t;

static char* mystrdup(const char *src)
{
    char *dst = (char*)malloc(strlen(src)+1);
    if (dst != NULL) {
        strcpy(dst, src);
    }
    return dst;
}

static void learn_option_init(learn_option_t* opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->num_params = 0;
    opt->holdout = -1;
    opt->model = mystrdup("crfsuite.model");
    opt->algorithm = mystrdup("lbfgs");
    opt->type = mystrdup("dyad");
}

static void learn_option_finish(learn_option_t* opt)
{
    int i;

    free(opt->model);

    for (i = 0;i < opt->num_params;++i) {
        free(opt->params[i]);
    }
    free(opt->params);
}

BEGIN_OPTION_MAP(parse_learn_options, learn_option_t)

    ON_OPTION_WITH_ARG(SHORTOPT('m') || LONGOPT("model"))
        free(opt->model);
        opt->model = mystrdup(arg);

    ON_OPTION_WITH_ARG(SHORTOPT('t') || LONGOPT("test"))
        opt->holdout = atoi(arg)-1;

    ON_OPTION(SHORTOPT('h') || LONGOPT("help"))
        opt->help = 1;

    ON_OPTION_WITH_ARG(SHORTOPT('a') || LONGOPT("algorithm"))
        free(opt->algorithm);
        opt->algorithm = mystrdup(arg);

    ON_OPTION_WITH_ARG(SHORTOPT('f') || LONGOPT("feature"))
        free(opt->type);
        opt->model = mystrdup(arg);

    ON_OPTION_WITH_ARG(SHORTOPT('p') || LONGOPT("param"))
        opt->params = (char **)realloc(opt->params, sizeof(char*) * (opt->num_params + 1));
        opt->params[opt->num_params] = mystrdup(arg);
        ++opt->num_params;

END_OPTION_MAP()

static void show_usage(FILE *fp, const char *argv0, const char *command)
{
    fprintf(fp, "USAGE: %s %s [OPTIONS] [DATA]\n", argv0, command);
    fprintf(fp, "Obtain a model from a training set of instances given by a file (DATA).\n");
    fprintf(fp, "If argument DATA is omitted or '-', this utility reads a data from STDIN.\n");
    fprintf(fp, "\n");
    fprintf(fp, "OPTIONS:\n");
    fprintf(fp, "    -m, --model=MODEL   Store the obtained model in a file (MODEL)\n");
    fprintf(fp, "    -t, --test=TEST     Report the performance of the model on a data (TEST)\n");
    fprintf(fp, "    -p, --param=NAME=VALUE  Set the parameter NAME to VALUE\n");
    fprintf(fp, "    -h, --help          Show the usage of this command and exit\n");
}



static int message_callback(void *instance, const char *format, va_list args)
{
    vfprintf(stdout, format, args);
    fflush(stdout);
    return 0;
}

int main_learn(int argc, char *argv[], const char *argv0)
{
    int i, ret = 0, arg_used = 0;
    time_t ts;
    char timestamp[80];
    clock_t clk_begin, clk_current;
    learn_option_t opt;
    const char *command = argv[0];
    FILE *fpi = stdin, *fpo = stdout, *fpe = stderr;
    crf_data_t data;
    crf_trainer_t *trainer = NULL;
    crf_dictionary_t *attrs = NULL, *labels = NULL;

    /* Initializations. */
    learn_option_init(&opt);
    crf_data_init(&data);

    /* Parse the command-line option. */
    arg_used = option_parse(++argv, --argc, parse_learn_options, &opt);
    if (arg_used < 0) {
        ret = 1;
        goto force_exit;
    }

    /* Show the help message for this command if specified. */
    if (opt.help) {
        show_usage(fpo, argv0, command);
        goto force_exit;
    }

    /* Create dictionaries for attributes and labels. */
    ret = crf_create_instance("dictionary", (void**)&attrs);
    if (!ret) {
        fprintf(fpe, "ERROR: Failed to create a dictionary instance.\n");
        ret = 1;
        goto force_exit;
    }
    ret = crf_create_instance("dictionary", (void**)&labels);
    if (!ret) {
        fprintf(fpe, "ERROR: Failed to create a dictionary instance.\n");
        ret = 1;
        goto force_exit;
    }

    /* Create a trainer instance. */
    ret = crf_create_instance("train/crf1d/lbfgs", (void**)&trainer);
    if (!ret) {
        fprintf(fpe, "ERROR: Failed to create a trainer instance.\n");
        ret = 1;
        goto force_exit;
    }

    /* Set parameters. */
    for (i = 0;i < opt.num_params;++i) {
        char *value = NULL;
        char *name = opt.params[i];
        crf_params_t* params = trainer->params(trainer);
        
        /* Split the parameter argument by the first '=' character. */
        value = strchr(name, '=');
        if (value != NULL) {
            *value++ = 0;
        }

        params->set(params, name, value);
        params->release(params);
    }

    /* Log the start time. */
    time(&ts);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&ts));
    fprintf(fpo, "Start time of the training: %s\n", timestamp);
    fprintf(fpo, "\n");

    /* Read the training data. */
    fprintf(fpo, "Reading the data set(s)\n");
    clk_begin = clock();
    for (i = arg_used;i < argc;++i) {
        FILE *fp = (strcmp(argv[i], "-") == 0) ? fpi : fopen(argv[i], "r");
        if (fp == NULL) {
            fprintf(fpe, "ERROR: Failed to open the data set: %s\n", argv[i]);
            ret = 1;
            goto force_exit;        
        }

        fprintf(fpo, "%d - %s\n", i-arg_used+1, argv[i]);
        read_data(fp, fpo, &data, attrs, labels, i-arg_used);
        fclose(fp);
    }
    clk_current = clock();

    /* Report the statistics of the training data. */
    fprintf(fpo, "Number of instances: %d\n", data.num_instances);
    fprintf(fpo, "Total number of items: %d\n", crf_data_totalitems(&data));
    fprintf(fpo, "Number of attributes: %d\n", labels->num(attrs));
    fprintf(fpo, "Number of labels: %d\n", labels->num(labels));
    fprintf(fpo, "Seconds required: %.3f\n", (clk_current - clk_begin) / (double)CLOCKS_PER_SEC);
    fprintf(fpo, "\n");
    fflush(fpo);

    /* Set callback procedures that receive messages and taggers. */
    trainer->set_message_callback(trainer, NULL, message_callback);

    /* Start training. */
    if (ret = trainer->train(
        trainer,
        data.instances,
        data.num_instances,
        attrs,
        labels,
        opt.model,
        opt.holdout
        )) {
        goto force_exit;
    }

    /* Log the end time. */
    time(&ts);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&ts));
    fprintf(fpo, "End time of the training: %s\n", timestamp);
    fprintf(fpo, "\n");

force_exit:
    SAFE_RELEASE(trainer);
    SAFE_RELEASE(labels);
    SAFE_RELEASE(attrs);

    crf_data_finish(&data);
    learn_option_finish(&opt);

    return ret;
}

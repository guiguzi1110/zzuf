/*
 *  zzcat - various cat reimplementations for testing purposes
 *  Copyright (c) 2006-2010 Sam Hocevar <sam@hocevar.net>
 *                All Rights Reserved
 *
 *  $Id$
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What The Fuck You Want
 *  To Public License, Version 2, as published by Sam Hocevar. See
 *  http://sam.zoy.org/wtfpl/COPYING for more details.
 */

/*
 * TODO: fsetpos64, fgetln
 */

#include "config.h"

/* Needed for lseek64() */
#define _LARGEFILE64_SOURCE
/* Needed for O_RDONLY on HP-UX */
#define _INCLUDE_POSIX_SOURCE
/* Needed for fgets_unlocked() */
#define _GNU_SOURCE

#if defined HAVE_STDINT_H
#   include <stdint.h>
#elif defined HAVE_INTTYPES_H
#   include <inttypes.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#if defined HAVE_UNISTD_H
#   include <unistd.h>
#endif
#if defined HAVE_SYS_MMAN_H
#   include <sys/mman.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if !defined HAVE_GETOPT_LONG
#   include "mygetopt.h"
#elif defined HAVE_GETOPT_H
#   include <getopt.h>
#endif

#if defined HAVE_GETOPT_LONG
#   define mygetopt getopt_long
#   define myoptind optind
#   define myoptarg optarg
#   define myoption option
#endif

static int run(char const *sequence, char const *file);

static void syntax(void);
static void version(void);
static void usage(void);

/*
 * Main program.
 */

int main(int argc, char *argv[])
{
    char const *sequence = "repeat(-1, fgetc(), feof(1))";
    int i;

    for (;;)
    {
#define OPTSTR "+x:lhV"
#define MOREINFO "Try `%s --help' for more information.\n"
        int option_index = 0;
        static struct myoption long_options[] =
        {
            { "execute",     1, NULL, 'x' },
            { "list",        0, NULL, 'l' },
            { "help",        0, NULL, 'h' },
            { "version",     0, NULL, 'V' },
            { NULL,          0, NULL,  0  }
        };
        int c = mygetopt(argc, argv, OPTSTR, long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
        case 'x': /* --execute */
            if (myoptarg[0] == '=')
                myoptarg++;
            sequence = myoptarg;
            break;
        case 'l': /* --list */
            syntax();
            return 0;
        case 'h': /* --help */
            usage();
            return 0;
        case 'V': /* --version */
            version();
            return 0;
        default:
            fprintf(stderr, "%s: invalid option -- %c\n", argv[0], c);
            printf(MOREINFO, argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (myoptind >= argc)
    {
        fprintf(stderr, "E: zzcat: too few arguments\n");
        return EXIT_FAILURE;
    }

    for (i = myoptind; i < argc; i++)
    {
        int ret = run(sequence, argv[i]);
        if (ret)
            return ret;
    }

    return EXIT_SUCCESS;
}

/*
 * Command intepreter
 */

#define MY_FOPEN(cmd) \
    do { \
        cmd; \
        if (!f) \
        { \
            fprintf(stderr, "E: zzcat: cannot open `%s'\n", file); \
            return EXIT_FAILURE; \
        } \
        retoff = 0; \
        sequence = strchr(sequence, ')') + 1; \
    } while(0)

#define MY_FCLOSE(cmd) \
    do { \
        cmd; \
        f = NULL; \
        sequence = strchr(sequence, ')') + 1; \
    } while(0)

#define MERGE(address, cnt, off) \
    do { \
        size_t _cnt = cnt, _off = off; \
        if (_cnt && retoff + _cnt > retlen) \
        { \
            retlen = retoff + _cnt; \
            retbuf = realloc(retbuf, retlen); \
        } \
        if (_cnt > 0) \
            memcpy(retbuf + retoff, address, _cnt); \
        retoff += _off; \
    } while(0)

#define MY_FREAD(cmd, buf, cnt) MY_FCALL(cmd, buf, cnt, cnt)
#define MY_FSEEK(cmd, off) MY_FCALL(cmd, /* unused */ "", 0, off)

#define MY_FCALL(cmd, buf, cnt, off) \
    do { \
        if (!f) \
        { \
            f = fopen(file, "r"); \
            if (!f) \
            { \
                fprintf(stderr, "E: zzcat: cannot open `%s'\n", file); \
                return EXIT_FAILURE; \
            } \
        } \
        /* fprintf(stderr, "debug: %s\n", #cmd); */ \
        cmd; \
        MERGE(buf, cnt, off); \
        sequence = strchr(sequence, ')') + 1; \
    } while(0)

#define MY_FEOF() \
    do { \
        if (!f) \
        { \
            f = fopen(file, "r"); \
            if (!f) \
            { \
                fprintf(stderr, "E: zzcat: cannot open `%s'\n", file); \
                return EXIT_FAILURE; \
            } \
        } \
        if (feof(f)) \
            feofs++; \
        if (feofs >= l1) \
            finish = 1; \
        sequence = strchr(sequence, ')') + 1; \
    } while(0)

/*
 * Command parser. We rewrite fmt by replacing the last character with
 * '%c' and check that the sscanf() call returns the expected number of
 * matches plus one (for the last character). We use this macro trick to
 * avoid using vsscanf() which does not exist on all platforms.
 */

struct parser
{
    char tmpfmt[1024], ch, lastch;
};

static int make_fmt(struct parser *p, char const *fmt)
{
    char const *tmp;
    size_t len;
    int ret = 0;

    len = strlen(fmt);
    p->lastch = fmt[len - 1];

    memcpy(p->tmpfmt, fmt, len - 1);
    p->tmpfmt[len - 1] = '%';
    p->tmpfmt[len] = 'c';
    p->tmpfmt[len + 1] = '\0';

    for (tmp = p->tmpfmt; *tmp; tmp++)
        if (*tmp == '%')
            tmp++, ret++;

    return ret;
}

#define PARSECMD(fmt, arg...) \
    (make_fmt(&parser, fmt) == sscanf(sequence, parser.tmpfmt, \
                                      ##arg, &parser.ch) \
         && parser.ch == parser.lastch)

/*
 * File reader. We parse a command line and perform all the operations it
 * contains on the specified file.
 */

static int run(char const *sequence, char const *file)
{
    struct { char const *p; int count; } loops[128];
    char *retbuf = NULL, *tmp;
    FILE *f = NULL;
    size_t retlen = 0, retoff = 0;
    int nloops = 0, fd = -1, feofs = 0, finish = 0;

    /* Allocate 32MB for our temporary buffer. Any larger value will crash. */
    tmp = malloc(32 * 1024 * 1024);

    while (*sequence)
    {
        struct parser parser;
        long int l1, l2;
        char *s, *lineptr = NULL;
        size_t k;
        ssize_t l;
        int n;
        char ch;

        (void)k;

        /* Ignore punctuation */
        if (strchr(" \t,;\r\n", *sequence))
            sequence++;

        /* Loop handling */
        else if (PARSECMD("repeat ( %li ,", &l1))
        {
            sequence = strchr(sequence, ',') + 1;
            loops[nloops].p = sequence;
            loops[nloops].count = l1;
            nloops++;
        }
        else if (PARSECMD(")"))
        {
            if (nloops == 0)
            {
                fprintf(stderr, "E: zzcat: ')' outside a loop\n");
                return EXIT_FAILURE;
            }
            if (loops[nloops - 1].count == 1 || finish)
            {
                nloops--;
                sequence = strchr(sequence, ')') + 1;
            }
            else
            {
                loops[nloops - 1].count--;
                sequence = loops[nloops - 1].p;
            }

            finish = 0;
        }

        /* FILE * opening functions */
        else if (PARSECMD("fopen ( )"))
            MY_FOPEN(f = fopen(file, "r"));
#if defined HAVE_FOPEN64
        else if (PARSECMD("fopen64 ( )"))
            MY_FOPEN(f = fopen64(file, "r"));
#endif
#if defined HAVE___FOPEN64
        else if (PARSECMD("__fopen64 ( )"))
            MY_FOPEN(f = __fopen64(file, "r"));
#endif
        else if (PARSECMD("freopen ( )"))
            MY_FOPEN(f = freopen(file, "r", f));
#if defined HAVE_FREOPEN64
        else if (PARSECMD("freopen64 ( )"))
            MY_FOPEN(f = freopen64(file, "r", f));
#endif
#if defined HAVE___FREOPEN64
        else if (PARSECMD("__freopen64 ( )"))
            MY_FOPEN(f = __freopen64(file, "r", f));
#endif

        /* FILE * EOF detection */
        else if (PARSECMD("feof ( %li )", &l1))
            MY_FEOF();

        /* FILE * closing functions */
        else if (PARSECMD("fclose ( )"))
            MY_FCLOSE(fclose(f));

        /* FILE * reading functions */
        else if (PARSECMD("fread ( %li , %li )", &l1, &l2))
            MY_FREAD(l = fread(tmp, l1, l2, f), tmp, l > 0 ? l * l1 : 0);
        else if (PARSECMD("getc ( )"))
            MY_FREAD(ch = (n = getc(f)), &ch, (n != EOF));
        else if (PARSECMD("fgetc ( )"))
            MY_FREAD(ch = (n = fgetc(f)), &ch, (n != EOF));
        else if (PARSECMD("fgets ( %li )", &l1))
            MY_FREAD(s = fgets(tmp, l1, f), tmp, s ? strlen(tmp) : 0);
#if defined HAVE__IO_GETC
        else if (PARSECMD("_IO_getc ( )"))
            MY_FREAD(ch = (n = _IO_getc(f)), &ch, (n != EOF));
#endif
#if defined HAVE_FREAD_UNLOCKED
        else if (PARSECMD("fread_unlocked ( %li , %li )", &l1, &l2))
            MY_FREAD(l = fread_unlocked(tmp, l1, l2, f), tmp, l > 0 ? l * l1 : 0);
#endif
#if defined HAVE_FGETS_UNLOCKED
        else if (PARSECMD("fgets_unlocked ( %li )", &l1))
            MY_FREAD(s = fgets_unlocked(tmp, l1, f), tmp, s ? strlen(tmp) : 0);
#endif
#if defined HAVE_GETC_UNLOCKED
        else if (PARSECMD("getc_unlocked ( )"))
            MY_FREAD(ch = (n = getc_unlocked(f)), &ch, (n != EOF));
#endif
#if defined HAVE_FGETC_UNLOCKED
        else if (PARSECMD("fgetc_unlocked ( )"))
            MY_FREAD(ch = (n = fgetc_unlocked(f)), &ch, (n != EOF));
#endif

        /* FILE * getdelim functions */
#if defined HAVE_GETLINE
        else if (PARSECMD("getline ( )"))
            MY_FREAD(l = getline(&lineptr, &k, f), lineptr, l >= 0 ? l : 0);
#endif
#if defined HAVE_GETDELIM
        else if (PARSECMD("getdelim ( '%c' )", &ch))
            MY_FREAD(l = getdelim(&lineptr, &k, ch, f), lineptr, l >= 0 ? l : 0);
        else if (PARSECMD("getdelim ( %i )", &n))
            MY_FREAD(l = getdelim(&lineptr, &k, n, f), lineptr, l >= 0 ? l : 0);
#endif
#if defined HAVE___GETDELIM
        else if (PARSECMD("__getdelim ( '%c' )", &ch))
            MY_FREAD(l = __getdelim(&lineptr, &k, ch, f), lineptr, l >= 0 ? l : 0);
        else if (PARSECMD("__getdelim ( %i )", &n))
            MY_FREAD(l = __getdelim(&lineptr, &k, n, f), lineptr, l >= 0 ? l : 0);
#endif

        /* FILE * seeking functions */
        else if (PARSECMD("fseek ( %li , SEEK_CUR )", &l1))
            MY_FSEEK(l = fseek(f, l1, SEEK_CUR),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("fseek ( %li , SEEK_SET )", &l1))
            MY_FSEEK(l = fseek(f, l1, SEEK_SET),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("fseek ( %li , SEEK_END )", &l1))
            MY_FSEEK(l = fseek(f, l1, SEEK_END),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
#if defined HAVE_FSEEKO
        else if (PARSECMD("fseeko ( %li , SEEK_CUR )", &l1))
            MY_FSEEK(l = fseeko(f, l1, SEEK_CUR),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("fseeko ( %li , SEEK_SET )", &l1))
            MY_FSEEK(l = fseeko(f, l1, SEEK_SET),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("fseeko ( %li , SEEK_END )", &l1))
            MY_FSEEK(l = fseeko(f, l1, SEEK_END),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
#endif
#if defined HAVE_FSEEKO64
        else if (PARSECMD("fseeko64 ( %li , SEEK_CUR )", &l1))
            MY_FSEEK(l = fseeko64(f, l1, SEEK_CUR),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("fseeko64 ( %li , SEEK_SET )", &l1))
            MY_FSEEK(l = fseeko64(f, l1, SEEK_SET),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("fseeko64 ( %li , SEEK_END )", &l1))
            MY_FSEEK(l = fseeko64(f, l1, SEEK_END),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
#endif
#if defined HAVE___FSEEKO64
        else if (PARSECMD("__fseeko64 ( %li , SEEK_CUR )", &l1))
            MY_FSEEK(l = __fseeko64(f, l1, SEEK_CUR),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("__fseeko64 ( %li , SEEK_SET )", &l1))
            MY_FSEEK(l = __fseeko64(f, l1, SEEK_SET),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
        else if (PARSECMD("__fseeko64 ( %li , SEEK_END )", &l1))
            MY_FSEEK(l = __fseeko64(f, l1, SEEK_END),
                     ftell(f) >= 0 ? ftell(f) - retoff : 0);
#endif
        else if (PARSECMD("rewind ( )"))
            MY_FSEEK(rewind(f), -retlen);
        else if (PARSECMD("ungetc ( )"))
            MY_FSEEK(if(retoff) ungetc((unsigned char)retbuf[retoff - 1], f),
                     retoff ? -1 : 0);

        /* Unrecognised sequence */
        else
        {
            char buf[16];
            snprintf(buf, 16, strlen(sequence) < 16 ? "%s" : "%.12s...",
                     sequence);
            fprintf(stderr, "E: zzcat: syntax error near `%s'\n", buf);
            return EXIT_FAILURE;
        }

        /* Clean up our mess */
        if (lineptr)
            free(lineptr);

        if (finish && !nloops)
            break;
    }

    if (f)
        fclose(f);

    if (fd >= 0)
        close(fd);

    fwrite(retbuf, retlen, 1, stdout);

    free(retbuf);
    free(tmp);

    return EXIT_SUCCESS;
}

#if 0
/* Only read() calls */
static int zzcat_read(char const *name, unsigned char *data, int64_t len,
                      int64_t chunk)
{
    int i, fd = open(name, O_RDONLY);
    if(fd < 0)
        return EXIT_FAILURE;
    for(i = 0; i < len; i += chunk)
        read(fd, data + i, chunk);
    close(fd);
    return EXIT_SUCCESS;
}

/* Socket seeks and reads */
static int zzcat_random_socket(char const *name, unsigned char *data,
                               int64_t len)
{
    int i, j, fd = open(name, O_RDONLY);
    if(fd < 0)
        return EXIT_FAILURE;
    for(i = 0; i < 128; i++)
    {
        lseek(fd, myrand() % len, SEEK_SET);
        for(j = 0; j < 4; j++)
            read(fd, data + lseek(fd, 0, SEEK_CUR), myrand() % 4096);
#ifdef HAVE_LSEEK64
        lseek64(fd, myrand() % len, SEEK_SET);
        for(j = 0; j < 4; j++)
            read(fd, data + lseek(fd, 0, SEEK_CUR), myrand() % 4096);
#endif
    }
    close(fd);
    return EXIT_SUCCESS;
}

/* Standard stream seeks and reads */
static int zzcat_random_stream(char const *name, unsigned char *data,
                               int64_t len)
{
    FILE *stream = fopen(name, "r");
    int i, j;
    if(!stream)
        return EXIT_FAILURE;
    for(i = 0; i < 128; i++)
    {
        long int now;
        fseek(stream, myrand() % len, SEEK_SET);
        for(j = 0; j < 4; j++)
            fread(data + ftell(stream),
                  myrand() % (len - ftell(stream)), 1, stream);
        fseek(stream, myrand() % len, SEEK_SET);
        now = ftell(stream);
        for(j = 0; j < 16; j++)
            data[now + j] = getc(stream);
        now = ftell(stream);
        for(j = 0; j < 16; j++)
            data[now + j] = fgetc(stream);
    }
    fclose(stream);
    return EXIT_SUCCESS;
}

#ifdef HAVE_MMAP
/* mmap() followed by random memory reads */
static int zzcat_random_mmap(char const *name, unsigned char *data,
                               int64_t len)
{
    int i, j, fd = open(name, O_RDONLY);
    if(fd < 0)
        return EXIT_FAILURE;
    for(i = 0; i < 128; i++)
    {
        char *map;
        int moff, mlen, pgsz = len + 1;
#ifdef HAVE_GETPAGESIZE
        pgsz = getpagesize();
#endif
        moff = len < pgsz ? 0 : (myrand() % (len / pgsz)) * pgsz;
        mlen = 1 + (myrand() % (len - moff));
        map = mmap(NULL, mlen, PROT_READ, MAP_PRIVATE, fd, moff);
        if(map == MAP_FAILED)
            return EXIT_FAILURE;
        for(j = 0; j < 128; j++)
        {
            int x = myrand() % mlen;
            data[moff + x] = map[x];
        }
        munmap(map, mlen);
    }
    close(fd);
    return EXIT_SUCCESS;
}
#endif
#endif

static char const *keyword_list[] =
{
    "repeat", "(<int>,<sequence>)", "loop <int> times through <sequence>",
    "feof", "(<int>)", "break out of loop or sequence after <int> EOFs",
    NULL
};

static char const *function_list[] =
{
    "fopen", "()", "open file",
#if defined HAVE_FOPEN64
    "fopen64", "()", "same as fopen()",
#endif
#if defined HAVE___FOPEN64
    "__fopen64", "()", "same as fopen()",
#endif
    "freopen", "()", "reopen file",
#if defined HAVE_FREOPEN64
    "freopen64", "()", "same as reopen()",
#endif
#if defined HAVE___FREOPEN64
    "__freopen64", "()", "same as reopen()",
#endif
    "fclose", "()", "close file",
    "fread", "(<inta>,<intb>)", "read <intb> chunks of <inta> bytes",
    "getc", "()", "get one character (can be a macro)",
    "fgetc", "()", "get one character",
    "fgets", "(<int>)", "read one line no longer than <int> bytes",
#if defined HAVE__IO_GETC
    "_IO_getc", "()", "get one character",
#endif
#if defined HAVE_FREAD_UNLOCKED
    "fread_unlocked", "(<inta>,<intb>)", "same as fread(), unlocked I/O version",
#endif
#if defined HAVE_FGETS_UNLOCKED
    "fgets_unlocked", "(<int>)", "same as fgets(), unlocked I/O version",
#endif
#if defined HAVE_GETC_UNLOCKED
    "getc_unlocked", "()", "same as getc(), unlocked I/O version",
#endif
#if defined HAVE_FGETC_UNLOCKED
    "fgetc_unlocked", "()", "same as fgetc(), unlocked I/O version",
#endif
#if defined HAVE_GETLINE
    "getline", "()", "read one complete line of text",
#endif
#if defined HAVE_GETDELIM
    "getdelim", "('<char>')", "read all data until delimiter character <char>",
    "getdelim", "(<int>)", "read all data until delimiter character <int>",
#endif
#if defined HAVE___GETDELIM
    "__getdelim", "('<char>')", "same as getdelim()",
    "__getdelim", "(<int>)", "same as getdelim()",
#endif
    "fseek", "(<int>,<whence>)", "seek using SEEK_CUR, SEEK_SET or SEEK_END",
#if defined HAVE_FSEEKO
    "fseeko", "(<int>,<whence>)", "same as fseek()",
#endif
#if defined HAVE_FSEEKO64
    "fseeko64", "(<int>,<whence>)", "same as fseek()",
#endif
#if defined HAVE___FSEEKO64
    "__fseeko64", "(<int>,<whence>)", "same as fseek()",
#endif
    "rewind", "()", "rewind to the beginning of the stream",
    "ungetc", "()", "put one byte back in the stream",
    NULL
};

static void print_list(char const **list)
{
    static char const spaces[] = "                                ";

    while (*list)
    {
        size_t len = printf("  %s%s", list[0], list[1]);
        if (len < strlen(spaces))
            printf("%s", spaces + len);
        printf("%s\n", list[2]);
        list += 3;
    }
}

static void syntax(void)
{
    printf("Available control keywords:\n");
    print_list(keyword_list);
    printf("\n");
    printf("Available functions:\n");
    print_list(function_list);
}

static void version(void)
{
    printf("zzcat %s\n", PACKAGE_VERSION);
    printf("Copyright (C) 2002-2010 Sam Hocevar <sam@hocevar.net>\n");
    printf("This program is free software. It comes without any warranty, to the extent\n");
    printf("permitted by applicable law. You can redistribute it and/or modify it under\n");
    printf("the terms of the Do What The Fuck You Want To Public License, Version 2, as\n");
    printf("published by Sam Hocevar. See <http://sam.zoy.org/wtfpl/> for more details.\n");
    printf("\n");
    printf("Written by Sam Hocevar. Report bugs to <sam@hocevar.net>.\n");
}

static void usage(void)
{
    printf("Usage: zzcat [-x sequence] [FILE...]\n");
    printf("       zzcat -l | --list\n");
    printf("       zzcat -h | --help\n");
    printf("       zzcat -V | --version\n");
    printf("Read FILE using a sequence of various I/O methods.\n");
    printf("\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");
    printf("  -x, --execute <sequence>  execute commands in <sequence>\n");
    printf("  -l, --list                list available program functions\n");
    printf("  -h, --help                display this help and exit\n");
    printf("  -V, --version             output version information and exit\n");
    printf("\n");
    printf("Written by Sam Hocevar. Report bugs to <sam@hocevar.net>.\n");
}


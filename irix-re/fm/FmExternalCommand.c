#include <stddef.h>
#include <string.h>

#include "FmExternalCommand.h"

static int exact(const char *s, const char *word)
{
    return strcmp(s, word) == 0;
}

static int prefixWithArgs(const char *s, const char *word, const char **args)
{
    size_t n = strlen(word);

    if(strncmp(s, word, n) != 0)
        return 0;

    if(s[n] == '\0')
    {
        *args = NULL;
        return 1;
    }

    if(s[n] != ' ' && s[n] != '\t')
        return 0;

    s += n;
    while(*s == ' ' || *s == '\t')
        ++s;

    *args = (*s != '\0') ? s : NULL;
    return 1;
}

int fmParseExternalCommand(const char *command, FmExternalCommand *out)
{
    const char *args = NULL;

    if(command == NULL || out == NULL)
        return 0;

    out->type = FM_EXT_UNKNOWN;
    out->raw = command;
    out->arguments = NULL;

    if(exact(command, "open"))
        out->type = FM_EXT_OPEN;
    else if(exact(command, "copy"))
        out->type = FM_EXT_COPY;
    else if(exact(command, "link") || exact(command, "reference"))
        out->type = FM_EXT_REFERENCE;
    else if(exact(command, "remove"))
        out->type = FM_EXT_REMOVE;
    else if(exact(command, "new directory"))
        out->type = FM_EXT_NEW_DIRECTORY;
    else if(exact(command, "print"))
        out->type = FM_EXT_PRINT;
    else if(exact(command, "startShare"))
        out->type = FM_EXT_START_SHARE;
    else if(exact(command, "stopShare"))
        out->type = FM_EXT_STOP_SHARE;
    else if(exact(command, "publish"))
        out->type = FM_EXT_PUBLISH;
    else if(exact(command, "permissions"))
        out->type = FM_EXT_PERMISSIONS;
    else if(exact(command, "get Info"))
        out->type = FM_EXT_GET_INFO;
    else if(exact(command, "fetch"))
        out->type = FM_EXT_FETCH;
    else if(exact(command, "restart"))
        out->type = FM_EXT_RESTART;
    else if(prefixWithArgs(command, "setenv", &args))
    {
        out->type = FM_EXT_SETENV;
        out->arguments = args;
    }
    else if(exact(command, "fetchHost"))
        out->type = FM_EXT_FETCH_HOST;
    else if(exact(command, "quit") || exact(command, "close"))
        out->type = FM_EXT_QUIT;
    else if(prefixWithArgs(command, "opendir", &args))
    {
        out->type = FM_EXT_OPEN_DIR;
        out->arguments = args;
    }
    else if(exact(command, "select all"))
        out->type = FM_EXT_SELECT_ALL;
    else if(exact(command, "empty dumpster"))
        out->type = FM_EXT_EMPTY_DUMPSTER;
    else if(prefixWithArgs(command, "dynamicMenuCmd", &args))
    {
        out->type = FM_EXT_DYNAMIC_MENU;
        out->arguments = args;
    }
    else if(exact(command, "shape pretty"))
        out->type = FM_EXT_SHAPE_PRETTY;
    else if(exact(command, "shape fast"))
        out->type = FM_EXT_SHAPE_FAST;
    else if(exact(command, "shape on"))
        out->type = FM_EXT_SHAPE_ON;
    else if(exact(command, "shape off"))
        out->type = FM_EXT_SHAPE_OFF;
    else
        return 0;

    return 1;
}

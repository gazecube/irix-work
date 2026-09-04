#ifndef IRIX_RE_FM_EXTERNAL_COMMAND_H
#define IRIX_RE_FM_EXTERNAL_COMMAND_H

/*
 * Portable command-dispatch boundary for the legacy
 * _SGI_DESKS_DESKTOP_COMMAND protocol.
 */

typedef enum FmExternalCommandType
{
    FM_EXT_UNKNOWN = 0,
    FM_EXT_OPEN,
    FM_EXT_COPY,
    FM_EXT_REFERENCE,
    FM_EXT_REMOVE,
    FM_EXT_NEW_DIRECTORY,
    FM_EXT_PRINT,
    FM_EXT_START_SHARE,
    FM_EXT_STOP_SHARE,
    FM_EXT_PUBLISH,
    FM_EXT_PERMISSIONS,
    FM_EXT_GET_INFO,
    FM_EXT_FETCH,
    FM_EXT_RESTART,
    FM_EXT_SETENV,
    FM_EXT_FETCH_HOST,
    FM_EXT_QUIT,
    FM_EXT_OPEN_DIR,
    FM_EXT_SELECT_ALL,
    FM_EXT_EMPTY_DUMPSTER,
    FM_EXT_DYNAMIC_MENU,
    FM_EXT_SHAPE_PRETTY,
    FM_EXT_SHAPE_FAST,
    FM_EXT_SHAPE_ON,
    FM_EXT_SHAPE_OFF
} FmExternalCommandType;

typedef struct FmExternalCommand
{
    FmExternalCommandType type;
    const char *raw;
    const char *arguments;
} FmExternalCommand;

/*
 * Parse an original IRIX FM external command string. `arguments` points into
 * the caller-owned input string and is NULL for commands without trailing
 * text. Returns 1 when a known command is recognized, otherwise 0.
 */
int fmParseExternalCommand(const char *command, FmExternalCommand *out);

#endif /* IRIX_RE_FM_EXTERNAL_COMMAND_H */

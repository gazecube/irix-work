from pathlib import Path

p = Path('reconstruction/PandoraViewKit.c++')
s = p.read_text()

s = s.replace('#include "Base/PwdNameList.h"\n', '#include "Base/PwdNameList.h"\n#include "Filesystem/DirectoryCheck.h"\n', 1)

old = '''bool linuxProbeInteractiveShell(const char *shell)\n{\n    if (!shell || !*shell)\n        return false;\n\n    const char *base = std::strrchr(shell, '/');\n    base = base ? base + 1 : shell;\n    return std::strcmp(base, "nologin") != 0 &&\n           std::strcmp(base, "false") != 0;\n}\n'''
if old not in s:
    raise SystemExit('linuxProbeInteractiveShell block not found')
s = s.replace(old, '', 1)

old = '''        if (!pwd->pw_dir || !*pwd->pw_dir)\n            continue;\n        if (pwd->pw_passwd && std::strcmp(pwd->pw_passwd, "*") == 0)\n            continue;\n\n        const PwdNameListEntry *entry = config.findEntry(pwd->pw_name);\n'''
new = '''        if (!pwd->pw_dir || !*pwd->pw_dir)\n            continue;\n\n        const PwdNameListEntry *entry = config.findEntry(pwd->pw_name);\n'''
if old not in s:
    raise SystemExit('locked-password filter block not found')
s = s.replace(old, new, 1)

old = '''        if (homeIsIgnored(pwd->pw_dir))\n            continue;\n        if (!homeIsDirectory(pwd->pw_dir))\n            continue;\n\n        if (!haveIrixConfig && !linuxProbeInteractiveShell(pwd->pw_shell))\n            continue;\n\n        /*\n'''
new = '''        if (homeIsIgnored(pwd->pw_dir))\n            continue;\n\n        /*\n         * Binary-proven createIconList() caller semantics:\n         *   3 -> reject\n         *   2 -> ordinary stat()/S_IFDIR fallback\n         *   0/1 -> continue\n         * checkDirectoryPortable() performs the state-2 stat fallback while\n         * preserving Linux filesystems as OTHER instead of relabelling them\n         * as the original IRIX LOCAL state.\n         */\n        const clogin::DirectoryState directoryState =\n            clogin::checkDirectoryPortable(pwd->pw_dir);\n        if (directoryState == clogin::kDirectoryMissing)\n            continue;\n\n        /*\n'''
if old not in s:
    raise SystemExit('home/shell filter block not found')
s = s.replace(old, new, 1)

p.write_text(s)

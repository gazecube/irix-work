from pathlib import Path

p = Path("clogin_reconstructed.h")
s = p.read_text()

old = '''static void clogin_add_shortcuts(Widget container)
{
\tstruct passwd *pw;
\tclogin_load_policy();
\tsetpwent();
\twhile((pw = getpwent()) != NULL)
\t{
\t\tif(!clogin_user_eligible(pw)) continue;
\t\tclogin_add_shortcut(container, pw,
\t\t\tstrcmp(pw->pw_name, "root") == 0 ? SHIRT_COLOR_ROOT : SHIRT_COLOR_USER);
\t}
\tendpwent();
}
'''

new = '''typedef struct
{
\tstruct passwd pw;
\tchar name[MAX_USERNAME_LEN + 1];
\tchar dir[MAXPATHLEN];
\tchar gecos[512];
} CloginPasswdSnapshot;

static void clogin_snapshot_passwd(CloginPasswdSnapshot *dst,
\tconst struct passwd *src, const char *name_override)
{
\tmemset(dst, 0, sizeof(*dst));
\tdst->pw = *src;
\tstrncpy(dst->name,
\t\tname_override != NULL ? name_override : (src->pw_name != NULL ? src->pw_name : ""),
\t\tMAX_USERNAME_LEN);
\tdst->name[MAX_USERNAME_LEN] = '\\0';
\tstrncpy(dst->dir, src->pw_dir != NULL ? src->pw_dir : "", MAXPATHLEN - 1);
\tdst->dir[MAXPATHLEN - 1] = '\\0';
\tstrncpy(dst->gecos, src->pw_gecos != NULL ? src->pw_gecos : "", sizeof(dst->gecos) - 1);
\tdst->gecos[sizeof(dst->gecos) - 1] = '\\0';
\tdst->pw.pw_name = dst->name;
\tdst->pw.pw_dir = dst->dir;
\tdst->pw.pw_gecos = dst->gecos;
}

static void clogin_restore_passwd_position(size_t seen)
{
\tsize_t i;
\tsetpwent();
\tfor(i = 0; i < seen; ++i)
\t{
\t\tif(getpwent() == NULL) break;
\t}
}

static void clogin_add_shortcuts(Widget container)
{
\tstruct passwd *pw;
\tsize_t seen = 0;

\tclogin_load_policy();
\tsetpwent();
\twhile((pw = getpwent()) != NULL)
\t{
\t\tconst struct passwd *candidate = pw;
\t\tCloginPasswdSnapshot snap;
\t\tconst char *name;

\t\t++seen;
\t\tif(pw->pw_name == NULL || pw->pw_name[0] == '\\0') continue;

\t\t/* Binary-backed createIconList() behavior: a leading '-' record
\t\t * terminates this enumeration routine. */
\t\tif(pw->pw_name[0] == '-') break;

\t\tname = pw->pw_name[0] == '+' ? pw->pw_name + 1 : pw->pw_name;

\t\t/* Empty-home records are resolved by name.  The original copies the
\t\t * result before rewinding passwd enumeration to its previous count. */
\t\tif(pw->pw_dir == NULL || pw->pw_dir[0] == '\\0')
\t\t{
\t\t\tstruct passwd *resolved = getpwnam(name);
\t\t\tif(resolved == NULL)
\t\t\t{
\t\t\t\tclogin_restore_passwd_position(seen);
\t\t\t\tbreak;
\t\t\t}
\t\t\tif(resolved->pw_name != NULL && resolved->pw_name[0] == '+')
\t\t\t\tclogin_snapshot_passwd(&snap, resolved, resolved->pw_name + 1);
\t\t\telse
\t\t\t\tclogin_snapshot_passwd(&snap, resolved, NULL);
\t\t\tcandidate = &snap.pw;
\t\t\tclogin_restore_passwd_position(seen);
\t\t}
\t\telse if(pw->pw_name[0] == '+')
\t\t{
\t\t\tclogin_snapshot_passwd(&snap, pw, name);
\t\t\tcandidate = &snap.pw;
\t\t}

\t\tif(!clogin_user_eligible(candidate)) continue;
\t\tclogin_add_shortcut(container, candidate,
\t\t\tstrcmp(candidate->pw_name, "root") == 0 ? SHIRT_COLOR_ROOT : SHIRT_COLOR_USER);
\t}
\tendpwent();
}
'''

if old not in s:
    raise SystemExit("target clogin_add_shortcuts block not found")

p.write_text(s.replace(old, new, 1))

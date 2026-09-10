/* Recovered IRIX clogin one-shot autologin bridge. */
#ifndef DVLOGIN_CLOGIN_AUTOLOGIN_H
#define DVLOGIN_CLOGIN_AUTOLOGIN_H

#define CLOGIN_AUTOLOGIN_MARKER "/etc/autologin.on"
#define CLOGIN_AUTOLOGIN_USER   "/etc/autologin"

/*
 * Mirrors pandora::checkAutoLogin():
 *   access(marker)
 *   unlink(marker)             -- consumed before username validation
 *   fopen(user file)
 *   fscanf("%s", username)
 *   getpwnam(username)
 *
 * Returns 1 only for a valid passwd entry.  The marker stays consumed on all
 * failures after the initial access(), matching the original quirk.
 */
static int clogin_check_autologin(char *username_out, size_t username_len)
{
	FILE *fp;
	char name[MAX_USERNAME_LEN + 1];
	struct passwd *pw;

	if(username_out == NULL || username_len == 0) return 0;
	username_out[0] = '\0';

	if(access(CLOGIN_AUTOLOGIN_MARKER, F_OK) != 0) return 0;
	(void)unlink(CLOGIN_AUTOLOGIN_MARKER);

	fp = fopen(CLOGIN_AUTOLOGIN_USER, "r");
	if(fp == NULL) return 0;
	name[0] = '\0';
	if(fscanf(fp, "%64s", name) != 1)
	{
		fclose(fp);
		return 0;
	}
	fclose(fp);

	pw = getpwnam(name);
	if(pw == NULL) return 0;

	strncpy(username_out, pw->pw_name, username_len - 1);
	username_out[username_len - 1] = '\0';
	return 1;
}

#endif /* DVLOGIN_CLOGIN_AUTOLOGIN_H */

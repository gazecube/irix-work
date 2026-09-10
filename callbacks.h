
void addCustomShortcuts(Widget container)
{
	if(enableRootShortcut)
	{
		add_root_shortcut(container);
	}
	
	add_shortcut_internal(container, "demo", NULL, SHIRT_COLOR_DEMO);
	add_user_shortcut(container, "user", NULL);
}


void toggleHostnameCB(Widget w, XtPointer client_data, XtPointer call_data)
{
	if(XmToggleButtonGetState(w))
	{
		XtVaSetValues(hostLabel, XmNmappedWhenManaged, 1, NULL);
	}
	else
	{
		XtVaSetValues(hostLabel, XmNmappedWhenManaged, 0, NULL);
	}
	
	XmUpdateDisplay(hostLabel);
}


/* Shadow password buffer */
static char g_shadow_password[MAX_PASSWORD_LEN + 1];
static int g_shadow_len = 0;
static Boolean g_pw_internal = False;


static void clear_password_field(void)
{
	g_pw_internal = True;
	memset(g_shadow_password, 0, sizeof(g_shadow_password));
	g_shadow_len = 0;
	XmTextSetString(textBox2, "");
	g_pw_internal = False;
}


void passwordModifyVerifyCB(Widget w, XtPointer client_data, XtPointer call_data)
{
	XmTextVerifyCallbackStruct *cbs = (XmTextVerifyCallbackStruct *)call_data;
	int start, end, del_len, new_len, result_len, i;

	if(g_pw_internal) { return; }

	start   = cbs->startPos;
	end     = cbs->endPos;
	del_len = end - start;
	new_len = (cbs->text != NULL) ? cbs->text->length : 0;

	result_len = g_shadow_len - del_len + new_len;
	if(result_len > MAX_PASSWORD_LEN)
	{
		cbs->doit = False;
		return;
	}

	if(del_len > 0)
	{
		memmove(&g_shadow_password[start],
			&g_shadow_password[end],
			g_shadow_len - end + 1);
		g_shadow_len -= del_len;
	}

	if(new_len > 0 && cbs->text->ptr != NULL)
	{
		memmove(&g_shadow_password[start + new_len],
			&g_shadow_password[start],
			g_shadow_len - start + 1);
		memcpy(&g_shadow_password[start], cbs->text->ptr, new_len);
		g_shadow_len += new_len;
		g_shadow_password[g_shadow_len] = '\0';

		for(i = 0; i < new_len; i++)
		{
			cbs->text->ptr[i] = '*';
		}
	}
}


void quitNow()
{
	_exit(0);
}


void verifySuperUser()
{
	if(getuid() != 0)
	{
		fprintf(stderr, "\n Login manager must be run as UID 0 (root).\n\n");
		fflush(stderr);
		exit(1);
	}
}


void optButtonCB()
{
	if(XmToggleButtonGetState(optionsButton) == 1)
	{
		XtVaSetValues(scrolledWin, XmNmappedWhenManaged, 1, NULL);
	}
	else
	{
		XtVaSetValues(scrolledWin, XmNmappedWhenManaged, 0, NULL);
	}
}


void mouseOverLoginPanel(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
	Display *display = XtDisplay(widget);
	Cursor handCursor = XCreateFontCursor(display, XC_left_ptr);
	XDefineCursor(display, XtWindow(widget), handCursor);
	XFreeCursor(display, handCursor);
}


void mouseOverPushButton(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
	Display *display = XtDisplay(widget);
	Cursor handCursor = XCreateFontCursor(display, XC_hand1);
	XDefineCursor(display, XtWindow(widget), handCursor);
	XFreeCursor(display, handCursor);
}


void mouseLeftPushButton(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
	Display *display = XtDisplay(widget);
	XUndefineCursor(display, XtWindow(widget));
}


void lockWidgets()
{
	XtVaSetValues(textBox1, XmNmappedWhenManaged, 0, NULL);
	XtVaSetValues(textBox2, XmNmappedWhenManaged, 0, NULL);
}

void unlockWidgets()
{
	XtVaSetValues(textBox1, XmNmappedWhenManaged, 1, NULL);
	XtVaSetValues(textBox2, XmNmappedWhenManaged, 1, NULL);
}


static Boolean is_valid_username_char(char c)
{
	return (isalnum(c) || c == '_' || c == '-');
}


void textInputMotionCB(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	char *text;
	int max_len;
	Boolean (*validator)(char) = NULL;

	/* Use widget identity, not instance names. This permits both the old
	 * prototype names and the original Pandora userText/passwordText names. */
	if(widgetName == textBox1)
	{
		max_len = MAX_USERNAME_LEN;
		validator = is_valid_username_char;
	}
	else if(widgetName == textBox2)
	{
		max_len = MAX_PASSWORD_LEN;
		validator = NULL;
	}
	else
	{
		return;
	}

	text = XmTextGetString(widgetName);

	if(text != NULL)
	{
		int len = strlen(text);
		Boolean modified = False;

		if(len > max_len)
		{
			text[max_len] = '\0';
			modified = True;
		}

		if(validator != NULL)
		{
			int i, j = 0;
			for(i = 0; text[i] != '\0'; i++)
			{
				if(validator(text[i]))
				{
					text[j++] = text[i];
				}
				else
				{
					modified = True;
				}
			}
			text[j] = '\0';
		}

		if(modified)
		{
			XmTextSetString(widgetName, text);
			XmTextSetInsertionPosition(widgetName,
				XmTextGetLastPosition(widgetName));
		}

		XtFree(text);
	}
}


struct pam_response_data
{
	char *username;
	char *password;
};


static int pam_conversation(int num_msg, const struct pam_message **msg,
	struct pam_response **resp, void *appdata_ptr)
{
	struct pam_response_data *creds = (struct pam_response_data *)appdata_ptr;
	struct pam_response *reply;
	int i;

	if(num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG)
	{
		return PAM_CONV_ERR;
	}

	reply = (struct pam_response *)calloc(num_msg, sizeof(struct pam_response));
	if(reply == NULL) { return PAM_BUF_ERR; }

	for(i = 0; i < num_msg; i++)
	{
		reply[i].resp_retcode = 0;
		reply[i].resp = NULL;

		switch(msg[i]->msg_style)
		{
			case PAM_PROMPT_ECHO_OFF:
				if(creds->password)
				{
					reply[i].resp = strdup(creds->password);
				}
				break;

			case PAM_PROMPT_ECHO_ON:
				if(creds->username)
				{
					reply[i].resp = strdup(creds->username);
				}
				break;

			case PAM_ERROR_MSG:
			case PAM_TEXT_INFO:
				break;

			default:
				goto fail;
		}
	}

	*resp = reply;
	return PAM_SUCCESS;

fail:
	for(i = 0; i < num_msg; i++)
	{
		if(reply[i].resp)
		{
			memset(reply[i].resp, 0, strlen(reply[i].resp));
			free(reply[i].resp);
		}
	}
	free(reply);
	return PAM_CONV_ERR;
}


/*
 * Host-side authentication diagnostics only. Keep this separate from the
 * reconstructed SGI behavior and never record password data.
 */
static void auth_trace_stage(const char *username, const char *stage,
	pam_handle_t *pamh, int retval)
{
	FILE *fp = fopen("/tmp/dvlogin-auth.log", "a");
	if(fp == NULL) { return; }

	fprintf(fp, "user=%s stage=%s code=%d message=%s\n",
		username ? username : "",
		stage ? stage : "",
		retval,
		pam_strerror(pamh, retval));
	fclose(fp);
}


int authenticateLogin(char *username, char *password)
{
	pam_handle_t *pamh = NULL;
	struct pam_response_data creds;
	struct pam_conv conv;
	int retval;

	fprintf(stdout, "Attempting auth for user: %s\n", username);
	fflush(stdout);

	creds.username = username;
	creds.password = password;

	conv.conv = pam_conversation;
	conv.appdata_ptr = &creds;

	retval = pam_start("login", username, &conv, &pamh);
	auth_trace_stage(username, "pam_start", pamh, retval);
	if(retval != PAM_SUCCESS)
	{
		fprintf(stderr, "PAM start failed: %s\n", pam_strerror(pamh, retval));
		return 0;
	}

	retval = pam_authenticate(pamh, 0);
	auth_trace_stage(username, "pam_authenticate", pamh, retval);
	if(retval != PAM_SUCCESS)
	{
		fprintf(stderr, "Authentication failed: %s\n", pam_strerror(pamh, retval));
		pam_end(pamh, retval);
		sleep(2);
		return 0;
	}

	retval = pam_acct_mgmt(pamh, 0);
	auth_trace_stage(username, "pam_acct_mgmt", pamh, retval);
	if(retval != PAM_SUCCESS)
	{
		fprintf(stderr, "Account validation failed: %s\n", pam_strerror(pamh, retval));
		pam_end(pamh, retval);
		return 0;
	}

	fprintf(stdout, "Authentication successful for user: %s\n", username);
	fflush(stdout);

	pam_end(pamh, PAM_SUCCESS);
	memset(password, 0, strlen(password));
	return 1;
}


static void auth_success_exit(const char *username)
{
	strncpy(g_auth_username, username, MAX_USERNAME_LEN);
	g_auth_username[MAX_USERNAME_LEN] = '\0';
	XtAppSetExitFlag(app);
}


void textInputActivatedCB(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	if(widgetName == textBox1)
	{
		XmProcessTraversal(textBox2, XmTRAVERSE_CURRENT);
	}
	else if(widgetName == textBox2)
	{
		char *username;
		char *password;

		lockWidgets();

		/* Read the bound username widget directly. Pandora intentionally uses
		 * the original instance name userText, so name-based lookup is wrong. */
		username = XmTextGetString(textBox1);
		password = strdup(g_shadow_password);

		if(username != NULL && strlen(username) > 0 &&
			password != NULL && strlen(password) > 0)
		{
			if(authenticateLogin(username, password))
			{
				clear_password_field();
				auth_success_exit(username);

				if(username) { XtFree(username); }
				if(password) { free(password); }
				return;
			}
			else
			{
				clear_password_field();
			}
		}
		else
		{
			fprintf(stdout, "\nA username and password are required.\n");
			fflush(stdout);
		}

		if(username) { XtFree(username); }
		if(password)
		{
			memset(password, 0, strlen(password));
			free(password);
		}

		unlockWidgets();
	}
}


void loginButtonCB(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	char *username = XmTextGetString(textBox1);
	char *password = strdup(g_shadow_password);

	if(username == NULL || strlen(username) <= 0 ||
		password == NULL || strlen(password) <= 0)
	{
		fprintf(stdout, "\nA username and password are required.\n");
		fflush(stdout);
	}
	else
	{
		if(authenticateLogin(username, password))
		{
			clear_password_field();
			auth_success_exit(username);

			if(username) { XtFree(username); }
			if(password) { free(password); }
			return;
		}
		else
		{
			clear_password_field();
		}
	}

	if(username) { XtFree(username); }
	if(password)
	{
		memset(password, 0, strlen(password));
		free(password);
	}
}


void textWidgetGainedFocus(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	XtVaSetValues(widgetName, XmNcursorPositionVisible, 1, NULL);
}

void textWidgetLostFocus(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	XtVaSetValues(widgetName, XmNcursorPositionVisible, 0, NULL);
}

void mouseOverTextInput(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
	Display *display = XtDisplay(widget);
	Cursor handCursor = XCreateFontCursor(display, XC_xterm);
	XDefineCursor(display, XtWindow(widget), handCursor);
	XFreeCursor(display, handCursor);
}


void mouseLeftTextInput(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
	Display *display = XtDisplay(widget);
	XUndefineCursor(display, XtWindow(widget));
}

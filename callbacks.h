
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


/*	Shadow password buffer

	The real password is always stored here. When masking is
	active, the XmText widget displays asterisks while the
	shadow buffer holds the actual characters.
*/
static char g_shadow_password[MAX_PASSWORD_LEN + 1];
static int g_shadow_len = 0;
static Boolean g_mask_password = True;   /* True = show asterisks */
static Boolean g_pw_internal = False;    /* suppresses modifyVerify during programmatic updates */


/*	clear_password_field
	
	zeroes the shadow buffer and clears the textBox2 
	widget without triggering the odifyVerify callback.
*/
static void clear_password_field(void)
{
	g_pw_internal = True;
	memset(g_shadow_password, 0, sizeof(g_shadow_password));
	g_shadow_len = 0;
	XmTextSetString(textBox2, "");
	g_pw_internal = False;
}


/*	passwordModifyVerifyCB
	
	Fires BEFORE a change is applied to the widget. Keeps the
	shadow buffer in sync with every insert/delete, and replaces
	displayed characters with '*' when masking is active.
*/
void passwordModifyVerifyCB(Widget w, XtPointer client_data, XtPointer call_data)
{
	XmTextVerifyCallbackStruct *cbs = (XmTextVerifyCallbackStruct *)call_data;
	int start, end, del_len, new_len, result_len, i;

	/* skip processing during our own programmatic updates */
	if(g_pw_internal) { return; }

	start   = cbs->startPos;
	end     = cbs->endPos;
	del_len = end - start;
	new_len = (cbs->text != NULL) ? cbs->text->length : 0;

	/* enforce length limit */
	result_len = g_shadow_len - del_len + new_len;
	if(result_len > MAX_PASSWORD_LEN)
	{
		cbs->doit = False;
		return;
	}

	/* remove deleted range from shadow buffer */
	if(del_len > 0)
	{
		memmove(&g_shadow_password[start],
			&g_shadow_password[end],
			g_shadow_len - end + 1);
		g_shadow_len -= del_len;
	}

	/* insert new characters into shadow buffer */
	if(new_len > 0 && cbs->text->ptr != NULL)
	{
		memmove(&g_shadow_password[start + new_len],
			&g_shadow_password[start],
			g_shadow_len - start + 1);
		memcpy(&g_shadow_password[start], cbs->text->ptr, new_len);
		g_shadow_len += new_len;
		g_shadow_password[g_shadow_len] = '\0';

		/* replace displayed characters with asterisks */
		if(g_mask_password)
		{
			for(i = 0; i < new_len; i++)
			{
				cbs->text->ptr[i] = '*';
			}
		}
	}
}


/*	togglePasswordCB

	"Use plain text password" toggle:
	checked   (set=1) -> plain text
	unchecked (set=0) -> asterisks
*/
void togglePasswordCB(Widget w, XtPointer client_data, XtPointer call_data)
{
	g_mask_password = (XmToggleButtonGetState(w) == 0);

	/* re-render the password field to match the new mode */
	g_pw_internal = True;

	if(g_mask_password)
	{
		/* build a string of asterisks matching the real password length */
		char stars[MAX_PASSWORD_LEN + 1];
		int i;
		for(i = 0; i < g_shadow_len; i++) { stars[i] = '*'; }
		stars[g_shadow_len] = '\0';
		XmTextSetString(textBox2, stars);
	}
	else
	{
		/* show the real password */
		XmTextSetString(textBox2, g_shadow_password);
	}

	XmTextSetInsertionPosition(textBox2, XmTextGetLastPosition(textBox2));
	g_pw_internal = False;
}



void quitNow()
{
/*
	In the forked GUI child, just exit.
	g_auth_username will be empty so the parent
	knows no auth occurred.
*/
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
/* get XtDisplay associated with the calling widget */
	Display *display = XtDisplay(widget);

/* set up custom cursor */
	Cursor handCursor = XCreateFontCursor(display, XC_left_ptr);

/* change cursor */
	XDefineCursor(display, XtWindow(widget), handCursor);

/* clean up */
	XFreeCursor(display, handCursor);
}




void mouseOverPushButton(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
/* get XtDisplay associated with the calling widget */
	Display *display = XtDisplay(widget);

/* set up custom cursor */
	Cursor handCursor = XCreateFontCursor(display, XC_hand1);

/* change cursor */
	XDefineCursor(display, XtWindow(widget), handCursor);

/* clean up */
	XFreeCursor(display, handCursor);
}


void mouseLeftPushButton(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
/* get XtDisplay associated with the calling widget */
	Display *display = XtDisplay(widget);

/* revert to default cursor */
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






/* Helper function to validate username characters */
static Boolean is_valid_username_char(char c)
{
	return (isalnum(c) || c == '_' || c == '-');
}

/* Check for valid chars and limit while typing */
void textInputMotionCB(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	char *widgetNameStr = XtName(widgetName);
	char *text;
	int max_len;
	Boolean (*validator)(char) = NULL;

	/* Determine which widget and its constraints */
	if(strcmp(widgetNameStr, "textBox1") == 0)
	{
		max_len = MAX_USERNAME_LEN;
		validator = is_valid_username_char;
	}
	else if(strcmp(widgetNameStr, "textBox2") == 0)
	{
		max_len = MAX_PASSWORD_LEN;
		validator = NULL; /* Allow any chars in password */
	}
	else
	{
		return;
	}

	/* Get current text */
	text = XmTextGetString(widgetName);

	if(text != NULL)
	{
		int len = strlen(text);
		Boolean modified = False;

		/* Check length limit */
		if(len > max_len)
		{
			text[max_len] = '\0';
			modified = True;
		}

		/* Validate characters (username only) */
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

		/* Update widget if we modified the text */
		if(modified)
		{
			XmTextSetString(widgetName, text);
			XmTextSetInsertionPosition(widgetName,
				XmTextGetLastPosition(widgetName));
		}

		XtFree(text);
	}
}




/* Structure to pass credentials to PAM conversation function */
struct pam_response_data
{
	char *username;
	char *password;
};

/* PAM conversation function - handles password prompts */
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
			case PAM_PROMPT_ECHO_OFF:  /* Password prompt */
				if(creds->password)
				{
					reply[i].resp = strdup(creds->password);
				}
				break;

			case PAM_PROMPT_ECHO_ON:   /* Username prompt */
				if(creds->username)
				{
					reply[i].resp = strdup(creds->username);
				}
				break;

			case PAM_ERROR_MSG:
			case PAM_TEXT_INFO:
				/* Just acknowledge info/error messages */
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

/* Returns 1 on success, 0 on failure */
int authenticateLogin(char *username, char *password)
{
	pam_handle_t *pamh = NULL;
	struct pam_response_data creds;
	struct pam_conv conv;
	int retval;

	fprintf(stdout, "Attempting auth for user: %s\n", username);
	fflush(stdout);

	/* Set up credentials */
	creds.username = username;
	creds.password = password;

	/* Set up PAM conversation */
	conv.conv = pam_conversation;
	conv.appdata_ptr = &creds;

	/* Initialize PAM */
	retval = pam_start("login", username, &conv, &pamh);
	if(retval != PAM_SUCCESS)
	{
		fprintf(stderr, "PAM start failed: %s\n", pam_strerror(pamh, retval));
		return 0;
	}

	/* Authenticate the user */
	retval = pam_authenticate(pamh, 0);
	if(retval != PAM_SUCCESS)
	{
		fprintf(stderr, "Authentication failed: %s\n", pam_strerror(pamh, retval));
		pam_end(pamh, retval);

		/* Add delay to prevent brute force */
		sleep(2);
		return 0;
	}

	/* Check account validity (expired password, etc.) */
	retval = pam_acct_mgmt(pamh, 0);
	if(retval != PAM_SUCCESS)
	{
		fprintf(stderr, "Account validation failed: %s\n", pam_strerror(pamh, retval));
		pam_end(pamh, retval);
		return 0;
	}

	/* Success! */
	fprintf(stdout, "Authentication successful for user: %s\n", username);
	fflush(stdout);

	/* Clean up */
	pam_end(pamh, PAM_SUCCESS);

	/* Clear password from memory */
	memset(password, 0, strlen(password));

	return 1;
}


/*	auth_success_exit - common handler for successful authentication.
	
	Stores the username in g_auth_username and signals
	XtAppMainLoop to return so the GUI child can exit
	cleanly and pass the username back via pipe.
*/
static void auth_success_exit(const char *username)
{
	strncpy(g_auth_username, username, MAX_USERNAME_LEN);
	g_auth_username[MAX_USERNAME_LEN] = '\0';
	XtAppSetExitFlag(app);
}


/* Runs when enter key is pressed */
void textInputActivatedCB(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	char *widgetNameStr = XtName(widgetName);

	if(strcmp(widgetNameStr, "textBox1") == 0)
	{
		/* username field - just focus the password field */
		XmProcessTraversal(textBox1, XmTRAVERSE_NEXT_TAB_GROUP);
	}
	else if(strcmp(widgetNameStr, "textBox2") == 0)
	{
		lockWidgets();

		/* get username from widget, password from shadow buffer */
		char *username = XmTextGetString(
			XtNameToWidget(XtParent(widgetName), "textBox1"));
		char *password = strdup(g_shadow_password);

		if(username != NULL && strlen(username) > 0 &&
			password != NULL && strlen(password) > 0)
		{
			if(authenticateLogin(username, password))
			{
				/* success clear sensitive data and hand off */
				clear_password_field();
				auth_success_exit(username);

				if(username) { XtFree(username); }
				if(password) { free(password); }
				return;
			}
			else
			{
				/* Failed - clear password field */
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
			/* success - clear sensitive data and hand off */
			clear_password_field();
			auth_success_exit(username);

			if(username) { XtFree(username); }
			if(password) { free(password); }
			return;
		}
		else
		{
			/* Failed - clear password field */
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
	/* enable visible cursor for insert position */
	XtVaSetValues(widgetName, XmNcursorPositionVisible, 1, NULL);
}

void textWidgetLostFocus(Widget widgetName, XtPointer client_data, XtPointer call_data)
{
	/* disables insert cursor */
	XtVaSetValues(widgetName, XmNcursorPositionVisible, 0, NULL);
}

void mouseOverTextInput(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
/* get XtDisplay associated with the calling widget */
	Display *display = XtDisplay(widget);

/* set up custom cursor */
	Cursor handCursor = XCreateFontCursor(display, XC_xterm);

/* change cursor */
	XDefineCursor(display, XtWindow(widget), handCursor);

/* clean up */
	XFreeCursor(display, handCursor);
}


void mouseLeftTextInput(Widget widget, XtPointer client_data, XEvent *event, Boolean *none)
{
/* get XtDisplay associated with the calling widget */
	Display *display = XtDisplay(widget);

/* revert to default cursor */
	XUndefineCursor(display, XtWindow(widget));
}

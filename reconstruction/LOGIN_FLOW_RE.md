# selectCB / doLogin / LoginTheUser reconstruction

Targets:

- `PandoraComponent::selectCB(...)` at `0x100111d8`
- `PandoraComponent::doLogin()` at `0x10010d14`
- `pandora::doLogin(char *, char *, char *)` at `0x100145d8`
- `pandora::doubleClicked(char *, char *)` at `0x10014704`
- `pandora::LoginTheUser(xpasswd *, char *)` at `0x10014830`

## Selection path

`PandoraComponent::selectCB()` obtains the current selection from the Imd icon panel with `getSelectedViewList()` and dynamic-casts the selected view/model.

When a user is selected it:

1. extracts the selected user's `ImdString` login name,
2. writes that login name into the Motif username field with `XmTextSetString`,
3. stores a duplicated copy in `PandoraComponent +0x60`,
4. calls `pandora::icSelected(name)`,
5. reveals/manages the appropriate password UI when needed,
6. checks `alreadyKnown(name)` and presents the selected user's picture/help state.

Selection and command/double-click notifications are separate Imd callback channels in the original binary.

## Component doLogin()

`PandoraComponent::doLogin()` first calls the original `getTypedName()`.

If a name is present it calls:

```text
pandora::doLogin(username, password, alsoTypedBuffer)
```

using component fields around:

```text
+0x58 pandora/controller pointer
+0x60 selected/typed username
+0x64 password string
+0x6c additional typed input buffer
```

If no name is present, the component clears both text widgets and restores the normal instructional login message instead of attempting authentication.

Both the Login button callback and the password-text activate callback call this same `PandoraComponent::doLogin()` routine.

## pandora::doLogin()

Recovered behavior at `0x100145d8`:

1. Ignore an empty username.
2. `handleAlsoTyped()` processes text typed while the UI was transitioning.
3. Temporarily changes the private passwd-enumeration globals used by the IRIX libc lookup path.
4. `getpwnam(username)` obtains the account record.
5. `pwd0_copyPwd()` copies the returned passwd record into stable storage.
6. If an account exists and a password was supplied, call `LoginTheUser(copy, password)`.
7. If an account exists, no password was supplied, and `copy->pw_passwd` is empty, call `LoginTheUser(copy, NULL)`.
8. If the account exists with a non-empty stored password and the password field is not already enabled, call `enablePwdField(1)`.
9. If the account does not exist, preserve the same password-prompt behavior before later generic failure rather than immediately disclosing account validity.

The `strlen(copy->pw_passwd)` test is proven by the MIPS GOT: the call through GOT offset `-31720` resolves to `strlen`.

## doubleClicked()

Recovered behavior at `0x10014704` uses the same account-copy and stored-password decision:

```text
resolve selected login
  -> getpwnam()
  -> pwd0_copyPwd()
  -> strlen(copy->pw_passwd)

empty stored passwd
  -> LoginTheUser(copy, NULL)

non-empty stored passwd
  -> if pandora byte +0 already set: return
  -> otherwise enablePwdField(1)
```

`pandora::enablePwdField(int)` at `0x10014e84` forwards the requested state to `PandoraComponent::enablePwdField(int)` and stores the same value as a byte at `pandora +0x00`. This identifies the byte tested by `doubleClicked()` as the password-field-enabled state.

## LoginTheUser()

`LoginTheUser()` is now mapped directly from the shipped binary.

### Home-directory gate

1. `checkDirectory(pw_dir)` is called first.
2. State `2` performs the same ordinary `stat()` fallback seen in account enumeration.
3. A failed fallback or non-directory target reports `err_noHomeDir`.
4. State `3` is a definite missing/not-directory rejection.

### Password-policy gate

The original then calls `strlen(pw_passwd)`, `isMandPass()`, and `isPassReq()`.

- If `isMandPass()` is true and the stored passwd field is empty, the original uses the `err_badUser` information path.
- If `isPassReq()` is true and the stored passwd field is empty, it calls `CreateGetNewPwDialog()`.
- Otherwise it proceeds to `pwd0_checkPwd(passwd, suppliedPassword)`.

### Exact pwd0_checkPwd result map

The return-code dispatch in `LoginTheUser()` is binary-proven:

```text
0   -> authentication success path
1   -> failure(err_pwdIncorrect)
2   -> CreatePwAgeDialog(...)
3   -> failure(err_loginRestricted)
14  -> failure(err_loginInactive)
other -> clear/reset the associated failure-state word and return
```

The dependency-free reconstruction of this branch map lives in:

```text
reconstruction/Login/LoginTheUserModel.h
reconstruction/Login/LoginTheUserModel.c++
```

### Success path

For `pwd0_checkPwd() == 0`, the binary performs:

```text
setupEnvironment(passwd, ...)
  -> environment vector preparation

X access/session preparation
  -> XAddHosts(...) when host entries are present
  -> XDisableAccessControl(...) when no explicit host list is present
  -> XSync(...)

reportSuccess(passwd)
```

`reportSuccess()` writes the prepared success/environment payload through the program's output stream, flushes it, and exits with status 0. This is the original greeter-to-parent/session-manager handoff; `LoginTheUser()` itself is not where Linux-style `setuid()`/`setgid()` session spawning occurs.

This corrects an earlier assumption that the tail of `LoginTheUser()` was probably a direct credential-drop sequence. The GOT resolves those calls to Xlib operations instead.

## Linux integration boundary

The original IRIX authentication backend depends on `pwd0_*`, IRIX password policy, and the parent login-manager protocol. Those are platform-specific.

For the Linux port, preserve the recovered Pandora/UI and decision structure but isolate the host replacement at the authentication/session boundary:

```text
Pandora UI/control flow (binary-backed)
  -> host authentication adapter (PAM on Linux)
  -> existing privileged session launcher
```

The existing runnable C prototype already has a PAM authentication implementation and a separate session launcher. Reusing those as explicitly marked host glue is acceptable; they must not be represented as original SGI behavior.

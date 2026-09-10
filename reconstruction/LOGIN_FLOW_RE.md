# selectCB / doLogin / LoginTheUser reconstruction

Targets:

- `PandoraComponent::selectCB(...)` at `0x100111d8`
- `PandoraComponent::doLogin()` at `0x10010d14`
- `pandora::doLogin(char *, char *, char *)` at `0x100145d8`
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

The recovered help text matches this flow: single selection fills the account name; double-clicking an icon can initiate login.

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

If no name is present, the component clears the text fields and restores the instructional login message instead of attempting authentication.

## pandora::doLogin()

Recovered behavior:

1. Ignore an empty username.
2. `handleAlsoTyped()` processes any text typed while the UI was transitioning.
3. `getpwnam(username)` obtains the account record.
4. Copy the passwd record with `pwd0_copyPwd()` so authentication/session code owns a stable structure.
5. If the account exists, call `LoginTheUser(passwd, password)`.
6. If the account does not exist and password UI is not yet enabled, enable the password field rather than immediately disclosing that the account is invalid.
7. On later failure, use the generic `Login failed.` path.

This is an important anti-enumeration behavior: the UI can request a password for an unknown typed name before giving the same generic failure used for a wrong password.

## LoginTheUser()

The account/login checks are performed in this order at high confidence:

1. `checkDirectory(pw_dir)` verifies/classifies the home directory. A definite missing/non-directory state generates `err_noHomeDir`.
2. System password policy is read through `isMandPass()` and `isPassReq()`.
3. `pwd0_checkPwd(passwd, suppliedPassword)` verifies the password and returns a multi-valued status.
4. Password status codes drive distinct paths:
   - success continues,
   - missing mandatory password can open `CreateGetNewPwDialog()`,
   - aged/expired password can open `CreatePwAgeDialog()`,
   - bad credentials use `err_pwdIncorrect` / generic `Login failed.`,
   - additional returned states feed restriction/inactivity handling.
5. On successful authentication, `setupEnvironment(passwd, ...)` constructs the login environment.
6. X access control/session preparation is adjusted (`XDisableAccessControl` appears in the success path).
7. `reportSuccess(passwd)` performs the successful-login handoff/reporting path.

Related original helpers present in the binary include:

```text
pwd0_checkPwd
pwd0_isLoginRestricted
pwd0_isInactive
pwd0_isPasswdAged
setupEnvironment
reportSuccess
CreateGetNewPwDialog
CreatePwAgeDialog
```

## Linux integration boundary

For a working Linux build today, preserve the recovered UI/control flow but use PAM at the authentication boundary:

```text
select user
  -> fill username
  -> reveal password field
  -> submit
  -> Linux PAM authenticate/account management
  -> portable session environment/startup
```

The IRIX `pwd0_*` routines remain RE targets for behavioral fidelity, especially password aging and account error presentation, but they do not need to block the first functional Linux login cycle.

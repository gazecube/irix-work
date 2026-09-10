from pathlib import Path

p = Path('reconstruction/PandoraViewKit.c++')
s = p.read_text()

old_ctor_tail = '''    createIconAreaScaffold();
}

void PandoraComponent::createIconAreaScaffold()
'''
new_ctor_tail = '''    createIconList();
    createIconAreaScaffold();
}

void PandoraComponent::addUser(const struct passwd *pwd, const char *loginOverride)
{
    if (!pwd)
        return;

    const char *login = loginOverride ? loginOverride : pwd->pw_name;
    if (!login || !*login || !pwd->pw_dir || !*pwd->pw_dir)
        return;

    PandoraUserModel user;
    user.login = login;
    user.fullName = pwd->pw_gecos ? pwd->pw_gecos : "";
    user.home = pwd->pw_dir;
    user.uid = pwd->pw_uid;
    _users.push_back(user);
}

void PandoraComponent::createIconList()
{
    _users.clear();

    const char *configPath = "/var/sysadm/config/clogin.conf";
    const bool haveIrixConfig = access(configPath, R_OK) == 0;
    PwdNameList config(haveIrixConfig ? configPath : NULL);

    setpwent();
    std::size_t seen = 0;
    for (struct passwd *pwd = getpwent(); pwd; pwd = getpwent())
    {
        ++seen;
        if (!pwd->pw_name || !*pwd->pw_name)
            continue;

        /* Binary-proven pandora::createIconList() compatibility path. */
        if (pwd->pw_name[0] == '-')
            break;

        const bool plusRecord = pwd->pw_name[0] == '+';
        const char *logicalName = plusRecord ? pwd->pw_name + 1 : pwd->pw_name;
        if (!*logicalName)
            continue;

        struct passwd *candidate = pwd;
        bool restoreEnumeration = false;
        const char *loginOverride = plusRecord ? logicalName : NULL;

        if (!pwd->pw_dir || !*pwd->pw_dir)
        {
            candidate = getpwnam(logicalName);
            restoreEnumeration = true;
            if (!candidate)
            {
                setpwent();
                for (std::size_t i = 0; i < seen; ++i)
                    if (!getpwent()) break;
                break;
            }
            if (candidate->pw_name && candidate->pw_name[0] == '+')
                loginOverride = candidate->pw_name + 1;
            else
                loginOverride = NULL;
        }

        const char *candidateName = loginOverride ? loginOverride : candidate->pw_name;
        if (candidateName && *candidateName && candidate->pw_dir && *candidate->pw_dir)
        {
            const PwdNameListEntry *entry = config.findEntry(candidateName);
            if (!(entry && (entry->flags &
                  (PwdNameListEntry::kNoShow | PwdNameListEntry::kNoIcon))) &&
                !homeIsIgnored(candidate->pw_dir))
            {
                const clogin::DirectoryState directoryState =
                    clogin::checkDirectoryPortable(candidate->pw_dir);
                if (directoryState != clogin::kDirectoryMissing)
                    addUser(candidate, loginOverride);
            }
        }

        if (restoreEnumeration)
        {
            setpwent();
            for (std::size_t i = 0; i < seen; ++i)
                if (!getpwent()) break;
        }
    }
    endpwent();
}

void PandoraComponent::createIconAreaScaffold()
'''
if old_ctor_tail not in s:
    raise SystemExit('constructor/scaffold insertion point not found')
s = s.replace(old_ctor_tail, new_ctor_tail, 1)

start = s.index('void PandoraComponent::displayIcon()\n{')
end = s.index('\nvoid PandoraComponent::showPasswordPrompt()', start)
new_display = '''void PandoraComponent::displayIcon()
{
    if (!_iconPanel || !_iconPanel->baseWidget() || !_layout)
        return;

    Widget panelWidget = _iconPanel->baseWidget();
    ImdViewPanelBeginBlockChanges(panelWidget);

    /*
     * Binary-proven split: createIconList()/addUser() populate the component
     * model collection first. displayIcon() consumes that collection and
     * constructs UserView instances; it does not enumerate passwd itself.
     */
    for (std::size_t i = 0; i < _users.size(); ++i)
    {
        const PandoraUserModel &user = _users[i];
        UserView *view = new UserView(
            user.login,
            static_cast<long>(user.uid),
            user.home);

        _layout->addView(view);
        _iconPanel->adoptView(view);
        _userViews.push_back(view);
    }

    _layout->update();
    ImdViewPanelEndBlockChanges(panelWidget);
}
'''
s = s[:start] + new_display + s[end:]

p.write_text(s)

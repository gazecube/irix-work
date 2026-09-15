# 4Dwm recovery map

The IRIX 6.5 executable exports 1,069 text symbols. Do not equate a matching function name with an identical implementation. This map tracks only classifications already supported by source/binary evidence; the classifier tool emits candidate matches for the rest.

## Verified mwm-base architecture

These names and roles are present in the Motif mwm baseline and in the IRIX 6.5 4Dwm binary. Each still requires per-function machine-code comparison before a final `MWM-BASE` classification:

```
InitWmGlobal
WmDispatchMenuEvent
WmDispatchClientEvent
WmDispatchWsEvent
AdoptInitialClients
ManageWindow
AdoptClient
GetClientInfo
FrameWindow
UnManageWindow
SendConfigureNotify
AddClientToList
DeleteClientFromList
AddTransient
AddWmResourceConverters
AddWmTimer
```

`main` is already control-flow compared. IRIX keeps the classic mwm event-loop architecture, but also contains SGI-specific state handling including per-button modifier normalization and additional global-WM fields. It is therefore `MWM-MODIFIED`, not a blind upstream copy.

## Strong SGI-only candidates

The following exported surface is tied to SGI rooms/DESKS/desktop integration and is not part of the classic mwm architecture:

```
CreateRoom
DestroyRoom
F_CreateRoom
F_RemoveRoom
F_Room
F_InAllRooms
DesksGeometry
DesksState
CheckRoomsClientMessage
F_TellDesktop
F_ExplicitSave
F_AutoSave
F_SaveConfiguration
F_LoadConfiguration
CreateRoomAppAtoms
AddARoomNameProperty
DeleteARoomNameProperty
AppActivated
AppDeactivated
AppProperty
AppRename
AppStateChanged
ActiveAppsIterate
ActiveXApp
```

These remain `SGI-NEW-CANDIDATE` until absence from the pinned upstream source and their binary call/data relationships are checked. SGI atom families in the executable include `_SGI_DESKS_*`, `_SGI_TELL_WM`, `_SGI_ICON_HINTS`, `_SGI_ROOTPAINTER_ID`, `_SGI_DESKS_BACKGROUND*`, and `_SGI_WM_OVERLAY`.

## Source-unit recovery order

The first shared mwm units to reconcile are `WmMain`, `WmManage`, `WmEvent`, `WmCEvent`, `WmFunction`, `WmResource`, `WmResParse`, `WmCDecor`, `WmIDecor`, `WmWinInfo`, `WmWinState`, `WmWinConf`, `WmKeyFocus`, `WmIconBox`, `WmMenu`, `WmProperty`, `WmProtocol`, placement, graphics, and colormap. SGI room/DESKS/session modules are then recovered against the resulting base.

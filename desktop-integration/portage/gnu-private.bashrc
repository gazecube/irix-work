# Portage hook: the IRIX userland in /bin, /usr/bin, /sbin, /usr/sbin; the GNU tools it replaces in /usr/lib/gnu/bin.
#
# Install: source this file from /etc/portage/bashrc.  It does two things.
#
# 1. Builds see GNU first.  Ebuilds, eclasses and Portage's own helpers assume GNU options (sed -i, find -print0,
#    mktemp -d, install -D, cp -a), so every ebuild phase runs with /usr/lib/gnu/bin at the front of PATH.
#
# 2. Relocation at install time.  For the packages listed below, a command is moved from its usual directory to
#    /usr/lib/gnu/bin when the IRIX userland installs a command of the same name (listed one per line in
#    /etc/portage/irix-userland.list, written by the irix-userland package).  Portage records the files at their new
#    place, so updates, unmerges and file ownership keep working; the packages need no mask and no copied ebuild.
#    Names on the keep list below never move: the IRIX versions do not work on Linux (mount, fsck, su need the
#    Linux file systems, fstab and PAM).
#
# A permanent fallback (/etc/env.d/99gnu-fallback) puts /usr/lib/gnu/bin at the END of the system PATH, so a GNU
# command the IRIX userland does not have is still found, and nothing is missing while packages are re-merged.

GNU_PRIVATE_DIR=/usr/lib/gnu/bin
GNU_PRIVATE_PACKAGES="sys-apps/coreutils sys-apps/sed sys-apps/findutils sys-apps/grep sys-apps/diffutils
	app-arch/tar sys-devel/patch sys-apps/util-linux"
GNU_PRIVATE_KEEP="mount umount fsck mkfs su sulogin login kill ps top getopt env"

PATH="${GNU_PRIVATE_DIR}:${PATH}"

gnu_private_relocate() {
	local list=/etc/portage/irix-userland.list d f n
	[[ -r ${list} ]] || return 0
	[[ " ${GNU_PRIVATE_PACKAGES//$'\n'/ } " == *" ${CATEGORY}/${PN} "* ]] || return 0
	for d in bin usr/bin sbin usr/sbin; do
		[[ -d ${ED}/${d} ]] || continue
		for f in "${ED}/${d}"/*; do
			[[ -e ${f} || -L ${f} ]] || continue
			n=${f##*/}
			[[ " ${GNU_PRIVATE_KEEP} " == *" ${n} "* ]] && continue
			grep -qxF -- "${n}" "${list}" || continue
			mkdir -p "${ED}${GNU_PRIVATE_DIR}" || die
			mv -- "${f}" "${ED}${GNU_PRIVATE_DIR}/${n}" || die
			einfo "gnu-private: /${d}/${n} -> ${GNU_PRIVATE_DIR}/${n}"
		done
	done
}

post_src_install() {
	gnu_private_relocate
}

#!/bin/bash
# Published prod installer with reviewed signing keys and compatible, completed releases.
set -euo pipefail

if [[ $# != 0 ]]; then
  echo "Usage: sudo bash $0" >&2
  exit 1
fi
repository_url='https://persistent.oaistatic.com/codex-app-prod/linux'
package_name='chatgpt'
if [[ $EUID != 0 ]]; then
  echo "Run this script with sudo: sudo bash $0" >&2
  exit 1
fi
for command in pacman pacman-key pacman-conf curl gpg uname; do
  command -v "$command" >/dev/null || {
    echo "Missing $command; install pacman, curl, gnupg, and coreutils before continuing." >&2
    exit 1
  }
done

temporary=$(mktemp -d)
bootstrap_cache=''
trap 'if [[ -n $bootstrap_cache ]]; then rm -f -- "$bootstrap_cache.db" "$bootstrap_cache.db.sig"; fi; rm -rf "$temporary"' EXIT
architecture=$(uname -m)
case "$architecture" in
  x86_64|aarch64) ;;
  *) echo 'Unsupported architecture.' >&2; exit 1 ;;
esac
versions=(26.915.31945 26.915.31029 26.911.61220)
# The current reviewed set is authoritative for every fallback candidate.
expected_fingerprints='3BFA0E4AE8B8CC16A2D9BA684A3B4A566C4660E4'
verification_home="$temporary/verification"
mkdir -m 0700 "$verification_home"
selected=''
for version in "${versions[@]}"; do
  candidate="$temporary/$version"
  mkdir "$candidate"
  package="$package_name-bin-$version-1-$architecture.pkg.tar.zst"
  unavailable=0
  for relative_path in \
    "arch/$version/repository-signing-key.gpg" \
    "arch/$version/$architecture/$package" \
    "arch/$version/$architecture/$package.sig" \
    "arch/$version/$architecture/openai-$package_name.db" \
    "arch/$version/$architecture/openai-$package_name.db.sig"; do
    if status=$(curl --proto '=https' --tlsv1.2 --connect-timeout 15 -fL \
      --write-out '%{http_code}' -o "$candidate/${relative_path##*/}" "$repository_url/$relative_path"); then
      continue
    else
      curl_exit=$?
    fi
    case "$curl_exit:$status" in
      22:404|22:410|22:408|22:429|22:5??|5:*|6:*|7:*|18:*|28:*|52:*|56:*)
        echo "Release $version is unavailable; trying the next published release." >&2
        unavailable=1
        break ;;
      *) echo "Download failed for $relative_path; not falling back. No changes were made." >&2; exit 1 ;;
    esac
  done
  if [[ $unavailable == 1 ]]; then continue; fi

  if ! fingerprints=$(gpg --homedir "$verification_home" --batch --show-keys --with-colons "$candidate/repository-signing-key.gpg" |
    awk -F: '$1 == "pub" { primary=1 } $1 == "sub" { primary=0 } $1 == "fpr" && primary { print $10; primary=0 }' |
    LC_ALL=C sort); then
    echo 'Invalid OpenAI signing-key bundle; not falling back. No keys imported.' >&2
    exit 1
  fi
  if [[ $fingerprints != "$expected_fingerprints" ]]; then
    echo 'OpenAI repository signing-key fingerprint mismatch; not falling back. No keys imported.' >&2
    exit 1
  fi
  gpg --homedir "$verification_home" --batch --import "$candidate/repository-signing-key.gpg"
  if ! gpg --homedir "$verification_home" --batch --no-auto-key-retrieve --no-auto-key-import \
    --verify "$candidate/openai-$package_name.db.sig" "$candidate/openai-$package_name.db" ||
    ! gpg --homedir "$verification_home" --batch --no-auto-key-retrieve --no-auto-key-import \
    --verify "$candidate/$package.sig" "$candidate/$package"; then
    echo 'OpenAI release signature verification failed; not falling back. No keys imported or configuration changed.' >&2
    exit 1
  fi
  selected="$candidate"
  if [[ $version != "${versions[0]}" ]]; then
    echo "Using previously published release $version." >&2
  fi
  break
done
if [[ -z $selected ]]; then
  echo 'No compatible published release is available. Files may still be propagating. No changes were made; download the installer again and retry shortly.' >&2
  exit 1
fi

sources_file="/etc/pacman.d/openai-$package_name.conf"
cat >"$temporary/repository.conf" <<EOF
### THIS FILE IS AUTOMATICALLY CONFIGURED ###
# Remove it and its Include line to opt out of automatic package updates.
[openai-$package_name]
SigLevel = Required DatabaseRequired TrustedOnly
Server = $repository_url/arch/\$arch
EOF
if [[ -e $sources_file ]] && ! cmp -s "$sources_file" "$temporary/repository.conf"; then
  echo "Not replacing existing pacman configuration: $sources_file" >&2
  exit 1
fi
pacman-key --init
pacman-key --add "$selected/repository-signing-key.gpg"
while IFS= read -r fingerprint; do
  pacman-key --lsign-key "$fingerprint"
done <<<"$expected_fingerprints"
install -m 0644 "$temporary/repository.conf" "$sources_file"
include_line="Include = $sources_file"
if ! grep -qxF "$include_line" /etc/pacman.conf; then
  printf '\n%s\n' "$include_line" >>/etc/pacman.conf
fi
# The first transaction must consume the files verified above, not refetch a
# newer CDN generation. Keep the installed configuration on the online repo.
# Give that snapshot its own cache identity so an older fallback cannot mask
# the online catalog's Last-Modified timestamp on the next ordinary upgrade.
bootstrap_repo="openai-$package_name-bootstrap-${temporary##*/}"
install -m 0644 "$selected/openai-$package_name.db" "$selected/$bootstrap_repo.db"
install -m 0644 "$selected/openai-$package_name.db.sig" "$selected/$bootstrap_repo.db.sig"
sed -e "s|^Server = .*|Server = file://$selected|" \
  -e "s|^\\[openai-$package_name\\]$|[$bootstrap_repo]|" "$temporary/repository.conf" \
  >"$temporary/repository-local.conf"
# The copied system configuration can contain private repository credentials.
install -m 0600 /dev/null "$temporary/pacman.conf"
awk -v original="$include_line" -v replacement="Include = $temporary/repository-local.conf" \
  '$0 == original { $0 = replacement } { print }' /etc/pacman.conf >"$temporary/pacman.conf"
bootstrap_cache="$(pacman-conf --config "$temporary/pacman.conf" DBPath)/sync/$bootstrap_repo"
# Allow pacman's unprivileged downloader to read verified artifacts while
# keeping all files and the directory writable only by root.
chmod 0755 "$temporary" "$selected"
chmod 0644 "$selected"/*
# Keep pacman's normal confirmation prompts and perform a full system upgrade.
pacman --config "$temporary/pacman.conf" -Syu --needed "$package_name-bin"

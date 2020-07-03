#!/bin/bash
__DIR__=$(cd "$(dirname "$0")" || exit 1; pwd); [ -z "${__DIR__}" ] && exit 1

# dependency manager

__exit__() {
  if [ "${tmp_dir}"x != ""x ]; then
    rm -rf "${tmp_dir}" 2>&1;
  fi
  exit "$1"
}
error(){ echo "[ERROR] $1"; __exit__ 1; }
ok(){ echo "[OK] $1"; __exit__ 0; }

if [ $# -ne 5 ] ; then
  printf "Usage:\n %s\n" \
  "work_dir[absolute], name, url, src_dir[relative], target_dir[absolute]"
  exit
fi

work_dir="$1"
name="$2"
url="$3"
src_dir="$4"
target_dir="$5"

if [ -L "${target_dir}" ]; then
  rm -f "${target_dir}"
fi

if ! cd "${work_dir}"; then
  error "Cd to ${work_dir} failed"
fi

if ! git rev-parse; then
  error "Unable to find git repo"
fi

tmp_dir="/tmp/cat_deps"
rm -rf "${tmp_dir}" 2>&1;
if ! mkdir -p "${tmp_dir}"; then
  tmp_dir=""
  error "Make tmp dir failed"
fi

if [ "$(echo "${url}" | grep "\.git$")"x == ""x ]; then
  # use wget to download source.tar.gz
  tmp_file="${tmp_dir}/${name}.tar.gz"

  if [ "$(wget --help 2>&1 | grep "\-\-version")"x = ""x ]; then
    error "Unable to find wget"
  fi

  if ! wget "${url}" -x -O "${tmp_file}" || [ ! -f "${tmp_file}" ]; then
    error "Download source file of ${name} failed"
  fi

  tar -zxvf "${tmp_file}" -C "${tmp_dir}"
else
  # use git clone to get source files
  tmp_source_dir="${tmp_dir}/${name}"

  if ! git clone --depth=1 "${url}" "${tmp_source_dir}" || [ ! -d "${tmp_source_dir}/.git" ]; then
    error "Clone source files of ${name} failed"
  fi

  rm -rf "${tmp_source_dir}/.git"
fi

if [ ! -d "${tmp_dir}/${src_dir}" ]; then
  error "Unable to find source dir"
fi

if [ -d "${target_dir}" ]; then
  tmp_backup_dir="/tmp/cat_deps_backup_$(date +%s)"
  mkdir -p "${tmp_backup_dir}" 2>&1
  if ! mv "${target_dir}" "${tmp_backup_dir}"; then
    error "Backup failed"
  fi
fi

if ! mv "${tmp_dir}/${src_dir}" "${target_dir}"; then
  error "Update dir failed"
fi

rm -rf "${tmp_dir}" 2>&1
tmp_dir=""

git add --ignore-errors -A

ok "${name} updated"

#!/bin/sh
set -eu

launcher=$0
while [ -L "${launcher}" ]; do
  launcher_dir=$(CDPATH= cd -- "$(dirname -- "${launcher}")" && pwd)
  launcher_target=$(readlink "${launcher}")
  case "${launcher_target}" in
    /*) launcher=${launcher_target} ;;
    *) launcher=${launcher_dir}/${launcher_target} ;;
  esac
done

script_dir=$(CDPATH= cd -- "$(dirname -- "${launcher}")" && pwd)
package_root=$(CDPATH= cd -- "${script_dir}/.." && pwd)
runtime_dir="${package_root}/lib/cw-buddy"

module_dir=
for candidate in "${package_root}"/lib/SoapySDR/modules*; do
  if [ -d "${candidate}" ]; then
    module_dir=${candidate}
    break
  fi
done

if [ -n "${LD_LIBRARY_PATH:-}" ]; then
  export LD_LIBRARY_PATH="${runtime_dir}:${LD_LIBRARY_PATH}"
else
  export LD_LIBRARY_PATH="${runtime_dir}"
fi
if [ -n "${module_dir}" ]; then
  if [ -n "${SOAPY_SDR_PLUGIN_PATH:-}" ]; then
    export SOAPY_SDR_PLUGIN_PATH="${module_dir}:${SOAPY_SDR_PLUGIN_PATH}"
  else
    export SOAPY_SDR_PLUGIN_PATH="${module_dir}"
  fi
fi

exec "${script_dir}/cw-buddy-desktop.bin" "$@"

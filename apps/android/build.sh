#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")" && pwd)"
build_dir="$project_dir/build"
sdk_root="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
android_jar="${ANDROID_JAR:-}"
if [[ -z "$android_jar" && -n "$sdk_root" ]]; then
    android_jar="$(find "$sdk_root/platforms" -mindepth 2 -maxdepth 2 -name android.jar -print 2>/dev/null | sort -V | tail -n 1)"
fi
aapt_bin="${AAPT:-$(command -v aapt || true)}"
dx_bin="${DX:-$(command -v dalvik-exchange || command -v dx || true)}"
d8_bin="${D8:-$(command -v d8 || true)}"
zipalign_bin="${ZIPALIGN:-$(command -v zipalign || true)}"
apksigner_bin="${APKSIGNER:-$(command -v apksigner || true)}"

if [[ -z "$android_jar" || ! -f "$android_jar" ]]; then
    echo "Set ANDROID_JAR to an Android SDK platform android.jar (API 23 or newer)." >&2
    exit 2
fi
for tool in AAPT="$aapt_bin" ZIPALIGN="$zipalign_bin" APKSIGNER="$apksigner_bin"; do
    if [[ "${tool#*=}" == "" ]]; then
        echo "Required Android build tool is missing: ${tool%%=*}" >&2
        exit 2
    fi
done
if [[ -z "$dx_bin" && -z "$d8_bin" ]]; then
    echo "Required Android DEX tool is missing: install d8, dx, or dalvik-exchange." >&2
    exit 2
fi

mkdir -p "$build_dir/gen" "$build_dir/classes"
asset_args=()
if [[ -d "$project_dir/assets" ]]; then
    asset_args=(-A "$project_dir/assets")
fi
"$aapt_bin" package -f -m \
    -J "$build_dir/gen" \
    -M "$project_dir/AndroidManifest.xml" \
    -S "$project_dir/res" \
    "${asset_args[@]}" \
    -I "$android_jar" \
    -F "$build_dir/unsigned.apk"

source_roots=("$project_dir/src" "$build_dir/gen")
mapfile -t java_sources < <(find "${source_roots[@]}" -name '*.java' -print)
java_classpath="$android_jar"
if command -v javac >/dev/null 2>&1; then
    javac -source 8 -target 8 -bootclasspath "$android_jar" -classpath "$java_classpath" \
        -d "$build_dir/classes" "${java_sources[@]}"
elif command -v ecj >/dev/null 2>&1; then
    ecj -proc:none -source 1.8 -target 1.8 -bootclasspath "$android_jar" -classpath "$java_classpath" \
        -d "$build_dir/classes" "${java_sources[@]}"
else
    echo "Install a Java compiler (javac or ECJ) to build the app." >&2
    exit 2
fi

if [[ -n "$d8_bin" ]]; then
    mkdir -p "$build_dir/dex"
    classes_jar="$build_dir/classes.jar"
    rm -f "$classes_jar"
    (cd "$build_dir/classes" && zip -q -r "$classes_jar" .)
    d8_inputs=("$classes_jar")
    "$d8_bin" --min-api 23 --lib "$android_jar" --output "$build_dir/dex" "${d8_inputs[@]}"
    cp "$build_dir/dex/classes.dex" "$build_dir/classes.dex"
else
    dx_inputs=("$build_dir/classes")
    "$dx_bin" --dex --output="$build_dir/classes.dex" "${dx_inputs[@]}"
fi
cp "$build_dir/unsigned.apk" "$build_dir/un-aligned.apk"
zip -q -j "$build_dir/un-aligned.apk" "$build_dir/classes.dex"
"$zipalign_bin" -f 4 "$build_dir/un-aligned.apk" "$build_dir/aligned.apk"

keystore="${HAYLOU_DEBUG_KEYSTORE:-${XDG_DATA_HOME:-$HOME/.local/share}/haylou-s30-control/haylou-debug.keystore}"
if [[ ! -f "$keystore" ]]; then
    mkdir -p "$(dirname "$keystore")"
    keytool -genkeypair -noprompt \
        -keystore "$keystore" -storepass android -keypass android \
        -alias haylou-debug -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=HAYLOU S30 Control Debug,O=Local,C=US" >/dev/null 2>&1
    chmod 600 "$keystore"
fi

"$apksigner_bin" sign \
    --ks "$keystore" --ks-key-alias haylou-debug \
    --ks-pass pass:android --key-pass pass:android \
    --out "$build_dir/HaylouS30Control.apk" "$build_dir/aligned.apk"
"$apksigner_bin" verify "$build_dir/HaylouS30Control.apk"
echo "Built: $build_dir/HaylouS30Control.apk"

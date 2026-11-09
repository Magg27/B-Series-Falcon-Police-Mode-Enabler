# B-Series Falcon Police Mode Enabler

A small utility for reading, enabling and disabling Police Mode on Ford instrument clusters. Includes a Linux GUI, Linux CLI and Android app. The diagnostic code is shared C; Android uses a small Java/JNI interface for USB access.

## Compatibility

- **Tested:** 2006 BF Falcon E-Gas, cluster firmware `5R29`, using a CH340-based ELM327 v1.5 USB adapter at **38400 baud**. Police Mode was enabled and verified on this cluster.
- **Android tested:** Google Pixel 9 running GrapheneOS, connected to the USB ELM327 via USB-C OTG.
- **Not tested:** BA Falcon and SX/SY Territory clusters. They are expected to use a compatible protocol, but this has not been verified. Other BF firmware versions are also unverified.

The tool reads the original configuration before changing the Police Mode bit, preserves the other bits and verifies the result. Writes to unrecognised firmware are blocked unless you explicitly override the check.

**Use at your own risk.** A failed or incompatible write could misconfigure the cluster. Keep the vehicle stationary and the battery voltage stable. Read the status first; do not retry an uncertain write without checking the current configuration.

## Downloads

Prebuilt Linux binaries and Android APKs are available on the [Releases page](https://github.com/Magg27/B-Series-Falcon-Police-Mode-Enabler/releases).

The repository contains source code and build scripts only. Compiled executables and APKs are not committed to Git.

## Use

**Linux:** Plug the ELM327 into a USB port and connect it to the vehicle's OBD-II socket. Launch `pme-gui`, select the serial port and baud rate, then select **Read Status**. On Arch Linux the adapter often appears as `/dev/ttyUSB0` or `/dev/ttyUSB1`.

The CLI offers the same functions:

```bash
./bin/pme-cli --list
./bin/pme-cli --port /dev/ttyUSB0 --baud 38400 --read
./bin/pme-cli --port /dev/ttyUSB0 --baud 38400 --enable
./bin/pme-cli --port /dev/ttyUSB0 --baud 38400 --disable
```

Enabling and disabling require confirmation. Replace `/dev/ttyUSB0` with the detected port.

**Android:** Connect the USB ELM327 using a USB-C OTG adapter. Open the app, select the adapter and serial speed, grant USB permission and press **Read Status** before changing anything.

## Build for Linux

Arch Linux / CachyOS:

```bash
sudo pacman -Syu --needed base-devel libx11 git
```

Debian / Ubuntu:

```bash
sudo apt update
sudo apt install build-essential libx11-dev git
```

Clone and build:

```bash
git clone https://github.com/Magg27/B-Series-Falcon-Police-Mode-Enabler.git
cd B-Series-Falcon-Police-Mode-Enabler
make clean all test
./bin/pme-gui
```

The executables are `bin/pme-gui` and `bin/pme-cli`. The GUI requires X11/XWayland. To fix serial-port permission errors on Arch, add your user to the `uucp` group (`sudo usermod -aG uucp "$USER"`), then log out and back in. On other distributions the group is often `dialout`.

## Build for Android

Requires JDK 21, Android SDK Platform 35, Build Tools 35.0.0, NDK 27.0.12077973 and CMake 3.22.1. Install the Android SDK with [Android Studio](https://developer.android.com/studio), or install [Android command-line tools](https://developer.android.com/studio#command-tools) into `$ANDROID_HOME/cmdline-tools/latest/`.

On Arch, install the host tools:

```bash
sudo pacman -S --needed jdk21-openjdk curl unzip android-tools
```

With the command-line SDK installed at `~/Android/Sdk`:

```bash
export ANDROID_HOME="$HOME/Android/Sdk"
unset ANDROID_SDK_ROOT
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$ANDROID_HOME" --licenses
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$ANDROID_HOME" \
  'platforms;android-35' 'build-tools;35.0.0' 'platform-tools' \
  'ndk;27.0.12077973' 'cmake;3.22.1'

bash android/build-android.sh
```

The build script downloads Gradle 8.13 if needed. The debug APK is at:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

To install with USB debugging enabled:

```bash
adb devices
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

Alternatively, open the `android/` folder in Android Studio and build the debug APK there. A debug APK and a release-signed APK cannot update each other because they use different signing keys.

## GitHub builds and releases

- Every push to `main` runs `.github/workflows/build.yml`, which builds/tests Linux and builds an Android debug APK. Download those temporary builds under **Actions → Artifacts**.
- Pushing a tag such as `v1.0.0` runs `.github/workflows/release.yml`. It builds the Linux applications and a **release-signed** Android APK and publishes them under **Releases**.

For automatic Android releases, set up a persistent signing key **once**. Install the GitHub CLI (`github-cli` on Arch), run `gh auth login`, then from the repository root:

```bash
mkdir -p ~/.local/share/falcon-police-mode
keytool -genkeypair -keystore ~/.local/share/falcon-police-mode/release.jks \
  -alias falcon-police-mode -keyalg RSA -keysize 3072 -validity 10000
base64 -w0 ~/.local/share/falcon-police-mode/release.jks | gh secret set APK_KEYSTORE_B64
gh secret set APK_KEYSTORE_PASSWORD
gh secret set APK_KEY_PASSWORD
printf '%s' 'falcon-police-mode' | gh secret set APK_KEY_ALIAS
```

Enter the keystore password and key password when prompted. Back up the keystore and passwords privately; **do not commit them**. The release workflow requires these secrets and will not publish an unsigned APK.

For a new version, update `versionCode` and `versionName` in `android/app/build.gradle` first, then push the matching tag:

```bash
git tag -a v1.0.0 -m "v1.0.0"
git push origin v1.0.0
```

You can also upload your own locally built files manually through GitHub's **Releases** page. Keep the same Android signing key for future APK updates.

## Licence

GPL-3.0. See [LICENSE](LICENSE). Not affiliated with Ford; does not contain the original Windows BS_PME executable.

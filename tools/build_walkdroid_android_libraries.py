#!/usr/bin/env python3
"""Build the Android support libraries used by WalkDroid from source.

The caller supplies a writable platform checkout, an Android SDK and Gradle
build tools. No device is accessed and no proprietary firmware is involved.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from apply_walkdroid_library_sources import integrate
from apply_walkdroid_slice_source import integrate as integrate_slices
from walkdroid_support_stage import stage

ROOT = Path(__file__).resolve().parents[1]
SUPPORT_TASKS = [
    ':annotation:jar', ':collection:jar', ':arch:core-common:jar', ':lifecycle:lifecycle-common:jar',
    *(':' + name + ':assembleRelease' for name in (
        'appcompat', 'arch:core-runtime', 'asynclayoutinflater', 'cardview', 'coordinatorlayout',
        'core', 'cursoradapter', 'customview', 'documentfile', 'drawerlayout', 'fragment',
        'gridlayout', 'heifwriter', 'interpolator', 'legacy-support-core-ui',
        'legacy-support-core-utils', 'legacy-support-v13', 'legacy-support-v4',
        'lifecycle:lifecycle-livedata-core', 'lifecycle:lifecycle-runtime',
        'lifecycle:lifecycle-viewmodel', 'loader', 'localbroadcastmanager', 'media', 'palette',
        'print', 'recyclerview', 'slidingpanelayout', 'swiperefreshlayout', 'transition',
        'vectordrawable', 'vectordrawable-animated', 'viewpager', 'slice-core', 'slice-view',
        'slice-builders', 'car', 'dynamicanimation', 'percentlayout', 'legacy-preference-v14',
        'leanback', 'leanback-preference', 'preference', 'mediarouter',
        'lifecycle:lifecycle-extensions', 'lifecycle:lifecycle-livedata')),
]
ARCHIVES = {
    'material': 'material-components-1.0.0-alpha1-source.tar.zst',
    'material-legacy': 'material-support-legacy-source.tar.zst',
    'support-design-widget': 'support-design-widget-source.tar.zst',
    'versionedparcelable': 'versionedparcelable-source.tar.zst',
    'sherpa': 'sherpa-source.tar.zst',
    'flexbox': 'flexbox-source.tar.zst',
    'slices': 'slices-source.tar.zst',
    'iconcompat': 'iconcompat-source.tar.zst',
    'widgetcompat': 'widgetcompat-source.tar.zst',
    'textcompat': 'textcompat-source.tar.zst',
}


def prepare_support(platform: Path, sdk: Path, car_stubs: Path):
    support = platform / 'frameworks/support'
    for file, needle in (
        (support / 'buildSrc/build.gradle', '    repositories {\n'),
        (support / 'buildSrc/repos.gradle', 'def addMavenRepositories(RepositoryHandler handler) {\n'),
    ):
        text = file.read_text(encoding='utf-8')
        addition = needle + '    ' + '    maven { url "https://repo.maven.apache.org/maven2" }\n'
        if file.name == 'repos.gradle':
            addition = needle + '    handler.maven { url "https://repo.maven.apache.org/maven2" }\n'
        if addition not in text:
            file.write_text(text.replace(needle, addition, 1), encoding='utf-8')
    init = support / 'buildSrc/init.gradle'
    text = init.read_text(encoding='utf-8')
    start = text.index('def getFullSdkPath() {')
    end = text.index('\n/**', start)
    text = text[:start] + '''def getFullSdkPath() {
    return System.getenv("ANDROID_HOME") ?: "${repos.prebuiltsRoot}/fullsdk-linux"
}
''' + text[end:]
    start = text.index('def setSdkInLocalPropertiesFile() {')
    end = text.index('\ndef setupRepoOutAndBuildNumber()', start)
    text = text[:start] + '''def setSdkInLocalPropertiesFile() {
    final File fullSdkPath = file(getFullSdkPath())
    project.ext.fullSdkPath = fullSdkPath
    file("local.properties").write "sdk.dir=${fullSdkPath.getAbsolutePath()}"
    ext.usingFullSdk = true
}
''' + text[end:]
    init.write_text(text, encoding='utf-8')
    car = support / 'car/src/main/java/androidx/car/utils/CarUxRestrictionsHelper.java'
    car.write_text(car.read_text(encoding='utf-8').replace(
        'CarUxRestrictionsManager.onUxRestrictionsChangedListener',
        'CarUxRestrictionsManager.OnUxRestrictionsChangedListener'), encoding='utf-8')
    stub = support / 'car/car-stubs/android.car.jar'
    stub.parent.mkdir(parents=True, exist_ok=True)
    if stub.is_symlink():
        stub.unlink()
    shutil.copyfile(car_stubs, stub)


def prepare_material(source: Path, platform: Path, sdk: Path):
    file = source / 'build.gradle'
    text = file.read_text(encoding='utf-8')
    text = text.replace('    google()\n', '    mavenCentral()\n    google()\n', 1)
    text = text.replace('    jcenter()\n', '    mavenCentral()\n', 1)
    local_maven = platform / 'prebuilts/sdk/current/support/m2repository'
    text = text.replace('  repositories {\n    google()\n    jcenter()\n    mavenLocal()',
                        '  repositories {\n    maven { url "' + str(local_maven) + '" }\n'
                        '    google()\n    mavenCentral()\n    mavenLocal()')
    text = text.replace("compileSdkVersion = 'android-P'", 'compileSdkVersion = 28')
    text = text.replace("supportVersion = '28.0.0-alpha1'", "supportVersion = '28.0.0'")
    text = text.replace('        compileSdkVersion rootProject.ext.compileSdkVersion\n',
                        '        compileSdkVersion rootProject.ext.compileSdkVersion\n'
                        '        buildToolsVersion "27.0.3"\n')
    file.write_text(text, encoding='utf-8')
    (source / 'settings.gradle').write_text("include ':lib'\n", encoding='utf-8')
    (source / 'local.properties').write_text('sdk.dir=' + str(sdk) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform-root', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True, help='Android SDK with platform 28 and build-tools 27.0.3')
    parser.add_argument('--java-home', type=Path, required=True, help='JDK 8 for Gradle')
    parser.add_argument('--gradle-47', type=Path, required=True, help='Gradle 4.7 executable')
    parser.add_argument('--gradle-41', type=Path, required=True, help='Gradle 4.1 executable')
    parser.add_argument('--android-car-stubs', type=Path, help='optional Android car API stub jar')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 1, 12))
    args = parser.parse_args()
    platform, work, sdk = args.platform_root.resolve(), args.work_dir.resolve(), args.sdk.resolve()
    sources = work / 'sources'
    sources.mkdir(parents=True, exist_ok=True)
    for name, archive in ARCHIVES.items():
        destination = sources / name
        destination.mkdir(parents=True, exist_ok=True)
        subprocess.run(['tar', '--zstd', '-xf', str(ROOT / 'upstream' / archive),
                        '-C', str(destination)], check=True)
    java_home = args.java_home.resolve()
    car_stubs = (args.android_car_stubs or platform / 'prebuilts/sdk/current/optional/android.car.jar').resolve()
    env = os.environ.copy()
    env.pop('OUT_DIR', None)
    env.pop('DIST_DIR', None)
    env.update(JAVA_HOME=str(java_home), ANDROID_HOME=str(sdk),
               GRADLE_USER_HOME=str(work / 'gradle-home'), ALLOW_PUBLIC_REPOS='1')
    prepare_support(platform, sdk, car_stubs)
    integrate_slices(platform, sources)
    support = platform / 'frameworks/support'
    command = [str(args.gradle_47.resolve()), *SUPPORT_TASKS, '--no-daemon',
               '--max-workers=' + str(max(1, args.jobs))]
    subprocess.run(command, cwd=support, env=env, check=True)
    built = platform / 'out/host/gradle/frameworks/support'
    converted = work / 'converted'
    stage(platform, built, converted, java_home, args.jobs)
    for name in ('material', 'material-legacy'):
        source = sources / name
        prepare_material(source, platform, sdk)
        subprocess.run([str(args.gradle_41.resolve()), ':lib:assembleRelease', '--no-daemon',
                        '--max-workers=' + str(max(1, args.jobs))], cwd=source, env=env, check=True)
    modern = sources / 'material/lib/build/outputs/aar/material-release.aar'
    modern_androidx = work / 'material-androidx.aar'
    jetifier = platform / 'prebuilts/sdk/tools/jetifier/jetifier-standalone/bin/jetifier-standalone'
    with (work / 'jetifier.log').open('w', encoding='utf-8') as log:
        subprocess.run([str(jetifier), '-outputfile', str(modern_androidx), '-i', str(modern)],
                       env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    stage(platform, built, converted, java_home, args.jobs, modern_androidx)
    integrate(platform, sources, sources / 'material-legacy/lib/build/outputs/aar/design-release.aar')
    print('Android support libraries built from source and connected to', platform, flush=True)


if __name__ == '__main__':
    main()

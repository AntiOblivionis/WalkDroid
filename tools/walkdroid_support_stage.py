"""Connect locally compiled Android support libraries to the platform tree."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import re
import shutil
import subprocess
import io
import zipfile

from walkdroid_jetifier import repair

LEGACY_NAMES = {
    'support-compat': 'core', 'support-core-ui': 'legacy-support-core-ui',
    'support-core-utils': 'legacy-support-core-utils', 'support-v4': 'legacy-support-v4',
    'support-v13': 'legacy-support-v13', 'support-fragment': 'fragment',
    'support-media-compat': 'media', 'support-vector-drawable': 'vectordrawable',
    'animated-vector-drawable': 'vectordrawable-animated', 'appcompat-v7': 'appcompat',
    'cardview-v7': 'cardview', 'gridlayout-v7': 'gridlayout', 'mediarouter-v7': 'mediarouter',
    'palette-v7': 'palette', 'recyclerview-v7': 'recyclerview',
    'support-dynamic-animation': 'dynamicanimation', 'percent': 'percentlayout',
    'preference-v14': 'legacy-preference-v14', 'leanback-v17': 'leanback',
    'preference-leanback-v17': 'leanback-preference', 'preference-v7': 'preference',
    'slices-builders': 'slice-builders', 'slices-core': 'slice-core', 'slices-view': 'slice-view',
    'support-annotations': 'annotation', 'collections': 'collection',
}


def install(source: Path, target: Path):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_symlink():
        target.unlink()
    shutil.copyfile(source, target)
    # The new artifact was just built. Old upstream sidecars do not describe it.
    for suffix in ('.sha1', '.md5'):
        sidecar = target.with_name(target.name + suffix)
        if sidecar.exists() or sidecar.is_symlink():
            sidecar.unlink()


def reverse_input(source: Path, work: Path) -> Path:
    # The newer core source produces both the AndroidX parcelizer and its old
    # support-package bridge. Reverse Jetifier creates that same bridge name
    # from the AndroidX class, so omit the generated bridge in this input only.
    if source.name != 'core-release.aar':
        return source
    target = work / ('reverse-input-' + source.name)
    with zipfile.ZipFile(source) as aar, zipfile.ZipFile(target, 'w') as output:
        for info in aar.infolist():
            content = aar.read(info.filename)
            if info.filename == 'classes.jar':
                buffer = io.BytesIO()
                with zipfile.ZipFile(io.BytesIO(content)) as classes, zipfile.ZipFile(buffer, 'w') as filtered:
                    for entry in classes.infolist():
                        if entry.filename != 'android/support/v4/graphics/drawable/IconCompatParcelizer.class':
                            filtered.writestr(entry, classes.read(entry.filename))
                content = buffer.getvalue()
            output.writestr(info, content)
    return target


def stage(platform: Path, output: Path, work: Path, java_home: Path, jobs: int,
          material_androidx: Path | None = None):
    sdk = platform / 'prebuilts/sdk/current'
    jetifier = platform / 'prebuilts/sdk/tools/jetifier/jetifier-standalone/bin/jetifier-standalone'
    work.mkdir(parents=True, exist_ok=True)
    aars = {p.name.removesuffix('-release.aar'): p for p in output.rglob('*-release.aar')}
    jars = {}
    for path in output.rglob('build/libs/*.jar'):
        key = path.name.split('-1.0.0-alpha1.jar')[0].split('-2.0.0-alpha1.jar')[0]
        jars[key] = path
    conversions, pending = {}, []
    for area in ('androidx', 'support', 'extras/app-toolkit'):
        text = (sdk / area / 'Android.bp').read_text(encoding='utf-8')
        for kind, block in re.findall(r'(android_library_import|java_import)\s*\{(.*?)\n\}', text, re.S):
            entry = re.search(r'(?:aars|jars):\s*\["([^"]+)', block)
            if not entry:
                continue
            target = sdk / area / entry.group(1)
            stem = target.parent.parent.name
            if area == 'androidx':
                key = stem
            elif area == 'support':
                key = LEGACY_NAMES.get(stem, stem)
            else:
                key = stem
                if stem in ('runtime', 'common', 'common-java8', 'extensions', 'livedata',
                            'livedata-core', 'viewmodel', 'process', 'service'):
                    key = ('core-' if target.parts[-4] == 'core' else 'lifecycle-') + stem
            source = (aars if kind == 'android_library_import' else jars).get(key)
            if source is None:
                continue
            if area == 'androidx':
                install(source, target)
            else:
                converted = work / ('legacy-' + source.name)
                conversions[source] = converted
                pending.append((converted, target))

    env = os.environ.copy()
    env['JAVA_HOME'] = str(java_home)
    def reverse(item):
        source, target = item
        input_aar = reverse_input(source, work)
        result = subprocess.run([str(jetifier), '-outputfile', str(target), '-i', str(input_aar),
                                 '-r', '-s', '-l', 'error'], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(result.stdout.decode(errors='replace')[-4000:])
    with ThreadPoolExecutor(max_workers=max(1, min(jobs, 8))) as pool:
        list(pool.map(reverse, conversions.items()))
    repair(list(conversions.items()))
    for source, target in pending:
        install(source, target)
    if material_androidx is not None:
        install(material_androidx, sdk / 'extras/material-design-x/repo_out/com/google/android/material/'
                                     'material/1.0.0-alpha1/material-1.0.0-alpha1.aar')

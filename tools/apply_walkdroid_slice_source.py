"""Use the matching upstream Slice and parcelable sources in the support build."""
from pathlib import Path
import shutil

REMOVED_SOURCES = {
    'view': ('java/androidx/slice/SliceManager.java', 'java/androidx/slice/SliceManagerBase.java',
             'java/androidx/slice/SliceManagerCompat.java', 'java/androidx/slice/SliceManagerWrapper.java'),
    'builders': ('java/androidx/slice/builders/GridBuilder.java',
                 'java/androidx/slice/builders/impl/GridRowBuilder.java',
                 'java/androidx/slice/builders/impl/GridRowBuilderBasicImpl.java'),
}


def integrate(platform: Path, sources: Path):
    support = platform / 'frameworks/support'
    for name in ('core', 'view', 'builders'):
        destination = support / 'slices' / name / 'src/main'
        for relative in REMOVED_SOURCES.get(name, ()):
            (destination / relative).unlink(missing_ok=True)
        shutil.copytree(sources / 'slices' / name / 'src/main', destination, dirs_exist_ok=True)
    shutil.copytree(sources / 'versionedparcelable', support / 'versionedparcelable', dirs_exist_ok=True)
    icon = support / 'compat/src/main/java/androidx/core/graphics/drawable/IconCompat.java'
    shutil.copyfile(sources / 'iconcompat/IconCompat.java', icon)
    text_view = support / 'compat/src/main/java/androidx/core/widget/TextViewCompat.java'
    shutil.copyfile(sources / 'widgetcompat/TextViewCompat.java', text_view)
    precomputed = support / 'compat/src/main/java/androidx/core/text/PrecomputedTextCompat.java'
    shutil.copyfile(sources / 'textcompat/PrecomputedTextCompat.java', precomputed)
    settings = support / 'settings.gradle'
    text = settings.read_text(encoding='utf-8')
    for line in ('includeProject(":versionedparcelable", "versionedparcelable")',
                 'includeProject(":versionedparcelable-annotation", "versionedparcelable/annotation")'):
        if line not in text:
            text += '\n' + line + '\n'
    settings.write_text(text, encoding='utf-8')
    versioned = support / 'versionedparcelable/build.gradle'
    text = versioned.read_text(encoding='utf-8')
    text = text.replace('LibraryVersions.VERSIONED_PARCELABLE', 'LibraryVersions.SUPPORT_LIBRARY')
    text = text.replace('LibraryGroups.VERSIONEDPARCELABLE', '"androidx.versionedparcelable"')
    versioned.write_text(text, encoding='utf-8')
    for name in ('compat/build.gradle', 'slices/core/build.gradle'):
        file = support / name
        text = file.read_text(encoding='utf-8')
        text = text.replace('    implementation project(":versionedparcelable")\n',
                            '    api project(":versionedparcelable")\n')
        addition = ('dependencies {\n    api project(":versionedparcelable")\n'
                    '    annotationProcessor project(":versionedparcelable-annotation")\n')
        if addition not in text:
            text = text.replace('dependencies {\n', addition, 1)
        file.write_text(text, encoding='utf-8')

"""Integrate source-built support-library modules into an Android source tree."""
from pathlib import Path
import re,shutil
import textwrap

def integrate(platform, sources, legacy_design_aar):
    p=platform/'prebuilts/sdk/current'
    def copy_source(name,area):
     dest=p/area/'walkdroid-source'/name
     shutil.copytree(sources/name,dest,dirs_exist_ok=True)
     return dest
    def normalize(block):
        first, separator, rest = block.partition('\n')
        return first + separator + textwrap.dedent(rest)
    def replace_module(text,name,replacement):
     replacement = normalize(replacement)
     pattern=r'(?:android_library_import|java_import|android_library|java_library_static)\s*\{(?:(?!\n\}).)*?\bname:\s*"'+re.escape(name)+r'"(?:(?!\n\}).)*?\n\}'
     out,count=re.subn(pattern,replacement,text,count=1,flags=re.S)
     if count!=1:raise RuntimeError('module missing: '+name)
     return out
    
    copy_source('versionedparcelable','androidx')
    bp=p/'androidx/Android.bp'
    text=bp.read_text()
    text=replace_module(text,'androidx.versionedparcelable_versionedparcelable-nodeps','''android_library {
        name: "androidx.versionedparcelable_versionedparcelable-nodeps",
        sdk_version: "current",
        manifest: "walkdroid-source/versionedparcelable/src/main/AndroidManifest.xml",
        srcs: ["walkdroid-source/versionedparcelable/src/main/java/**/*.java"],
        static_libs: ["androidx.annotation_annotation", "androidx.collection_collection"],
        java_version: "1.7",
    }''')
    bp.write_text(text)
    
    bp=p/'support/Android.bp'
    text=bp.read_text()
    text=replace_module(text,'android-versionedparcelable-nodeps','''android_library {
        name: "android-versionedparcelable-nodeps",
        sdk_version: "current",
        manifest: "manifests/android-versionedparcelable/AndroidManifest.xml",
        static_libs: ["androidx.versionedparcelable_versionedparcelable-nodeps"],
    }''')
    def add_car_preference(match):
     block=match.group(0)
     # Android car API jars from the builder's SDK are compile interfaces.
     # Do not merge their stub implementations into the shipped APKs.
     block=re.sub(r'^[ \t]*"prebuilt-android\.car-stubs",[ \t]*\n','',block,flags=re.M)
     if 'libs: ["prebuilt-android.car-stubs"]' not in block:
      block=block.replace('    static_libs: [','    libs: ["prebuilt-android.car-stubs"],\n    static_libs: [',1)
     if '"android-support-v7-preference"' not in block:
      block=block.replace('"android-support-v7-gridlayout",','"android-support-v7-gridlayout",\n        "android-support-v7-preference",')
     if '"com.google.android.material_material"' not in block:
      block=block.replace('"android-support-v7-gridlayout",','"android-support-v7-gridlayout",\n        "com.google.android.material_material",')
     return block
    text=re.sub(r'(?:android_library_import|android_library)\s*\{(?:(?!\n\}).)*?\bname:\s*"android-support-car(?:-nodeps)?"(?:(?!\n\}).)*?\n\}',add_car_preference,text,flags=re.S)
    bp.write_text(text)

    # The installer includes the shared car UI library but does not use the
    # automotive path on the player. Resolve its API against AOSP's car source
    # module, without packaging the builder's SDK stub jar into the APK.
    installer=platform/'packages/apps/PackageInstaller/Android.mk'
    text=installer.read_text()
    if 'LOCAL_JAVA_LIBRARIES += android.car' not in text:
     text=text.replace('LOCAL_STATIC_JAVA_LIBRARIES :=',
                       'LOCAL_JAVA_LIBRARIES += android.car\n\nLOCAL_STATIC_JAVA_LIBRARIES :=',1)
     installer.write_text(text)
    
    copy_source('flexbox','extras/material-design')
    widget=p/'extras/material-design/walkdroid-source/support-design-widget'
    shutil.copytree(sources/'support-design-widget',widget,dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('Android.mk','Android.bp'))
    text_input=widget/'src/android/support/design/widget/TextInputLayout.java'
    text_input.write_text(text_input.read_text().replace('android.support.v7.appcompat.R.color.error_color_material',
        'android.support.design.R.color.design_error'))
    bp=p/'extras/material-design/Android.bp'
    text=bp.read_text()
    widget_module='''android_library {
        name: "android-support-design-widget-source",
        sdk_version: "current",
        manifest: "walkdroid-source/support-design-widget/AndroidManifest.xml",
        srcs: ["walkdroid-source/support-design-widget/base/**/*.java", "walkdroid-source/support-design-widget/lollipop/**/*.java", "walkdroid-source/support-design-widget/src/**/*.java"],
        resource_dirs: ["walkdroid-source/support-design-widget/res"],
        static_libs: ["android-support-annotations", "android-support-compat", "android-support-media-compat", "android-support-core-ui", "android-support-core-utils", "android-support-fragment", "android-support-transition", "android-support-v7-appcompat", "android-support-v7-recyclerview"],
        java_version: "1.7",
    }'''
    if 'name: "android-support-design-widget-source"' not in text:text+='\n'+normalize(widget_module)+'\n'
    text=replace_module(text,'flexbox-nodeps','''android_library {
        name: "flexbox-nodeps",
        sdk_version: "current",
        manifest: "walkdroid-source/flexbox/flexbox/src/main/AndroidManifest.xml",
        srcs: ["walkdroid-source/flexbox/flexbox/src/main/java/**/*.java"],
        resource_dirs: ["walkdroid-source/flexbox/flexbox/src/main/res"],
        static_libs: ["android-support-compat", "android-support-v7-recyclerview", "android-support-annotations"],
        java_version: "1.7",
    }''')
    design=p/'extras/material-design/repository/com/android/support/design/27.1.0/design-27.1.0.aar'
    if design.is_symlink():design.unlink()
    shutil.copyfile(legacy_design_aar,design)
    text=replace_module(text,'android-support-design-nodeps','''android_library_import {
        name: "android-support-design-nodeps",
        aars: ["repository/com/android/support/design/27.1.0/design-27.1.0.aar"],
        sdk_version: "current",
        static_libs: ["android-support-annotations", "android-support-compat", "android-support-core-ui", "android-support-core-utils", "android-support-fragment", "android-support-transition", "android-support-v7-appcompat", "android-support-v7-cardview", "android-support-v7-recyclerview"],
    }''')
    def simplify(match):
     block=match.group(0)
     n=re.search(r'name:\s*"([^"]+)',block)
     if not n:return block
     name=n.group(1)
     if name=='android-support-design':
      block=re.sub(r'^[ \t]*"android-support-design-(?!nodeps")[^"]+",[ \t]*\n','',block,flags=re.M)
      block=block.replace('"android-support-design-nodeps",','"android-support-design-nodeps",\n        "android-support-design-widget-source",')
      if '"android-support-v7-cardview"' not in block:
       block=block.replace('"android-support-v7-appcompat",','"android-support-v7-appcompat",\n        "android-support-v7-cardview",')
      return block
     if name.startswith('android-support-design-') and not name.endswith('-nodeps') and name!='android-support-design-widget-source':
      return re.sub(r'static_libs:\s*\[.*?\]', 'static_libs: ["android-support-design-nodeps", "android-support-design-widget-source"]',block,flags=re.S)
     return block
    text=re.sub(r'android_library\s*\{.*?\n\}',simplify,text,flags=re.S)
    bp.write_text(text)
    
    sherpa=copy_source('sherpa','extras/constraint-layout-x')
    for f in sherpa.rglob('*'):
     if f.is_file() and f.suffix in ('.java','.xml'):
      text=f.read_text(encoding='utf-8')
      text=text.replace('android.support.constraint.solver','androidx.constraintlayout.solver')
      text=text.replace('android.support.constraint','androidx.constraintlayout.widget')
      f.write_text(text,encoding='utf-8')
    bp=p/'extras/constraint-layout-x/Android.bp'
    text=bp.read_text()
    text=replace_module(text,'androidx-constraintlayout_constraintlayout-nodeps','''android_library {
        name: "androidx-constraintlayout_constraintlayout-nodeps",
        sdk_version: "current",
        manifest: "walkdroid-source/sherpa/constraintlayout/src/main/AndroidManifest.xml",
        srcs: ["walkdroid-source/sherpa/constraintlayout/src/main/java/**/*.java"],
        resource_dirs: ["walkdroid-source/sherpa/constraintlayout/src/main/res"],
        static_libs: ["androidx-constraintlayout_constraintlayout-solver-nodeps"],
        java_version: "1.7",
    }''')
    text=replace_module(text,'androidx-constraintlayout_constraintlayout-solver-nodeps','''java_library_static {
        name: "androidx-constraintlayout_constraintlayout-solver-nodeps",
        sdk_version: "current",
        srcs: ["walkdroid-source/sherpa/solver/src/main/java/**/*.java"],
        java_version: "1.7",
    }''')
    bp.write_text(text)
    print('Integrated source-based Android library modules')

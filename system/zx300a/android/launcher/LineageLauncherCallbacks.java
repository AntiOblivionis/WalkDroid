/*
 * Copyright (C) 2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.launcher3.lineage;

import android.content.Intent;
import android.os.Bundle;
import com.android.launcher3.AppInfo;
import com.android.launcher3.LauncherCallbacks;
import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.ArrayList;

/** Launcher callbacks for the product without the optional Google Feed client. */
public class LineageLauncherCallbacks implements LauncherCallbacks {
    public static final String SEARCH_PACKAGE = "com.google.android.googlequicksearchbox";

    public LineageLauncherCallbacks(LineageLauncher launcher) { }
    @Override public void onCreate(Bundle state) { }
    @Override public void onResume() { }
    @Override public void onStart() { }
    @Override public void onStop() { }
    @Override public void onPause() { }
    @Override public void onDestroy() { }
    @Override public void onSaveInstanceState(Bundle state) { }
    @Override public void onActivityResult(int request, int result, Intent data) { }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) { }
    @Override public void onAttachedToWindow() { }
    @Override public void onDetachedFromWindow() { }
    @Override public void dump(String prefix, FileDescriptor fd, PrintWriter writer, String[] args) { }
    @Override public void onHomeIntent(boolean internalStateHandled) { }
    @Override public boolean handleBackPressed() { return false; }
    @Override public void onTrimMemory(int level) { }
    @Override public void onLauncherProviderChange() { }
    @Override public void bindAllApplications(ArrayList<AppInfo> apps) { }
    @Override public boolean startSearch(String query, boolean select, Bundle data) { return false; }
    @Override public boolean hasSettings() { return true; }
}

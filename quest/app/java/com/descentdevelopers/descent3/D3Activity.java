/*
 * Descent 3 - Meta Quest port.
 * Copyright (C) 2026 Descent Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package com.descentdevelopers.descent3;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

import org.libsdl.app.SDLActivity;

/**
 * The app's activity. On top of SDLActivity it passes the engine its
 * command line: -novr for the flat build flavour, plus launch arguments:
 *
 *   adb shell am start -n com.descentdevelopers.descent3/.D3Activity \
 *       --es args "-nointro -pilot Quest -mission d3 -loadlevel 1"
 *
 * Arguments are honoured only in debuggable builds: the activity is exported,
 * so otherwise any app on the device could pass engine options.
 */
public class D3Activity extends SDLActivity {
    private static final String TAG = "Descent3";

    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        if (!vrEnabled()) {
            args.add("-novr");
        }
        Intent intent = getIntent();
        String extra = intent != null ? intent.getStringExtra("args") : null;
        if (extra != null && !extra.isEmpty()) {
            if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
                for (String a : split(extra)) {
                    args.add(a);
                }
            } else {
                Log.w(TAG, "ignoring launch arguments: not a debuggable build");
            }
        }
        if (!args.isEmpty()) {
            Log.i(TAG, "launch arguments: " + String.join(" ", args));
        }
        return args.toArray(new String[0]);
    }

    /** False for the flat (2D panel) build flavour; see quest/build_apk.sh. */
    private boolean vrEnabled() {
        try {
            Bundle meta = getPackageManager()
                    .getApplicationInfo(getPackageName(), PackageManager.GET_META_DATA).metaData;
            return meta == null || meta.getBoolean("D3VR_ENABLED", true);
        } catch (PackageManager.NameNotFoundException e) {
            return true;
        }
    }

    /** Splits on whitespace; double quotes group words ("my pilot"). */
    static String[] split(String s) {
        List<String> out = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        boolean quoted = false;
        boolean inToken = false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"') {
                quoted = !quoted;
                inToken = true;
            } else if (Character.isWhitespace(c) && !quoted) {
                if (inToken) {
                    out.add(cur.toString());
                    cur.setLength(0);
                    inToken = false;
                }
            } else {
                cur.append(c);
                inToken = true;
            }
        }
        if (inToken) {
            out.add(cur.toString());
        }
        return out.toArray(new String[0]);
    }
}

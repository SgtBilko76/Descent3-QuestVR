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
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

import org.libsdl.app.SDLActivity;

/**
 * The app's activity. For now it only adds engine command-line arguments on
 * top of SDLActivity:
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
        Intent intent = getIntent();
        String args = intent != null ? intent.getStringExtra("args") : null;
        if (args == null || args.isEmpty()) {
            return new String[0];
        }
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            Log.w(TAG, "ignoring launch arguments: not a debuggable build");
            return new String[0];
        }
        String[] parsed = split(args);
        Log.i(TAG, "launch arguments: " + String.join(" ", parsed));
        return parsed;
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

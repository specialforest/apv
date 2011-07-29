/*
 * Copyright 2010 Ludovic Drolez
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

package cx.hell.android.pdfview;

import java.io.File;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.SQLException;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;

/**
 * Class to manage the last opened page and bookmarks of PDF files
 * 
 * @author Ludovic Drolez
 * 
 */
public class Bookmark {

	/** db fields */
	public static final String KEY_ID = "_id";
	public static final String KEY_BOOK = "book";
	public static final String KEY_NAME = "name";
	public static final String KEY_PAGE = "page";
	public static final String KEY_COMMENT = "comment";
	public static final String KEY_TIME = "time";
	public static final String KEY_ZOOM = "zoom";
	public static final String KEY_ROTATION = "rotation";

	private static final int DB_VERSION = 4;

	private static final String DATABASE_CREATE = "create table bookmark "
			+ "(_id integer primary key autoincrement, "
			+ "book text not null, name text not null, "
			+ "page integer, zoom integer, rotation integer, comment text, time integer);";

	private final Context context;

	private DatabaseHelper DBHelper;
	private SQLiteDatabase db;

	/**
	 * Constructor
	 * 
	 * @param ctx
	 *            application context
	 */
	public Bookmark(Context ctx) {
		this.context = ctx;
		DBHelper = new DatabaseHelper(context);
	}

	/**
	 * Database helper class
	 */
	private static class DatabaseHelper extends SQLiteOpenHelper {
		DatabaseHelper(Context context) {
			super(context, "bookmark.db", null, DB_VERSION);
		}

		@Override
		public void onCreate(SQLiteDatabase db) {
			db.execSQL(DATABASE_CREATE);
		}

		@Override
		public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
			if (oldVersion < 2) {
				db.execSQL("ALTER TABLE bookmark ADD COLUMN " + KEY_TIME + " integer");
			}
			if (oldVersion < 3) {
				db.execSQL("ALTER TABLE bookmark ADD COLUMN " + KEY_ZOOM + " integer");
			}
			if (oldVersion < 4) {
				db.execSQL("ALTER TABLE bookmark ADD COLUMN " + KEY_ROTATION + " integer");
			}
		}
	}

	/**
	 * open the bookmark database
	 * 
	 * @return the Bookmark object
	 * @throws SQLException
	 */
	public Bookmark open() throws SQLException {
		db = DBHelper.getWritableDatabase();
		return this;
	}

	/**
	 * close the database
	 */
	public void close() {
		DBHelper.close();
	}

	/**
	 * Set the last page seen for a file
	 * 
	 * @param file
	 *            full file path
	 * @param page
	 *            last page
	 */
	public void setLast(String file, int page, int zoomLevel, int rotation) {
		String md5 = nameToMD5(file);
		ContentValues cv = new ContentValues();
		cv.put(KEY_BOOK, md5);
		cv.put(KEY_PAGE, page);
		cv.put(KEY_ZOOM, zoomLevel);
		cv.put(KEY_ROTATION, rotation);
		cv.put(KEY_NAME, "last");
		cv.put(KEY_TIME, System.currentTimeMillis() / 1000);
		if (db.update("bookmark", cv, KEY_BOOK + "='" + md5 + "' AND "
				+ KEY_NAME + "= 'last'", null) == 0) {
			db.insert("bookmark", null, cv);
		}
	}
	
	public int getInt(String file, String key, int defaultValue) {
		int value = defaultValue;
		String md5 = nameToMD5(file);

		Cursor cur = db.query(true, "bookmark", new String[] { key },
				KEY_BOOK + "='" + md5 + "' AND " + KEY_NAME + "= 'last'", null,
				null, null, null, "1");
		if (cur != null) {
			if (cur.moveToFirst()) {
				value = cur.getInt(0);
			}
		}
		cur.close();
		return value;
	}

	/**
	 * Get the last recorded page for the given file
	 * 
	 * @param file
	 * @return page number (0-based) or 0 if not found
	 */
	public int getLastPage(String file) {
		return getInt(file, KEY_PAGE, 0);
	}

	/**
	 * Get the last recorded zoom for the given file
	 * 
	 * @param file
	 * @return zoom level or 0 if not found
	 */
	public int getLastZoom(String file) {
		return getInt(file, KEY_ZOOM, 0);
	}

	/**
	 * Get the last recorded rotation for the given file
	 * 
	 * @param file
	 * @return rotation or 0 if not found
	 */
	public int getLastRotation(String file) {
		return getInt(file, KEY_ROTATION, 0);
	}

	/**
	 * Hash the file name to be sure that no strange characters will be in the
	 * DB, and include file length.
	 * 
	 * @param file
	 *            path
	 * @return md5
	 */
	private String nameToMD5(String file) {
		// Create MD5 Hash
		MessageDigest digest;
		try {
			digest = java.security.MessageDigest.getInstance("MD5");
		} catch (NoSuchAlgorithmException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return "";
		}
		
		String message = file + ":" + (new File(file)).length();

		digest.update(message.getBytes());
		byte messageDigest[] = digest.digest();
		StringBuffer hexString = new StringBuffer();
		for (int i = 0; i < messageDigest.length; i++)
			hexString.append(Integer.toHexString(0xFF & messageDigest[i]));
		return hexString.toString();
	}
}

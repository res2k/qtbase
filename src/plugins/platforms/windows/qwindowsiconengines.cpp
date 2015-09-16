/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwindowsintegration.h"
#include "qwindowsiconengines.h"
#include "qwindowsscaling.h"

#include <QtCore/qset.h>

// Defined in qpixmap_win.cpp
Q_GUI_EXPORT QPixmap qt_pixmapFromWinHICON(HICON icon);

QWindowsResourceIconEngine::QWindowsResourceIconEngine()
{
}

QWindowsResourceIconEngine::QWindowsResourceIconEngine(const QWindowsResourceIconEngine& other)
    : QPixmapIconEngine(other)
{
}

static HMODULE LoadLibraryAsDataFile (const QString& path)
{
    if (QSysInfo::WindowsVersion >= QSysInfo::WV_VISTA
        && (QSysInfo::WindowsVersion & QSysInfo::WV_NT_based)) {
        return LoadLibraryExW(reinterpret_cast<LPCWSTR> (path.unicode()),
            NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    }
    else {
        return LoadLibraryExW(reinterpret_cast<LPCWSTR> (path.unicode()),
            NULL, LOAD_LIBRARY_AS_DATAFILE);
    }
}

bool QWindowsResourceIconEngine::load(const QString& path, const QString& resourceName)
{
    HMODULE module = LoadLibraryAsDataFile(path);
    bool result = load(module, reinterpret_cast<LPCWSTR> (resourceName.unicode()));
    FreeLibrary(module);
    return result;
}

bool QWindowsResourceIconEngine::load(const QString& path, int iconIndex)
{
    HMODULE module = LoadLibraryAsDataFile(path);
    bool result = false;
    if (LPCWSTR resourceName = resourceFromIconIndex (module, iconIndex)) {
        result = load(module, resourceName);
    }
    FreeLibrary(module);
    return result;
}

#include <pshpack1.h>

// As found here: http://blogs.msdn.com/b/oldnewthing/archive/2012/07/20/10331787.aspx
typedef struct GRPICONDIRENTRY
{
    BYTE  bWidth;
    BYTE  bHeight;
    BYTE  bColorCount;
    BYTE  bReserved;
    WORD  wPlanes;
    WORD  wBitCount;
    DWORD dwBytesInRes;
    WORD  nId;
} GRPICONDIRENTRY;

typedef struct GRPICONDIR
{
    WORD idReserved;
    WORD idType;
    WORD idCount;
    GRPICONDIRENTRY idEntries[1];
} GRPICONDIR;

#include <poppack.h>

bool QWindowsResourceIconEngine::load(HMODULE module, LPCWSTR resourceName)
{
    HRSRC iconGroupResource = FindResourceW(module, resourceName, RT_GROUP_ICON);
    if (!iconGroupResource) return false;

    HGLOBAL iconGroupData = LoadResource(module, iconGroupResource);
    if (!iconGroupData) return false;

    LPVOID iconGroupPtr = LockResource(iconGroupData);
    DWORD iconGroupSize = SizeofResource(module, iconGroupResource);
    if (!iconGroupPtr || (iconGroupSize < (sizeof(GRPICONDIR) - sizeof(GRPICONDIRENTRY)))) return false;

    // Extract icon sizes from icon group
    const GRPICONDIR* iconDir = reinterpret_cast<const GRPICONDIR*> (iconGroupPtr);
    QSet<int> iconSizes;
    for (unsigned int i = 0; i < iconDir->idCount; i++) {
        int size = iconDir->idEntries[i].bHeight;
        if (size == 0) size = 256;
        iconSizes.insert(size);
    }
    const int scaleFactor = QWindowsScaling::factor();
    // Let LoadImage() do image loading heavy lifting
    bool hasImage = false;
    Q_FOREACH (int size, iconSizes) {
        HICON icon = reinterpret_cast<HICON> (LoadImageW (module,
            resourceName, IMAGE_ICON, size, size, 0));
        if (icon) {
            QPixmap pixmap = qt_pixmapFromWinHICON(icon);
            pixmap.setDevicePixelRatio (scaleFactor);
            addPixmap(pixmap, QIcon::Normal, QIcon::Off);
            DestroyIcon(icon);
            hasImage = true;
        }
    }
    return hasImage;
}

struct EnumIconGroupInfo
{
    int countdown;
    LPCWSTR foundRes;
};

static BOOL CALLBACK EnumIconGroup (HMODULE /*module*/, LPCWSTR /*type*/,
                                    LPWSTR name, LONG_PTR lParam)
{
    EnumIconGroupInfo* enumInfo = reinterpret_cast<EnumIconGroupInfo*> (lParam);
    if (enumInfo->countdown == 0)
    {
        enumInfo->foundRes = name;
        return FALSE;
    }
    --enumInfo->countdown;
    return TRUE;
}

LPCWSTR QWindowsResourceIconEngine::resourceFromIconIndex(HMODULE module, int iconIndex)
{
    if (iconIndex < 0) return MAKEINTRESOURCE (-iconIndex);

    EnumIconGroupInfo enumInfo;
    enumInfo.countdown = iconIndex;
    enumInfo.foundRes = 0;
    EnumResourceNamesW(module, RT_GROUP_ICON, &EnumIconGroup,
        reinterpret_cast<LONG_PTR>(&enumInfo));
    // EnumIconGroup wil set foundRes if the icon was found
    return enumInfo.foundRes;
}


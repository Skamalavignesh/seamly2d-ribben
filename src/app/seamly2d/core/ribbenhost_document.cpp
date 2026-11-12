//-----------------------------------------------------------------------------
//  @file   ribbenhost_document.cpp
//
//  @brief
//  Ribben addon: the pattern file for live clients -- create a new pattern
//  (File > New without its dialog, saved at once to a new file) and save the
//  open pattern to its own file.
//
//  Neither ever discards work or replaces a file. Every request is checked
//  completely before anything changes: a new pattern needs a full .sm2d path
//  that does not exist yet, and an open pattern is closed only when the caller
//  names exactly that file and it has no unsaved changes (then as File > Close
//  Pattern closes it). Saving needs the caller to name the open pattern's own
//  file, so it can never write over a different one.
//
//  @copyright
//  This source code is part of the Seamly2D project, a pattern making
//  program to create and model patterns of clothing.
//  Copyright (C) 2013-2026 Seamly2D project
//  <https://github.com/fashionfreedom/seamly2d> All Rights Reserved.
//
//  Seamly2D is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Seamly2D is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Seamly2D.  If not, see <http://www.gnu.org/licenses/>.
//-----------------------------------------------------------------------------

#include "ribbenhostsupport.h"
#include "ui_mainwindow.h"

#include "../ifc/xml/vpatternconverter.h"
#include "../vmisc/vlockguard.h"
#include "../vwidgets/mouse_coordinates.h"
#include "../vwidgets/vmaingraphicsscene.h"
#include "../vwidgets/vmaingraphicsview.h"

#include <QDir>
#include <QFileInfo>

using namespace RibbenSupport;

namespace
{
    // The units File > New offers (DialogNewPattern).
    bool IsPatternUnit(const QString &unit)
    {
        return unit == UnitsToStr(Unit::Cm) || unit == UnitsToStr(Unit::Mm) || unit == UnitsToStr(Unit::Inch);
    }

    bool SameFile(const QString &a, const QString &b)
    {
        return !a.isEmpty() && !b.isEmpty() && QFileInfo(a) == QFileInfo(b);
    }
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::createPattern(const QString &filePath, const QString &draftBlockName, const QString &unit,
                                      const QString &closeFile)
{
    // -- Check everything before changing anything --
    if (draftBlockName.trimmed().isEmpty())
    {
        throw RibbenError(RibbenError::InvalidParams, QStringLiteral("\"draft_block_name\" must not be empty."));
    }
    if (!IsPatternUnit(unit))
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("\"unit\" must be cm, mm or inch, not \"%1\".").arg(unit));
    }
    const QFileInfo file(filePath);
    if (filePath.isEmpty() || !file.isAbsolute())
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("\"file_path\" must be a full path, not \"%1\".").arg(filePath));
    }
    if (file.suffix() != sm2dExt)
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("\"file_path\" must end in .%1: \"%2\".").arg(sm2dExt, filePath));
    }
    if (file.exists())
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("%1 already exists; a new pattern never replaces a file.").arg(filePath));
    }
    if (!file.absoluteDir().exists())
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("The folder %1 does not exist.").arg(file.absolutePath()));
    }

    QString closedFile;
    if (draftBlockComboBox->count() > 0)
    {
        const QString open = RibbenPatternFilePath();
        const QString shown = open.isEmpty() ? QStringLiteral("an unsaved pattern") : open;
        if (closeFile.isEmpty())
        {
            throw RibbenError(RibbenError::PatternOpen,
                              QStringLiteral("Seamly2D already has a pattern open (%1). Close it with File > Close "
                                             "Pattern, then retry. Nothing was changed.").arg(shown));
        }
        if (!SameFile(open, closeFile))
        {
            throw RibbenError(RibbenError::PatternOpen,
                              QStringLiteral("The open pattern is %1, not %2, so it was left open. Close it with "
                                             "File > Close Pattern, then retry. Nothing was changed.")
                                  .arg(shown, closeFile));
        }
        if (isWindowModified() || doc->IsModified())
        {
            throw RibbenError(RibbenError::PatternOpen,
                              QStringLiteral("%1 has unsaved changes. Save or close it in Seamly2D first. Nothing "
                                             "was changed.").arg(open));
        }
        closedFile = open;
    }

    // -- Close the named file (as File > Close Pattern does once nothing needs saving) --
    if (!closedFile.isEmpty())
    {
        FileClosedCorrect();
        Clear();
    }

    // -- File > New, without its dialog --
    qApp->setPatternUnit(StrToUnits(unit));
    VMainGraphicsView::NewSceneRect(draftScene, ui->view);
    VMainGraphicsView::NewSceneRect(pieceScene, ui->view);
    addDraftBlock(draftBlockName);
    if (mouseCoordinates == nullptr)
    {
        mouseCoordinates = new MouseCoordinates(qApp->patternUnit());
        ui->statusBar->addPermanentWidget((mouseCoordinates));
    }
    m_curFileFormatVersion = VPatternConverter::PatternMaxVer;
    m_curFileFormatVersionStr = VPatternConverter::PatternMaxVerStr;
    patternReadOnly = false;

    // -- Save it at once, and lock the file as File > Save As does --
    const QString fileName = file.absoluteFilePath();
    QString error;
    if (!SavePattern(fileName, error))
    {
        throw RibbenError(RibbenError::InternalError,
                          QStringLiteral("The new pattern is open in Seamly2D but could not be saved to %1: %2")
                              .arg(fileName, error));
    }
    VlpCreateLock(m_lock, fileName);

    QJsonObject result = getStatus();
    result.insert(QStringLiteral("closed_file"), closedFile.isEmpty() ? QJsonValue() : QJsonValue(closedFile));
    result.insert(QStringLiteral("locked"), m_lock && m_lock->IsLocked());
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::saveOpenPattern(const QString &filePath)
{
    if (draftBlockComboBox->count() == 0)
    {
        throw RibbenError(RibbenError::NoPatternOpen, QStringLiteral("No pattern is open."));
    }
    const QString open = RibbenPatternFilePath();
    if (open.isEmpty())
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("The open pattern has never been saved, so it has no file to save to."));
    }
    if (!SameFile(open, filePath))
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("The open pattern is %1, not %2; nothing was saved.").arg(open, filePath));
    }
    if (patternReadOnly)
    {
        throw RibbenError(RibbenError::InvalidParams, QStringLiteral("%1 is read only; nothing was saved.").arg(open));
    }
    if (m_curFileFormatVersion < VPatternConverter::PatternMaxVer)
    {
        throw RibbenError(RibbenError::InvalidParams,
                          QStringLiteral("%1 is in an older file format; File > Save asks before rewriting it, so "
                                         "nothing was saved.").arg(open));
    }

    const bool wasModified = isWindowModified() || doc->IsModified();
    QString error;
    if (!SavePattern(open, error))
    {
        throw RibbenError(RibbenError::InternalError, QStringLiteral("Could not save %1: %2").arg(open, error));
    }
    QJsonObject result = getStatus();
    result.insert(QStringLiteral("had_unsaved_changes"), wasModified);
    return result;
}

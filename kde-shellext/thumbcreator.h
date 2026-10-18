/// thumbcreator.h
///
/// Copyright 2023 Henry Kroll III <nospam@thenerdshow.com>
///
/// This program is free software; you can redistribute it and/or modify
/// it under the terms of the GNU General Public License as published by
/// the Free Software Foundation; either version 2 of the License, or
/// (at your option) any later version.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program; if not, write to the Free Software
/// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
/// MA 02110-1301, USA.
///
/// KDE thumbnail plugin (KIO::ThumbnailCreator) for 3MF and STL files.
/// Dolphin and friends load thumbnailers from a shared library, not from a
/// freedesktop .thumbnailer, so this is the KDE counterpart of the
/// command-line tool. It draws with the shared mesh renderer directly.

#ifndef THREEMF_THUMBCREATOR_H
#define THREEMF_THUMBCREATOR_H

#include <KIO/ThumbnailCreator>

#include <QByteArray>
#include <QImage>

class ThumbnailCreator : public KIO::ThumbnailCreator {
  Q_OBJECT
public:
  ThumbnailCreator(QObject *parent, const QVariantList &args);
  KIO::ThumbnailResult create(const KIO::ThumbnailRequest &request) override;
};

#endif

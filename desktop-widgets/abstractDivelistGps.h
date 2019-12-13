// SPDX-License-Identifier: GPL-2.0
#ifndef ABSTRACTDIVELISTGPS_H
#define ABSTRACTDIVELISTGPS_H

#include "core/dive.h"

//#include <QIcon>
#include <QWidget>

class AbstractDivelistGps : public QWidget {
	Q_OBJECT
public:
	AbstractDivelistGps(const QString& gpsFilename, dive *dive);
	QString gpsFilename() const;
	dive *dive;

	virtual void readGpsFile() = 0;
	virtual void setCoordinates() = 0;

signals:

private:
	QString _gpsFilename;
};
#endif

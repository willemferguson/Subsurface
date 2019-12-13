// SPDX-License-Identifier: GPL-2.0
#ifndef DIVELIST_GPS_H
#define DIVELIST_GPS__H

class DivelistGPS : public QDialog {
	Q_OBJECT
public:
	explicit DivelistGPS(QWidget *parent, QString fileName);
	void print();
private
slots:

private:
	QString fileName;
	Ui::DivelistGPS ui;
};

#endif


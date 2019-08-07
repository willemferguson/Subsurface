// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_INFORMATION_H
#define TAB_DIVE_INFORMATION_H

#include "TabBase.h"
#include "core/subsurface-qt/DiveListNotifier.h"

#define INFOBLUE #0000c0

namespace Ui {
	class TabDiveInformation;
};

class TabDiveInformation : public TabBase {
	Q_OBJECT
public:
	TabDiveInformation(QWidget *parent = 0);
	~TabDiveInformation();
	void updateData() override;
	void clear() override;
private slots:
	void divesChanged(const QVector<dive *> &dives, DiveField field);
	void on_atmPressVal_editingFinished();
	void on_atmPressType_currentIndexChanged(int index);
	void diveMode_Changed(int index);
	void on_visibility_valueChanged(int value);
	void on_wavesize_valueChanged(int value);
	void on_current_valueChanged(int value);
	void on_surge_valueChanged(int value);
	void on_chill_valueChanged(int value);
	void on_airtemp_editingFinished();
	void on_watertemp_editingFinished();
private:
	Ui::TabDiveInformation *ui;
	void updateProfile();
	void updateWhen();
	int pressTypeIndex;
	void updateTextBox(int event);
	void updateMode(struct dive *d);
	void divesEdited(int);
	void closeWarning();
};

#endif


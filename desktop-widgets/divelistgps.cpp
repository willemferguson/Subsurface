// SPDX-License-Identifier: GPL-2.0
#include "ui_divelistgps.h"
#include "desktop-widgets/divelistgps.h"
#include "core/dive.h"

#include <QFileDialog>
#include <QProcess>
#include <QMessageBox>

#include <stdlib.h>
#include <stdio.h>

DivelistGPS::DivelistGPS(QWidget *parent, QString fileName) : QDialog(parent),
	fileName(fileName)
{
	ui.setupUi(this);
}

void DivelistGPS::print()
{
fprintf(stderr,"Success\n");
}


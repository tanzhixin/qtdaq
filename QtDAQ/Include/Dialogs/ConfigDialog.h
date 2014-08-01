#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QMessageBox>
#include "ui_dialogConfig.h"
#include "AcquisitionConfig.h"
#include "DRS4/DRS.h"
#include "globals.h"

class ConfigDialog : public QDialog
{
	Q_OBJECT
public:
	ConfigDialog (AcquisitionConfig* s_config, QWidget * parent = 0, Qt::WindowFlags f = 0 );
	void updateConfig();
	void setDRS(DRS* s_drs);
private:
	void setUIFromConfig(AcquisitionConfig* s_config);
	void updateUI();
	
public:
	Ui::DialogConfig ui;	
public slots:
	//channels
	void onChannelCheckBoxChanged();
	void onChannelTriggerCheckBoxChanged();
	
	void onComboBoxSelfTriggeringChanged();
	void onVoltageOffsetChanged();
	void onTriggerThresholdChanged();
signals:
	void drsObjectChanged(DRS* drs, DRSBoard* board);
private:
	AcquisitionConfig* config;
	DRS* drs;
	DRSBoard* board;
	//UI arrays
	QCheckBox* checkBoxEnabled[NUM_DIGITIZER_CHANNELS];
	QCheckBox* checkBoxTriggerEnabled[NUM_DIGITIZER_CHANNELS];
};

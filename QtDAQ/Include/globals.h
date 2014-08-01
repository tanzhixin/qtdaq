#pragma once

#include "DRS4Acquisition.h"
#include <QString>
#include <QDateTime>

#ifdef _MSC_VER
#include "windows.h"
#endif

#define SAFE_DELETE( ptr ) \
if (ptr != NULL)      \
{                     \
    delete ptr;       \
    ptr = NULL;       \
}

#define SAFE_DELETE_ARRAY( ptr ) \
if (ptr != NULL)            \
{                           \
    delete[] ptr;           \
    ptr = NULL;             \
}


#define UI_SAVE_VERSION ((quint32)(0x02))
#define ANALYSIS_SAVE_VERSION ((quint32)(0x01))

enum HistogramParameter
{
	LONG_INTEGRAL=0,	
	SHORT_INTEGRAL=1,
	PSD_FACTOR=2,
	FILTERED_PSD_FACTOR=3,
	BASELINE=4,
	TIME_OF_CFD=5,
	CFD_DELTA=6,
	MAX_VALUE=7,
	MIN_VALUE=8,
	TIME_OF_MAX=9,
	TIME_OF_MIN=10,
	HALF_RISE_VALUE=11,
	TIME_OF_HALF_RISE=12,
	TEMPERATURE=13,
	SECONDS_FROM_FIRST_EVENT=14,
	TIME_OF_FLIGHT=15,
	CUSTOM_1=16,
	CUSTOM_2=17,
	CUSTOM_3=18,
	CUSTOM_4=19,
	CUSTOM_5=20
};

QString axisNameFromParameter(HistogramParameter parameter);
double valueFromStatsAndParameter(SampleStatistics stats, HistogramParameter parameter);
QString abbrFromParameter(HistogramParameter parameter);

float getSecondsFromFirstEvent(EventTimestamp firstEvent, EventTimestamp currentEvent);

struct EnergyCalibration
{
	double scale;
	double offset;
	bool calibrated;
};


extern bool useCustomParameterName[];
extern QString customParameterName[];
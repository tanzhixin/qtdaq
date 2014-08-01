#include "Threads/VxBinaryReaderThread.h"



EventVx* EventVx::eventFromInfoAndData(CAEN_DGTZ_EventInfo_t& info, CAEN_DGTZ_UINT16_EVENT_t* data)
{
	EventVx* ev=new EventVx();	
	memcpy(&(ev->info), &(info), sizeof(CAEN_DGTZ_EventInfo_t));
	memcpy(ev->data.ChSize, data->ChSize, MAX_UINT16_CHANNEL_SIZE*sizeof(uint16_t));

	for (int i=0;i<MAX_UINT16_CHANNEL_SIZE;i++)
	{
		int numSamples=data->ChSize[i];
		if (numSamples)
		{
			ev->data.DataChannel[i]=new uint16_t[numSamples];
			memcpy(ev->data.DataChannel[i], data->DataChannel[i], numSamples*sizeof(uint16_t));
		}
	}
	return ev;
}

EventVx* EventVx::eventFromInfoAndData(CAEN_DGTZ_EventInfo_t& info, CAEN_DGTZ_FLOAT_EVENT_t* fData)
{
	EventVx* ev=new EventVx();	
	memcpy(&(ev->info), &(info), sizeof(CAEN_DGTZ_EventInfo_t));
	memcpy(ev->fData.ChSize, fData->ChSize, MAX_UINT16_CHANNEL_SIZE*sizeof(uint16_t));

	for (int i=0;i<MAX_UINT16_CHANNEL_SIZE;i++)
	{
		int numSamples=fData->ChSize[i];
		if (numSamples)
		{
			ev->fData.DataChannel[i]=new float[numSamples];
			memcpy(ev->fData.DataChannel[i], fData->DataChannel[i], numSamples*sizeof(float));
		}
	}
	return ev;
}

void freeEvent(EventVx* &ev)
{
	for (int i=0;i<MAX_UINT16_CHANNEL_SIZE;i++)
	{
		if (ev->data.ChSize[i])
			SAFE_DELETE_ARRAY(ev->data.DataChannel[i]);
	}
	SAFE_DELETE(ev);
}

void freeEvent(EventVx &ev)
{
	for (int i=0;i<MAX_UINT16_CHANNEL_SIZE;i++)
	{
		if (ev.data.ChSize[i])
			SAFE_DELETE_ARRAY(ev.data.DataChannel[i]);
	}
}

DataHeader::DataHeader()
{
	int x=sizeof(DataHeader);
	memset(this, 0, sizeof(DataHeader));
	time(&dateTime);
	sprintf(description, "Test output.");
	sprintf(futureUse, "RSV");
	sprintf(magicNumber, "UCT001B");
	MSPS=4000;
	numEvents=2;
	numSamples=1024;
	offsetDC=1000;
	peakSaturation=1;
}

VxBinaryReaderThread::VxBinaryReaderThread(QMutex* s_rawBuffer1Mutex, QMutex* s_rawBuffer2Mutex, EventVx* s_rawBuffer1, EventVx* s_rawBuffer2, QObject *parent)
	: QThread(parent)
{
	inputFileCompressed=NULL;
	updateTimer=NULL;
	doRewindFile=false;
	doExitReadLoop=false;
	isReadingFile=false;
	requiresPause=false;

	rawBuffers[0]=s_rawBuffer1;
	rawBuffers[1]=s_rawBuffer2;
	rawMutexes[0]=s_rawBuffer1Mutex;
	rawMutexes[1]=s_rawBuffer2Mutex;
}



bool VxBinaryReaderThread::initVxBinaryReaderThread(QString s_filename, bool isCompressedInput, AnalysisConfig* s_analysisConfig, int updateTime)
{	
	filename=s_filename;
	if (isReadingFile)
	{
		gzclose(inputFileCompressed);
		inputFileCompressed=NULL;

		doExitReadLoop=true;
	}

	analysisConfig=s_analysisConfig;
	if (inputFileCompressed)
		gzclose(inputFileCompressed);
	inputFileCompressed=gzopen(filename.toStdString().data(), "rb");

	if (!inputFileCompressed)
	{
		inputFileCompressed=NULL;
		return false;
	}
	if (gzread(inputFileCompressed, &header, sizeof(DataHeader))!=sizeof(DataHeader))
	{
		//problem with the header file!
		gzclose(inputFileCompressed);
		inputFileCompressed=NULL;
		return false;
	}
	
	numEventsRead=0;
	if (updateTimer)
	{
		updateTimer->stop();
		updateTimer->disconnect();
		SAFE_DELETE(updateTimer);
	}

	//start reading into buffer0, but need to lock and unlock buffer1 as well to make sure it's free
	rawMutexes[0]->lock();
	rawMutexes[1]->lock();
	for (int i=0;i<EVENT_BUFFER_SIZE;i++)	
	{
		rawBuffers[0][i].processed=false;
		rawBuffers[1][i].processed=false;
	}
	rawMutexes[1]->unlock();
	currentBufferIndex=0;
	currentBufferPosition=0;

	updateTimer=new QTimer();
	updateTimer->setInterval(updateTime);
	connect(updateTimer, SIGNAL(timeout()), this, SLOT(onUpdateTimerTimeout()));
    updateTimer->start();
	doExitReadLoop=false;
	return true;
}

void VxBinaryReaderThread::rewindFile()
{
	//if eof has been reached
	if (gzeof(inputFileCompressed) || !inputFileCompressed)
	{
		if (inputFileCompressed)
			gzclose(inputFileCompressed);
		numEventsRead=0;
		initVxBinaryReaderThread(filename, true, analysisConfig, updateTimer->interval());
		start();
	}
	else
		doRewindFile=true;
}

void VxBinaryReaderThread::run()
{	
	isReadingFile=true;
	currentBufferIndex=0;
	currentBufferPosition=0;
	int eventCounter=0;

	while (inputFileCompressed && !gzeof(inputFileCompressed) && !doExitReadLoop)
	{
		pauseMutex.lock();
		if (requiresPause)
		{
			pauseMutex.unlock();
			msleep(100);
			continue;
		}
		else
			pauseMutex.unlock();
		
		if (doRewindFile)
		{
			gzrewind(inputFileCompressed);	
			if (gzread(inputFileCompressed, &header, sizeof(DataHeader))!=sizeof(DataHeader))
			{
				//problem with the header file!
				gzclose(inputFileCompressed);
				inputFileCompressed=NULL;
				break;
				isReadingFile=true;
			}			
			numEventsRead=0;
			doRewindFile=false;
		}
		//read

		//release and swap buffers when position overflows
		if (currentBufferPosition>=EVENT_BUFFER_SIZE)
		{
			rawMutexes[currentBufferIndex]->unlock();
			//swap
			currentBufferIndex=1-currentBufferIndex;
			rawMutexes[currentBufferIndex]->lock();
			currentBufferPosition=0;			
		}

		EventVx& ev=rawBuffers[currentBufferIndex][currentBufferPosition];
		//retain previous event sizes
		uint32_t prevEventChSize[MAX_UINT16_CHANNEL_SIZE];
		uint32_t prevEventChSizeFloat[MAX_UINT16_CHANNEL_SIZE];
		memcpy(prevEventChSize, ev.data.ChSize, sizeof(uint32_t)*MAX_UINT16_CHANNEL_SIZE);		
		memcpy(prevEventChSizeFloat, ev.fData.ChSize, sizeof(uint32_t)*MAX_UINT16_CHANNEL_SIZE);		
		//ev.processed=false;

		if (gzread(inputFileCompressed, &ev.info, sizeof(CAEN_DGTZ_EventInfo_t))!=sizeof(CAEN_DGTZ_EventInfo_t))
		{
			freeEvent(ev);
			break;
		}
		
		//vx1742: BoardId of 2
		if (ev.info.BoardId==2)
		{
			vx1742Mode=true;
			//vx1742 in 1 GSPS mode by default
			header.MSPS=1000;

			uint8_t GrPresent[MAX_X742_GROUP_SIZE];
			uint32_t ChSize[MAX_X742_CHANNEL_SIZE];
			int adjustedChNum=0;
			//read which groups are present
			if (gzread(inputFileCompressed, GrPresent, sizeof(uint8_t) * MAX_X742_GROUP_SIZE)!= sizeof(uint8_t) * MAX_X742_GROUP_SIZE)
			{
				freeEvent(ev);
				break;
			}
			for (int gr=0;gr<MAX_X742_GROUP_SIZE;gr++)
			{
				if (GrPresent[gr])
				{
					//if there is a group present, read the channel sizes out
					if (gzread(inputFileCompressed, ChSize, sizeof(uint32_t) * MAX_X742_CHANNEL_SIZE)!= sizeof(uint32_t) * MAX_X742_CHANNEL_SIZE)
					{
						freeEvent(ev);
						break;
					}

					//run through each channel, if channel is present, read data
					for (int ch=0;ch<MAX_X742_CHANNEL_SIZE;ch++)
					{
						if (ChSize[ch])
						{							
							int i=gr*MAX_X742_CHANNEL_SIZE+ch;
							ev.fData.ChSize[i]=ChSize[ch];
							//clear and re-init array if size change is required
							if (ev.fData.ChSize[i]!=prevEventChSizeFloat[i])
							{
								SAFE_DELETE_ARRAY(ev.fData.DataChannel[i]);
								ev.fData.DataChannel[i]=new float[ev.fData.ChSize[i]];
							}
							int s=gzread(inputFileCompressed, ev.fData.DataChannel[i], sizeof(float) * ev.fData.ChSize[i]);
							if (s!=sizeof(float)*ev.fData.ChSize[i])
							{
								freeEvent(ev);
								break;
							}
						}
					}

				}
			}
		}
		else
		{
			vx1742Mode=false;
			header.MSPS=4000;

			if (gzread(inputFileCompressed, &ev.data.ChSize, sizeof(uint32_t) * MAX_UINT16_CHANNEL_SIZE)!= sizeof(uint32_t) * MAX_UINT16_CHANNEL_SIZE)
			{
				freeEvent(ev);
				break;
			}
			//error handling: bad event
			if (ev.info.BoardId>100 || ev.info.EventSize>20000)
			{
				freeEvent(ev);
				break;
			}
			for (int i=0;i<MAX_UINT16_CHANNEL_SIZE;i++)
			{
				if (!ev.data.ChSize[i])
					continue;
				//error handling: bad event
				if (ev.data.ChSize[0]>75000)
				{
					freeEvent(ev);
					break;
				}

				//clear and re-init array if size change is required
				if (ev.data.ChSize[i]!=prevEventChSize[i])
				{
					SAFE_DELETE_ARRAY(ev.data.DataChannel[i]);
					ev.data.DataChannel[i]=new uint16_t[ev.data.ChSize[i]];
				}

				int s=gzread(inputFileCompressed, ev.data.DataChannel[i], sizeof(uint16_t) * ev.data.ChSize[i]);
				if (s!=sizeof(uint16_t)*ev.data.ChSize[i])
				{
					freeEvent(ev);
					break;
				}
			}

		}

		ev.processed=false;
		currentBufferPosition++;
		numEventsRead++;		
	}
	rawMutexes[currentBufferIndex]->unlock();
	//swap and lock other buffer to prevent re-processing of data
	currentBufferIndex=1-currentBufferIndex;
	//rawMutexes[currentBufferIndex]->lock();
	stopReading(false);	
}

void VxBinaryReaderThread::onUpdateTimerTimeout()
{
	
}

void VxBinaryReaderThread::setPaused(bool paused)
{
	pauseMutex.lock();
	requiresPause=paused;
	pauseMutex.unlock();
}

void VxBinaryReaderThread::stopReading(bool forceExit)
{
	if (inputFileCompressed)
	{
		gzclose(inputFileCompressed);
		inputFileCompressed=NULL;
	}
	isReadingFile=false;
	//only emit if we're finished reading without manual exit
	if (!forceExit)
		emit eventReadingFinished();
}

bool VxBinaryReaderThread::isReading()
{
	return isReadingFile;
}
/*******************************************************************************************************
 DkProcess.cpp
 Created on:	27.12.2014
 
 nomacs is a fast and small image viewer with the capability of synchronizing multiple instances
 
 Copyright (C) 2011-2014 Markus Diem <markus@nomacs.org>
 Copyright (C) 2011-2014 Stefan Fiel <stefan@nomacs.org>
 Copyright (C) 2011-2014 Florian Kleber <florian@nomacs.org>

 This file is part of nomacs.

 nomacs is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 nomacs is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 *******************************************************************************************************/

#include "DkProcess.h"
#include "DkUtils.h"
#include "DkImageContainer.h"
#include "DkImageStorage.h"
#include "DkPluginManager.h"
#include "DkSettings.h"
#include "DkMath.h"

#pragma warning(push, 0)	// no warnings from includes - begin
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrentMap>
#include <QWidget>
#include <QUuid>
#pragma warning(pop)		// no warnings from includes - end

namespace nmc {

/// <summary>
/// Generic compute method for DkBatch Info <see cref="DkBatchInfo"/>.
/// This method allows for a simplified interface if a derived class
/// just needs to process the image itself (not meta data).
/// </summary>
/// <param name="container">Container the image container to be processed.</param>
/// <param name="logStrings">log strings.</param>
/// <param name="batchInfo">The batch information. You can save any information into this class.</param>
/// <returns>
/// true on success
/// </returns>
bool DkAbstractBatch::compute(QSharedPointer<DkImageContainer> container, QStringList& logStrings, QVector<QSharedPointer<DkBatchInfo> >&) const {

	return compute(container, logStrings);
}

/// <summary>
/// Generic compute method.
/// This method allows for a simplified interface if a derived class
/// just needs to process the image itself (not meta data).
/// </summary>
/// <param name="container">Container the image container to be processed.</param>
/// <param name="logStrings">log strings.</param>
/// <returns>true on success</returns>
bool DkAbstractBatch::compute(QSharedPointer<DkImageContainer> container, QStringList& logStrings) const {
	
	QImage img = container->image();
	
	bool isOk = compute(img, logStrings);

	if (isOk)
		container->setImage(img, QObject::tr("Batch Action"));

	return isOk;
}

// DkResizeBatch --------------------------------------------------------------------
DkResizeBatch::DkResizeBatch() {

	mIplMethod = DkImage::ipl_area;	// define here because of included
}

QString DkResizeBatch::name() const {

	return QObject::tr("[Resize Batch]");
}

void DkResizeBatch::setProperties(float scaleFactor, int mode, int prop, int iplMethod, bool correctGamma) {

	mScaleFactor = scaleFactor;
	mMode = mode;
	mProperty = prop;
	mIplMethod = iplMethod;
	mCorrectGamma = correctGamma;
}

bool DkResizeBatch::isActive() const {

	if (mMode != mode_default)
		return true;

	if (mScaleFactor != 1.0f)
		return true;

	return false;
}

bool DkResizeBatch::compute(QImage& img, QStringList& logStrings) const {

	if (mScaleFactor == 1.0f) {
		logStrings.append(QObject::tr("%1 scale factor is 1 -> ignoring").arg(name()));
		return true;
	}

	QSize size;
	float sf = 1.0f;
	QImage tmpImg;

	if (prepareProperties(img.size(), size, sf, logStrings))
		tmpImg = DkImage::resizeImage(img, size, sf, mIplMethod, mCorrectGamma);
	else {
		logStrings.append(QObject::tr("%1 no need for resizing.").arg(name()));
		return true;
	}

	if (tmpImg.isNull()) {
		logStrings.append(QObject::tr("%1 could not resize image.").arg(name()));
		return false;
	}

	if (mMode == mode_default)
		logStrings.append(QObject::tr("%1 image resized, scale factor: %2%").arg(name()).arg(mScaleFactor*100.0f));
	else
		logStrings.append(QObject::tr("%1 image resized, new side: %2 px").arg(name()).arg(mScaleFactor));

	img = tmpImg;

	return true;
}

bool DkResizeBatch::prepareProperties(const QSize& imgSize, QSize& size, float& scaleFactor, QStringList& logStrings) const {

	float sf = 1.0f;
	QSize normalizedSize = imgSize; 

	if (mMode == mode_default) {
		scaleFactor = this->mScaleFactor;
		return true;
	}
	else if (mMode == mode_long_side) {
		
		if (imgSize.width() < imgSize.height())
			normalizedSize.transpose();
	}
	else if (mMode == mode_short_side) {

		if (imgSize.width() > imgSize.height())
			normalizedSize.transpose();
	}
	else if (mMode == mode_height)
		normalizedSize.transpose();

	sf = this->mScaleFactor/normalizedSize.width();

	if (sf > 1.0 && this->mProperty == prop_decrease_only) {
		
		logStrings.append(QObject::tr("%1 I need to increase the image, but the option is set to decrease only -> skipping.").arg(name()));
		return false;
	}
	else if (sf < 1.0f && this->mProperty == prop_increase_only) {
		logStrings.append(QObject::tr("%1 I need to decrease the image, but the option is set to increase only -> skipping.").arg(name()));
		return false;
	}
	else if (sf == 1.0f) {
		logStrings.append(QObject::tr("%1 image size matches scale factor -> skipping.").arg(name()));
		return false;
	}

	size.setWidth(qRound(this->mScaleFactor));
	size.setHeight(qRound(sf*normalizedSize.height()));

	if (normalizedSize != imgSize)
		size.transpose();

	return true;
}

// DkTransformBatch --------------------------------------------------------------------
DkBatchTransform::DkBatchTransform() {
}

QString DkBatchTransform::name() const {
	return QObject::tr("[Transform Batch]");
}

void DkBatchTransform::setProperties(int angle, bool horizontalFlip /* = false */, bool verticalFlip /* = false */, bool cropFromMetadata /* = false */) {
	
	mAngle = angle;
	mHorizontalFlip = horizontalFlip;
	mVerticalFlip = verticalFlip;
	mCropFromMetadata = cropFromMetadata;
}

bool DkBatchTransform::isActive() const {

	return mHorizontalFlip || mVerticalFlip || mAngle != 0 || mCropFromMetadata;
}

bool DkBatchTransform::compute(QSharedPointer<DkImageContainer> container, QStringList& logStrings) const {

	
	if (!isActive()) {
		logStrings.append(QObject::tr("%1 inactive -> skipping").arg(name()));
		return true;
	}

	DkRotatingRect rect = container->cropRect();
	if(mCropFromMetadata) {
		if (!rect.isEmpty())
			container->cropImage(rect, QColor(), false);
	}
	QImage img = container->image();
	QImage tmpImg;

	if (mAngle != 0) {
		QTransform rotationMatrix;
		rotationMatrix.rotate((double)mAngle);
		tmpImg = img.transformed(rotationMatrix);
	}
	else
		tmpImg = img;

	tmpImg = tmpImg.mirrored(mHorizontalFlip, mVerticalFlip);
	if (!tmpImg.isNull()) {
		img = tmpImg;
		if (rect.isEmpty() && mCropFromMetadata)
			logStrings.append(QObject::tr("%1 image transformed.").arg(name()));
		else
			logStrings.append(QObject::tr("%1 image transformed and cropped.").arg(name()));

	}
	else {
		logStrings.append(QObject::tr("%1 error, could not transform image.").arg(name()));
		return false;
	}

	return true;
}

#ifdef WITH_PLUGINS
// DkPluginBatch --------------------------------------------------------------------
DkPluginBatch::DkPluginBatch() {
}

void DkPluginBatch::setProperties(const QStringList & pluginList) {
	mPluginList = pluginList;
}

void DkPluginBatch::preLoad() {

	loadAllPlugins();
}

void DkPluginBatch::postLoad(const QVector<QSharedPointer<DkBatchInfo> >& batchInfo) const {

	for (int idx = 0; idx < mPlugins.size(); idx++) {

		QSharedPointer<DkPluginContainer> pluginContainer = mPlugins[idx];
		QString runID = mRunIDs[idx];

		if (pluginContainer) {
	
			// get plugin
			DkBatchPluginInterface* plugin = pluginContainer->batchPlugin();

			qDebug() << "[POST LOAD]" << pluginContainer->pluginName() << "id:" << runID;
			QVector<QSharedPointer<DkBatchInfo> > fInfos = DkBatchInfo::filter(batchInfo, runID);

			// check if it is ok
			if (plugin) {
				plugin->postLoadPlugin(fInfos);
			}
		}
	}
}

bool DkPluginBatch::compute(QSharedPointer<DkImageContainer> container, QStringList & logStrings, QVector<QSharedPointer<DkBatchInfo> >& batchInfos) const {

	if (!isActive()) {
		logStrings.append(QObject::tr("%1 inactive -> skipping").arg(name()));
		return true;
	}

	for (int idx = 0; idx < mPlugins.size(); idx++) {

		QSharedPointer<DkPluginContainer> pluginContainer = mPlugins[idx];
		QString runID = mRunIDs[idx];

		if (pluginContainer) {
			// get plugin
			DkPluginInterface* plugin = pluginContainer->plugin();

			// check if it is ok
			if ( plugin && 
				(plugin->interfaceType() == DkPluginInterface::interface_basic || 
				 plugin->interfaceType() == DkPluginInterface::interface_batch)) {

				// apply the plugin
				QSharedPointer<DkImageContainer> result;
				
				if (plugin->interfaceType() == DkPluginInterface::interface_basic)
					result = plugin->runPlugin(runID, container);
				else if (plugin->interfaceType() == DkPluginInterface::interface_batch) {

					DkBatchPluginInterface* bPlugin = pluginContainer->batchPlugin();
					QSharedPointer<DkBatchInfo> info;

					if (bPlugin)
						result = bPlugin->runPlugin(runID, container, info);
					else 
						logStrings.append(QObject::tr("%1 Cannot cast batch plugin %2.").arg(name()).arg(pluginContainer->pluginName()));

					batchInfos << info;
				}

				if (result && result->hasImage())
					container = result;
				else
					logStrings.append(QObject::tr("%1 Cannot apply %2.").arg(name()).arg(pluginContainer->pluginName()));
			}
			else
				logStrings.append(QObject::tr("%1 illegal plugin interface: %2").arg(name()).arg(pluginContainer->pluginName()));
		}
		else 
			logStrings.append(QObject::tr("%1 Cannot apply plugin because it is NULL.").arg(name()));
	}

	if (!container || !container->hasImage()) {
		logStrings.append(QObject::tr("%1 error, could not apply plugins.").arg(name()));
		return false;
	}
	else
		logStrings.append(QObject::tr("%1 plugins applied.").arg(name()));

	return true;
}

QString DkPluginBatch::name() const {
	return QObject::tr("[Plugin Batch]");
}

bool DkPluginBatch::isActive() const {
	
	return !mPluginList.empty();
}

void DkPluginBatch::loadAllPlugins() {

	QString runId;

	for (const QString& cPluginString : mPluginList) {

		// load plugin
		QSharedPointer<DkPluginContainer> pluginContainer;
		QString runID;
		loadPlugin(cPluginString, pluginContainer, runID);
		mPlugins << pluginContainer;	// also add the empty ones...
		mRunIDs << runID;

		if (pluginContainer) {

			qDebug() << "loading" << pluginContainer->pluginName() << "id:" << runID;

			// get plugin
			DkBatchPluginInterface* plugin = pluginContainer->batchPlugin();

			// check if it is ok
			if (plugin) {
				plugin->preLoadPlugin();
			}
		}
		else
			qWarning() << "could not load: " << cPluginString;
	}
}

void DkPluginBatch::loadPlugin(const QString & pluginString, QSharedPointer<DkPluginContainer> & plugin, QString& runID) const {

	QString uiSeparator = " | ";	// TODO: make a nice define

	QStringList ids = pluginString.split(uiSeparator);

	if (ids.size() != 2) {
		qWarning() << "plugin string does not match:" << pluginString;
	}
	else {
		plugin = DkPluginManager::instance().getPluginByName(ids[0]);

		if (plugin)
			runID = plugin->actionNameToRunId(ids[1]);
	}
}
#endif

// DkBatchProcess --------------------------------------------------------------------
DkBatchProcess::DkBatchProcess(const QString& filePathIn, const QString& filePathOut) {
	mFilePathIn = filePathIn;
	mFilePathOut = filePathOut;

	mMode = DkBatchConfig::mode_skip_existing;
}

void DkBatchProcess::setProcessChain(const QVector<QSharedPointer<DkAbstractBatch> > processes) {

	mProcessFunctions = processes;
}

void DkBatchProcess::setMode(int mode) {

	mMode = mode;
}

void DkBatchProcess::setDeleteOriginal(bool deleteOriginal) {

	mDeleteOriginal = deleteOriginal;
}

void DkBatchProcess::setCompression(int compression) {
	mCompression = compression;
}

QString DkBatchProcess::inputFile() const {

	return mFilePathIn;
}

QString DkBatchProcess::outputFile() const {

	return mFilePathOut;
}

QVector<QSharedPointer<DkBatchInfo> > DkBatchProcess::batchInfo() const {

	return mInfos;
}

bool DkBatchProcess::hasFailed() const {

	return mFailure != 0;
}

bool DkBatchProcess::wasProcessed() const {
	
	return mIsProcessed;
}

bool DkBatchProcess::compute() {

	mIsProcessed = true;

	QFileInfo fInfoIn(mFilePathIn);
	QFileInfo fInfoOut(mFilePathOut);

	// check errors
	if (fInfoOut.exists() && mMode == DkBatchConfig::mode_skip_existing) {
		mLogStrings.append(QObject::tr("%1 already exists -> skipping (check 'overwrite' if you want to overwrite the file)").arg(mFilePathOut));
		mFailure++;
		return mFailure == 0;
	}
	else if (!fInfoIn.exists()) {
		mLogStrings.append(QObject::tr("Error: input file does not exist"));
		mLogStrings.append(QObject::tr("Input: %1").arg(mFilePathIn));
		mFailure++;
		return mFailure == 0;
	}
	else if (mFilePathIn == mFilePathOut && mProcessFunctions.empty()) {
		mLogStrings.append(QObject::tr("Skipping: nothing to do here."));
		mFailure++;
		return mFailure == 0;
	}
	
	// do the work
	if (mProcessFunctions.empty() && mFilePathIn == mFilePathOut && fInfoIn.suffix() == fInfoOut.suffix()) {	// rename?
		if (!renameFile())
			mFailure++;
		return mFailure == 0;
	}
	else if (mProcessFunctions.empty() && fInfoIn.suffix() == fInfoOut.suffix()) {	// copy?
		if (!copyFile())
			mFailure++;
		else
			deleteOriginalFile();

		return mFailure == 0;
	}

	process();

	return mFailure == 0;
}

QStringList DkBatchProcess::getLog() const {

	return mLogStrings;
}

bool DkBatchProcess::process() {

	mLogStrings.append(QObject::tr("processing %1").arg(mFilePathIn));

	QSharedPointer<DkImageContainer> imgC(new DkImageContainer(mFilePathIn));

	if (!imgC->loadImage() || imgC->image().isNull()) {
		mLogStrings.append(QObject::tr("Error while loading..."));
		mFailure++;
		return false;
	}

	for (QSharedPointer<DkAbstractBatch> batch : mProcessFunctions) {

		if (!batch) {
			mLogStrings.append(QObject::tr("Error: cannot process a NULL function."));
			continue;
		}

		QVector<QSharedPointer<DkBatchInfo> > cInfos;
		if (!batch->compute(imgC, mLogStrings, cInfos)) {
			mLogStrings.append(QObject::tr("%1 failed").arg(batch->name()));
			mFailure++;
		}

		mInfos << cInfos;
	}

	// report we could not back-up & break here
	if (!prepareDeleteExisting()) {
		mFailure++;
		return false;
	}

	if (imgC->saveImage(mFilePathOut, mCompression)) {
		mLogStrings.append(QObject::tr("%1 saved...").arg(mFilePathOut));
	}
	else {
		mLogStrings.append(QObject::tr("Could not save: %1").arg(mFilePathOut));
		mFailure++;
	}

	if (!deleteOrRestoreExisting()) {
		mFailure++;
		return false;
	}

	return true;
}

bool DkBatchProcess::renameFile() {

	if (QFileInfo(mFilePathOut).exists()) {
		mLogStrings.append(QObject::tr("Error: could not rename file, the target file exists already."));
		return false;
	}

	QFile file(mFilePathIn);

	// Note: if two images are renamed at the same time to the same name, one image is lost -> see Qt comment Race Condition
	if (!file.rename(mFilePathOut)) {
		mLogStrings.append(QObject::tr("Error: could not rename file"));
		mLogStrings.append(file.errorString());
		return false;
	}
	else
		mLogStrings.append(QObject::tr("Renaming: %1 -> %2").arg(mFilePathIn).arg(mFilePathOut));

	return true;
}

bool DkBatchProcess::copyFile() {

	QFile file(mFilePathIn);

	if (QFileInfo(mFilePathOut).exists() && mMode == DkBatchConfig::mode_overwrite) {
		if (!deleteOrRestoreExisting())
			return false;	// early break
	}

	if (!file.copy(mFilePathOut)) {
		mLogStrings.append(QObject::tr("Error: could not copy file"));
		mLogStrings.append(QObject::tr("Input: %1").arg(mFilePathIn));
		mLogStrings.append(QObject::tr("Output: %1").arg(mFilePathOut));
		mLogStrings.append(file.errorString());
		return false;
	}
	else
		mLogStrings.append(QObject::tr("Copying: %1 -> %2").arg(mFilePathIn).arg(mFilePathOut));

	return true;
}

bool DkBatchProcess::prepareDeleteExisting() {

	if (QFileInfo(mFilePathOut).exists() && mMode == DkBatchConfig::mode_overwrite) {

		// create unique back-up file name
		QFileInfo buFile(mFilePathOut);
		buFile = QFileInfo(buFile.absolutePath(), buFile.baseName() + QUuid::createUuid().toString() + "." + buFile.suffix());

		// check the uniqueness : )
		if (buFile.exists()) {
			mLogStrings.append(QObject::tr("Error: back-up (%1) file already exists").arg(buFile.absoluteFilePath()));
			return false;
		}

		QFile file(mFilePathOut);

		if (!file.rename(buFile.absoluteFilePath())) {
			mLogStrings.append(QObject::tr("Error: could not rename existing file to %1").arg(buFile.absoluteFilePath()));
			mLogStrings.append(file.errorString());
			return false;
		}
		else
			mBackupFilePath = buFile.absoluteFilePath();
	}

	return true;
}

bool DkBatchProcess::deleteOrRestoreExisting() {

	QFileInfo outInfo(mFilePathOut);
	if (outInfo.exists() && !mBackupFilePath.isEmpty() && QFileInfo(mBackupFilePath).exists()) {
		QFile file(mBackupFilePath);

		if (!file.remove()) {
			mLogStrings.append(QObject::tr("Error: could not delete existing file"));
			mLogStrings.append(file.errorString());
			return false;
		}
	}
	// fall-back
	else if (!outInfo.exists()) {
		
		QFile file(mBackupFilePath);

		if (!file.rename(mFilePathOut)) {
			mLogStrings.append(QObject::tr("Ui - a lot of things went wrong sorry, your original file can be found here: %1").arg(mBackupFilePath));
			mLogStrings.append(file.errorString());
			return false;
		}
		else {
			mLogStrings.append(QObject::tr("I could not save to %1 so I restored the original file.").arg(mFilePathOut));
		}
	}

	return true;
}

bool DkBatchProcess::deleteOriginalFile() {

	if (mFilePathIn == mFilePathOut)
		return true;

	if (!mFailure && mDeleteOriginal) {
		QFile oFile(mFilePathIn);

		if (oFile.remove())
			mLogStrings.append(QObject::tr("%1 deleted.").arg(mFilePathIn));
		else {
			mFailure++;
			mLogStrings.append(QObject::tr("I could not delete %1").arg(mFilePathIn));
			return false;
		}
	}
	else if (mFailure)
		mLogStrings.append(QObject::tr("I did not delete the original because I detected %1 failure(s).").arg(mFailure));

	return true;
}

// DkBatchConfig --------------------------------------------------------------------
DkBatchConfig::DkBatchConfig(const QStringList& fileList, const QString& outputDir, const QString& fileNamePattern) {

	mFileList = fileList;
	mOutputDirPath = outputDir;
	mFileNamePattern = fileNamePattern;
	
	init();
};

void DkBatchConfig::init() {

	mCompression = -1;
	mMode = mode_skip_existing;
}

bool DkBatchConfig::isOk() const {

	if (mOutputDirPath.isEmpty())
		return false;

	QDir oDir(mOutputDirPath);

	if (!oDir.exists()) {
		if (!oDir.mkpath("."))
			return false;	// output dir does not exist & I cannot create it
	}

	if (mFileList.empty())
		return false;

	if (mFileNamePattern.isEmpty())
		return false;

	return true;
}

// DkBatchProcessing --------------------------------------------------------------------
DkBatchProcessing::DkBatchProcessing(const DkBatchConfig& config, QWidget* parent /*= 0*/) : QObject(parent) {

	mBatchConfig = config;

	connect(&mBatchWatcher, SIGNAL(progressValueChanged(int)), this, SIGNAL(progressValueChanged(int)));
	connect(&mBatchWatcher, SIGNAL(finished()), this, SIGNAL(finished()));
}

void DkBatchProcessing::init() {

	mBatchItems.clear();
	
	QStringList fileList = mBatchConfig.getFileList();

	for (int idx = 0; idx < fileList.size(); idx++) {

		QFileInfo cFileInfo = QFileInfo(fileList.at(idx));
		QString outDir = mBatchConfig.inputDirIsOutputDir() ? cFileInfo.absolutePath() : mBatchConfig.getOutputDirPath();

		DkFileNameConverter converter(cFileInfo.fileName(), mBatchConfig.getFileNamePattern(), idx);
		QFileInfo newFileInfo(outDir, converter.getConvertedFileName());

		DkBatchProcess cProcess(fileList.at(idx), newFileInfo.absoluteFilePath());
		cProcess.setMode(mBatchConfig.getMode());
		cProcess.setDeleteOriginal(mBatchConfig.getDeleteOriginal());
		cProcess.setProcessChain(mBatchConfig.getProcessFunctions());
		cProcess.setCompression(mBatchConfig.getCompression());

		mBatchItems.push_back(cProcess);
	}
}

void DkBatchProcessing::compute() {

	init();

	qDebug() << "computing...";

	if (mBatchWatcher.isRunning())
		mBatchWatcher.waitForFinished();

	QFuture<void> future = QtConcurrent::map(mBatchItems, &nmc::DkBatchProcessing::computeItem);
	mBatchWatcher.setFuture(future);
}

bool DkBatchProcessing::computeItem(DkBatchProcess& item) {

	return item.compute();
}

void DkBatchProcessing::postLoad() {

	// collect batch infos
	QVector<QSharedPointer<DkBatchInfo> > batchInfo;

	for (DkBatchProcess batch : mBatchItems) {
		batchInfo << batch.batchInfo();
	}

	for (QSharedPointer<DkAbstractBatch> fun : mBatchConfig.getProcessFunctions()) {
		fun->postLoad(batchInfo);
	}
}

QStringList DkBatchProcessing::getLog() const {

	QStringList log;

	for (DkBatchProcess batch : mBatchItems) {

		log << batch.getLog();
		log << "";	// add empty line between images
	}

	return log;
}

int DkBatchProcessing::getNumFailures() const {

	int numFailures = 0;

	for (DkBatchProcess batch : mBatchItems) {
		
		if (batch.hasFailed())
			numFailures++;
	}

	return numFailures;
}

int DkBatchProcessing::getNumProcessed() const {

	int numProcessed = 0;

	for (DkBatchProcess batch : mBatchItems) {

		if (batch.wasProcessed())
			numProcessed++;
	}

	return numProcessed;
}

QList<int> DkBatchProcessing::getCurrentResults() {

	if (mResList.empty()) {
		for (int idx = 0; idx < mBatchItems.size(); idx++)
			mResList.append(batch_item_not_computed);
	}

	for (int idx = 0; idx < mResList.size(); idx++) {

		if (mResList.at(idx) != batch_item_not_computed)
			continue;

		if (mBatchItems.at(idx).wasProcessed())
			mResList[idx] = mBatchItems.at(idx).hasFailed() ? batch_item_failed : batch_item_succeeded;
	}

	return mResList;
}

QStringList DkBatchProcessing::getResultList() const {

	QStringList results;

	for (DkBatchProcess batch : mBatchItems) {

		if (batch.wasProcessed())
			results.append(getBatchSummary(batch));
	}

	return results;
}

QString DkBatchProcessing::getBatchSummary(const DkBatchProcess& batch) const {

	QString res = batch.inputFile() + "\t";

	if (!batch.hasFailed())
		res += " <span style=\" color:#00aa00;\">" + tr("[OK]") + "</span>";
	else
		res += " <span style=\" color:#aa0000;\">" + tr("[FAIL]") + "</span>";

	return res;
}

void DkBatchProcessing::waitForFinished() {
	mBatchWatcher.waitForFinished();
}

int DkBatchProcessing::getNumItems() const {

	return mBatchItems.size();
}

bool DkBatchProcessing::isComputing() const {

	return mBatchWatcher.isRunning();
}

void DkBatchProcessing::cancel() {

	mBatchWatcher.cancel();
}

}
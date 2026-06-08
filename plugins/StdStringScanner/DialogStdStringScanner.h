/*
 * Copyright (C) 2006 - 2025 Evan Teran <evan.teran@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DIALOG_STD_STRING_SCANNER_H_
#define DIALOG_STD_STRING_SCANNER_H_

#include "Types.h"

#include <QDialog>
#include <QVector>

class QLineEdit;
class QProgressBar;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

namespace StdStringScannerPlugin {

class DialogStdStringScanner : public QDialog {
	Q_OBJECT

public:
	explicit DialogStdStringScanner(QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
	~DialogStdStringScanner() override = default;

private Q_SLOTS:
	void scan();
	void resultActivated(QTableWidgetItem *item);

private:
	struct Result {
		edb::address_t objectAddress;
		edb::address_t dataAddress;
		quint64 size;
		quint64 capacity;
		QString storage;
		QString text;
	};

private:
	[[nodiscard]] bool parseInputs(edb::address_t *start, quint64 *size) const;
	void addResult(const Result &result);
	void clearResults();
	void scanRange(edb::address_t start, quint64 size);
	void scanObject(const std::vector<uint8_t> &bytes, quint64 offset, edb::address_t objectAddress);
	void scanPointer(const std::vector<uint8_t> &bytes, quint64 offset, edb::address_t pointerAddress);

private:
	QLineEdit *startAddressEdit_ = nullptr;
	QLineEdit *scanSizeEdit_     = nullptr;
	QCheckBox *bruteModeCheckBox_ = nullptr;
	QSpinBox *minLengthSpin_     = nullptr;
	QSpinBox *maxLengthSpin_     = nullptr;
	QPushButton *scanButton_     = nullptr;
	QProgressBar *progressBar_   = nullptr;
	QTableWidget *resultsTable_  = nullptr;
	QVector<Result> results_;
};

}

#endif

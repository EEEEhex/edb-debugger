/*
 * Copyright (C) 2006 - 2025 Evan Teran <evan.teran@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DialogStdStringScanner.h"
#include "IDebugger.h"
#include "IProcess.h"
#include "IRegion.h"
#include "MemoryRegions.h"
#include "edb.h"
#include "util/Math.h"

#include <QDialogButtonBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <vector>

namespace StdStringScannerPlugin {

namespace {

constexpr quint64 StringObjectSize = 32;
constexpr quint64 LocalDataOffset  = 16;
constexpr quint64 LocalCapacity    = 15;
constexpr quint64 ChunkSize        = 1024 * 1024;

bool is_allowed_text_codepoint(uint32_t codepoint) {
	return codepoint == '\t' ||
		   codepoint == '\r' ||
		   codepoint == '\n' ||
		   (codepoint >= 0x20 && codepoint != 0x7f);
}

bool decode_utf8_codepoint(const uint8_t *data, quint64 size, quint64 *offset, uint32_t *codepoint) {
	const uint8_t first = data[*offset];

	if (first < 0x80) {
		*codepoint = first;
		++(*offset);
		return true;
	}

	uint32_t value       = 0;
	quint64 continuation = 0;
	uint32_t minimum     = 0;

	if ((first & 0xe0) == 0xc0) {
		value        = first & 0x1f;
		continuation = 1;
		minimum      = 0x80;
	} else if ((first & 0xf0) == 0xe0) {
		value        = first & 0x0f;
		continuation = 2;
		minimum      = 0x800;
	} else if ((first & 0xf8) == 0xf0) {
		value        = first & 0x07;
		continuation = 3;
		minimum      = 0x10000;
	} else {
		return false;
	}

	if (*offset + continuation >= size) {
		return false;
	}

	for (quint64 i = 1; i <= continuation; ++i) {
		const uint8_t byte = data[*offset + i];
		if ((byte & 0xc0) != 0x80) {
			return false;
		}
		value = (value << 6) | (byte & 0x3f);
	}

	if (value < minimum || (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) {
		return false;
	}

	*offset += continuation + 1;
	*codepoint = value;
	return true;
}

bool is_printable_string(const uint8_t *data, quint64 size) {
	quint64 offset = 0;
	while (offset < size) {
		uint32_t codepoint = 0;
		if (!decode_utf8_codepoint(data, size, &offset, &codepoint) || !is_allowed_text_codepoint(codepoint)) {
			return false;
		}
	}

	return true;
}

QString string_from_bytes(const uint8_t *data, quint64 size) {
	return QString::fromUtf8(reinterpret_cast<const char *>(data), static_cast<int>(size));
}

quint64 read_u64(const std::vector<uint8_t> &bytes, quint64 offset) {
	return qFromLittleEndian<quint64>(&bytes[static_cast<std::size_t>(offset)]);
}

bool checked_add(quint64 a, quint64 b, quint64 *result) {
	if (a > std::numeric_limits<quint64>::max() - b) {
		return false;
	}

	*result = a + b;
	return true;
}

bool region_contains_range(edb::address_t address, quint64 size) {
	quint64 end = 0;
	if (!checked_add(address.toUint(), size, &end)) {
		return false;
	}

	const std::shared_ptr<IRegion> region = edb::v1::memory_regions().findRegion(address);
	return region && region->readable() && end <= region->end().toUint();
}

}

DialogStdStringScanner::DialogStdStringScanner(QWidget *parent, Qt::WindowFlags f)
	: QDialog(parent, f) {

	setWindowTitle(tr("std::string Scanner"));
	resize(900, 520);

	auto *layout = new QVBoxLayout(this);
	auto *form   = new QFormLayout;

	startAddressEdit_ = new QLineEdit(this);
	scanSizeEdit_     = new QLineEdit(this);
	bruteModeCheckBox_ = new QCheckBox(tr("List any pointer whose target looks like a string"), this);
	minLengthSpin_    = new QSpinBox(this);
	maxLengthSpin_    = new QSpinBox(this);

	minLengthSpin_->setRange(0, 1024);
	minLengthSpin_->setValue(1);
	maxLengthSpin_->setRange(16, 1024 * 1024);
	maxLengthSpin_->setValue(4096);

	form->addRow(tr("Start address:"), startAddressEdit_);
	form->addRow(tr("Scan size:"), scanSizeEdit_);
	form->addRow(tr("Brute mode:"), bruteModeCheckBox_);
	form->addRow(tr("Minimum string length:"), minLengthSpin_);
	form->addRow(tr("Maximum heap string length:"), maxLengthSpin_);
	layout->addLayout(form);

	resultsTable_ = new QTableWidget(this);
	resultsTable_->setColumnCount(6);
	resultsTable_->setHorizontalHeaderLabels({
		tr("Location"),
		tr("Data"),
		tr("Size"),
		tr("Capacity"),
		tr("Storage"),
		tr("Content"),
	});
	resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
	resultsTable_->verticalHeader()->hide();
	resultsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	resultsTable_->horizontalHeader()->setStretchLastSection(true);
	connect(resultsTable_, &QTableWidget::itemDoubleClicked, this, &DialogStdStringScanner::resultActivated);
	layout->addWidget(resultsTable_);

	progressBar_ = new QProgressBar(this);
	progressBar_->setRange(0, 100);
	progressBar_->setValue(0);
	layout->addWidget(progressBar_);

	auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
	scanButton_     = new QPushButton(QIcon::fromTheme("edit-find"), tr("Scan"), this);
	buttonBox->addButton(scanButton_, QDialogButtonBox::ActionRole);
	connect(scanButton_, &QPushButton::clicked, this, &DialogStdStringScanner::scan);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::hide);
	layout->addWidget(buttonBox);
}

bool DialogStdStringScanner::parseInputs(edb::address_t *start, quint64 *size) const {

	const ::Result<edb::address_t, QString> startAddress = edb::v1::string_to_address(startAddressEdit_->text().trimmed());
	if (!startAddress) {
		QMessageBox::critical(nullptr, tr("Invalid Start Address"), tr("The start address is not valid."));
		return false;
	}

	bool ok = false;
	const quint64 scanSize = scanSizeEdit_->text().trimmed().toULongLong(&ok, 0);
	const quint64 minimumScanSize = bruteModeCheckBox_->isChecked() ? sizeof(quint64) : StringObjectSize;
	if (!ok || scanSize < minimumScanSize) {
		QMessageBox::critical(nullptr, tr("Invalid Scan Size"), tr("The scan size must be at least %1 bytes.").arg(minimumScanSize));
		return false;
	}

	*start = startAddress.value();
	*size  = scanSize;
	return true;
}

void DialogStdStringScanner::clearResults() {
	results_.clear();
	resultsTable_->setRowCount(0);
}

void DialogStdStringScanner::addResult(const Result &result) {
	const int row = resultsTable_->rowCount();
	resultsTable_->insertRow(row);
	results_.push_back(result);

	auto addItem = [this, row](int column, const QString &text) {
		auto *item = new QTableWidgetItem(text);
		item->setData(Qt::UserRole, row);
		resultsTable_->setItem(row, column, item);
	};

	addItem(0, edb::v1::format_pointer(result.objectAddress));
	addItem(1, edb::v1::format_pointer(result.dataAddress));
	addItem(2, QString::number(result.size));
	addItem(3, result.capacity == 0 ? QString() : QString::number(result.capacity));
	addItem(4, result.storage);
	addItem(5, result.text);
}

void DialogStdStringScanner::scan() {

	edb::address_t start = 0;
	quint64 size        = 0;
	if (!parseInputs(&start, &size)) {
		return;
	}

	if (edb::v1::pointer_size() != sizeof(quint64)) {
		QMessageBox::critical(nullptr, tr("Unsupported Target"), tr("This scanner currently supports 64-bit libstdc++ std::string objects only."));
		return;
	}

	edb::v1::memory_regions().sync();

	if (!region_contains_range(start, size)) {
		QMessageBox::critical(nullptr, tr("Invalid Memory Range"), tr("The requested scan range is not fully readable."));
		return;
	}

	clearResults();
	scanButton_->setEnabled(false);
	progressBar_->setValue(0);

	scanRange(start, size);

	progressBar_->setValue(100);
	scanButton_->setEnabled(true);

	if (results_.empty()) {
		QMessageBox::information(nullptr, tr("No Results"), tr("No possible std::string objects were found."));
	}
}

void DialogStdStringScanner::scanRange(edb::address_t start, quint64 size) {

	IProcess *const process = edb::v1::debugger_core->process();
	if (!process) {
		return;
	}

	const quint64 startValue = start.toUint();
	quint64 processed       = 0;

	while (processed < size) {
		const quint64 remaining = size - processed;
		const quint64 mainRead  = std::min(remaining, ChunkSize);
		const quint64 readSize  = std::min(remaining, mainRead + StringObjectSize - 1);
		const edb::address_t chunkAddress = startValue + processed;

		std::vector<uint8_t> bytes(static_cast<std::size_t>(readSize));
		const std::size_t bytesRead = process->readBytes(chunkAddress, bytes.data(), bytes.size());
		bytes.resize(bytesRead);

		for (quint64 offset = 0; offset + sizeof(quint64) <= bytes.size() && offset < mainRead; ++offset) {
			const edb::address_t objectAddress = chunkAddress + offset;
			if ((objectAddress.toUint() % alignof(quint64)) != 0) {
				continue;
			}

			if (offset + StringObjectSize <= bytes.size()) {
				scanObject(bytes, offset, objectAddress);
			}

			if (bruteModeCheckBox_->isChecked()) {
				scanPointer(bytes, offset, objectAddress);
			}
		}

		processed += mainRead;
		progressBar_->setValue(util::percentage(processed, size));
		QCoreApplication::processEvents();
	}
}

void DialogStdStringScanner::scanObject(const std::vector<uint8_t> &bytes, quint64 offset, edb::address_t objectAddress) {

	IProcess *const process = edb::v1::debugger_core->process();
	if (!process) {
		return;
	}

	const quint64 dataAddress = read_u64(bytes, offset);
	const quint64 stringSize  = read_u64(bytes, offset + 8);
	const quint64 localData   = objectAddress.toUint() + LocalDataOffset;
	const quint64 minLength   = static_cast<quint64>(minLengthSpin_->value());
	const quint64 maxLength   = static_cast<quint64>(maxLengthSpin_->value());

	if (stringSize < minLength || stringSize > maxLength) {
		return;
	}

	if (dataAddress == localData) {
		if (stringSize > LocalCapacity) {
			return;
		}

		const quint64 localOffset = offset + LocalDataOffset;
		if (localOffset + stringSize >= bytes.size()) {
			return;
		}

		const auto *const localBytes = &bytes[static_cast<std::size_t>(localOffset)];
		if (localBytes[stringSize] != '\0' || !is_printable_string(localBytes, stringSize)) {
			return;
		}

		addResult({objectAddress, dataAddress, stringSize, LocalCapacity, tr("SSO"), string_from_bytes(localBytes, stringSize)});
		return;
	}

	if (stringSize <= LocalCapacity) {
		return;
	}

	const quint64 capacity = read_u64(bytes, offset + 16);
	if (capacity < stringSize || capacity > maxLength || dataAddress == 0) {
		return;
	}

	if (!region_contains_range(dataAddress, stringSize + 1)) {
		return;
	}

	std::vector<uint8_t> stringBytes(static_cast<std::size_t>(stringSize + 1));
	if (process->readBytes(dataAddress, stringBytes.data(), stringBytes.size()) != stringBytes.size()) {
		return;
	}

	if (stringBytes[stringSize] != '\0' || !is_printable_string(stringBytes.data(), stringSize)) {
		return;
	}

	addResult({objectAddress, dataAddress, stringSize, capacity, tr("Heap"), string_from_bytes(stringBytes.data(), stringSize)});
}

void DialogStdStringScanner::scanPointer(const std::vector<uint8_t> &bytes, quint64 offset, edb::address_t pointerAddress) {

	IProcess *const process = edb::v1::debugger_core->process();
	if (!process) {
		return;
	}

	const quint64 dataAddress = read_u64(bytes, offset);
	if (dataAddress == 0 || dataAddress == pointerAddress.toUint()) {
		return;
	}

	const edb::address_t targetAddress = dataAddress;
	const std::shared_ptr<IRegion> region = edb::v1::memory_regions().findRegion(targetAddress);
	if (!region || !region->readable()) {
		return;
	}

	const quint64 minLength = static_cast<quint64>(minLengthSpin_->value());
	const quint64 maxLength = static_cast<quint64>(maxLengthSpin_->value());
	const quint64 regionRemaining = region->end().toUint() - dataAddress;
	const quint64 readSize = std::min(maxLength + 1, regionRemaining);
	if (readSize <= minLength) {
		return;
	}

	std::vector<uint8_t> stringBytes(static_cast<std::size_t>(readSize));
	if (process->readBytes(targetAddress, stringBytes.data(), stringBytes.size()) != stringBytes.size()) {
		return;
	}

	const auto nul = std::find(stringBytes.begin(), stringBytes.end(), '\0');
	if (nul == stringBytes.end()) {
		return;
	}

	const quint64 stringSize = static_cast<quint64>(std::distance(stringBytes.begin(), nul));
	if (stringSize < minLength || stringSize > maxLength || !is_printable_string(stringBytes.data(), stringSize)) {
		return;
	}

	addResult({pointerAddress, targetAddress, stringSize, 0, tr("Pointer"), string_from_bytes(stringBytes.data(), stringSize)});
}

void DialogStdStringScanner::resultActivated(QTableWidgetItem *item) {
	if (!item) {
		return;
	}

	const int row = item->data(Qt::UserRole).toInt();
	if (row >= 0 && row < results_.size()) {
		edb::v1::dump_data(results_[row].objectAddress);
	}
}

}

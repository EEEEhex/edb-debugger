/*
 * Copyright (C) 2006 - 2025 Evan Teran <evan.teran@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "BinaryString.h"
#include "HexStringValidator.h"

#include <QStringList>

#include "ui_BinaryString.h"

namespace {

constexpr auto CharHexLength = 3; // "hh "

// magic numerator from Qt defaults
constexpr auto UnlimitedMaxLength = 32767 / CharHexLength;

QByteArray truncateUtf8(const QString &text, int maxLength) {

	QByteArray data = text.toUtf8();
	if (data.size() <= maxLength) {
		return data;
	}

	int validLength = 0;
	for (int i = 0; i < data.size() && i < maxLength;) {
		const auto ch = static_cast<uint8_t>(data[i]);
		int charLength;

		if ((ch & 0x80) == 0) {
			charLength = 1;
		} else if ((ch & 0xe0) == 0xc0) {
			charLength = 2;
		} else if ((ch & 0xf0) == 0xe0) {
			charLength = 3;
		} else {
			charLength = 4;
		}

		if (i + charLength > maxLength) {
			break;
		}

		i += charLength;
		validLength = i;
	}

	data.truncate(validLength);
	return data;
}

}

/**
 * @brief BinaryString::setEntriesMaxLength
 * @param n
 */
void BinaryString::setEntriesMaxLength(int n) {

	ui->txtAscii->setMaxLength(n);
	ui->txtUTF8->setMaxLength(n);
	ui->txtUTF16->setMaxLength(n / 2);
	ui->txtHex->setMaxLength(n * CharHexLength);
}

/**
 * @brief BinaryString::setMaxLength
 * @param n
 */
void BinaryString::setMaxLength(int n) {
	requestedMaxLength_ = n;
	if (n) {
		mode_ = Mode::LengthLimited;
		ui->keepSize->hide();
	} else {
		mode_ = Mode::MemoryEditing;
		n     = UnlimitedMaxLength;
		ui->keepSize->show();
	}
	setEntriesMaxLength(n);
}

/**
 * @brief BinaryString::BinaryString
 * @param parent
 * @param f
 */
BinaryString::BinaryString(QWidget *parent, Qt::WindowFlags f)
	: QWidget(parent, f), ui(new Ui::BinaryStringWidget) {

	ui->setupUi(this);
	ui->txtHex->setValidator(new HexStringValidator(this));
	ui->keepSize->setFocusPolicy(Qt::TabFocus);
	ui->txtHex->setFocus(Qt::OtherFocusReason);
	connect(ui->keepSize, &QCheckBox::stateChanged, this, &BinaryString::on_keepSize_stateChanged);
}

/**
 * @brief BinaryString::~BinaryString
 */
BinaryString::~BinaryString() {
	// NOTE(eteran): we CAN'T use std::unique_ptr here because it doesn't
	// support incomplete types
	delete ui;
}

/**
 * @brief BinaryString::on_keepSize_stateChanged
 * @param state
 */
void BinaryString::on_keepSize_stateChanged(int state) {

	Q_UNUSED(state)

	if (mode_ != Mode::MemoryEditing) {
		return;
	}

	// There's a comment in get_binary_string_from_user(), that max length must be set before value.
	// FIXME: do we need this here? What does "truncate incorrectly" mean there?
	// NOTE: not doing this for now
	if (ui->keepSize->checkState() == Qt::Unchecked) {
		setEntriesMaxLength(UnlimitedMaxLength);
	} else {
		setEntriesMaxLength(valueOriginalLength_);
	}
}

/**
 * @brief BinaryString::on_txtAscii_textEdited
 * @param text
 */
void BinaryString::on_txtAscii_textEdited(const QString &text) {

	updateEntries(text.toLatin1(), ui->txtAscii);
}

/**
 * @brief BinaryString::on_txtUTF8_textEdited
 * @param text
 */
void BinaryString::on_txtUTF8_textEdited(const QString &text) {

	const QByteArray data = truncateUtf8(text, ui->txtAscii->maxLength());
	if (data.size() != text.toUtf8().size()) {
		ui->txtUTF8->setText(QString::fromUtf8(data));
	}
	updateEntries(data, ui->txtUTF8);
}

/**
 * @brief BinaryString::on_txtUTF16_textEdited
 * @param text
 */
void BinaryString::on_txtUTF16_textEdited(const QString &text) {

	QByteArray data;
	data.reserve(text.size() * 2);

	for (QChar i : text) {
		const uint16_t ch = i.unicode();

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
		data += static_cast<char>(ch & 0xff);
		data += static_cast<char>((ch >> 8) & 0xff);
#else
		data += static_cast<char>((ch >> 8) & 0xff);
		data += static_cast<char>(ch & 0xff);
#endif
	}

	updateEntries(data, ui->txtUTF16);
}

/**
 * @brief BinaryString::on_txtHex_textEdited
 * @param text
 */
void BinaryString::on_txtHex_textEdited(const QString &text) {

	QByteArray data;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
	const QStringList list1 = text.split(" ", Qt::SkipEmptyParts);
#else
	const QStringList list1 = text.split(" ", QString::SkipEmptyParts);
#endif

	for (const QString &s : list1) {
		data += static_cast<uint8_t>(s.toUInt(nullptr, 16));
	}

	updateEntries(data, ui->txtHex);
}

/**
 * @brief BinaryString::updateEntries
 * @param data
 * @param source
 */
void BinaryString::updateEntries(const QByteArray &data, QLineEdit *source) {

	QString textHex;
	QString textUTF16;
	uint16_t utf16Char = 0;
	int counter        = 0;

	textHex.reserve(data.size() * CharHexLength);
	textUTF16.reserve(data.size() / 2);

	for (uint8_t ch : data) {
		textHex += QString::asprintf("%02x ", ch & 0xff);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
		utf16Char = (utf16Char >> 8) | (ch << 8);
#else
		utf16Char = (utf16Char << 8) | ch;
#endif
		if (counter++ & 1) {
			textUTF16 += QChar(utf16Char);
		}
	}

	if (source != ui->txtAscii) {
		ui->txtAscii->setText(QString::fromLatin1(data.data(), data.size()));
	}
	if (source != ui->txtUTF8) {
		ui->txtUTF8->setText(QString::fromUtf8(data.data(), data.size()));
	}
	if (source != ui->txtUTF16) {
		ui->txtUTF16->setText(textUTF16);
	}
	if (source != ui->txtHex) {
		ui->txtHex->setText(textHex.simplified());
	}
}

/**
 * @brief BinaryString::value
 * @return
 */
QByteArray BinaryString::value() const {

	QByteArray ret;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
	const QStringList list1 = ui->txtHex->text().split(" ", Qt::SkipEmptyParts);
#else
	const QStringList list1 = ui->txtHex->text().split(" ", QString::SkipEmptyParts);
#endif
	for (const QString &i : list1) {
		ret += static_cast<uint8_t>(i.toUInt(nullptr, 16));
	}

	return ret;
}

/**
 * @brief BinaryString::setValue
 * @param data
 */
void BinaryString::setValue(const QByteArray &data) {

	valueOriginalLength_ = data.size();
	on_keepSize_stateChanged(ui->keepSize->checkState());
	updateEntries(data, nullptr);
}

/**
 * @brief BinaryString::setShowKeepSize
 * @param visible
 */
void BinaryString::setShowKeepSize(bool visible) {
	ui->keepSize->setVisible(visible);
}

/**
 * @brief BinaryString::showKeepSize
 * @return
 */
bool BinaryString::showKeepSize() const {
	return ui->keepSize->isVisible();
}

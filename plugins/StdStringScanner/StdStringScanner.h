/*
 * Copyright (C) 2006 - 2025 Evan Teran <evan.teran@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef STD_STRING_SCANNER_H_
#define STD_STRING_SCANNER_H_

#include "IPlugin.h"

class QMenu;
class QDialog;

namespace StdStringScannerPlugin {

class StdStringScanner : public QObject, public IPlugin {
	Q_OBJECT
	Q_INTERFACES(IPlugin)
	Q_PLUGIN_METADATA(IID "edb.IPlugin/1.0")
	Q_CLASSINFO("author", "EDB")
	Q_CLASSINFO("url", "https://github.com/eteran/edb-debugger")

public:
	explicit StdStringScanner(QObject *parent = nullptr);
	~StdStringScanner() override;

public:
	[[nodiscard]] QMenu *menu(QWidget *parent = nullptr) override;

public Q_SLOTS:
	void showMenu();

private:
	QMenu *menu_ = nullptr;
	QDialog *dialog_ = nullptr;
};

}

#endif

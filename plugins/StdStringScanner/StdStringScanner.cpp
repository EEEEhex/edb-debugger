/*
 * Copyright (C) 2006 - 2025 Evan Teran <evan.teran@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "StdStringScanner.h"
#include "DialogStdStringScanner.h"
#include "edb.h"

#include <QMenu>

namespace StdStringScannerPlugin {

StdStringScanner::StdStringScanner(QObject *parent)
	: QObject(parent) {
}

StdStringScanner::~StdStringScanner() {
	delete dialog_;
}

QMenu *StdStringScanner::menu(QWidget *parent) {

	Q_ASSERT(parent);

	if (!menu_) {
		menu_ = new QMenu(tr("std::string Scanner"), parent);
		menu_->addAction(tr("&std::string Scanner"), this, SLOT(showMenu()));
	}

	return menu_;
}

void StdStringScanner::showMenu() {

	if (!dialog_) {
		dialog_ = new DialogStdStringScanner(edb::v1::debugger_ui);
	}

	dialog_->show();
}

}

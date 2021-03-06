////////////////////////////////////////////////////////////////////////////////
//
// This file is part of Strata.
//
// Strata is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// Strata is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// Strata.  If not, see <http://www.gnu.org/licenses/>.
//
// Copyright 2007 Albert Kottke
//
////////////////////////////////////////////////////////////////////////////////

#include "StringListDelegate.h"
#include <QComboBox>
#include <QList>

StringListDelegate::StringListDelegate(QObject *parent)
        : QItemDelegate(parent) {
}

QWidget *StringListDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem & /*option*/,
                                          const QModelIndex & /*index*/) const {
    QComboBox *editor = new QComboBox(parent);
    return editor;
}

void StringListDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    QComboBox *comboBox = static_cast<QComboBox *>(editor);
    // Retrieve the map containing the list of soil names and the selected index
    QMap<QString, QVariant> map = index.model()->data(index, Qt::EditRole).toMap();

    // Add the items to the combo box
    comboBox->addItems(map.value("list").toStringList());

    // Get the selected index
    comboBox->setCurrentIndex(map.value("index").toInt());
}

void StringListDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                      const QModelIndex &index) const {
    QComboBox *comboBox = static_cast<QComboBox *>(editor);
    model->setData(index, comboBox->currentIndex());
    return;
}

/*
 * 文件名: fittingparameterchart.cpp
 * 文件作用: 拟合参数图表管理类实现文件
 * 功能描述:
 * 1. 初始化表格样式。
 * 2. 实现主界面表格的滚轮参数调节。
 * 3. 提供原始文本获取接口。
 */

#include "fittingparameterchart.h"
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QDebug>
#include <QBrush>
#include <QColor>
#include <QRegularExpression>
#include <QWheelEvent>

FittingParameterChart::FittingParameterChart(QTableWidget *parentTable, QObject *parent)
    : QObject(parent), m_table(parentTable), m_modelManager(nullptr)
{
    if(m_table) {
        QStringList headers;
        headers << "序号" << "参数名称" << "数值" << "单位";
        m_table->setColumnCount(headers.size());
        m_table->setHorizontalHeaderLabels(headers);

        m_table->horizontalHeader()->setStyleSheet(
            "QHeaderView::section { background-color: #E0E0E0; color: black; font-weight: bold; border: 1px solid #A0A0A0; }"
            );

        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_table->horizontalHeader()->setStretchLastSection(true);

        m_table->setColumnWidth(0, 40);
        m_table->setColumnWidth(1, 160);
        m_table->setColumnWidth(2, 80);

        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setAlternatingRowColors(false);
        m_table->verticalHeader()->setVisible(false);

        // 安装事件过滤器以监听滚轮事件
        m_table->viewport()->installEventFilter(this);
    }
}

// 主界面表格的滚轮处理
bool FittingParameterChart::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);

        QPoint pos = wheelEvent->position().toPoint();
        QTableWidgetItem *item = m_table->itemAt(pos);

        if (item && item->column() == 2) {
            int row = item->row();
            QTableWidgetItem *keyItem = m_table->item(row, 1);
            if (!keyItem) return false;
            QString paramName = keyItem->data(Qt::UserRole).toString();

            FitParameter *targetParam = nullptr;
            for (auto &p : m_params) {
                if (p.name == paramName) {
                    targetParam = &p;
                    break;
                }
            }

            if (targetParam) {
                QString currentText = item->text();
                // 多值模式下禁用滚轮
                if (currentText.contains(',') || currentText.contains(QChar(0xFF0C))) {
                    return false;
                }

                bool ok;
                double currentVal = currentText.toDouble(&ok);
                if (ok) {
                    int steps = wheelEvent->angleDelta().y() / 120;
                    double newVal = currentVal + steps * targetParam->step; // 使用设定的步长

                    item->setText(QString::number(newVal, 'g', 6));
                    targetParam->value = newVal;
                    emit parameterChangedByWheel();
                    return true;
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void FittingParameterChart::setModelManager(ModelManager *m)
{
    m_modelManager = m;
}

void FittingParameterChart::resetParams(ModelManager::ModelType type)
{
    if(!m_modelManager) return;
    m_params.clear();

    QMap<QString, double> defaultMap = m_modelManager->getDefaultParameters(type);
    QMapIterator<QString, double> it(defaultMap);
    while(it.hasNext()) {
        it.next();
        FitParameter p;
        p.name = it.key();
        p.value = it.value();
        p.isFit = false;

        if (p.value > 0) {
            p.min = p.value * 0.01; p.max = p.value * 100.0;
        } else {
            p.min = 0.0; p.max = 100.0;
        }

        // 初始化默认步长
        if (p.name == "k" || p.name == "kf" || p.name == "km") p.step = 1.0;
        else if (p.name == "S") p.step = 0.1;
        else if (p.name == "C" || p.name == "cD") p.step = 0.01;
        else if (p.name == "phi") p.step = 0.01;
        else p.step = p.value != 0 ? std::abs(p.value * 0.1) : 0.1;

        QString symbol, uniSym, unit;
        getParamDisplayInfo(p.name, p.displayName, symbol, uniSym, unit);
        p.isVisible = true;
        m_params.append(p);
    }
    refreshParamTable();
}

QList<FitParameter> FittingParameterChart::getParameters() const { return m_params; }
void FittingParameterChart::setParameters(const QList<FitParameter> &params) { m_params = params; refreshParamTable(); }

void FittingParameterChart::switchModel(ModelManager::ModelType newType)
{
    QMap<QString, double> oldValues;
    for(const auto& p : m_params) oldValues.insert(p.name, p.value);
    resetParams(newType);
    for(auto& p : m_params) {
        if(oldValues.contains(p.name)) p.value = oldValues[p.name];
    }
    refreshParamTable();
}

void FittingParameterChart::updateParamsFromTable()
{
    if(!m_table) return;
    for(int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem* itemKey = m_table->item(i, 1);
        if(!itemKey) continue;
        QString key = itemKey->data(Qt::UserRole).toString();
        QTableWidgetItem* itemVal = m_table->item(i, 2);

        QString text = itemVal->text();
        double val = 0.0;
        // 如果是多值，取第一个用于拟合初值
        if (text.contains(',') || text.contains(QChar(0xFF0C))) {
            QString firstPart = text.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts).first();
            val = firstPart.toDouble();
        } else {
            val = text.toDouble();
        }

        for(auto& p : m_params) {
            if(p.name == key) { p.value = val; break; }
        }
    }
}

QMap<QString, QString> FittingParameterChart::getRawParamTexts() const
{
    QMap<QString, QString> rawTexts;
    if(!m_table) return rawTexts;
    for(int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem* itemKey = m_table->item(i, 1);
        QTableWidgetItem* itemVal = m_table->item(i, 2);
        if (itemKey && itemVal) {
            QString key = itemKey->data(Qt::UserRole).toString();
            rawTexts.insert(key, itemVal->text());
        }
    }
    return rawTexts;
}

void FittingParameterChart::refreshParamTable()
{
    if(!m_table) return;
    m_table->blockSignals(true);
    m_table->setRowCount(0);
    int serialNo = 1;
    for(const auto& p : m_params) { if(p.isVisible && p.isFit) addRowToTable(p, serialNo, true); }
    for(const auto& p : m_params) { if(p.isVisible && !p.isFit) addRowToTable(p, serialNo, false); }
    m_table->blockSignals(false);
}

void FittingParameterChart::addRowToTable(const FitParameter& p, int& serialNo, bool highlight)
{
    int row = m_table->rowCount();
    m_table->insertRow(row);
    QColor bgColor = highlight ? QColor(255, 255, 224) : Qt::white;

    QTableWidgetItem* numItem = new QTableWidgetItem(QString::number(serialNo++));
    numItem->setFlags(numItem->flags() & ~Qt::ItemIsEditable);
    numItem->setTextAlignment(Qt::AlignCenter);
    numItem->setBackground(bgColor);
    m_table->setItem(row, 0, numItem);

    QString displayNameFull = QString("%1 (%2)").arg(p.displayName).arg(p.name);
    QTableWidgetItem* nameItem = new QTableWidgetItem(displayNameFull);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setData(Qt::UserRole, p.name);
    nameItem->setBackground(bgColor);
    if(highlight) { QFont f = nameItem->font(); f.setBold(true); nameItem->setFont(f); }
    m_table->setItem(row, 1, nameItem);

    QTableWidgetItem* valItem = new QTableWidgetItem(QString::number(p.value, 'g', 6));
    valItem->setBackground(bgColor);
    if(highlight) { QFont f = valItem->font(); f.setBold(true); valItem->setFont(f); }
    m_table->setItem(row, 2, valItem);

    QString dummy, symbol, uniSym, unit;
    getParamDisplayInfo(p.name, dummy, symbol, uniSym, unit);
    if(unit == "无因次" || unit == "小数") unit = "-";
    QTableWidgetItem* unitItem = new QTableWidgetItem(unit);
    unitItem->setFlags(unitItem->flags() & ~Qt::ItemIsEditable);
    unitItem->setBackground(bgColor);
    m_table->setItem(row, 3, unitItem);
}

void FittingParameterChart::getParamDisplayInfo(const QString &name, QString &chName, QString &symbol, QString &uniSym, QString &unit)
{
    if(name == "k")      { chName = "渗透率";         unit = "mD"; }
    else if(name == "h")      { chName = "有效厚度";       unit = "m"; }
    else if(name == "phi")    { chName = "孔隙度";         unit = "小数"; }
    else if(name == "mu")     { chName = "流体粘度";       unit = "mPa·s"; }
    else if(name == "B")      { chName = "体积系数";       unit = "无因次"; }
    else if(name == "Ct")     { chName = "综合压缩系数";   unit = "MPa⁻¹"; }
    else if(name == "rw")     { chName = "井筒半径";       unit = "m"; }
    else if(name == "q")      { chName = "测试产量";       unit = "m³/d"; }
    else if(name == "C")      { chName = "井筒储存系数";   unit = "m³/MPa"; }
    else if(name == "cD")     { chName = "无因次井储";     unit = "无因次"; }
    else if(name == "S")      { chName = "表皮系数";       unit = "无因次"; }
    else if(name == "L")      { chName = "水平井长";       unit = "m"; }
    else if(name == "Lf")     { chName = "裂缝半长";       unit = "m"; }
    else if(name == "nf")     { chName = "裂缝条数";       unit = "条"; }
    else if(name == "kf")     { chName = "裂缝渗透率";     unit = "mD"; }
    else if(name == "km")     { chName = "基质渗透率";     unit = "mD"; }
    else if(name == "reD")    { chName = "无因次泄油半径"; unit = "无因次"; }
    else if(name == "lambda1"){ chName = "窜流系数";       unit = "无因次"; }
    else if(name == "omega1") { chName = "储容比1";        unit = "无因次"; }
    else if(name == "omega2") { chName = "储容比2";        unit = "无因次"; }
    else if(name == "gamaD")  { chName = "压敏系数";       unit = "无因次"; }
    else if(name == "rmD")    { chName = "无因次内半径";   unit = "无因次"; }
    else if(name == "LfD")    { chName = "无因次缝长";     unit = "无因次"; }
    else { chName = name; unit = ""; }
    symbol = name; uniSym = name;
}

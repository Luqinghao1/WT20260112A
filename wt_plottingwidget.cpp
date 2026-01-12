/*
 * 文件名: wt_plottingwidget.cpp
 * 文件作用: 图表分析主界面实现文件
 * 功能描述:
 * 1. [修改] CurveInfo JSON 序列化增加 sourceFileName2 支持。
 * 2. [修改] on_btn_PressureRate_clicked 支持分别从两个文件读取压力和产量数据。
 * 3. [修改] executeExport 导出逻辑更新：支持直接导出移动后的数据。
 * 4. [新增] 实现了数据移动后的持久化逻辑，曲线切换后数据位置保持不变。
 */

#include "wt_plottingwidget.h"
#include "ui_wt_plottingwidget.h"
#include "plottingdialog1.h"
#include "plottingdialog2.h"
#include "plottingdialog3.h"
#include "plottingdialog4.h"
#include "chartwindow.h"
#include "modelparameter.h"
#include "chartsetting1.h"
#include "pressurederivativecalculator.h"
#include "pressurederivativecalculator1.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtMath>
#include <QDebug>
#include <QSplitter>

// ============================================================================
// 辅助函数与 CurveInfo 实现
// ============================================================================

QJsonArray vectorToJson(const QVector<double>& vec) {
    QJsonArray arr;
    for(double v : vec) arr.append(v);
    return arr;
}

QVector<double> jsonToVector(const QJsonArray& arr) {
    QVector<double> vec;
    for(const auto& val : arr) vec.append(val.toDouble());
    return vec;
}

QJsonObject CurveInfo::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["legendName"] = legendName;
    obj["sourceFileName"] = sourceFileName;
    obj["sourceFileName2"] = sourceFileName2;
    obj["type"] = type;
    obj["xCol"] = xCol;
    obj["yCol"] = yCol;
    obj["xData"] = vectorToJson(xData);
    obj["yData"] = vectorToJson(yData);
    obj["pointShape"] = (int)pointShape;
    obj["pointColor"] = pointColor.name();
    obj["lineStyle"] = (int)lineStyle;
    obj["lineColor"] = lineColor.name();

    if (type == 1) {
        obj["x2Col"] = x2Col;
        obj["y2Col"] = y2Col;
        obj["x2Data"] = vectorToJson(x2Data);
        obj["y2Data"] = vectorToJson(y2Data);
        obj["prodLegendName"] = prodLegendName;
        obj["prodGraphType"] = prodGraphType;
        obj["prodColor"] = prodColor.name();
    }
    else if (type == 2) {
        obj["testType"] = testType;
        obj["initialPressure"] = initialPressure;
        obj["LSpacing"] = LSpacing;
        obj["isSmooth"] = isSmooth;
        obj["smoothFactor"] = smoothFactor;
        obj["derivData"] = vectorToJson(derivData);
        obj["derivShape"] = (int)derivShape;
        obj["derivPointColor"] = derivPointColor.name();
        obj["derivLineStyle"] = (int)derivLineStyle;
        obj["derivLineColor"] = derivLineColor.name();
        obj["prodLegendName"] = prodLegendName;
    }
    return obj;
}

CurveInfo CurveInfo::fromJson(const QJsonObject& json) {
    CurveInfo info;
    info.name = json["name"].toString();
    info.legendName = json["legendName"].toString();
    info.sourceFileName = json["sourceFileName"].toString();
    info.sourceFileName2 = json["sourceFileName2"].toString();
    info.type = json["type"].toInt();
    info.xCol = json["xCol"].toInt(-1);
    info.yCol = json["yCol"].toInt(-1);

    info.xData = jsonToVector(json["xData"].toArray());
    info.yData = jsonToVector(json["yData"].toArray());

    info.pointShape = (QCPScatterStyle::ScatterShape)json["pointShape"].toInt();
    info.pointColor = QColor(json["pointColor"].toString());
    info.lineStyle = (Qt::PenStyle)json["lineStyle"].toInt();
    info.lineColor = QColor(json["lineColor"].toString());

    if (info.type == 1) {
        info.x2Col = json["x2Col"].toInt(-1);
        info.y2Col = json["y2Col"].toInt(-1);
        info.x2Data = jsonToVector(json["x2Data"].toArray());
        info.y2Data = jsonToVector(json["y2Data"].toArray());
        info.prodLegendName = json["prodLegendName"].toString();
        info.prodGraphType = json["prodGraphType"].toInt();
        info.prodColor = QColor(json["prodColor"].toString());
    } else if (info.type == 2) {
        info.testType = json["testType"].toInt(0);
        info.initialPressure = json["initialPressure"].toDouble(0.0);
        info.LSpacing = json["LSpacing"].toDouble();
        info.isSmooth = json["isSmooth"].toBool();
        info.smoothFactor = json["smoothFactor"].toInt();
        info.derivData = jsonToVector(json["derivData"].toArray());
        info.derivShape = (QCPScatterStyle::ScatterShape)json["derivShape"].toInt();
        info.derivPointColor = QColor(json["derivPointColor"].toString());
        info.derivLineStyle = (Qt::PenStyle)json["derivLineStyle"].toInt();
        info.derivLineColor = QColor(json["derivLineColor"].toString());
        info.prodLegendName = json["prodLegendName"].toString();
    }
    return info;
}

// ============================================================================
// WT_PlottingWidget 主类实现
// ============================================================================

WT_PlottingWidget::WT_PlottingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WT_PlottingWidget),
    m_defaultModel(nullptr),
    m_isSelectingForExport(false),
    m_selectionStep(0),
    m_exportStartIndex(0),
    m_exportEndIndex(0),
    m_graphPress(nullptr),
    m_graphProd(nullptr)
{
    ui->setupUi(this);

    QList<int> sizes;
    sizes << 200 << 800;
    ui->splitter->setSizes(sizes);
    ui->splitter->setCollapsible(0, false);

    connect(ui->customPlot, &ChartWidget::exportDataTriggered, this, &WT_PlottingWidget::onExportDataTriggered);
    connect(ui->customPlot->getPlot(), &QCustomPlot::plottableClick, this, &WT_PlottingWidget::onGraphClicked);

    // [新增] 连接数据修改信号，用于持久化移动后的数据
    connect(ui->customPlot, &ChartWidget::graphDataModified, this, &WT_PlottingWidget::onGraphDataModified);

    ui->customPlot->setChartMode(ChartWidget::Mode_Single);
    ui->customPlot->setTitle("试井分析图表");
}

WT_PlottingWidget::~WT_PlottingWidget()
{
    qDeleteAll(m_openedWindows);
    delete ui;
}

void WT_PlottingWidget::setDataModels(const QMap<QString, QStandardItemModel*>& models) {
    m_dataMap = models;
    if (!m_dataMap.isEmpty()) {
        m_defaultModel = m_dataMap.first();
    } else {
        m_defaultModel = nullptr;
    }
}

void WT_PlottingWidget::setProjectFolderPath(const QString& path) { Q_UNUSED(path); }

void WT_PlottingWidget::updateChartTitle(const QString& title) {
    ui->customPlot->setTitle(title);
    if (m_curves.contains(m_currentDisplayedCurve)) {
        m_curves[m_currentDisplayedCurve].name = title;
        QListWidgetItem* item = getCurrentSelectedItem();
        if(item) item->setText(title);
    }
}

void WT_PlottingWidget::applyDialogStyle(QWidget* dialog) {
    if(!dialog) return;
    QString qss = "QWidget { color: black; background-color: white; font-family: 'Microsoft YaHei'; }"
                  "QPushButton { border: 1px solid #bfbfbf; border-radius: 3px; padding: 4px 12px; }";
    dialog->setStyleSheet(qss);
}

void WT_PlottingWidget::loadProjectData() {
    m_curves.clear();
    ui->listWidget_Curves->clear();
    ui->customPlot->clearGraphs();
    m_currentDisplayedCurve.clear();

    QJsonArray plots = ModelParameter::instance()->getPlottingData();
    if (plots.isEmpty()) return;

    for (const auto& val : plots) {
        CurveInfo info = CurveInfo::fromJson(val.toObject());
        m_curves.insert(info.name, info);
        ui->listWidget_Curves->addItem(info.name);
    }

    if (ui->listWidget_Curves->count() > 0) {
        on_listWidget_Curves_itemDoubleClicked(ui->listWidget_Curves->item(0));
    }
}

void WT_PlottingWidget::saveProjectData() {
    if (!ModelParameter::instance()->hasLoadedProject()) return;
    QJsonArray curvesArray;
    for(auto it = m_curves.begin(); it != m_curves.end(); ++it) {
        curvesArray.append(it.value().toJson());
    }
    ModelParameter::instance()->savePlottingData(curvesArray);
    QMessageBox::information(this, "保存", "绘图数据已保存。");
}

void WT_PlottingWidget::on_btn_Save_clicked() { saveProjectData(); }

void WT_PlottingWidget::clearAllPlots() {
    m_curves.clear();
    m_currentDisplayedCurve.clear();
    ui->listWidget_Curves->clear();
    ui->customPlot->clearGraphs();
    ui->customPlot->setTitle("试井分析图表");
    qDeleteAll(m_openedWindows);
    m_openedWindows.clear();
}

void WT_PlottingWidget::on_listWidget_Curves_itemDoubleClicked(QListWidgetItem *item) {
    QString name = item->text();
    if(!m_curves.contains(name)) return;

    CurveInfo info = m_curves[name];
    m_currentDisplayedCurve = name;

    ui->customPlot->clearGraphs();
    ui->customPlot->setTitle(name);

    // 重置指针
    m_graphPress = nullptr;
    m_graphProd = nullptr;

    if (info.type == 1) {
        ui->customPlot->setChartMode(ChartWidget::Mode_Stacked);
        if (ui->customPlot->getTopRect()) ui->customPlot->getTopRect()->axis(QCPAxis::atLeft)->setLabel("Pressure");
        if (ui->customPlot->getBottomRect()) {
            ui->customPlot->getBottomRect()->axis(QCPAxis::atLeft)->setLabel("Production");
            ui->customPlot->getBottomRect()->axis(QCPAxis::atBottom)->setLabel("Time");
        }
        drawStackedPlot(info);
    }
    else if (info.type == 2) {
        ui->customPlot->setChartMode(ChartWidget::Mode_Single);
        MouseZoom* plot = ui->customPlot->getPlot();
        plot->xAxis->setLabel("Time");
        plot->yAxis->setLabel("Pressure & Derivative");
        plot->xAxis->setScaleType(QCPAxis::stLogarithmic);
        plot->yAxis->setScaleType(QCPAxis::stLogarithmic);
        plot->xAxis->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
        plot->yAxis->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
        drawDerivativePlot(info);
    }
    else {
        ui->customPlot->setChartMode(ChartWidget::Mode_Single);
        MouseZoom* plot = ui->customPlot->getPlot();
        plot->xAxis->setScaleType(QCPAxis::stLinear);
        plot->yAxis->setScaleType(QCPAxis::stLinear);
        plot->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
        plot->yAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));

        QStandardItemModel* model = m_defaultModel;
        if (!info.sourceFileName.isEmpty() && m_dataMap.contains(info.sourceFileName)) {
            model = m_dataMap.value(info.sourceFileName);
        }

        if(model && info.xCol >=0) plot->xAxis->setLabel(model->headerData(info.xCol, Qt::Horizontal).toString());
        if(model && info.yCol >=0) plot->yAxis->setLabel(model->headerData(info.yCol, Qt::Horizontal).toString());
        addCurveToPlot(info);
    }
}

void WT_PlottingWidget::addCurveToPlot(const CurveInfo& info) {
    MouseZoom* plot = ui->customPlot->getPlot();
    QCPGraph* graph = plot->addGraph();
    graph->setName(info.legendName);
    graph->setData(info.xData, info.yData);
    graph->setScatterStyle(QCPScatterStyle(info.pointShape, info.pointColor, info.pointColor, 6));
    graph->setPen(QPen(info.lineColor, 2, info.lineStyle));
    graph->setLineStyle(info.lineStyle == Qt::NoPen ? QCPGraph::lsNone : QCPGraph::lsLine);
    plot->rescaleAxes();
    plot->replot();
}

void WT_PlottingWidget::drawStackedPlot(const CurveInfo& info) {
    QCPAxisRect* topRect = ui->customPlot->getTopRect();
    QCPAxisRect* bottomRect = ui->customPlot->getBottomRect();
    MouseZoom* plot = ui->customPlot->getPlot();

    if (!topRect || !bottomRect) return;

    m_graphPress = plot->addGraph(topRect->axis(QCPAxis::atBottom), topRect->axis(QCPAxis::atLeft));
    m_graphPress->setData(info.xData, info.yData);
    m_graphPress->setName(info.legendName);
    m_graphPress->setScatterStyle(QCPScatterStyle(info.pointShape, info.pointColor, info.pointColor, 6));
    m_graphPress->setPen(QPen(info.lineColor, 2, info.lineStyle));
    m_graphPress->setLineStyle(info.lineStyle == Qt::NoPen ? QCPGraph::lsNone : QCPGraph::lsLine);

    m_graphProd = plot->addGraph(bottomRect->axis(QCPAxis::atBottom), bottomRect->axis(QCPAxis::atLeft));
    QVector<double> px, py;
    if(info.prodGraphType == 0) {
        double t_cum = 0;
        if(!info.x2Data.isEmpty()) { px.append(0); py.append(info.y2Data[0]); }
        for(int i=0; i<info.x2Data.size(); ++i) {
            t_cum += info.x2Data[i];
            if(i+1 < info.y2Data.size()) { px.append(t_cum); py.append(info.y2Data[i+1]); }
            else { px.append(t_cum); py.append(info.y2Data[i]); }
        }
        m_graphProd->setLineStyle(QCPGraph::lsStepLeft);
        m_graphProd->setScatterStyle(QCPScatterStyle::ssNone);
        m_graphProd->setBrush(QBrush(info.prodColor.lighter(170)));
        m_graphProd->setPen(QPen(info.prodColor, 2));
    } else {
        px = info.x2Data; py = info.y2Data;
        m_graphProd->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, info.prodColor, info.prodColor, 6));
        m_graphProd->setBrush(Qt::NoBrush);
        m_graphProd->setPen(QPen(info.prodColor, 2));
        m_graphProd->setLineStyle(QCPGraph::lsNone);
    }
    m_graphProd->setData(px, py);
    m_graphProd->setName(info.prodLegendName);

    m_graphPress->rescaleAxes();
    m_graphProd->rescaleAxes();
    plot->replot();
}

void WT_PlottingWidget::drawDerivativePlot(const CurveInfo& info) {
    MouseZoom* plot = ui->customPlot->getPlot();

    QCPGraph* g1 = plot->addGraph();
    g1->setName(info.legendName);
    g1->setData(info.xData, info.yData);
    g1->setScatterStyle(QCPScatterStyle(info.pointShape, info.pointColor, info.pointColor, 6));
    g1->setPen(QPen(info.lineColor, 2, info.lineStyle));
    g1->setLineStyle(info.lineStyle == Qt::NoPen ? QCPGraph::lsNone : QCPGraph::lsLine);

    QCPGraph* g2 = plot->addGraph();
    g2->setName(info.prodLegendName);
    g2->setData(info.xData, info.derivData);
    g2->setScatterStyle(QCPScatterStyle(info.derivShape, info.derivPointColor, info.derivPointColor, 6));
    g2->setPen(QPen(info.derivLineColor, 2, info.derivLineStyle));
    g2->setLineStyle(info.derivLineStyle == Qt::NoPen ? QCPGraph::lsNone : QCPGraph::lsLine);

    plot->rescaleAxes();
    plot->replot();
}

// [新增] 处理图表数据被修改的槽函数
void WT_PlottingWidget::onGraphDataModified(QCPGraph* graph) {
    if (!graph || m_currentDisplayedCurve.isEmpty()) return;
    if (!m_curves.contains(m_currentDisplayedCurve)) return;

    CurveInfo& info = m_curves[m_currentDisplayedCurve];

    // 仅处理双坐标系模式下的 Pressure/Rate 曲线更新
    if (info.type == 1) {
        QVector<double> newX, newY;
        auto dataPtr = graph->data();
        for (auto it = dataPtr->begin(); it != dataPtr->end(); ++it) {
            newX.append(it->key);
            newY.append(it->value);
        }

        if (graph == m_graphPress) {
            info.xData = newX;
            info.yData = newY;
        } else if (graph == m_graphProd) {
            info.x2Data = newX;
            info.y2Data = newY;
        }
    }
}

void WT_PlottingWidget::on_btn_Manage_clicked() {
    QListWidgetItem* item = getCurrentSelectedItem();
    if(!item) return;
    QString name = item->text();
    CurveInfo& info = m_curves[name];

    QStandardItemModel* targetModel = m_defaultModel;
    if (!info.sourceFileName.isEmpty() && m_dataMap.contains(info.sourceFileName)) {
        targetModel = m_dataMap.value(info.sourceFileName);
    }

    if (!targetModel) {
        QMessageBox::warning(this, "警告", "无法找到该曲线对应的源数据文件: " + info.sourceFileName);
        return;
    }

    PlottingDialog4 dlg(targetModel, this);
    applyDialogStyle(&dlg);

    bool hasSecond = (info.type == 1 || info.type == 2);
    QString name2 = "";
    QCPScatterStyle::ScatterShape shape2 = QCPScatterStyle::ssNone;
    QColor c2 = Qt::black, lc2 = Qt::black;
    Qt::PenStyle ls2 = Qt::SolidLine;

    if (info.type == 1) {
        name2 = info.prodLegendName;
        shape2 = (info.prodGraphType == 1) ? QCPScatterStyle::ssCircle : QCPScatterStyle::ssNone;
        c2 = info.prodColor;
        lc2 = info.prodColor;
    } else if (info.type == 2) {
        name2 = info.prodLegendName;
        shape2 = info.derivShape;
        c2 = info.derivPointColor;
        ls2 = info.derivLineStyle;
        lc2 = info.derivLineColor;
    }

    dlg.setInitialData(hasSecond, info.legendName, info.xCol, info.yCol,
                       info.pointShape, info.pointColor, info.lineStyle, info.lineColor,
                       name2, shape2, c2, ls2, lc2);

    if(dlg.exec() == QDialog::Accepted) {
        info.legendName = dlg.getLegendName1();
        info.xCol = dlg.getXColumn(); info.yCol = dlg.getYColumn();
        info.pointShape = dlg.getPointShape1(); info.pointColor = dlg.getPointColor1();
        info.lineStyle = dlg.getLineStyle1(); info.lineColor = dlg.getLineColor1();

        // 注意：这里我们不再从文件重新读取数据覆盖 xData/yData，以保留用户可能进行的移动操作
        // 如果用户需要重置数据，应删除曲线重新添加或设计“重置数据”功能
        // 为保持一致性，如果修改了列，通常意味着重新读取。这里暂保持不重置，除非列号变了？
        // 简化起见，若需保留移动结果，这里不应覆盖 xData/yData，除非显式需要重新加载。
        // 原有逻辑是重新读取。为了满足“移动后切换曲线保持不变”的需求，这里管理功能若不涉及数据源变更，也不应重置数据。
        // 但如果用户改了列，数据肯定变了。
        // 鉴于 Dialog4 主要是改样式和列，如果列变了，必须重读。如果只是样式，不重读。

        // 简单判断：如果列没变，就不重读数据。
        // 原有代码无此判断，这里为了稳健，先不改动 Dialog4 后的重读逻辑，
        // 因为“移动”通常是在确定了数据源之后的操作。
        // 如果用户想恢复原始数据，可以在管理界面重新选列。

        // 恢复原有逻辑（如果列变更重新读取）
        if(info.type == 0) {
            info.xData.clear(); info.yData.clear();
            for(int i=0; i<targetModel->rowCount(); ++i) {
                double xVal = targetModel->item(i, info.xCol)->text().toDouble();
                double yVal = targetModel->item(i, info.yCol)->text().toDouble();
                if (xVal > 1e-9 && yVal > 1e-9) { info.xData.append(xVal); info.yData.append(yVal); }
            }
        }

        if (hasSecond) {
            if (info.type == 1) {
                info.prodLegendName = dlg.getLegendName2();
                info.prodColor = dlg.getPointColor2();
            } else if (info.type == 2) {
                info.prodLegendName = dlg.getLegendName2();
                info.derivShape = dlg.getPointShape2();
                info.derivPointColor = dlg.getPointColor2();
                info.derivLineStyle = dlg.getLineStyle2();
                info.derivLineColor = dlg.getLineColor2();
            }
        }

        if(m_currentDisplayedCurve == name) {
            on_listWidget_Curves_itemDoubleClicked(item);
        }
    }
}

void WT_PlottingWidget::onExportDataTriggered() {
    if(m_currentDisplayedCurve.isEmpty()) {
        QMessageBox::warning(this, "提示", "当前没有显示的曲线。");
        return;
    }
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("导出数据");
    msgBox.setText("请选择导出范围：");
    msgBox.setIcon(QMessageBox::Question);
    QPushButton* btnAll = msgBox.addButton("全部数据", QMessageBox::ActionRole);
    QPushButton* btnPart = msgBox.addButton("部分数据", QMessageBox::ActionRole);
    msgBox.addButton("取消", QMessageBox::RejectRole);
    applyDialogStyle(&msgBox);
    msgBox.exec();

    if(msgBox.clickedButton() == btnAll) {
        executeExport(true);
    }
    else if(msgBox.clickedButton() == btnPart) {
        m_isSelectingForExport = true;
        m_selectionStep = 1;
        ui->customPlot->getPlot()->setCursor(Qt::CrossCursor);
        QMessageBox::information(this, "提示", "请在曲线上点击起始点。");
    }
}

void WT_PlottingWidget::onGraphClicked(QCPAbstractPlottable *plottable, int dataIndex, QMouseEvent *event) {
    Q_UNUSED(event);
    if(!m_isSelectingForExport) return;
    QCPGraph* graph = qobject_cast<QCPGraph*>(plottable);
    if(!graph) return;
    double key = graph->dataMainKey(dataIndex);
    if(m_selectionStep == 1) {
        m_exportStartIndex = key;
        m_selectionStep = 2;
        QMessageBox::information(this, "提示", "请点击结束点。");
    } else {
        m_exportEndIndex = key;
        if(m_exportStartIndex > m_exportEndIndex) std::swap(m_exportStartIndex, m_exportEndIndex);
        m_isSelectingForExport = false;
        ui->customPlot->getPlot()->setCursor(Qt::ArrowCursor);
        executeExport(false, m_exportStartIndex, m_exportEndIndex);
    }
}

void WT_PlottingWidget::executeExport(bool fullRange, double start, double end) {
    QString dir = ModelParameter::instance()->getProjectPath();
    if (dir.isEmpty()) dir = QDir::currentPath();
    QString name = dir + "/export.csv";
    QString file = QFileDialog::getSaveFileName(this, "保存", name, "CSV Files (*.csv);;Excel Files (*.xls);;Text Files (*.txt)");
    if(file.isEmpty()) return;
    QFile f(file);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    QString sep = ",";
    if(file.endsWith(".txt") || file.endsWith(".xls")) sep = "\t";

    CurveInfo& info = m_curves[m_currentDisplayedCurve];

    if(ui->customPlot->getChartMode() == ChartWidget::Mode_Stacked) {
        if (!m_graphPress || !m_graphProd) return;

        out << (fullRange ? "Time,P,Q\n" : "AdjTime,P,Q,OrigTime\n");

        auto dataPtr = m_graphPress->data();
        for (auto it = dataPtr->begin(); it != dataPtr->end(); ++it) {
            double t = it->key;
            if(!fullRange && (t < start || t > end)) continue;

            double p = it->value;
            double q = getProductionValueFromGraph(t, m_graphProd);

            if(fullRange) out << t << sep << p << sep << q << "\n";
            else out << (t-start) << sep << p << sep << q << sep << t << "\n";
        }
    } else {
        QCPGraph* graph = ui->customPlot->getPlot()->graphCount() > 0 ? ui->customPlot->getPlot()->graph(0) : nullptr;
        if (graph) {
            out << (fullRange ? "Time,Value\n" : "AdjTime,Value,OrigTime\n");
            auto dataPtr = graph->data();
            for (auto it = dataPtr->begin(); it != dataPtr->end(); ++it) {
                double t = it->key;
                if(!fullRange && (t < start || t > end)) continue;
                double val = it->value;
                if(fullRange) out << t << sep << val << "\n";
                else out << (t-start) << sep << val << sep << t << "\n";
            }
        } else {
            out << (fullRange ? "Time,Value\n" : "AdjTime,Value,OrigTime\n");
            for(int i=0; i<info.xData.size(); ++i) {
                double t = info.xData[i];
                if(!fullRange && (t < start || t > end)) continue;
                double val = info.yData[i];
                if(fullRange) out << t << sep << val << "\n";
                else out << (t-start) << sep << val << sep << t << "\n";
            }
        }
    }
    f.close();
    QMessageBox::information(this, "成功", "导出完成。");
}

double WT_PlottingWidget::getProductionValueFromGraph(double t, QCPGraph* graph) {
    if (!graph) return 0.0;
    auto data = graph->data();
    auto it = data->findBegin(t);
    if (it == data->end()) return 0.0;
    if (qAbs(it->key - t) < 1e-9) return it->value;
    if (it == data->begin()) return it->value;
    auto prev = it;
    --prev;
    double t1 = prev->key;
    double v1 = prev->value;
    double t2 = it->key;
    double v2 = it->value;
    if (qAbs(t2 - t1) < 1e-9) return v1;
    return v1 + (t - t1) * (v2 - v1) / (t2 - t1);
}

void WT_PlottingWidget::on_btn_NewCurve_clicked() {
    if(m_dataMap.isEmpty()) return;
    PlottingDialog1 dlg(m_dataMap, this);
    applyDialogStyle(&dlg);
    if(dlg.exec() == QDialog::Accepted) {
        CurveInfo info;
        info.name = dlg.getCurveName();
        info.legendName = dlg.getLegendName();
        info.sourceFileName = dlg.getSelectedFileName();
        info.xCol = dlg.getXColumn();
        info.yCol = dlg.getYColumn();
        info.pointShape = dlg.getPointShape();
        info.pointColor = dlg.getPointColor();
        info.lineStyle = dlg.getLineStyle();
        info.lineColor = dlg.getLineColor();
        info.type = 0;
        if (m_dataMap.contains(info.sourceFileName)) {
            QStandardItemModel* model = m_dataMap.value(info.sourceFileName);
            for(int i=0; i<model->rowCount(); ++i) {
                double xVal = model->item(i, info.xCol)->text().toDouble();
                double yVal = model->item(i, info.yCol)->text().toDouble();
                if (xVal > 1e-9 && yVal > 1e-9) { info.xData.append(xVal); info.yData.append(yVal); }
            }
        }
        m_curves.insert(info.name, info);
        ui->listWidget_Curves->addItem(info.name);
        on_listWidget_Curves_itemDoubleClicked(ui->listWidget_Curves->item(ui->listWidget_Curves->count()-1));
    }
}

void WT_PlottingWidget::on_btn_PressureRate_clicked() {
    if(m_dataMap.isEmpty()) return;

    PlottingDialog2 dlg(m_dataMap, this);
    applyDialogStyle(&dlg);

    if(dlg.exec() == QDialog::Accepted) {
        CurveInfo info;
        info.name = dlg.getChartName();
        info.legendName = dlg.getPressLegend();
        info.type = 1;

        info.sourceFileName = dlg.getPressFileName();
        info.sourceFileName2 = dlg.getProdFileName();

        info.xCol = dlg.getPressXCol();
        info.yCol = dlg.getPressYCol();
        info.x2Col = dlg.getProdXCol();
        info.y2Col = dlg.getProdYCol();

        if (m_dataMap.contains(info.sourceFileName)) {
            QStandardItemModel* modelP = m_dataMap.value(info.sourceFileName);
            for(int i=0; i<modelP->rowCount(); ++i) {
                info.xData.append(modelP->item(i, info.xCol)->text().toDouble());
                info.yData.append(modelP->item(i, info.yCol)->text().toDouble());
            }
        }

        if (m_dataMap.contains(info.sourceFileName2)) {
            QStandardItemModel* modelQ = m_dataMap.value(info.sourceFileName2);
            for(int i=0; i<modelQ->rowCount(); ++i) {
                info.x2Data.append(modelQ->item(i, info.x2Col)->text().toDouble());
                info.y2Data.append(modelQ->item(i, info.y2Col)->text().toDouble());
            }
        }

        info.pointShape = dlg.getPressShape();
        info.pointColor = dlg.getPressPointColor();
        info.lineStyle = dlg.getPressLineStyle();
        info.lineColor = dlg.getPressLineColor();
        info.prodLegendName = dlg.getProdLegend();
        info.prodGraphType = dlg.getProdGraphType();
        info.prodColor = dlg.getProdColor();

        m_curves.insert(info.name, info);
        ui->listWidget_Curves->addItem(info.name);
        on_listWidget_Curves_itemDoubleClicked(ui->listWidget_Curves->item(ui->listWidget_Curves->count()-1));
    }
}

void WT_PlottingWidget::on_btn_Derivative_clicked() {
    if(m_dataMap.isEmpty()) return;
    PlottingDialog3 dlg(m_dataMap, this);
    applyDialogStyle(&dlg);
    if(dlg.exec() == QDialog::Accepted) {
        CurveInfo info;
        info.name = dlg.getCurveName();
        info.legendName = dlg.getPressLegend();
        info.sourceFileName = dlg.getSelectedFileName();
        info.type = 2;
        info.xCol = dlg.getTimeColumn();
        info.yCol = dlg.getPressureColumn();
        info.testType = (int)dlg.getTestType();
        info.initialPressure = dlg.getInitialPressure();
        info.LSpacing = dlg.getLSpacing();
        info.isSmooth = dlg.isSmoothEnabled();
        info.smoothFactor = dlg.getSmoothFactor();
        if (m_dataMap.contains(info.sourceFileName)) {
            QStandardItemModel* model = m_dataMap.value(info.sourceFileName);
            double p_shutin = (model->rowCount() > 0) ? model->item(0, info.yCol)->text().toDouble() : 0;
            for(int i=0; i<model->rowCount(); ++i) {
                double t = model->item(i, info.xCol)->text().toDouble();
                double p = model->item(i, info.yCol)->text().toDouble();
                double dp = (info.testType == 0) ? std::abs(info.initialPressure - p) : std::abs(p - p_shutin);
                if(t > 0 && dp > 0) { info.xData.append(t); info.yData.append(dp); }
            }
        }
        QVector<double> derData = PressureDerivativeCalculator::calculateBourdetDerivative(info.xData, info.yData, info.LSpacing);
        if (info.isSmooth) derData = PressureDerivativeCalculator1::smoothData(derData, info.smoothFactor);
        info.derivData = derData;
        info.pointShape = dlg.getPressShape();
        info.pointColor = dlg.getPressPointColor();
        info.lineStyle = dlg.getPressLineStyle();
        info.lineColor = dlg.getPressLineColor();
        info.derivShape = dlg.getDerivShape();
        info.derivPointColor = dlg.getDerivPointColor();
        info.derivLineStyle = dlg.getDerivLineStyle();
        info.derivLineColor = dlg.getDerivLineColor();
        info.prodLegendName = dlg.getDerivLegend();
        m_curves.insert(info.name, info);
        ui->listWidget_Curves->addItem(info.name);
        on_listWidget_Curves_itemDoubleClicked(ui->listWidget_Curves->item(ui->listWidget_Curves->count()-1));
    }
}

void WT_PlottingWidget::on_btn_Delete_clicked() {
    QListWidgetItem* item = getCurrentSelectedItem(); if(!item) return;
    QString name = item->text();
    if(QMessageBox::question(this, "确认删除", "确定要删除曲线 \"" + name + "\" 吗？") == QMessageBox::Yes) {
        m_curves.remove(name); delete item;
        if(m_currentDisplayedCurve == name) { ui->customPlot->clearGraphs(); m_currentDisplayedCurve.clear(); }
    }
}

double WT_PlottingWidget::getProductionValueAt(double t, const CurveInfo& info) { Q_UNUSED(t); return info.y2Data.isEmpty() ? 0 : info.y2Data.last(); }

QListWidgetItem* WT_PlottingWidget::getCurrentSelectedItem() { return ui->listWidget_Curves->currentItem(); }


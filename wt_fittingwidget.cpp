/*
 * 文件名: wt_fittingwidget.cpp
 * 文件作用: 试井拟合分析主界面类的实现文件
 * 功能描述:
 * 1. 初始化界面，集成 ChartWidget 作为绘图容器。
 * 2. 实现了多线程 Levenberg-Marquardt 拟合算法。
 * 3. 实现了数据的加载及展示。
 * 4. [新增] 实现了参数敏感性分析的多曲线绘制逻辑。
 * 5. [新增] 响应鼠标滚轮调节参数的实时重绘。
 */

#include "wt_fittingwidget.h"
#include "ui_wt_fittingwidget.h"
#include "modelparameter.h"
#include "modelselect.h"
#include "fittingdatadialog.h"
#include "pressurederivativecalculator.h"
#include "pressurederivativecalculator1.h"

#include <QtConcurrent>
#include <QMessageBox>
#include <QDebug>
#include <cmath>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QBuffer>
#include <Eigen/Dense>

// 构造函数
FittingWidget::FittingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FittingWidget),
    m_modelManager(nullptr),
    m_chartWidget(nullptr),
    m_plot(nullptr),
    m_plotTitle(nullptr),
    m_currentModelType(ModelManager::Model_1),
    m_isFitting(false)
{
    ui->setupUi(this);

    // 初始化图表组件 ChartWidget 并添加到布局
    m_chartWidget = new ChartWidget(this);
    ui->plotContainer->layout()->addWidget(m_chartWidget);

    m_plot = m_chartWidget->getPlot();
    m_chartWidget->setTitle("试井解释拟合 (Well Test Fitting)");

    connect(m_chartWidget, &ChartWidget::exportDataTriggered, this, &FittingWidget::onExportCurveData);

    QList<int> sizes;
    sizes << 350 << 650;
    ui->splitter->setSizes(sizes);
    ui->splitter->setCollapsible(0, false);

    m_paramChart = new FittingParameterChart(ui->tableParams, this);

    // [新增] 连接参数图表的滚轮调节信号，实现实时刷新
    connect(m_paramChart, &FittingParameterChart::parameterChangedByWheel, this, &FittingWidget::updateModelCurve);

    setupPlot();

    qRegisterMetaType<QMap<QString,double>>("QMap<QString,double>");
    qRegisterMetaType<ModelManager::ModelType>("ModelManager::ModelType");
    qRegisterMetaType<QVector<double>>("QVector<double>");

    connect(this, &FittingWidget::sigIterationUpdated, this, &FittingWidget::onIterationUpdate, Qt::QueuedConnection);
    connect(this, &FittingWidget::sigProgress, ui->progressBar, &QProgressBar::setValue);
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &FittingWidget::onFitFinished);

    connect(ui->sliderWeight, &QSlider::valueChanged, this, &FittingWidget::onSliderWeightChanged);

    ui->sliderWeight->setRange(0, 100);
    ui->sliderWeight->setValue(50);
    onSliderWeightChanged(50);
}

FittingWidget::~FittingWidget()
{
    delete ui;
}

void FittingWidget::setModelManager(ModelManager *m)
{
    m_modelManager = m;
    m_paramChart->setModelManager(m);
    initializeDefaultModel();
}

void FittingWidget::setProjectDataModels(const QMap<QString, QStandardItemModel *> &models)
{
    m_dataMap = models;
}

void FittingWidget::updateBasicParameters()
{
    // 同步基础参数逻辑
}

void FittingWidget::initializeDefaultModel()
{
    if(!m_modelManager) return;
    m_currentModelType = ModelManager::Model_1;
    ui->btn_modelSelect->setText("当前: " + ModelManager::getModelTypeName(m_currentModelType));
    on_btnResetParams_clicked();
}

void FittingWidget::setupPlot() {
    if (!m_plot) return;

    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setBackground(Qt::white);
    m_plot->axisRect()->setBackground(Qt::white);

    QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    m_plot->xAxis->setScaleType(QCPAxis::stLogarithmic); m_plot->xAxis->setTicker(logTicker);
    m_plot->yAxis->setScaleType(QCPAxis::stLogarithmic); m_plot->yAxis->setTicker(logTicker);

    m_plot->xAxis->setNumberFormat("eb"); m_plot->xAxis->setNumberPrecision(0);
    m_plot->yAxis->setNumberFormat("eb"); m_plot->yAxis->setNumberPrecision(0);

    QFont labelFont("Microsoft YaHei", 10, QFont::Bold);
    QFont tickFont("Microsoft YaHei", 9);
    m_plot->xAxis->setLabel("时间 Time (h)");
    m_plot->yAxis->setLabel("压差 & 导数 Delta P & Derivative (MPa)");
    m_plot->xAxis->setLabelFont(labelFont); m_plot->yAxis->setLabelFont(labelFont);
    m_plot->xAxis->setTickLabelFont(tickFont); m_plot->yAxis->setTickLabelFont(tickFont);

    m_plot->xAxis2->setVisible(true); m_plot->yAxis2->setVisible(true);
    m_plot->xAxis2->setTickLabels(false); m_plot->yAxis2->setTickLabels(false);
    connect(m_plot->xAxis, SIGNAL(rangeChanged(QCPRange)), m_plot->xAxis2, SLOT(setRange(QCPRange)));
    connect(m_plot->yAxis, SIGNAL(rangeChanged(QCPRange)), m_plot->yAxis2, SLOT(setRange(QCPRange)));
    m_plot->xAxis2->setScaleType(QCPAxis::stLogarithmic); m_plot->yAxis2->setScaleType(QCPAxis::stLogarithmic);
    m_plot->xAxis2->setTicker(logTicker); m_plot->yAxis2->setTicker(logTicker);

    m_plot->xAxis->grid()->setVisible(true); m_plot->yAxis->grid()->setVisible(true);
    m_plot->xAxis->grid()->setSubGridVisible(true); m_plot->yAxis->grid()->setSubGridVisible(true);
    m_plot->xAxis->grid()->setPen(QPen(QColor(220, 220, 220), 1, Qt::SolidLine));
    m_plot->yAxis->grid()->setPen(QPen(QColor(220, 220, 220), 1, Qt::SolidLine));
    m_plot->xAxis->grid()->setSubGridPen(QPen(QColor(240, 240, 240), 1, Qt::DotLine));
    m_plot->yAxis->grid()->setSubGridPen(QPen(QColor(240, 240, 240), 1, Qt::DotLine));

    m_plot->xAxis->setRange(1e-3, 1e3); m_plot->yAxis->setRange(1e-3, 1e2);

    m_plot->addGraph(); m_plot->graph(0)->setPen(Qt::NoPen);
    m_plot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, QColor(0, 100, 0), 6));
    m_plot->graph(0)->setName("实测压差");

    m_plot->addGraph(); m_plot->graph(1)->setPen(Qt::NoPen);
    m_plot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssTriangle, Qt::magenta, 6));
    m_plot->graph(1)->setName("实测导数");

    m_plot->addGraph(); m_plot->graph(2)->setPen(QPen(Qt::red, 2));
    m_plot->graph(2)->setName("理论压差");

    m_plot->addGraph(); m_plot->graph(3)->setPen(QPen(Qt::blue, 2));
    m_plot->graph(3)->setName("理论导数");

    m_plot->legend->setVisible(true);
    m_plot->legend->setFont(QFont("Microsoft YaHei", 9));
    m_plot->legend->setBrush(QBrush(QColor(255, 255, 255, 200)));
}

void FittingWidget::on_btnLoadData_clicked() {
    FittingDataDialog dlg(m_dataMap, this);
    if (dlg.exec() != QDialog::Accepted) return;

    FittingDataSettings settings = dlg.getSettings();
    QStandardItemModel* sourceModel = dlg.getPreviewModel();

    if (!sourceModel || sourceModel->rowCount() == 0) {
        QMessageBox::warning(this, "警告", "所选数据源为空，无法加载！");
        return;
    }

    QVector<double> rawTime, rawPressureData, finalDeriv;
    int skip = settings.skipRows;
    int rows = sourceModel->rowCount();

    for (int i = skip; i < rows; ++i) {
        QStandardItem* itemT = sourceModel->item(i, settings.timeColIndex);
        QStandardItem* itemP = sourceModel->item(i, settings.pressureColIndex);

        if (itemT && itemP) {
            bool okT, okP;
            double t = itemT->text().toDouble(&okT);
            double p = itemP->text().toDouble(&okP);

            if (okT && okP && t > 0) {
                rawTime.append(t);
                rawPressureData.append(p);
                if (settings.derivColIndex >= 0) {
                    QStandardItem* itemD = sourceModel->item(i, settings.derivColIndex);
                    if (itemD) finalDeriv.append(itemD->text().toDouble());
                    else finalDeriv.append(0.0);
                }
            }
        }
    }

    if (rawTime.isEmpty()) {
        QMessageBox::warning(this, "警告", "未能提取到有效数据。");
        return;
    }

    QVector<double> finalDeltaP;
    double p_shutin = rawPressureData.first();

    for (double p : rawPressureData) {
        double deltaP = 0.0;
        if (settings.testType == Test_Drawdown) {
            deltaP = std::abs(settings.initialPressure - p);
        } else {
            deltaP = std::abs(p - p_shutin);
        }
        finalDeltaP.append(deltaP);
    }

    if (settings.derivColIndex == -1) {
        finalDeriv = PressureDerivativeCalculator::calculateBourdetDerivative(rawTime, finalDeltaP, settings.lSpacing);
        if (settings.enableSmoothing) {
            finalDeriv = PressureDerivativeCalculator1::smoothData(finalDeriv, settings.smoothingSpan);
        }
    } else {
        if (settings.enableSmoothing) {
            finalDeriv = PressureDerivativeCalculator1::smoothData(finalDeriv, settings.smoothingSpan);
        }
        if (finalDeriv.size() != rawTime.size()) {
            finalDeriv.resize(rawTime.size());
        }
    }

    setObservedData(rawTime, finalDeltaP, finalDeriv);
    QMessageBox::information(this, "成功", "观测数据已成功加载。");
}

void FittingWidget::setObservedData(const QVector<double>& t, const QVector<double>& deltaP, const QVector<double>& d) {
    m_obsTime = t;
    m_obsDeltaP = deltaP;
    m_obsDerivative = d;

    QVector<double> vt, vp, vd;
    for(int i=0; i<t.size(); ++i) {
        if(t[i]>1e-8 && deltaP[i]>1e-8) {
            vt << t[i];
            vp << deltaP[i];
            if(i < d.size() && d[i] > 1e-8) vd << d[i];
            else vd << 1e-10;
        }
    }

    m_plot->graph(0)->setData(vt, vp);
    m_plot->graph(1)->setData(vt, vd);

    m_plot->rescaleAxes();
    if(m_plot->xAxis->range().lower <= 0) m_plot->xAxis->setRangeLower(1e-3);
    if(m_plot->yAxis->range().lower <= 0) m_plot->yAxis->setRangeLower(1e-3);
    m_plot->replot();
}

void FittingWidget::onSliderWeightChanged(int value)
{
    double wPressure = value / 100.0;
    double wDerivative = 1.0 - wPressure;
    ui->label_ValDerivative->setText(QString("导数权重: %1").arg(wDerivative, 0, 'f', 2));
    ui->label_ValPressure->setText(QString("压差权重: %1").arg(wPressure, 0, 'f', 2));
}

void FittingWidget::on_btnSelectParams_clicked()
{
    m_paramChart->updateParamsFromTable();
    QList<FitParameter> currentParams = m_paramChart->getParameters();
    ParamSelectDialog dlg(currentParams, this);
    if(dlg.exec() == QDialog::Accepted) {
        m_paramChart->setParameters(dlg.getUpdatedParams());
        updateModelCurve();
    }
}

void FittingWidget::on_btnRunFit_clicked() {
    if(m_isFitting) return;
    if(m_obsTime.isEmpty()) {
        QMessageBox::warning(this,"错误","请先加载观测数据。");
        return;
    }

    m_paramChart->updateParamsFromTable();
    m_isFitting = true;
    m_stopRequested = false;
    ui->btnRunFit->setEnabled(false);

    ModelManager::ModelType modelType = m_currentModelType;
    QList<FitParameter> paramsCopy = m_paramChart->getParameters();
    double w = ui->sliderWeight->value() / 100.0;

    m_watcher.setFuture(QtConcurrent::run([this, modelType, paramsCopy, w](){
        runOptimizationTask(modelType, paramsCopy, w);
    }));
}

void FittingWidget::on_btnStop_clicked() {
    m_stopRequested = true;
}

void FittingWidget::on_btnImportModel_clicked() {
    updateModelCurve();
}

void FittingWidget::on_btnResetParams_clicked() {
    if(!m_modelManager) return;
    m_paramChart->resetParams(m_currentModelType);
    updateModelCurve();
}

void FittingWidget::on_btn_modelSelect_clicked() {
    ModelSelect dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString code = dlg.getSelectedModelCode();
        QString name = dlg.getSelectedModelName();

        bool found = false;
        ModelManager::ModelType newType = ModelManager::Model_1;

        if (code == "modelwidget1") newType = ModelManager::Model_1;
        else if (code == "modelwidget2") newType = ModelManager::Model_2;
        else if (code == "modelwidget3") newType = ModelManager::Model_3;
        else if (code == "modelwidget4") newType = ModelManager::Model_4;
        else if (code == "modelwidget5") newType = ModelManager::Model_5;
        else if (code == "modelwidget6") newType = ModelManager::Model_6;
        else if (!code.isEmpty()) found = true;

        if (code.startsWith("modelwidget")) found = true;

        if (found) {
            m_paramChart->switchModel(newType);
            m_currentModelType = newType;
            ui->btn_modelSelect->setText("当前: " + name);
            updateModelCurve();
        } else {
            QMessageBox::warning(this, "提示", "所选组合暂无对应的模型。\nCode: " + code);
        }
    }
}

void FittingWidget::on_btnExportData_clicked() {
    m_paramChart->updateParamsFromTable();
    QList<FitParameter> params = m_paramChart->getParameters();

    QString defaultDir = ModelParameter::instance()->getProjectPath();
    if(defaultDir.isEmpty()) defaultDir = ".";

    QString fileName = QFileDialog::getSaveFileName(this, "导出拟合参数", defaultDir + "/FittingParameters.csv", "CSV Files (*.csv);;Text Files (*.txt)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);

    if(fileName.endsWith(".csv", Qt::CaseInsensitive)) {
        file.write("\xEF\xBB\xBF");
        out << QString("参数中文名,参数英文名,拟合值,单位\n");
        for(const auto& param : params) {
            QString htmlSym, uniSym, unitStr, dummyName;
            FittingParameterChart::getParamDisplayInfo(param.name, dummyName, htmlSym, uniSym, unitStr);
            if(unitStr == "无因次" || unitStr == "小数") unitStr = "";
            out << QString("%1,%2,%3,%4\n").arg(param.displayName).arg(uniSym).arg(param.value, 0, 'g', 10).arg(unitStr);
        }
    } else {
        for(const auto& param : params) {
            QString htmlSym, uniSym, unitStr, dummyName;
            FittingParameterChart::getParamDisplayInfo(param.name, dummyName, htmlSym, uniSym, unitStr);
            if(unitStr == "无因次" || unitStr == "小数") unitStr = "";
            QString lineStr = QString("%1 (%2): %3 %4").arg(param.displayName).arg(uniSym).arg(param.value, 0, 'g', 10).arg(unitStr);
            out << lineStr.trimmed() << "\n";
        }
    }
    file.close();
    QMessageBox::information(this, "完成", "参数数据已成功导出。");
}

void FittingWidget::onExportCurveData() {
    QString defaultDir = ModelParameter::instance()->getProjectPath();
    if(defaultDir.isEmpty()) defaultDir = ".";

    QString path = QFileDialog::getSaveFileName(this, "导出拟合曲线数据", defaultDir + "/FittingCurves.csv", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    auto graphObsP = m_plot->graph(0);
    auto graphObsD = m_plot->graph(1);
    auto graphModP = m_plot->graph(2); // 注意：敏感性分析时 graph 2 可能不存在或被覆盖

    // 简单导出：只导出前两条（实测）和当前的理论（如果是单曲线模式）
    // 如果是敏感性分析模式，导出可能需要更复杂的逻辑，这里暂时保留原逻辑，只导出第2、3条（如果存在）
    auto graphModD = m_plot->graph(3);

    if (!graphObsP) return;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << "Obs_Time,Obs_DP,Obs_Deriv,Model_Time,Model_DP,Model_Deriv\n";

        auto itObsP = graphObsP->data()->begin();
        auto itObsD = graphObsD->data()->begin();

        QCPGraphDataContainer::const_iterator itModP, endModP, itModD;
        bool hasModel = (graphModP != nullptr && graphModD != nullptr);
        if(hasModel) {
            itModP = graphModP->data()->begin();
            endModP = graphModP->data()->end();
            itModD = graphModD->data()->begin();
        }

        auto endObsP = graphObsP->data()->end();

        while (itObsP != endObsP || (hasModel && itModP != endModP)) {
            QStringList line;

            if (itObsP != endObsP) {
                line << QString::number(itObsP->key, 'g', 10);
                line << QString::number(itObsP->value, 'g', 10);
                if (itObsD != graphObsD->data()->end()) {
                    line << QString::number(itObsD->value, 'g', 10);
                    ++itObsD;
                } else {
                    line << "";
                }
                ++itObsP;
            } else {
                line << "" << "" << "";
            }

            if (hasModel && itModP != endModP) {
                line << QString::number(itModP->key, 'g', 10);
                line << QString::number(itModP->value, 'g', 10);
                if (itModD != graphModD->data()->end()) {
                    line << QString::number(itModD->value, 'g', 10);
                    ++itModD;
                } else {
                    line << "";
                }
                ++itModP;
            } else {
                line << "" << "" << "";
            }

            out << line.join(",") << "\n";
        }

        f.close();
        QMessageBox::information(this, "导出成功", "拟合曲线数据已保存。");
    }
}

void FittingWidget::runOptimizationTask(ModelManager::ModelType modelType, QList<FitParameter> fitParams, double weight) {
    runLevenbergMarquardtOptimization(modelType, fitParams, weight);
}

// Levenberg-Marquardt
void FittingWidget::runLevenbergMarquardtOptimization(ModelManager::ModelType modelType, QList<FitParameter> params, double weight) {
    if(m_modelManager) m_modelManager->setHighPrecision(false);

    QVector<int> fitIndices;
    for(int i=0; i<params.size(); ++i) {
        if(params[i].isFit) fitIndices.append(i);
    }
    int nParams = fitIndices.size();

    if(nParams == 0) {
        QMetaObject::invokeMethod(this, "onFitFinished");
        return;
    }

    double lambda = 0.01;
    int maxIter = 50;
    double currentSSE = 1e15;

    QMap<QString, double> currentParamMap;
    for(const auto& p : params) currentParamMap.insert(p.name, p.value);

    if(currentParamMap.contains("L") && currentParamMap.contains("Lf") && currentParamMap["L"] > 1e-9)
        currentParamMap["LfD"] = currentParamMap["Lf"] / currentParamMap["L"];

    QVector<double> residuals = calculateResiduals(currentParamMap, modelType, weight);
    currentSSE = calculateSumSquaredError(residuals);

    ModelCurveData curve = m_modelManager->calculateTheoreticalCurve(modelType, currentParamMap);
    emit sigIterationUpdated(currentSSE/residuals.size(), currentParamMap, std::get<0>(curve), std::get<1>(curve), std::get<2>(curve));

    for(int iter = 0; iter < maxIter; ++iter) {
        if(m_stopRequested) break;
        if (!residuals.isEmpty() && (currentSSE / residuals.size()) < 3e-3) break;

        emit sigProgress(iter * 100 / maxIter);

        QVector<QVector<double>> J = computeJacobian(currentParamMap, residuals, fitIndices, modelType, params, weight);
        int nRes = residuals.size();

        QVector<QVector<double>> H(nParams, QVector<double>(nParams, 0.0));
        QVector<double> g(nParams, 0.0);

        for(int k=0; k<nRes; ++k) {
            for(int i=0; i<nParams; ++i) {
                g[i] += J[k][i] * residuals[k];
                for(int j=0; j<=i; ++j) {
                    H[i][j] += J[k][i] * J[k][j];
                }
            }
        }
        for(int i=0; i<nParams; ++i) {
            for(int j=i+1; j<nParams; ++j) {
                H[i][j] = H[j][i];
            }
        }

        bool stepAccepted = false;
        for(int tryIter=0; tryIter<5; ++tryIter) {
            QVector<QVector<double>> H_lm = H;
            for(int i=0; i<nParams; ++i) {
                H_lm[i][i] += lambda * (1.0 + std::abs(H[i][i]));
            }

            QVector<double> negG(nParams);
            for(int i=0;i<nParams;++i) negG[i] = -g[i];

            QVector<double> delta = solveLinearSystem(H_lm, negG);
            QMap<QString, double> trialMap = currentParamMap;

            for(int i=0; i<nParams; ++i) {
                int pIdx = fitIndices[i];
                QString pName = params[pIdx].name;
                double oldVal = currentParamMap[pName];
                bool isLog = (oldVal > 1e-12 && pName != "S" && pName != "nf");
                double newVal;

                if(isLog) newVal = pow(10.0, log10(oldVal) + delta[i]);
                else newVal = oldVal + delta[i];

                newVal = qMax(params[pIdx].min, qMin(newVal, params[pIdx].max));
                trialMap[pName] = newVal;
            }

            if(trialMap.contains("L") && trialMap.contains("Lf") && trialMap["L"] > 1e-9)
                trialMap["LfD"] = trialMap["Lf"] / trialMap["L"];

            QVector<double> newRes = calculateResiduals(trialMap, modelType, weight);
            double newSSE = calculateSumSquaredError(newRes);

            if(newSSE < currentSSE) {
                currentSSE = newSSE;
                currentParamMap = trialMap;
                residuals = newRes;
                lambda /= 10.0;
                stepAccepted = true;
                ModelCurveData iterCurve = m_modelManager->calculateTheoreticalCurve(modelType, currentParamMap);
                emit sigIterationUpdated(currentSSE/nRes, currentParamMap, std::get<0>(iterCurve), std::get<1>(iterCurve), std::get<2>(iterCurve));
                break;
            } else {
                lambda *= 10.0;
            }
        }
        if(!stepAccepted && lambda > 1e10) break;
    }

    if(m_modelManager) m_modelManager->setHighPrecision(true);

    if(currentParamMap.contains("L") && currentParamMap.contains("Lf") && currentParamMap["L"] > 1e-9)
        currentParamMap["LfD"] = currentParamMap["Lf"] / currentParamMap["L"];

    ModelCurveData finalCurve = m_modelManager->calculateTheoreticalCurve(modelType, currentParamMap);
    emit sigIterationUpdated(currentSSE/residuals.size(), currentParamMap, std::get<0>(finalCurve), std::get<1>(finalCurve), std::get<2>(finalCurve));

    QMetaObject::invokeMethod(this, "onFitFinished");
}

QVector<double> FittingWidget::calculateResiduals(const QMap<QString, double>& params, ModelManager::ModelType modelType, double weight) {
    if(!m_modelManager || m_obsTime.isEmpty()) return QVector<double>();

    ModelCurveData res = m_modelManager->calculateTheoreticalCurve(modelType, params, m_obsTime);
    const QVector<double>& pCal = std::get<1>(res);
    const QVector<double>& dpCal = std::get<2>(res);

    QVector<double> r;
    double wp = weight;
    double wd = 1.0 - weight;

    int count = qMin(m_obsDeltaP.size(), pCal.size());
    for(int i=0; i<count; ++i) {
        if(m_obsDeltaP[i] > 1e-10 && pCal[i] > 1e-10)
            r.append( (log(m_obsDeltaP[i]) - log(pCal[i])) * wp );
        else
            r.append(0.0);
    }

    int dCount = qMin(m_obsDerivative.size(), dpCal.size());
    dCount = qMin(dCount, count);
    for(int i=0; i<dCount; ++i) {
        if(m_obsDerivative[i] > 1e-10 && dpCal[i] > 1e-10)
            r.append( (log(m_obsDerivative[i]) - log(dpCal[i])) * wd );
        else
            r.append(0.0);
    }
    return r;
}

QVector<QVector<double>> FittingWidget::computeJacobian(const QMap<QString, double>& params, const QVector<double>& baseResiduals, const QVector<int>& fitIndices, ModelManager::ModelType modelType, const QList<FitParameter>& currentFitParams, double weight) {
    int nRes = baseResiduals.size();
    int nParams = fitIndices.size();
    QVector<QVector<double>> J(nRes, QVector<double>(nParams));

    for(int j = 0; j < nParams; ++j) {
        int idx = fitIndices[j];
        QString pName = currentFitParams[idx].name;
        double val = params.value(pName);
        bool isLog = (val > 1e-12 && pName != "S" && pName != "nf");

        double h;
        QMap<QString, double> pPlus = params;
        QMap<QString, double> pMinus = params;

        if(isLog) {
            h = 0.01;
            double valLog = log10(val);
            pPlus[pName] = pow(10.0, valLog + h);
            pMinus[pName] = pow(10.0, valLog - h);
        } else {
            h = 1e-4;
            pPlus[pName] = val + h;
            pMinus[pName] = val - h;
        }

        auto updateDeps = [](QMap<QString,double>& map) { if(map.contains("L") && map.contains("Lf") && map["L"] > 1e-9) map["LfD"] = map["Lf"] / map["L"]; };
        if(pName == "L" || pName == "Lf") { updateDeps(pPlus); updateDeps(pMinus); }

        QVector<double> rPlus = calculateResiduals(pPlus, modelType, weight);
        QVector<double> rMinus = calculateResiduals(pMinus, modelType, weight);

        if(rPlus.size() == nRes && rMinus.size() == nRes) {
            for(int i=0; i<nRes; ++i) {
                J[i][j] = (rPlus[i] - rMinus[i]) / (2.0 * h);
            }
        }
    }
    return J;
}

QVector<double> FittingWidget::solveLinearSystem(const QVector<QVector<double>>& A, const QVector<double>& b) {
    int n = b.size();
    if (n == 0) return QVector<double>();

    Eigen::MatrixXd matA(n, n);
    Eigen::VectorXd vecB(n);

    for (int i = 0; i < n; ++i) {
        vecB(i) = b[i];
        for (int j = 0; j < n; ++j) {
            matA(i, j) = A[i][j];
        }
    }

    Eigen::VectorXd x = matA.ldlt().solve(vecB);

    QVector<double> res(n);
    for (int i = 0; i < n; ++i) res[i] = x(i);
    return res;
}

double FittingWidget::calculateSumSquaredError(const QVector<double>& residuals) {
    double sse = 0.0;
    for(double v : residuals) sse += v*v;
    return sse;
}

// [新增] 辅助函数：解析逗号分隔字符串
QVector<double> FittingWidget::parseSensitivityValues(const QString& text) {
    QVector<double> values;
    QString cleanText = text;
    // 替换中文逗号
    cleanText.replace(QChar(0xFF0C), ",");
    QStringList parts = cleanText.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok;
        double v = part.trimmed().toDouble(&ok);
        if (ok) values.append(v);
    }
    // 如果没有有效数值，返回空
    return values;
}

// [修改] 更新模型曲线：支持敏感性分析（多值多曲线）
void FittingWidget::updateModelCurve() {
    if(!m_modelManager) {
        QMessageBox::critical(this, "错误", "ModelManager 未初始化！");
        return;
    }
    ui->tableParams->clearFocus();

    // 1. 获取所有参数的原始文本 (可能包含 "1,2,3" 这种多值)
    QMap<QString, QString> rawTexts = m_paramChart->getRawParamTexts();

    // 2. 检查是否有参数需要进行敏感性分析
    QString sensitivityKey = "";
    QVector<double> sensitivityValues;

    QMap<QString, double> baseParams;

    // 先构建基础参数表 (取多值的第一个数作为基准)
    for(auto it = rawTexts.begin(); it != rawTexts.end(); ++it) {
        QVector<double> vals = parseSensitivityValues(it.value());
        if (!vals.isEmpty()) {
            baseParams.insert(it.key(), vals.first());
            // 如果发现有多值，且尚未锁定敏感性参数，则锁定该参数
            if (vals.size() > 1 && sensitivityKey.isEmpty()) {
                sensitivityKey = it.key();
                sensitivityValues = vals;
            }
        } else {
            baseParams.insert(it.key(), 0.0);
        }
    }

    // 处理依赖参数
    if(baseParams.contains("L") && baseParams.contains("Lf") && baseParams["L"] > 1e-9)
        baseParams["LfD"] = baseParams["Lf"] / baseParams["L"];
    else
        baseParams["LfD"] = 0.0;

    ModelManager::ModelType type = m_currentModelType;
    QVector<double> targetT = m_obsTime;
    if(targetT.isEmpty()) {
        for(double e = -4; e <= 4; e += 0.1) targetT.append(pow(10, e));
    }

    bool isSensitivityMode = !sensitivityKey.isEmpty();

    // 如果是敏感性模式，禁用“开始拟合”按钮
    ui->btnRunFit->setEnabled(!isSensitivityMode);
    if(isSensitivityMode) {
        ui->label_Error->setText(QString("敏感性分析模式: %1 (%2 个值)").arg(sensitivityKey).arg(sensitivityValues.size()));
    }

    // 清除旧的理论曲线 (保留前2条实测数据)
    // 注意：QCustomPlot 的 clearGraphs 会清空所有。
    // 更好的做法是移除 index >= 2 的 graph
    for (int i = m_plot->graphCount() - 1; i >= 2; --i) {
        m_plot->removeGraph(i);
    }

    // 颜色列表
    QList<QColor> colors = { Qt::red, Qt::blue, QColor(0,180,0), Qt::magenta, QColor(255,140,0), Qt::cyan, Qt::darkRed, Qt::darkBlue };

    if (isSensitivityMode) {
        // 循环绘制多条曲线
        for(int i = 0; i < sensitivityValues.size(); ++i) {
            double val = sensitivityValues[i];
            QMap<QString, double> currentParams = baseParams;
            currentParams[sensitivityKey] = val;

            // 重新处理依赖
            if (sensitivityKey == "L" || sensitivityKey == "Lf") {
                if(currentParams["L"] > 1e-9) currentParams["LfD"] = currentParams["Lf"] / currentParams["L"];
            }

            ModelCurveData res = m_modelManager->calculateTheoreticalCurve(type, currentParams, targetT);

            QColor c = colors[i % colors.size()];
            QString legendSuffix = QString("%1=%2").arg(sensitivityKey).arg(val);

            plotCurves(std::get<0>(res), std::get<1>(res), std::get<2>(res), true);

            // 更新刚刚添加的曲线的样式
            int count = m_plot->graphCount();
            if(count >= 2) {
                m_plot->graph(count-2)->setName("P: " + legendSuffix);
                m_plot->graph(count-2)->setPen(QPen(c, 2, Qt::SolidLine));
                m_plot->graph(count-1)->setName("P': " + legendSuffix);
                m_plot->graph(count-1)->setPen(QPen(c, 2, Qt::DashLine));
            }
        }
    } else {
        // 标准单曲线模式
        ModelCurveData res = m_modelManager->calculateTheoreticalCurve(type, baseParams, targetT);

        // 调用原有的更新逻辑（包含误差计算）
        // 这里手动调用 plotCurves 会导致添加新 graph，但我们希望复用或者重建 graph(2) 和 (3)
        // 为简单起见，plotCurves 函数已修改为总是 append。所以标准模式下也是 append。
        // 但为了保持 graph 索引稳定 (0,1,2,3)，我们需要确保 plotCurves 在单模式下只保留 2 条理论线。

        plotCurves(std::get<0>(res), std::get<1>(res), std::get<2>(res), true);

        int count = m_plot->graphCount();
        if(count >= 4) {
            m_plot->graph(2)->setName("理论压差");
            m_plot->graph(2)->setPen(QPen(Qt::red, 2));
            m_plot->graph(3)->setName("理论导数");
            m_plot->graph(3)->setPen(QPen(Qt::blue, 2));
        }

        // 计算误差（仅在有观测数据时）
        if (!m_obsTime.isEmpty()) {
            QVector<double> residuals = calculateResiduals(baseParams, type, ui->sliderWeight->value()/100.0);
            double sse = calculateSumSquaredError(residuals);
            ui->label_Error->setText(QString("误差(MSE): %1").arg(sse/residuals.size(), 0, 'e', 3));
        }
    }
}

void FittingWidget::onIterationUpdate(double err, const QMap<QString,double>& p,
                                      const QVector<double>& t, const QVector<double>& p_curve, const QVector<double>& d_curve) {
    ui->label_Error->setText(QString("误差(MSE): %1").arg(err, 0, 'e', 3));

    ui->tableParams->blockSignals(true);
    for(int i=0; i<ui->tableParams->rowCount(); ++i) {
        QString key = ui->tableParams->item(i, 1)->data(Qt::UserRole).toString();
        if(p.contains(key)) {
            double val = p[key];
            ui->tableParams->item(i, 2)->setText(QString::number(val, 'g', 5));
        }
    }
    ui->tableParams->blockSignals(false);

    // 迭代更新时，强制清除多余线条，只保留标准4条线
    for (int i = m_plot->graphCount() - 1; i >= 2; --i) {
        m_plot->removeGraph(i);
    }
    plotCurves(t, p_curve, d_curve, true);

    // 恢复标准样式
    m_plot->graph(2)->setName("理论压差");
    m_plot->graph(2)->setPen(QPen(Qt::red, 2));
    m_plot->graph(3)->setName("理论导数");
    m_plot->graph(3)->setPen(QPen(Qt::blue, 2));
}

void FittingWidget::onFitFinished() {
    m_isFitting = false;
    ui->btnRunFit->setEnabled(true);
    QMessageBox::information(this, "完成", "拟合完成。");
}

void FittingWidget::plotCurves(const QVector<double>& t, const QVector<double>& p, const QVector<double>& d, bool isModel) {
    if (!m_plot) return;

    QVector<double> vt, vp, vd;
    for(int i=0; i<t.size(); ++i) {
        if(t[i]>1e-8 && p[i]>1e-8) {
            vt<<t[i];
            vp<<p[i];
            if(i<d.size() && d[i]>1e-8) vd<<d[i]; else vd<<1e-10;
        }
    }
    if(isModel) {
        // [修改] 始终添加新 Graph，以便支持多曲线绘制
        // 调用者负责清理旧 Graph
        QCPGraph* gP = m_plot->addGraph();
        gP->setData(vt, vp);

        QCPGraph* gD = m_plot->addGraph();
        gD->setData(vt, vd);

        if (m_obsTime.isEmpty() && !vt.isEmpty()) {
            m_plot->rescaleAxes();
            if(m_plot->xAxis->range().lower<=0) m_plot->xAxis->setRangeLower(1e-3);
            if(m_plot->yAxis->range().lower<=0) m_plot->yAxis->setRangeLower(1e-3);
        }
        m_plot->replot();
    }
}

void FittingWidget::on_btnExportReport_clicked()
{
    m_paramChart->updateParamsFromTable();
    QList<FitParameter> params = m_paramChart->getParameters();

    QString defaultDir = ModelParameter::instance()->getProjectPath();
    if(defaultDir.isEmpty()) defaultDir = ".";
    QString fileName = QFileDialog::getSaveFileName(this, "导出试井分析报告",
                                                    defaultDir + "/WellTestReport.doc",
                                                    "Word 文档 (*.doc);;HTML 文件 (*.html)");
    if(fileName.isEmpty()) return;

    ModelParameter* mp = ModelParameter::instance();

    QString html = "<html><head><style>";
    html += "body { font-family: 'Times New Roman', 'SimSun', serif; }";
    html += "h1 { text-align: center; font-size: 24px; font-weight: bold; margin-bottom: 20px; }";
    html += "h2 { font-size: 18px; font-weight: bold; background-color: #f2f2f2; padding: 5px; border-left: 5px solid #2d89ef; margin-top: 20px; }";
    html += "table { width: 100%; border-collapse: collapse; margin-bottom: 15px; font-size: 14px; }";
    html += "td, th { border: 1px solid #888; padding: 6px; text-align: center; }";
    html += "th { background-color: #e0e0e0; font-weight: bold; }";
    html += ".param-table td { text-align: left; padding-left: 10px; }";
    html += "</style></head><body>";

    html += "<h1>试井解释分析报告</h1>";
    html += "<p style='text-align:right;'>生成日期: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") + "</p>";

    html += "<h2>1. 基础信息</h2>";
    html += "<table class='param-table'>";
    html += "<tr><td width='30%'>项目路径</td><td>" + mp->getProjectPath() + "</td></tr>";
    html += "<tr><td>测试产量 (q)</td><td>" + QString::number(mp->getQ()) + " m³/d</td></tr>";
    html += "<tr><td>有效厚度 (h)</td><td>" + QString::number(mp->getH()) + " m</td></tr>";
    html += "<tr><td>孔隙度 (φ)</td><td>" + QString::number(mp->getPhi()) + "</td></tr>";
    html += "<tr><td>井筒半径 (rw)</td><td>" + QString::number(mp->getRw()) + " m</td></tr>";
    html += "</table>";

    html += "<h2>2. 流体高压物性 (PVT)</h2>";
    html += "<table class='param-table'>";
    html += "<tr><td width='30%'>原油粘度 (μ)</td><td>" + QString::number(mp->getMu()) + " mPa·s</td></tr>";
    html += "<tr><td>体积系数 (B)</td><td>" + QString::number(mp->getB()) + "</td></tr>";
    html += "<tr><td>综合压缩系数 (Ct)</td><td>" + QString::number(mp->getCt()) + " MPa⁻¹</td></tr>";
    html += "</table>";

    html += "<h2>3. 解释模型选择</h2>";
    html += "<p><strong>当前模型:</strong> " + ModelManager::getModelTypeName(m_currentModelType) + "</p>";

    html += "<h2>4. 拟合结果参数</h2>";
    html += "<table>";
    html += "<tr><th>参数名称</th><th>符号</th><th>拟合结果</th><th>单位</th></tr>";
    for(const auto& p : params) {
        QString dummy, symbol, uniSym, unit;
        FittingParameterChart::getParamDisplayInfo(p.name, dummy, symbol, uniSym, unit);
        if(unit == "无因次" || unit == "小数") unit = "-";

        html += "<tr>";
        html += "<td>" + p.displayName + "</td>";
        html += "<td>" + uniSym + "</td>";
        if(p.isFit)
            html += "<td><strong>" + QString::number(p.value, 'g', 6) + "</strong></td>";
        else
            html += "<td>" + QString::number(p.value, 'g', 6) + "</td>";
        html += "<td>" + unit + "</td>";
        html += "</tr>";
    }
    html += "</table>";

    html += "<h2>5. 拟合曲线图</h2>";
    QString imgBase64 = getPlotImageBase64();
    if(!imgBase64.isEmpty()) {
        html += "<div style='text-align:center;'><img src='data:image/png;base64," + imgBase64 + "' width='600' /></div>";
    } else {
        html += "<p>图像导出失败。</p>";
    }

    html += "</body></html>";

    QFile file(fileName);
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << html;
        file.close();
        QMessageBox::information(this, "导出成功", "报告已保存至:\n" + fileName);
    } else {
        QMessageBox::critical(this, "错误", "无法写入文件，请检查权限或文件是否被占用。");
    }
}

QString FittingWidget::getPlotImageBase64()
{
    if(!m_plot) return "";
    QPixmap pixmap = m_plot->toPixmap(800, 600);
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return QString::fromLatin1(byteArray.toBase64().data());
}

void FittingWidget::on_btnSaveFit_clicked()
{
    emit sigRequestSave();
}

QJsonObject FittingWidget::getJsonState() const
{
    const_cast<FittingWidget*>(this)->m_paramChart->updateParamsFromTable();
    QList<FitParameter> params = m_paramChart->getParameters();

    QJsonObject root;
    root["modelType"] = (int)m_currentModelType;
    root["modelName"] = ModelManager::getModelTypeName(m_currentModelType);
    root["fitWeightVal"] = ui->sliderWeight->value();

    QJsonObject plotRange;
    plotRange["xMin"] = m_plot->xAxis->range().lower;
    plotRange["xMax"] = m_plot->xAxis->range().upper;
    plotRange["yMin"] = m_plot->yAxis->range().lower;
    plotRange["yMax"] = m_plot->yAxis->range().upper;
    root["plotView"] = plotRange;

    QJsonArray paramsArray;
    for(const auto& p : params) {
        QJsonObject pObj;
        pObj["name"] = p.name;
        pObj["value"] = p.value;
        pObj["isFit"] = p.isFit;
        pObj["min"] = p.min;
        pObj["max"] = p.max;
        pObj["isVisible"] = p.isVisible;
        // [新增] 保存步长
        pObj["step"] = p.step;
        paramsArray.append(pObj);
    }
    root["parameters"] = paramsArray;

    QJsonArray timeArr, pressArr, derivArr;
    for(double v : m_obsTime) timeArr.append(v);
    for(double v : m_obsDeltaP) pressArr.append(v);
    for(double v : m_obsDerivative) derivArr.append(v);
    QJsonObject obsData;
    obsData["time"] = timeArr;
    obsData["pressure"] = pressArr;
    obsData["derivative"] = derivArr;
    root["observedData"] = obsData;

    return root;
}

void FittingWidget::loadFittingState(const QJsonObject& root)
{
    if (root.isEmpty()) return;

    if (root.contains("modelType")) {
        int type = root["modelType"].toInt();
        m_currentModelType = (ModelManager::ModelType)type;
        ui->btn_modelSelect->setText("当前: " + ModelManager::getModelTypeName(m_currentModelType));
    }

    m_paramChart->resetParams(m_currentModelType);

    if (root.contains("parameters")) {
        QJsonArray arr = root["parameters"].toArray();
        QList<FitParameter> currentParams = m_paramChart->getParameters();

        for(int i=0; i<arr.size(); ++i) {
            QJsonObject pObj = arr[i].toObject();
            QString name = pObj["name"].toString();

            for(auto& p : currentParams) {
                if(p.name == name) {
                    p.value = pObj["value"].toDouble();
                    p.isFit = pObj["isFit"].toBool();
                    p.min = pObj["min"].toDouble();
                    p.max = pObj["max"].toDouble();
                    if(pObj.contains("isVisible")) {
                        p.isVisible = pObj["isVisible"].toBool();
                    } else {
                        p.isVisible = true;
                    }
                    // [新增] 加载步长
                    if(pObj.contains("step")) {
                        p.step = pObj["step"].toDouble();
                    }
                    break;
                }
            }
        }
        m_paramChart->setParameters(currentParams);
    }

    if (root.contains("fitWeightVal")) {
        int val = root["fitWeightVal"].toInt();
        ui->sliderWeight->setValue(val);
    } else if (root.contains("fitWeight")) {
        double w = root["fitWeight"].toDouble();
        ui->sliderWeight->setValue((int)(w * 100));
    }

    if (root.contains("observedData")) {
        QJsonObject obs = root["observedData"].toObject();
        QJsonArray tArr = obs["time"].toArray();
        QJsonArray pArr = obs["pressure"].toArray();
        QJsonArray dArr = obs["derivative"].toArray();

        QVector<double> t, p, d;
        for(auto v : tArr) t.append(v.toDouble());
        for(auto v : pArr) p.append(v.toDouble());
        for(auto v : dArr) d.append(v.toDouble());

        setObservedData(t, p, d);
    }

    updateModelCurve();

    if (root.contains("plotView")) {
        QJsonObject range = root["plotView"].toObject();
        if (range.contains("xMin") && range.contains("xMax")) {
            double xMin = range["xMin"].toDouble();
            double xMax = range["xMax"].toDouble();
            double yMin = range["yMin"].toDouble();
            double yMax = range["yMax"].toDouble();
            if (xMax > xMin && yMax > yMin && xMin > 0 && yMin > 0) {
                m_plot->xAxis->setRange(xMin, xMax);
                m_plot->yAxis->setRange(yMin, yMax);
                m_plot->replot();
            }
        }
    }
}


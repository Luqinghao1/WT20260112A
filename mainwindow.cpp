/*
 * 文件名: mainwindow.cpp
 * 文件作用: 主窗口类实现文件
 * 功能描述:
 * 1. 负责应用程序的整体初始化和页面布局。
 * 2. 实现了左侧导航栏的逻辑控制和页面切换。
 * 3. 协调数据在不同模块（DataWidget, PlottingWidget, FittingPage）之间的流转。
 * 4. [修改] 更新了数据传输逻辑，支持将多文件数据映射表传递给下游模块。
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "navbtn.h"
#include "wt_projectwidget.h"
#include "wt_datawidget.h"
#include "modelmanager.h"
#include "modelparameter.h"
#include "wt_plottingwidget.h"
#include "fittingpage.h"
#include "settingswidget.h"

#include <QDateTime>
#include <QMessageBox>
#include <QDebug>
#include <QStandardItemModel>
#include <QTimer>
#include <QSpacerItem>
#include <QStackedWidget>
#include <cmath>
#include <QStatusBar>

// 辅助函数：统一的消息框样式定义，保持界面风格一致
static QString getGlobalMessageBoxStyle()
{
    return "QMessageBox { background-color: #ffffff; color: #000000; }"
           "QLabel { color: #000000; background-color: transparent; }"
           "QPushButton { "
           "   color: #000000; "
           "   background-color: #f0f0f0; "
           "   border: 1px solid #c0c0c0; "
           "   border-radius: 3px; "
           "   padding: 5px 15px; "
           "   min-width: 60px;"
           "}"
           "QPushButton:hover { background-color: #e0e0e0; }"
           "QPushButton:pressed { background-color: #d0d0d0; }";
}

// 构造函数
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_isProjectLoaded(false)
{
    ui->setupUi(this);
    this->setWindowTitle("PWT压力试井分析系统");
    this->setMinimumWidth(1024);

    // 执行核心初始化逻辑
    init();
}

// 析构函数
MainWindow::~MainWindow()
{
    delete ui;
}

// 核心初始化函数：创建导航、实例化子页面、建立信号连接
void MainWindow::init()
{
    // 初始化左侧导航栏，循环7次对应7个功能模块
    for(int i = 0 ; i<7;i++)
    {
        NavBtn* btn = new NavBtn(ui->widgetNav);
        btn->setMinimumWidth(110);
        btn->setIndex(i);
        btn->setStyleSheet("color: black;");

        // 配置导航按钮图标及对应页面索引
        // 0:项目, 1:数据, 2:图表, 3:模型, 4:拟合, 5:预测, 6:设置
        switch (i) {
        case 0:
            btn->setPicName("border-image: url(:/new/prefix1/Resource/X0.png);",tr("项目"));
            btn->setClickedStyle(); // 默认选中第一个
            ui->stackedWidget->setCurrentIndex(0);
            break;
        case 1:
            btn->setPicName("border-image: url(:/new/prefix1/Resource/X1.png);",tr("数据"));
            break;
        case 2:
            btn->setPicName("border-image: url(:/new/prefix1/Resource/X2.png);",tr("图表"));
            break;
        case 3:
            btn->setPicName("border-image: url(:/new/prefix1/Resource/X3.png);",tr("模型"));
            break;
        case 4:
            btn->setPicName("border-image: url(:/new/prefix1/Resource/X4.png);",tr("拟合"));
            break;
        case 5:
            btn->setPicName("border-image: url(:/new/prefix1/Resource/X5.png);",tr("预测"));
            break;
        case 6:
            btn->setPicName("border-image: url(:/new/prefix1/Resource/X6.png);",tr("设置"));
            break;
        default:
            break;
        }
        m_NavBtnMap.insert(btn->getName(),btn);
        ui->verticalLayoutNav->addWidget(btn);

        // 导航按钮点击事件处理
        connect(btn,&NavBtn::sigClicked,[=](QString name)
                {
                    int targetIndex = m_NavBtnMap.value(name)->getIndex();

                    // 权限检查：如果没有加载项目，禁止进入核心功能页
                    if ((targetIndex >= 1 && targetIndex <= 5) && !m_isProjectLoaded) {
                        QMessageBox msgBox;
                        msgBox.setWindowTitle("提示");
                        msgBox.setText("当前无活动项目，请先在“项目”界面新建或打开一个项目！");
                        msgBox.setIcon(QMessageBox::Warning);
                        msgBox.setStyleSheet(getMessageBoxStyle());
                        msgBox.exec();
                        return;
                    }

                    // 更新按钮样式（单选效果）
                    QMap<QString,NavBtn*>::Iterator item = m_NavBtnMap.begin();
                    while (item != m_NavBtnMap.end()) {
                        if(item.key() != name)
                        {
                            ((NavBtn*)(item.value()))->setNormalStyle();
                        }
                        item++;
                    }

                    // 切换堆叠窗口页面
                    ui->stackedWidget->setCurrentIndex(targetIndex);

                    // 特殊逻辑：切换到图表页时自动传输数据
                    if (name == tr("图表")) {
                        onTransferDataToPlotting();
                    }
                    else if (name == tr("拟合")) {
                        // 拟合页面可能需要的即时更新逻辑保留在此
                    }
                });
    }

    // 导航栏底部弹簧，将按钮顶上去
    QSpacerItem* verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    ui->verticalLayoutNav->addSpacerItem(verticalSpacer);

    // 时间显示定时器
    ui->labelTime->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss").replace(" ","\n"));
    connect(&m_timer,&QTimer::timeout,[=]
            {
                ui->labelTime->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").replace(" ","\n"));
                ui->labelTime->setStyleSheet("color: black;");
            });
    m_timer.start(1000);

    // --- 初始化各个子页面 ---

    // Page 0: 项目管理
    m_ProjectWidget = new WT_ProjectWidget(ui->pageMonitor);
    ui->verticalLayoutMonitor->addWidget(m_ProjectWidget);
    connect(m_ProjectWidget, &WT_ProjectWidget::projectOpened, this, &MainWindow::onProjectOpened);
    connect(m_ProjectWidget, &WT_ProjectWidget::projectClosed, this, &MainWindow::onProjectClosed);
    connect(m_ProjectWidget, &WT_ProjectWidget::fileLoaded, this, &MainWindow::onFileLoaded);

    // Page 1: 数据编辑器 (WT_DataWidget)
    m_DataEditorWidget = new WT_DataWidget(ui->pageHand);
    ui->verticalLayoutHandle->addWidget(m_DataEditorWidget);
    // 信号转发：文件加载和数据变化
    connect(m_DataEditorWidget, &WT_DataWidget::fileChanged, this, &MainWindow::onFileLoaded);
    connect(m_DataEditorWidget, &WT_DataWidget::dataChanged, this, &MainWindow::onDataEditorDataChanged);

    // Page 2: 图表分析 (WT_PlottingWidget)
    m_PlottingWidget = new WT_PlottingWidget(ui->pageData);
    ui->verticalLayout_2->addWidget(m_PlottingWidget);

    // Page 3: 模型参数管理
    m_ModelManager = new ModelManager(this);
    m_ModelManager->initializeModels(ui->pageParamter);
    connect(m_ModelManager, &ModelManager::calculationCompleted,
            this, &MainWindow::onModelCalculationCompleted);

    // Page 4: 拟合分析 (FittingPage)
    if (ui->pageFitting && ui->verticalLayoutFitting) {
        m_FittingPage = new FittingPage(ui->pageFitting);
        ui->verticalLayoutFitting->addWidget(m_FittingPage);
        m_FittingPage->setModelManager(m_ModelManager);
    } else {
        qWarning() << "MainWindow: pageFitting或verticalLayoutFitting为空！无法创建拟合界面";
        m_FittingPage = nullptr;
    }

    // Page 5: 产能预测 (预留位置)

    // Page 6: 系统设置
    m_SettingsWidget = new SettingsWidget(ui->pageAlarm);
    ui->verticalLayout_3->addWidget(m_SettingsWidget);
    connect(m_SettingsWidget, &SettingsWidget::settingsChanged,
            this, &MainWindow::onSystemSettingsChanged);

    // 调用各模块的特定初始化逻辑（主要是打印日志或预加载）
    initProjectForm();
    initDataEditorForm();
    initModelForm();
    initPlottingForm();
    initFittingForm();
    initPredictionForm();
}

void MainWindow::initProjectForm() { qDebug() << "初始化项目界面"; }
void MainWindow::initDataEditorForm() { qDebug() << "初始化数据编辑器界面"; }
void MainWindow::initModelForm() { if (m_ModelManager) qDebug() << "模型界面初始化完成"; }
void MainWindow::initPlottingForm() { qDebug() << "初始化绘图界面"; }
void MainWindow::initFittingForm() { if (m_FittingPage) qDebug() << "拟合界面初始化完成"; }
void MainWindow::initPredictionForm() { qDebug() << "初始化预测界面（预留）"; }

// 项目打开/新建成功响应
void MainWindow::onProjectOpened(bool isNew)
{
    qDebug() << "项目已加载，模式:" << (isNew ? "新建" : "打开");
    m_isProjectLoaded = true;

    // 更新模型管理器
    if (m_ModelManager) {
        m_ModelManager->updateAllModelsBasicParameters();
    }

    // 恢复数据编辑器状态
    if (m_DataEditorWidget) {
        if (!isNew) {
            m_DataEditorWidget->loadFromProjectData();
        }

        // [修改] 将所有已加载的数据模型传递给拟合页面
        if (m_FittingPage) {
            m_FittingPage->setProjectDataModels(m_DataEditorWidget->getAllDataModels());
        }
    }

    // 恢复拟合页面状态
    if (m_FittingPage) {
        m_FittingPage->updateBasicParameters();
        m_FittingPage->loadAllFittingStates();
    }

    // 恢复绘图页面状态
    if (m_PlottingWidget) {
        m_PlottingWidget->loadProjectData();
    }

    updateNavigationState();

    QString title = isNew ? "新建项目成功" : "加载项目成功";
    QString text = isNew ? "新项目已创建。\n基础参数已初始化，您可以开始进行数据录入或模型计算。"
                         : "项目文件加载完成。\n历史参数、数据及图表分析状态已完整恢复。";

    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setStyleSheet(getMessageBoxStyle());
    msgBox.exec();
}

// 项目关闭响应
void MainWindow::onProjectClosed()
{
    qDebug() << "项目已关闭，重置界面状态...";
    m_isProjectLoaded = false;
    m_hasValidData = false;

    // 清空数据编辑器
    if (m_DataEditorWidget) {
        m_DataEditorWidget->clearAllData();
    }

    // 清空绘图
    if (m_PlottingWidget) {
        m_PlottingWidget->clearAllPlots();
    }

    // 重置拟合页面
    if (m_FittingPage) {
        m_FittingPage->resetAnalysis();
    }

    // 清空模型缓存
    if (m_ModelManager) {
        m_ModelManager->clearCache();
    }

    ModelParameter::instance()->resetAllData();

    // 返回首页
    ui->stackedWidget->setCurrentIndex(0);
    updateNavigationState();

    QMessageBox msgBox;
    msgBox.setWindowTitle("提示");
    msgBox.setText("项目已保存并关闭。");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setStyleSheet(getMessageBoxStyle());
    msgBox.exec();
}

// 外部文件加载响应
void MainWindow::onFileLoaded(const QString& filePath, const QString& fileType)
{
    qDebug() << "文件加载：" << filePath;
    if (!m_isProjectLoaded) {
        QMessageBox msgBox;
        msgBox.setWindowTitle("警告");
        msgBox.setText("请先创建或打开项目！");
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStyleSheet(getMessageBoxStyle());
        msgBox.exec();
        return;
    }

    // 自动切换到数据界面
    ui->stackedWidget->setCurrentIndex(1);

    // 更新导航栏状态
    QMap<QString,NavBtn*>::Iterator item = m_NavBtnMap.begin();
    while (item != m_NavBtnMap.end()) {
        ((NavBtn*)(item.value()))->setNormalStyle();
        if(item.key() == tr("数据")) {
            ((NavBtn*)(item.value()))->setClickedStyle();
        }
        item++;
    }

    // 如果信号不是来自 DataWidget 本身（即是外部加载请求），则调用加载函数
    if (m_DataEditorWidget && sender() != m_DataEditorWidget) {
        m_DataEditorWidget->loadData(filePath, fileType);
    }

    // [修改] 每次文件加载后，更新下游模块的数据源列表
    if (m_FittingPage && m_DataEditorWidget) {
        m_FittingPage->setProjectDataModels(m_DataEditorWidget->getAllDataModels());
    }

    m_hasValidData = true;

    // 延时更新绘图数据，确保数据已完全就绪
    QTimer::singleShot(1000, this, &MainWindow::onDataReadyForPlotting);
}

void MainWindow::onPlotAnalysisCompleted(const QString &analysisType, const QMap<QString, double> &results)
{
    qDebug() << "绘图分析完成：" << analysisType;
}

void MainWindow::onDataReadyForPlotting()
{
    transferDataFromEditorToPlotting();
}

// 手动触发数据传输到绘图
void MainWindow::onTransferDataToPlotting()
{
    if (!hasDataLoaded()) return;
    transferDataFromEditorToPlotting();
}

// 数据发生变化时，如果当前在绘图页，实时更新
void MainWindow::onDataEditorDataChanged()
{
    // 如果当前页面是图表页 (Index 2)，则自动刷新
    if (ui->stackedWidget->currentIndex() == 2) {
        transferDataFromEditorToPlotting();
    }
    m_hasValidData = hasDataLoaded();
}

void MainWindow::onModelCalculationCompleted(const QString &analysisType, const QMap<QString, double> &results)
{
    qDebug() << "模型计算完成：" << analysisType;
}

// 将当前活动的观测数据推送到拟合页面 (Push模式)
void MainWindow::transferDataToFitting()
{
    if (!m_FittingPage || !m_DataEditorWidget) return;

    // 获取当前活动的数据模型
    QStandardItemModel* model = m_DataEditorWidget->getDataModel();
    if (!model || model->rowCount() == 0) {
        return;
    }

    // 简单的自动识别逻辑：第0列为时间，第1列为压力
    // 计算压差(Delta P)和导数
    QVector<double> tVec, pVec, dVec;
    double p_initial = 0.0;

    // 寻找初始压力（第一行非零压力）
    for(int r=0; r<model->rowCount(); ++r) {
        QModelIndex idx = model->index(r, 1);
        if(idx.isValid()) {
            double p = idx.data().toDouble();
            if (std::abs(p) > 1e-6) {
                p_initial = p;
                break;
            }
        }
    }

    // 提取时间与压差
    for(int r=0; r<model->rowCount(); ++r) {
        double t = model->index(r, 0).data().toDouble();
        double p_raw = model->index(r, 1).data().toDouble();
        if (t > 0) {
            tVec.append(t);
            pVec.append(std::abs(p_raw - p_initial)); // 简单的压差计算
        }
    }

    // 计算导数 (Bourdet导数简化版)
    dVec.resize(tVec.size());
    if (tVec.size() > 2) {
        dVec[0] = 0;
        dVec[tVec.size()-1] = 0;
        for(int i=1; i<tVec.size()-1; ++i) {
            double lnt1 = std::log(tVec[i-1]);
            double lnt2 = std::log(tVec[i]);
            double lnt3 = std::log(tVec[i+1]);
            if (std::abs(lnt2 - lnt1) < 1e-9 || std::abs(lnt3 - lnt2) < 1e-9) {
                dVec[i] = 0; continue;
            }
            double d1 = (pVec[i] - pVec[i-1]) / (lnt2 - lnt1);
            double d2 = (pVec[i+1] - pVec[i]) / (lnt3 - lnt2);
            double w1 = (lnt3 - lnt2) / (lnt3 - lnt1);
            double w2 = (lnt2 - lnt1) / (lnt3 - lnt1);
            dVec[i] = d1 * w1 + d2 * w2;
        }
    }

    // 将计算好的数据推送到当前拟合页签
    m_FittingPage->setObservedDataToCurrent(tVec, pVec, dVec);
}

void MainWindow::onFittingProgressChanged(int progress)
{
    if (this->statusBar()) {
        this->statusBar()->showMessage(QString("正在拟合... %1%").arg(progress));
        if(progress >= 100) this->statusBar()->showMessage("拟合完成", 5000);
    }
}

void MainWindow::onSystemSettingsChanged()
{
    qDebug() << "系统设置已变更";
}

void MainWindow::onPerformanceSettingsChanged() {}

// 获取当前活动的数据模型
QStandardItemModel* MainWindow::getDataEditorModel() const
{
    if (!m_DataEditorWidget) return nullptr;
    return m_DataEditorWidget->getDataModel();
}

QString MainWindow::getCurrentFileName() const
{
    if (!m_DataEditorWidget) return QString();
    return m_DataEditorWidget->getCurrentFileName();
}

bool MainWindow::hasDataLoaded()
{
    if (!m_DataEditorWidget) return false;
    return m_DataEditorWidget->hasData();
}

// [核心修改] 将所有数据模型传输给绘图模块
void MainWindow::transferDataFromEditorToPlotting()
{
    if (!m_DataEditorWidget || !m_PlottingWidget) return;

    // 获取所有已打开文件的数据模型映射表
    QMap<QString, QStandardItemModel*> models = m_DataEditorWidget->getAllDataModels();

    // 调用更新后的接口传递 Map
    m_PlottingWidget->setDataModels(models);

    if (!models.isEmpty()) {
        m_hasValidData = true;
    }
}

void MainWindow::updateNavigationState()
{
    QMap<QString,NavBtn*>::Iterator item = m_NavBtnMap.begin();
    while (item != m_NavBtnMap.end()) {
        ((NavBtn*)(item.value()))->setNormalStyle();
        if(item.key() == tr("项目")) {
            ((NavBtn*)(item.value()))->setClickedStyle();
        }
        item++;
    }
}

QString MainWindow::getMessageBoxStyle() const
{
    return getGlobalMessageBoxStyle();
}

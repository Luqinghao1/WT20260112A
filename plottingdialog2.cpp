/*
 * 文件名: plottingdialog2.cpp
 * 文件作用: 压力产量分析配置对话框实现文件
 * 功能描述:
 * 1. [修改] 初始化两个文件选择下拉框，并实现各自的数据列刷新逻辑。
 */

#include "plottingdialog2.h"
#include "ui_plottingdialog2.h"
#include <QColorDialog>
#include <QFileInfo>

int PlottingDialog2::s_counter = 1;

PlottingDialog2::PlottingDialog2(const QMap<QString, QStandardItemModel*>& models, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PlottingDialog2),
    m_dataMap(models),
    m_pressModel(nullptr),
    m_prodModel(nullptr),
    m_pressPointColor(Qt::red),
    m_pressLineColor(Qt::red),
    m_prodColor(Qt::blue)
{
    ui->setupUi(this);

    // 强制设置复选框选中颜色为蓝色
    this->setStyleSheet(
        "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #cccccc; border-radius: 3px; background-color: white; }"
        "QCheckBox::indicator:checked { background-color: #0078d7; border-color: #0078d7; }"
        "QCheckBox::indicator:hover { border-color: #0078d7; }"
        );

    ui->lineChartName->setText(QString("压力产量分析 %1").arg(s_counter++));
    ui->lineXLabel->setText("Time (h)");
    ui->linePLabel->setText("Pressure (MPa)");
    ui->lineQLabel->setText("Production (m3/d)");

    // 初始化两个文件下拉框
    ui->comboPressFile->clear();
    ui->comboProdFile->clear();

    if (!m_dataMap.isEmpty()) {
        for(auto it = m_dataMap.begin(); it != m_dataMap.end(); ++it) {
            QString filePath = it.key();
            QFileInfo fi(filePath);
            QString displayName = fi.fileName().isEmpty() ? filePath : fi.fileName();

            ui->comboPressFile->addItem(displayName, filePath);
            ui->comboProdFile->addItem(displayName, filePath);
        }
    } else {
        ui->comboPressFile->setEnabled(false);
        ui->comboProdFile->setEnabled(false);
    }

    setupStyleOptions();

    // 连接信号槽
    connect(ui->comboPressFile, SIGNAL(currentIndexChanged(int)), this, SLOT(onPressFileChanged(int)));
    connect(ui->comboProdFile, SIGNAL(currentIndexChanged(int)), this, SLOT(onProdFileChanged(int)));

    connect(ui->comboPressY, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PlottingDialog2::onPressYColChanged);
    connect(ui->comboProdY, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PlottingDialog2::onProdYColChanged);

    connect(ui->btnPressPointColor, &QPushButton::clicked, this, &PlottingDialog2::selectPressPointColor);
    connect(ui->btnPressLineColor, &QPushButton::clicked, this, &PlottingDialog2::selectPressLineColor);
    connect(ui->btnProdColor, &QPushButton::clicked, this, &PlottingDialog2::selectProdColor);

    // 触发初始加载
    if (ui->comboPressFile->count() > 0) {
        ui->comboPressFile->setCurrentIndex(0);
        onPressFileChanged(0);
    }
    if (ui->comboProdFile->count() > 0) {
        ui->comboProdFile->setCurrentIndex(0);
        onProdFileChanged(0);
    }
}

PlottingDialog2::~PlottingDialog2()
{
    delete ui;
}

// [修改] 压力文件切换
void PlottingDialog2::onPressFileChanged(int index)
{
    Q_UNUSED(index);
    QString key = ui->comboPressFile->currentData().toString();
    if (m_dataMap.contains(key)) m_pressModel = m_dataMap.value(key);
    else m_pressModel = nullptr;
    populatePressComboBoxes();
    if(ui->comboPressY->count()>0) onPressYColChanged(ui->comboPressY->currentIndex());
}

// [修改] 产量文件切换
void PlottingDialog2::onProdFileChanged(int index)
{
    Q_UNUSED(index);
    QString key = ui->comboProdFile->currentData().toString();
    if (m_dataMap.contains(key)) m_prodModel = m_dataMap.value(key);
    else m_prodModel = nullptr;
    populateProdComboBoxes();
    if(ui->comboProdY->count()>0) onProdYColChanged(ui->comboProdY->currentIndex());
}

// 填充压力列下拉框
void PlottingDialog2::populatePressComboBoxes()
{
    ui->comboPressX->clear();
    ui->comboPressY->clear();
    if (!m_pressModel) return;
    QStringList headers;
    for(int i=0; i<m_pressModel->columnCount(); ++i) {
        QStandardItem* item = m_pressModel->horizontalHeaderItem(i);
        headers << (item ? item->text() : QString("列 %1").arg(i+1));
    }
    ui->comboPressX->addItems(headers);
    ui->comboPressY->addItems(headers);
}

// 填充产量列下拉框
void PlottingDialog2::populateProdComboBoxes()
{
    ui->comboProdX->clear();
    ui->comboProdY->clear();
    if (!m_prodModel) return;
    QStringList headers;
    for(int i=0; i<m_prodModel->columnCount(); ++i) {
        QStandardItem* item = m_prodModel->horizontalHeaderItem(i);
        headers << (item ? item->text() : QString("列 %1").arg(i+1));
    }
    ui->comboProdX->addItems(headers);
    ui->comboProdY->addItems(headers);
}

void PlottingDialog2::setupStyleOptions()
{
    auto addShapes = [this](QComboBox* box) {
        box->addItem("实心圆 (Disc)", (int)QCPScatterStyle::ssDisc);
        box->addItem("空心圆 (Circle)", (int)QCPScatterStyle::ssCircle);
        box->addItem("正方形 (Square)", (int)QCPScatterStyle::ssSquare);
        box->addItem("三角形 (Triangle)", (int)QCPScatterStyle::ssTriangle);
        box->addItem("无 (None)", (int)QCPScatterStyle::ssNone);
    };
    addShapes(ui->comboPressShape);

    auto addLines = [this](QComboBox* box) {
        box->addItem("实线 (Solid)", (int)Qt::SolidLine);
        box->addItem("虚线 (Dash)", (int)Qt::DashLine);
        box->addItem("点线 (Dot)", (int)Qt::DotLine);
        box->addItem("无 (None)", (int)Qt::NoPen);
    };
    addLines(ui->comboPressLine);

    ui->comboProdType->addItem("阶梯图 (Step Chart)", 0);
    ui->comboProdType->addItem("散点图 (Scatter)", 1);
    ui->comboProdType->addItem("折线图 (Line)", 2);

    updateColorButton(ui->btnPressPointColor, m_pressPointColor);
    updateColorButton(ui->btnPressLineColor, m_pressLineColor);
    updateColorButton(ui->btnProdColor, m_prodColor);

    ui->comboPressLine->setCurrentIndex(3);
}

void PlottingDialog2::updateColorButton(QPushButton* btn, const QColor& color)
{
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; border-radius: 3px;").arg(color.name()));
}

void PlottingDialog2::selectPressPointColor() {
    QColor c = QColorDialog::getColor(m_pressPointColor, this);
    if(c.isValid()) { m_pressPointColor = c; updateColorButton(ui->btnPressPointColor, c); }
}
void PlottingDialog2::selectPressLineColor() {
    QColor c = QColorDialog::getColor(m_pressLineColor, this);
    if(c.isValid()) { m_pressLineColor = c; updateColorButton(ui->btnPressLineColor, c); }
}
void PlottingDialog2::selectProdColor() {
    QColor c = QColorDialog::getColor(m_prodColor, this);
    if(c.isValid()) { m_prodColor = c; updateColorButton(ui->btnProdColor, c); }
}

void PlottingDialog2::onPressYColChanged(int index) {
    if(index>=0) ui->linePressLegend->setText(ui->comboPressY->itemText(index));
}
void PlottingDialog2::onProdYColChanged(int index) {
    if(index>=0) ui->lineProdLegend->setText(ui->comboProdY->itemText(index));
}

// Getters
QString PlottingDialog2::getChartName() const { return ui->lineChartName->text(); }
QString PlottingDialog2::getPressFileName() const { return ui->comboPressFile->currentData().toString(); }
QString PlottingDialog2::getProdFileName() const { return ui->comboProdFile->currentData().toString(); }

QString PlottingDialog2::getPressLegend() const { return ui->linePressLegend->text(); }
int PlottingDialog2::getPressXCol() const { return ui->comboPressX->currentIndex(); }
int PlottingDialog2::getPressYCol() const { return ui->comboPressY->currentIndex(); }
QCPScatterStyle::ScatterShape PlottingDialog2::getPressShape() const { return (QCPScatterStyle::ScatterShape)ui->comboPressShape->currentData().toInt(); }
QColor PlottingDialog2::getPressPointColor() const { return m_pressPointColor; }
Qt::PenStyle PlottingDialog2::getPressLineStyle() const { return (Qt::PenStyle)ui->comboPressLine->currentData().toInt(); }
QColor PlottingDialog2::getPressLineColor() const { return m_pressLineColor; }

QString PlottingDialog2::getProdLegend() const { return ui->lineProdLegend->text(); }
int PlottingDialog2::getProdXCol() const { return ui->comboProdX->currentIndex(); }
int PlottingDialog2::getProdYCol() const { return ui->comboProdY->currentIndex(); }
int PlottingDialog2::getProdGraphType() const { return ui->comboProdType->currentData().toInt(); }
QColor PlottingDialog2::getProdColor() const { return m_prodColor; }

QString PlottingDialog2::getXLabel() const { return ui->lineXLabel->text(); }
QString PlottingDialog2::getPLabel() const { return ui->linePLabel->text(); }
QString PlottingDialog2::getQLabel() const { return ui->lineQLabel->text(); }

bool PlottingDialog2::isNewWindow() const { return ui->checkNewWindow->isChecked(); }

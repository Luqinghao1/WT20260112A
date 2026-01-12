/*
 * 文件名: plottingdialog3.cpp
 * 文件作用: 试井分析曲线配置对话框实现
 * 功能描述:
 * 1. 实现了对话框的初始化，支持多文件选择，设置默认值为标准的双对数曲线配置。
 * 2. 实现了试井类型（降落/恢复）的逻辑切换。
 * 3. 强制设置复选框选中样式为蓝色。
 */

#include "plottingdialog3.h"
#include "ui_plottingdialog3.h"
#include <QColorDialog>
#include <QFileInfo>

// 初始化静态计数器
int PlottingDialog3::s_counter = 1;

// [修改] 构造函数实现
PlottingDialog3::PlottingDialog3(const QMap<QString, QStandardItemModel*>& models, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PlottingDialog3),
    m_dataMap(models),
    m_currentModel(nullptr),
    m_pressPointColor(Qt::red),    // 默认压差点颜色：红
    m_pressLineColor(Qt::red),     // 默认压差线颜色：红
    m_derivPointColor(Qt::blue),   // 默认导数点颜色：蓝
    m_derivLineColor(Qt::blue)     // 默认导数线颜色：蓝
{
    ui->setupUi(this);

    // 强制设置复选框选中颜色为蓝色
    this->setStyleSheet(
        "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #cccccc; border-radius: 3px; background-color: white; }"
        "QCheckBox::indicator:checked { background-color: #0078d7; border-color: #0078d7; }"
        "QCheckBox::indicator:hover { border-color: #0078d7; }"
        );

    // 设置默认值
    ui->lineName->setText(QString("试井分析 %1").arg(s_counter++));

    // 默认图例名称
    ui->linePressLegend->setText("Delta P");
    ui->lineDerivLegend->setText("Derivative");

    // 设置坐标轴默认标签
    ui->lineXLabel->setText("dt (h)");
    ui->lineYLabel->setText("Delta P / Derivative (MPa)");

    // 初始化文件下拉框
    ui->comboFileSelect->clear();
    if (m_dataMap.isEmpty()) {
        ui->comboFileSelect->setEnabled(false);
    } else {
        for(auto it = m_dataMap.begin(); it != m_dataMap.end(); ++it) {
            QString filePath = it.key();
            QFileInfo fi(filePath);
            ui->comboFileSelect->addItem(fi.fileName().isEmpty() ? filePath : fi.fileName(), filePath);
        }
    }

    // 初始化样式选项
    setupStyleOptions();

    // 默认选中“压力降落”试井
    ui->radioDrawdown->setChecked(true);

    // 连接信号与槽
    connect(ui->comboFileSelect, SIGNAL(currentIndexChanged(int)), this, SLOT(onFileChanged(int)));

    connect(ui->checkSmooth, &QCheckBox::toggled, this, &PlottingDialog3::onSmoothToggled);
    onSmoothToggled(ui->checkSmooth->isChecked());

    connect(ui->radioDrawdown, &QRadioButton::toggled, this, &PlottingDialog3::onTestTypeChanged);
    connect(ui->radioBuildup, &QRadioButton::toggled, this, &PlottingDialog3::onTestTypeChanged);
    onTestTypeChanged();

    connect(ui->btnPressPointColor, &QPushButton::clicked, this, &PlottingDialog3::selectPressPointColor);
    connect(ui->btnPressLineColor, &QPushButton::clicked, this, &PlottingDialog3::selectPressLineColor);
    connect(ui->btnDerivPointColor, &QPushButton::clicked, this, &PlottingDialog3::selectDerivPointColor);
    connect(ui->btnDerivLineColor, &QPushButton::clicked, this, &PlottingDialog3::selectDerivLineColor);

    // 初始触发
    if (ui->comboFileSelect->count() > 0) {
        ui->comboFileSelect->setCurrentIndex(0);
        onFileChanged(0);
    }
}

PlottingDialog3::~PlottingDialog3()
{
    delete ui;
}

// [新增] 文件切换槽函数
void PlottingDialog3::onFileChanged(int index)
{
    Q_UNUSED(index);
    QString key = ui->comboFileSelect->currentData().toString();

    if (m_dataMap.contains(key)) {
        m_currentModel = m_dataMap.value(key);
    } else {
        m_currentModel = nullptr;
    }

    // 重新填充列下拉框
    populateComboBoxes();
}

// 填充列选择下拉框
void PlottingDialog3::populateComboBoxes()
{
    ui->comboTime->clear();
    ui->comboPress->clear();

    if (!m_currentModel) return;

    QStringList headers;
    for(int i=0; i<m_currentModel->columnCount(); ++i) {
        QStandardItem* item = m_currentModel->horizontalHeaderItem(i);
        headers << (item ? item->text() : QString("列 %1").arg(i+1));
    }
    ui->comboTime->addItems(headers);
    ui->comboPress->addItems(headers);
}

// 设置样式选项（点形、线型）
void PlottingDialog3::setupStyleOptions()
{
    // Lambda函数：添加点形状选项
    auto addShapes = [](QComboBox* box) {
        box->addItem("实心圆 (Disc)", (int)QCPScatterStyle::ssDisc);
        box->addItem("空心圆 (Circle)", (int)QCPScatterStyle::ssCircle);
        box->addItem("三角形 (Triangle)", (int)QCPScatterStyle::ssTriangle);
        box->addItem("菱形 (Diamond)", (int)QCPScatterStyle::ssDiamond);
        box->addItem("无 (None)", (int)QCPScatterStyle::ssNone);
    };
    // Lambda函数：添加线型选项
    auto addLines = [](QComboBox* box) {
        box->addItem("实线 (Solid)", (int)Qt::SolidLine);
        box->addItem("虚线 (Dash)", (int)Qt::DashLine);
        box->addItem("无 (None)", (int)Qt::NoPen);
    };

    addShapes(ui->comboPressShape);
    addLines(ui->comboPressLine);
    addShapes(ui->comboDerivShape);
    addLines(ui->comboDerivLine);

    ui->comboPressLine->setCurrentIndex(2); // 压力默认无连线 (NoPen)
    ui->comboDerivShape->setCurrentIndex(2); // 导数默认三角形 (Triangle)
    ui->comboDerivLine->setCurrentIndex(2); // 导数默认无连线 (NoPen)

    updateColorButton(ui->btnPressPointColor, m_pressPointColor);
    updateColorButton(ui->btnPressLineColor, m_pressLineColor);
    updateColorButton(ui->btnDerivPointColor, m_derivPointColor);
    updateColorButton(ui->btnDerivLineColor, m_derivLineColor);
}

// 平滑选项切换槽函数
void PlottingDialog3::onSmoothToggled(bool checked)
{
    ui->spinSmooth->setEnabled(checked);
}

// 试井类型切换槽函数
void PlottingDialog3::onTestTypeChanged()
{
    bool isDrawdown = ui->radioDrawdown->isChecked();
    ui->spinPi->setEnabled(isDrawdown);
    ui->labelPi->setEnabled(isDrawdown);
}

void PlottingDialog3::updateColorButton(QPushButton* btn, const QColor& color) {
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; border-radius: 3px;").arg(color.name()));
}

void PlottingDialog3::selectPressPointColor() {
    QColor c = QColorDialog::getColor(m_pressPointColor, this);
    if(c.isValid()) { m_pressPointColor = c; updateColorButton(ui->btnPressPointColor, c); }
}
void PlottingDialog3::selectPressLineColor() {
    QColor c = QColorDialog::getColor(m_pressLineColor, this);
    if(c.isValid()) { m_pressLineColor = c; updateColorButton(ui->btnPressLineColor, c); }
}
void PlottingDialog3::selectDerivPointColor() {
    QColor c = QColorDialog::getColor(m_derivPointColor, this);
    if(c.isValid()) { m_derivPointColor = c; updateColorButton(ui->btnDerivPointColor, c); }
}
void PlottingDialog3::selectDerivLineColor() {
    QColor c = QColorDialog::getColor(m_derivLineColor, this);
    if(c.isValid()) { m_derivLineColor = c; updateColorButton(ui->btnDerivLineColor, c); }
}

// --- Getters: 获取界面控件的值 ---
QString PlottingDialog3::getCurveName() const { return ui->lineName->text(); }
QString PlottingDialog3::getSelectedFileName() const { return ui->comboFileSelect->currentData().toString(); }

QString PlottingDialog3::getPressLegend() const { return ui->linePressLegend->text(); }
QString PlottingDialog3::getDerivLegend() const { return ui->lineDerivLegend->text(); }
int PlottingDialog3::getTimeColumn() const { return ui->comboTime->currentIndex(); }
int PlottingDialog3::getPressureColumn() const { return ui->comboPress->currentIndex(); }

PlottingDialog3::TestType PlottingDialog3::getTestType() const {
    if (ui->radioDrawdown->isChecked()) return Drawdown;
    return Buildup;
}

double PlottingDialog3::getInitialPressure() const {
    return ui->spinPi->value();
}

double PlottingDialog3::getLSpacing() const { return ui->spinL->value(); }
bool PlottingDialog3::isSmoothEnabled() const { return ui->checkSmooth->isChecked(); }
int PlottingDialog3::getSmoothFactor() const { return ui->spinSmooth->value(); }
QString PlottingDialog3::getXLabel() const { return ui->lineXLabel->text(); }
QString PlottingDialog3::getYLabel() const { return ui->lineYLabel->text(); }

QCPScatterStyle::ScatterShape PlottingDialog3::getPressShape() const { return (QCPScatterStyle::ScatterShape)ui->comboPressShape->currentData().toInt(); }
QColor PlottingDialog3::getPressPointColor() const { return m_pressPointColor; }
Qt::PenStyle PlottingDialog3::getPressLineStyle() const { return (Qt::PenStyle)ui->comboPressLine->currentData().toInt(); }
QColor PlottingDialog3::getPressLineColor() const { return m_pressLineColor; }

QCPScatterStyle::ScatterShape PlottingDialog3::getDerivShape() const { return (QCPScatterStyle::ScatterShape)ui->comboDerivShape->currentData().toInt(); }
QColor PlottingDialog3::getDerivPointColor() const { return m_derivPointColor; }
Qt::PenStyle PlottingDialog3::getDerivLineStyle() const { return (Qt::PenStyle)ui->comboDerivLine->currentData().toInt(); }
QColor PlottingDialog3::getDerivLineColor() const { return m_derivLineColor; }

bool PlottingDialog3::isNewWindow() const { return ui->checkNewWindow->isChecked(); }

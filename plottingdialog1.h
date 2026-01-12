/*
 * 文件名: plottingdialog1.h
 * 文件作用: 新建单一曲线配置对话框头文件
 * 功能描述:
 * 1. 设置曲线基础信息（名称、图例）。
 * 2. [修改] 支持选择数据源文件，适应多文件模式。
 * 3. 选择X/Y数据列及设置坐标轴标签。
 * 4. 设置详细的点/线样式（包含颜色调色盘）。
 */

#ifndef PLOTTINGDIALOG1_H
#define PLOTTINGDIALOG1_H

#include <QDialog>
#include <QStandardItemModel>
#include <QColor>
#include <QMap>
#include "qcustomplot.h"

namespace Ui {
class PlottingDialog1;
}

class PlottingDialog1 : public QDialog
{
    Q_OBJECT

public:
    // [修改] 构造函数接收所有数据模型的映射表
    explicit PlottingDialog1(const QMap<QString, QStandardItemModel*>& models, QWidget *parent = nullptr);
    ~PlottingDialog1();

    // --- 获取用户配置 ---
    QString getCurveName() const;
    QString getLegendName() const;

    // [新增] 获取用户选择的数据源文件名
    QString getSelectedFileName() const;

    int getXColumn() const;
    int getYColumn() const;
    QString getXLabel() const;
    QString getYLabel() const;

    // 样式获取
    QCPScatterStyle::ScatterShape getPointShape() const;
    QColor getPointColor() const;
    Qt::PenStyle getLineStyle() const;
    QColor getLineColor() const;

    bool isNewWindow() const;

private slots:
    // [新增] 数据源文件改变时触发
    void onFileChanged(int index);

    // 列改变时自动更新图例和标签
    void onXColumnChanged(int index);
    void onYColumnChanged(int index);

    // 弹出调色盘
    void selectPointColor();
    void selectLineColor();

private:
    Ui::PlottingDialog1 *ui;

    // [修改] 存储所有可用模型
    QMap<QString, QStandardItemModel*> m_dataMap;
    // [新增] 当前选中的模型指针
    QStandardItemModel* m_currentModel;

    static int s_curveCounter; // 静态计数器，用于生成默认名称

    QColor m_pointColor; // 当前选择的点颜色
    QColor m_lineColor;  // 当前选择的线颜色

    void populateComboBoxes(); // 根据当前模型初始化下拉框
    void setupStyleOptions();  // 初始化样式选项
    void updateColorButton(QPushButton* btn, const QColor& color); // 更新按钮背景色
};

#endif // PLOTTINGDIALOG1_H

/*
 * 文件名: plottingdialog2.h
 * 文件作用: 压力产量分析配置对话框头文件
 * 功能描述:
 * 1. [修改] 支持分别选择压力数据文件和产量数据文件。
 * 2. 维护两个独立的数据模型指针。
 */

#ifndef PLOTTINGDIALOG2_H
#define PLOTTINGDIALOG2_H

#include <QDialog>
#include <QStandardItemModel>
#include <QColor>
#include <QMap>
#include "qcustomplot.h"

namespace Ui {
class PlottingDialog2;
}

class PlottingDialog2 : public QDialog
{
    Q_OBJECT

public:
    explicit PlottingDialog2(const QMap<QString, QStandardItemModel*>& models, QWidget *parent = nullptr);
    ~PlottingDialog2();

    // --- 全局设置 ---
    QString getChartName() const;

    // [新增] 分别获取两个文件名
    QString getPressFileName() const;
    QString getProdFileName() const;

    // --- 压力数据 (上方图表) ---
    QString getPressLegend() const;
    int getPressXCol() const;
    int getPressYCol() const;
    QCPScatterStyle::ScatterShape getPressShape() const;
    QColor getPressPointColor() const;
    Qt::PenStyle getPressLineStyle() const;
    QColor getPressLineColor() const;

    // --- 产量数据 (下方图表) ---
    QString getProdLegend() const;
    int getProdXCol() const;
    int getProdYCol() const;
    int getProdGraphType() const; // 0=阶梯, 1=散点, 2=折线
    QColor getProdColor() const;

    // --- 坐标轴标签 ---
    QString getXLabel() const; // 时间轴
    QString getPLabel() const; // 压力轴
    QString getQLabel() const; // 产量轴

    // --- 显示设置 ---
    bool isNewWindow() const;

private slots:
    // [新增] 两个文件选择的槽函数
    void onPressFileChanged(int index);
    void onProdFileChanged(int index);

    // 列名变化时自动更新图例名称
    void onPressYColChanged(int index);
    void onProdYColChanged(int index);

    // 颜色选择按钮槽函数
    void selectPressPointColor();
    void selectPressLineColor();
    void selectProdColor();

private:
    Ui::PlottingDialog2 *ui;

    QMap<QString, QStandardItemModel*> m_dataMap;

    // [新增] 分别维护两个模型指针
    QStandardItemModel* m_pressModel;
    QStandardItemModel* m_prodModel;

    static int s_counter;

    QColor m_pressPointColor;
    QColor m_pressLineColor;
    QColor m_prodColor;

    // [修改] 分别填充下拉框
    void populatePressComboBoxes();
    void populateProdComboBoxes();

    void setupStyleOptions();
    void updateColorButton(QPushButton* btn, const QColor& color);
};

#endif // PLOTTINGDIALOG2_H

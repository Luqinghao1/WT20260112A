/*
 * 文件名: plottingdialog3.h
 * 文件作用: 试井分析曲线（双对数曲线）配置对话框头文件
 * 功能描述:
 * 1. 声明了用于配置曲线样式的对话框类。
 * 2. [修改] 支持选择数据源文件，适应多文件模式。
 * 3. 提供获取用户设置（如试井类型、地层压力、曲线名称、图例、L-Spacing等）的接口。
 * 4. 管理界面交互逻辑，如颜色选择、试井类型切换带来的输入框状态变化等。
 */

#ifndef PLOTTINGDIALOG3_H
#define PLOTTINGDIALOG3_H

#include <QDialog>
#include <QStandardItemModel>
#include <QColor>
#include <QMap>
#include "qcustomplot.h"

namespace Ui {
class PlottingDialog3;
}

class PlottingDialog3 : public QDialog
{
    Q_OBJECT

public:
    // 定义试井类型枚举
    enum TestType {
        Drawdown,   // 压力降落试井
        Buildup     // 压力恢复试井
    };

    // [修改] 构造函数：接收模型映射表
    explicit PlottingDialog3(const QMap<QString, QStandardItemModel*>& models, QWidget *parent = nullptr);
    ~PlottingDialog3();

    // --- 基础信息获取接口 ---
    QString getCurveName() const;       // 获取曲线名称
    QString getPressLegend() const;     // 获取压差曲线图例
    QString getDerivLegend() const;     // 获取导数曲线图例

    // [新增] 获取用户选择的文件名
    QString getSelectedFileName() const;

    // --- 数据源设置接口 ---
    int getTimeColumn() const;          // 获取时间列索引
    int getPressureColumn() const;      // 获取压力列索引

    // --- 试井分析参数接口 ---
    TestType getTestType() const;       // 获取试井类型（降落/恢复）
    double getInitialPressure() const;  // 获取地层初始压力 (仅降落试井有效)

    // --- 计算参数接口 ---
    double getLSpacing() const;         // 获取导数计算步长 L-Spacing
    bool isSmoothEnabled() const;       // 获取是否启用平滑处理
    int getSmoothFactor() const;        // 获取平滑因子

    // --- 坐标轴标签接口 ---
    QString getXLabel() const;          // 获取X轴标签文本
    QString getYLabel() const;          // 获取Y轴标签文本

    // --- 压差曲线样式接口 ---
    QCPScatterStyle::ScatterShape getPressShape() const; // 获取点形状
    QColor getPressPointColor() const;                   // 获取点颜色
    Qt::PenStyle getPressLineStyle() const;              // 获取线型
    QColor getPressLineColor() const;                    // 获取线颜色

    // --- 导数曲线样式接口 ---
    QCPScatterStyle::ScatterShape getDerivShape() const; // 获取点形状
    QColor getDerivPointColor() const;                   // 获取点颜色
    Qt::PenStyle getDerivLineStyle() const;              // 获取线型
    QColor getDerivLineColor() const;                    // 获取线颜色

    // 是否在新窗口中打开图表
    bool isNewWindow() const;

private slots:
    // [新增] 文件切换
    void onFileChanged(int index);

    // 槽函数：响应“启用平滑”复选框的状态变化
    void onSmoothToggled(bool checked);

    // 槽函数：响应试井类型变化
    void onTestTypeChanged();

    // 槽函数：响应各颜色选择按钮的点击事件
    void selectPressPointColor();
    void selectPressLineColor();
    void selectDerivPointColor();
    void selectDerivLineColor();

private:
    Ui::PlottingDialog3 *ui;

    // [修改] 数据存储
    QMap<QString, QStandardItemModel*> m_dataMap;
    QStandardItemModel* m_currentModel;

    static int s_counter;            // 静态计数器

    // 内部成员变量：存储当前选择的颜色
    QColor m_pressPointColor;
    QColor m_pressLineColor;
    QColor m_derivPointColor;
    QColor m_derivLineColor;

    void populateComboBoxes();
    void setupStyleOptions();
    void updateColorButton(QPushButton* btn, const QColor& color);
};

#endif // PLOTTINGDIALOG3_H

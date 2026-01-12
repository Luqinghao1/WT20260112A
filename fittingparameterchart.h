/*
 * 文件名: fittingparameterchart.h
 * 文件作用: 拟合参数图表管理类头文件
 * 功能描述:
 * 1. 定义拟合参数结构体 FitParameter。
 * 2. 管理主界面的参数表格显示与交互。
 * 3. 支持鼠标滚轮调节参数（在主界面表格中）。
 */

#ifndef FITTINGPARAMETERCHART_H
#define FITTINGPARAMETERCHART_H

#include <QObject>
#include <QTableWidget>
#include <QList>
#include <QMap>
#include <QEvent>
#include "modelmanager.h"

// 定义拟合参数结构体
struct FitParameter {
    QString name;           // 参数内部英文名
    QString displayName;    // 参数显示中文名
    double value = 0.0;     // 当前参数值
    bool isFit = false;     // 是否参与拟合
    double min = 0.0;       // 参数下限
    double max = 100.0;     // 参数上限
    bool isVisible = true;  // 是否显示
    double step = 0.1;      // [修改] 默认步长初始化为 0.1，防止野值
};

// 拟合参数图表管理类
class FittingParameterChart : public QObject
{
    Q_OBJECT
public:
    explicit FittingParameterChart(QTableWidget* parentTable, QObject *parent = nullptr);

    void setModelManager(ModelManager* m);
    void resetParams(ModelManager::ModelType type);

    QList<FitParameter> getParameters() const;
    void setParameters(const QList<FitParameter>& params);

    void switchModel(ModelManager::ModelType newType);
    void updateParamsFromTable();

    // 获取表格中参数的原始文本 (用于敏感性分析)
    QMap<QString, QString> getRawParamTexts() const;

    void refreshParamTable();

    static void getParamDisplayInfo(const QString& name, QString& chName, QString& symbol, QString& uniSymbol, QString& unit);

signals:
    // 当通过鼠标滚轮修改参数时发出此信号
    void parameterChangedByWheel();

protected:
    // 事件过滤器，用于拦截处理鼠标滚轮事件
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTableWidget* m_table;
    ModelManager* m_modelManager;
    QList<FitParameter> m_params;

    void addRowToTable(const FitParameter& p, int& serialNo, bool highlight);
};

#endif // FITTINGPARAMETERCHART_H

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialtree.h"

#include "materialutils.h"
#include <QColor>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPointer>
#include <QSlider>
#include <algorithm>
#include <cmath>
#include <functional>

namespace stageviz {

class MaterialTreePrivate {
public:
    void ensureInitialized();
    void init();
    void update();
    QColor color(const QString& prefix) const;

public:
    class FloatControl : public QWidget {
    public:
        FloatControl(double minimum, double maximum, double step, QWidget* parent = nullptr)
            : QWidget(parent)
            , m_minimum(minimum)
            , m_maximum(maximum)
        {
            auto* layout = new QHBoxLayout(this);
            layout->setContentsMargins(0, 0, 16, 0);
            layout->setSpacing(12);

            m_slider = new QSlider(Qt::Horizontal, this);
            m_slider->setRange(0, 1000);

            m_spin = new QDoubleSpinBox(this);
            m_spin->setRange(minimum, maximum);
            m_spin->setSingleStep(step);
            m_spin->setDecimals(3);
            m_spin->setMinimumWidth(80);

            layout->addWidget(m_slider, 1);
            layout->addWidget(m_spin);
            // connect
            connect(m_slider, &QSlider::valueChanged, this, [this](int sliderValue) {
                if (m_block)
                    return;

                m_block = true;
                const double t = sliderValue / 1000.0;
                m_spin->setValue(m_minimum + t * (m_maximum - m_minimum));
                m_block = false;

                if (previewChanged)
                    previewChanged(value());
            });
            connect(m_slider, &QSlider::sliderReleased, this, [this]() {
                if (!m_block && changed)
                    changed(value());
            });
            connect(m_spin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
                    [this](double spinValue) {
                        if (m_block)
                            return;

                        m_block = true;
                        const double t = (spinValue - m_minimum) / std::max(1e-9, m_maximum - m_minimum);
                        m_slider->setValue(static_cast<int>(std::round(std::clamp(t, 0.0, 1.0) * 1000.0)));
                        m_block = false;

                        if (previewChanged)
                            previewChanged(value());
                    });
            connect(m_spin, &QDoubleSpinBox::editingFinished, this, [this]() {
                if (!m_block && changed)
                    changed(value());
            });
        }

        double value() const { return m_spin->value(); }

        void setValue(double value, bool mixed)
        {
            m_block = true;
            value = std::clamp(value, m_minimum, m_maximum);

            m_spin->setValue(value);
            const double t = (value - m_minimum) / std::max(1e-9, m_maximum - m_minimum);
            m_slider->setValue(static_cast<int>(std::round(std::clamp(t, 0.0, 1.0) * 1000.0)));
            m_spin->setPrefix(mixed ? QStringLiteral("≈ ") : QString());

            m_block = false;
        }

        std::function<void(double)> previewChanged;
        std::function<void(double)> changed;

    private:
        QSlider* m_slider = nullptr;
        QDoubleSpinBox* m_spin = nullptr;
        double m_minimum = 0.0;
        double m_maximum = 1.0;
        bool m_block = false;
    };
    struct FloatAggregate {
        double value = 0.0;
        bool mixed = false;
    };
    template<typename Getter> FloatAggregate aggregateFloat(const QList<MaterialEntry>& entries, Getter getter)
    {
        FloatAggregate result;
        if (entries.isEmpty())
            return result;

        double total = 0.0;
        const double first = getter(entries.first());

        for (const MaterialEntry& entry : entries) {
            const double value = getter(entry);
            total += value;
            if (std::abs(value - first) > 1e-6)
                result.mixed = true;
        }

        result.value = total / entries.size();
        return result;
    }
    struct Data {
        QPointer<MaterialTree> tree;
        QList<MaterialEntry> materials;
        QHash<QString, FloatControl*> floats;
        bool initialized = false;
    };

    Data d;
};

void
MaterialTreePrivate::ensureInitialized()
{
    if (d.initialized)
        return;

    init();
    d.initialized = true;
}

QColor
MaterialTreePrivate::color(const QString& prefix) const
{
    const FloatControl* r = d.floats.value(prefix + "R");
    const FloatControl* g = d.floats.value(prefix + "G");
    const FloatControl* b = d.floats.value(prefix + "B");

    if (!r || !g || !b)
        return QColor();

    return QColor::fromRgbF(r->value(), g->value(), b->value());
}

void
MaterialTreePrivate::init()
{
    d.tree->setColumnCount(2);
    d.tree->setHeaderLabels({ "Name", "Value" });
    d.tree->header()->resizeSection(0, 160);
    d.tree->setRootIsDecorated(true);
    d.tree->setSelectionMode(QAbstractItemView::NoSelection);
    d.tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto addGroup = [this](const QString& name) {
        auto* item = new QTreeWidgetItem(d.tree.data());
        item->setText(0, name);
        item->setFirstColumnSpanned(true);
        item->setExpanded(true);

        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        return item;
    };

    auto addFloat = [this](QTreeWidgetItem* parent, const QString& key, const QString& label, double minimum,
                           double maximum, double step) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, label);

        auto* control = new FloatControl(minimum, maximum, step, d.tree.data());
        control->previewChanged = [this, key](double value) { Q_EMIT d.tree->floatPreviewChanged(key, value); };
        control->changed = [this, key](double value) { Q_EMIT d.tree->floatChanged(key, value); };

        d.tree->setItemWidget(item, 1, control);
        d.floats.insert(key, control);
    };

    auto addColor = [this](QTreeWidgetItem* parent, const QString& key, const QString& label) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, label);

        auto* control = new FloatControl(0.0, 1.0, 0.01, d.tree.data());
        control->previewChanged = [this, key](double) {
            const QString parameter = key.startsWith("baseColor") ? QStringLiteral("baseColor")
                                                                  : QStringLiteral("transmissionColor");
            const QColor value = color(parameter);
            if (value.isValid())
                Q_EMIT d.tree->colorPreviewChanged(parameter, value);
        };
        control->changed = [this, key](double) {
            const QString parameter = key.startsWith("baseColor") ? QStringLiteral("baseColor")
                                                                  : QStringLiteral("transmissionColor");
            const QColor value = color(parameter);
            if (value.isValid())
                Q_EMIT d.tree->colorChanged(parameter, value);
        };

        d.tree->setItemWidget(item, 1, control);
        d.floats.insert(key, control);
    };

    auto* base = addGroup("Base");
    addColor(base, "baseColorR", "Base Color R");
    addColor(base, "baseColorG", "Base Color G");
    addColor(base, "baseColorB", "Base Color B");
    addFloat(base, "metalness", "Metalness", 0.0, 1.0, 0.01);
    addFloat(base, "roughness", "Roughness", 0.0, 1.0, 0.01);
    addFloat(base, "opacity", "Opacity", 0.0, 1.0, 0.01);

    auto* specular = addGroup("Specular");
    addFloat(specular, "specular", "Specular", 0.0, 1.0, 0.01);
    addFloat(specular, "ior", "IOR", 1.0, 3.0, 0.01);

    auto* coat = addGroup("Coat");
    addFloat(coat, "coat", "Coat", 0.0, 1.0, 0.01);
    addFloat(coat, "coatRoughness", "Coat Roughness", 0.0, 1.0, 0.01);

    auto* transmission = addGroup("Transmission");
    addFloat(transmission, "transmission", "Transmission", 0.0, 1.0, 0.01);
    addColor(transmission, "transmissionColorR", "Transmission Color R");
    addColor(transmission, "transmissionColorG", "Transmission Color G");
    addColor(transmission, "transmissionColorB", "Transmission Color B");

    d.tree->expandAll();
    d.tree->setEnabled(false);
}

void
MaterialTreePrivate::update()
{
    if (d.materials.isEmpty()) {
        d.tree->setEnabled(false);
        return;
    }

    d.tree->setEnabled(true);

    auto setFloat = [this](const QString& key, auto getter) {
        const FloatAggregate aggregate = aggregateFloat(d.materials, getter);
        if (FloatControl* control = d.floats.value(key))
            control->setValue(aggregate.value, aggregate.mixed);
    };

    setFloat("baseColorR", [](const MaterialEntry& e) { return e.parameters.baseColor[0]; });
    setFloat("baseColorG", [](const MaterialEntry& e) { return e.parameters.baseColor[1]; });
    setFloat("baseColorB", [](const MaterialEntry& e) { return e.parameters.baseColor[2]; });
    setFloat("metalness", [](const MaterialEntry& e) { return e.parameters.metalness; });
    setFloat("roughness", [](const MaterialEntry& e) { return e.parameters.roughness; });
    setFloat("opacity", [](const MaterialEntry& e) { return e.parameters.opacity; });

    setFloat("specular", [](const MaterialEntry& e) { return e.parameters.specular; });
    setFloat("ior", [](const MaterialEntry& e) { return e.parameters.ior; });

    setFloat("coat", [](const MaterialEntry& e) { return e.parameters.coat; });
    setFloat("coatRoughness", [](const MaterialEntry& e) { return e.parameters.coatRoughness; });

    setFloat("transmission", [](const MaterialEntry& e) { return e.parameters.transmission; });
    setFloat("transmissionColorR", [](const MaterialEntry& e) { return e.parameters.transmissionColor[0]; });
    setFloat("transmissionColorG", [](const MaterialEntry& e) { return e.parameters.transmissionColor[1]; });
    setFloat("transmissionColorB", [](const MaterialEntry& e) { return e.parameters.transmissionColor[2]; });

    for (auto it = d.floats.begin(); it != d.floats.end(); ++it) {
        QString parameter = it.key();
        if (parameter.startsWith("baseColor"))
            parameter = "baseColor";
        else if (parameter.startsWith("transmissionColor"))
            parameter = "transmissionColor";

        int supported = 0;
        for (const MaterialEntry& entry : d.materials) {
            if (MaterialUtils::isSupportedParameter(entry, parameter))
                ++supported;
        }

        it.value()->setEnabled(supported > 0);
        it.value()->setToolTip(
            supported > 0 && supported < d.materials.size()
                ? QString("Available on %1 of %2 selected materials").arg(supported).arg(d.materials.size())
                : QString());
    }
}

MaterialTree::MaterialTree(QWidget* parent)
    : TreeWidget(parent)
    , p(new MaterialTreePrivate())
{
    p->d.tree = this;
}

MaterialTree::~MaterialTree() = default;

void
MaterialTree::setMaterials(const QList<MaterialEntry>& materials)
{
    p->ensureInitialized();
    p->d.materials = materials;
    p->update();
}

void
MaterialTree::clearMaterials()
{
    p->ensureInitialized();
    p->d.materials.clear();
    p->update();
}

}  // namespace stageviz

#pragma once

#include <QLabel>

#include "models/adminmodels.h"

class StatusTag : public QLabel
{
    Q_OBJECT

public:
    explicit StatusTag(ev::PileStatus state, QWidget *parent = nullptr);
    void setState(ev::PileStatus state);
};

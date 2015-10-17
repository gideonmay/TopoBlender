#include <cmath>
#include "Explore.h"
#include "ui_Explore.h"

#include "Document.h"
#include "GraphicsScene.h"
#include "GraphicsView.h"

#include "Thumbnail.h"
#include "ExploreLiveView.h"

#include "ExploreProcess.h"

#include <QGraphicsDropShadowEffect>

Explore::Explore(Document *document, const QRectF &bounds) : Tool(document), liveView(nullptr)
{
    // Enable keyboard
    this->setFlags(QGraphicsItem::ItemIsFocusable);

    setBounds(bounds);
    setObjectName("explore");
}

void Explore::init()
{
    //setProperty("hasBackground", true);
    //setProperty("backgroundColor", QColor(255,255,255,255));

    // Add widget
    auto toolsWidgetContainer = new QWidget();
    widget = new Ui::ExploreWidget();
    widget->setupUi(toolsWidgetContainer);
    widgetProxy = new QGraphicsProxyWidget(this);
    widgetProxy->setWidget(toolsWidgetContainer);

    // Place at bottom left corner
    auto delta = widgetProxy->sceneBoundingRect().bottomLeft() -
    scene()->views().front()->rect().bottomLeft();
    widgetProxy->moveBy(-delta.x(), -delta.y());

    // Fill categories box
    {
        for(auto cat : document->categories.keys()){
            widget->categoriesBox->insertItem(widget->categoriesBox->count(), cat);
        }

        int idx = widget->categoriesBox->findText(document->categoryOf(document->firstModelName()));
        widget->categoriesBox->setCurrentIndex(idx);
    }

    // Connect UI with actions
    {
        connect(widget->categoriesBox, &QComboBox::currentTextChanged, [&](QString text){
            document->currentCategory = text;
        });

        connect(widget->analyzeButton, &QPushButton::pressed, [&](){
            document->computePairwise(widget->categoriesBox->currentText());
        });

        connect(document, &Document::categoryPairwiseDone, [=](){

            // Clean up earlier results
            for(auto i : this->childItems())
            {
                if(i != widgetProxy) scene()->removeItem(i);
                thumbs.clear();
                lines.clear();
            }

            auto catModels = document->categories[ document->currentCategory ].toStringList();

            // Build distance matrix
            for(int i = 0; i < catModels.size(); i++)
            {
                for(int j = i+1; j < catModels.size(); j++)
                {
                    auto s = catModels[i], t = catModels[j];

                    double dist = std::max(document->datasetMatching[s][t]["min_cost"].toDouble(),
                                           document->datasetMatching[t][s]["min_cost"].toDouble());

                    distMat[i][j] = distMat[j][i] = dist;
                }
            }

            // MDS
            auto allPoints = ExploreProcess::embed(distMat, widget->vizOption->currentIndex());
            auto allPointsRect = allPoints.boundingRect();

            // Rescale and position points
            double dx = bounds.width() * 0.2;
            double dy = bounds.height() * 0.2;
            QRectF r(0, 0, bounds.width() - dx, bounds.height() - dy);
            QPolygonF positions;
			for (int i = 0; i < catModels.size(); i++)
            {
                double x = allPoints[i].x(), y = allPoints[i].y();

                auto posRelative = QPointF(
                            (x - allPointsRect.left()) / allPointsRect.width(),
                            (y - allPointsRect.top()) / allPointsRect.height());

                auto pos = QPointF((posRelative.x() * r.width()) + (dx / 2.0),
                                   (posRelative.y() * r.height()) + (dy / 2.0));

                positions << pos;
			}

            // Build voronoi
            auto graph = ExploreProcess::buildGraph(positions, bounds.width(), bounds.height());

            // Create thumbnails
			for (int i = 0; i < catModels.size(); i++)
            {
                QPointF pos = graph.cells[i].pos;

                auto s = catModels[i];

                thumbs[s] = ExploreProcess::makeThumbnail(this, document, s, pos, widget->hqRendering->isChecked());

                thumbs[s]->setProperty("isIgnoreMouse", true);
            }

            // Distance matrix ranges
            max_val = 0;
            min_val = 1;

            QVector<double> maximumColumn;
            maximumColumn.fill(-1, catModels.size());
            for(int i = 0; i < catModels.size(); i++){
                for(int j = 0; j < catModels.size(); j++){
                    auto d = distMat[i][j];

                    maximumColumn[i] = std::max(maximumColumn[i], d);

                    // Non-zero
                    if(d != 0){
                        min_val = std::min(min_val, d);
                        max_val = std::max(max_val, d);
                    }
                }
            }

            // Visualize Edges
            for(int i = 0; i < catModels.size(); i++)
            {
                double bestCost = maximumColumn[i];

                for(int j = i+1; j < catModels.size(); j++)
                {
                    auto s = catModels[i], t = catModels[j];

                    auto t1 = thumbs[s], t2 = thumbs[t];

                    QLineF linef(this->mapFromItem(t1, t1->boundingRect().center()),
                                 this->mapFromItem(t2, t2->boundingRect().center()));

                    double similarity = 1.0 - ((distMat[i][j] - min_val) / (max_val - min_val));

					similarity = pow(similarity, 3);

					// Check if edge is in triangulation
					if (similarity >= 0.25)
                    {
                        for (auto edge : graph.cells[i].edges)
                        {
                            if (edge.first < 0 || edge.second < 0) continue;
                            bool test1 = edge.first == i && edge.second == j;
                            bool test2 = edge.second == i && edge.first == j;
                            bool test3 = bestCost == distMat[i][j];

							if (test1 || test2 || test3)
                            {
                                //QColor color = ExploreProcess::qtJetColor(similarity);
                                //QColor color = QColor::fromHsl(0, 128, 255 * similarity, 255);
                                QColor color = QColor::fromHsl(0, 0, 255 * similarity, 255);
                                color.setAlphaF(0.9 * similarity);

                                auto line = scene()->addLine(linef, QPen(color, 1 + (similarity * 2)));

								line->setParentItem(this);
								line->setFlag(QGraphicsItem::ItemNegativeZStacksBehindParent);
								line->setZValue(-1);

                                lines[qMakePair(i,j)] = line;
							}
						}
                    }
                }
            }

            // Live synthesis
            liveView = new ExploreLiveView(this, document);
            //auto ritem = new QGraphicsRectItem(QRectF(-5,-5,10,10), liveView);
            //ritem->setPen(QPen(Qt::white, 3));
        });
    }
}

void Explore::mouseMoveEvent(QGraphicsSceneMouseEvent * event)
{
    if(liveView == nullptr || lines.empty()) return;
    if(event->buttons().testFlag(Qt::RightButton)) return;

    auto pointLineDist = [&](QPointF p, QLineF l, QPointF & projection){
        QVector2D v (l.p2() - l.p1());
        QVector2D w (p - l.p1());
        auto c1 = QVector2D::dotProduct(w,v);
        if ( c1 <= 0 ) {projection = l.p1(); return QVector2D(p - l.p1()).length();}
        auto c2 = QVector2D::dotProduct(v,v);
        if ( c2 <= c1 ) {projection = l.p2(); return QVector2D(p - l.p2()).length();}
        auto b = c1 / c2;
        QVector2D Pb (QVector2D(l.p1()) + b * v);
        projection = Pb.toPointF();
        return (QVector2D(p) - Pb).length();
    };

    QMap< double, QPair<int,int> > dists;
    for(auto key : lines.keys())
    {
        auto l = lines[key];
        auto line = l->line();

        QPointF proj;
        double dist = pointLineDist(event->pos(), line, proj);

        dists[dist] = key;

        auto pen = l->pen();
        auto color = pen.color();
        color.setAlphaF(0.25);
        pen.setColor(color);
        pen.setWidth(1);
        l->setPen(pen);
    }

    // Blend details
    QVariantMap info;

    // Find projection online
    {
        // Closest line
        auto key = dists.keys().front();
        auto best = dists[key];
        auto bestLine = lines[best];

        // Change visualization
        auto pen = bestLine->pen();
        auto color = pen.color();
        color.setAlphaF(1);
        pen.setColor(color);
        pen.setWidth(4);
        bestLine->setPen(pen);

        // Project on best line
        QPointF proj;
        pointLineDist(event->pos(),bestLine->line(),proj);
        liveView->setPos(proj);

        auto catModels = document->categories[ document->currentCategory ].toStringList();

        startShape = catModels[best.first];
        targetShape = catModels[best.second];

        alpha = std::min(1.0, std::max(0.0, (bestLine->line().p1() - proj).manhattanLength() / bestLine->line().length()));
        //((GraphicsScene*)scene())->displayMessage(QString("%1").arg(alpha));
    }

    // Do blend
    info["source"].setValue(startShape);
    info["target"].setValue(targetShape);
    info["alpha"].setValue(alpha);
    info["hqRendering"].setValue(widget->hqRendering->isChecked());
    liveView->showBlend( info );
}

void Explore::mousePressEvent(QGraphicsSceneMouseEvent *  event)
{
    if(liveView == nullptr || thumbs.empty()) return;

    auto closestThumbnail = [&](){
        double min_dist = bounds.height() * bounds.width();
        Thumbnail * best = nullptr;
        for(auto t : thumbs){
            QPointF c = this->mapFromItem(t, t->boundingRect().center());
            double dist = (event->pos() - c).manhattanLength();
            if(dist < min_dist){
                min_dist = dist;
                best = t;
            }
        }
        return best;
    };

    if(event->button() == Qt::RightButton)
    {
        startShape = closestThumbnail()->data["shape"].toString();
    }
}

void Explore::mouseReleaseEvent(QGraphicsSceneMouseEvent *  event)
{
    if(liveView == nullptr || thumbs.empty()) return;

    if(!(event->button() == Qt::RightButton)) return;

    auto closestThumbnail = [&](){
        double min_dist = bounds.height() * bounds.width();
        Thumbnail * best = nullptr;
        for(auto t : thumbs){
            QPointF c = this->mapFromItem(t, t->boundingRect().center());
            double dist = (event->pos() - c).manhattanLength();
            if(dist < min_dist){
                min_dist = dist;
                best = t;
            }
        }
        return best;
    };

    QString target = closestThumbnail()->data["shape"].toString();
    if(startShape == target) return;

    // Add a new link
    {
        auto catModels = document->categories[ document->currentCategory ].toStringList();

        int i = catModels.indexOf(startShape);
        int j = catModels.indexOf(target);

        auto s = catModels[i], t = catModels[j];

        auto t1 = thumbs[s], t2 = thumbs[t];

        QLineF linef(this->mapFromItem(t1, t1->boundingRect().center()),
                     this->mapFromItem(t2, t2->boundingRect().center()));

        double similarity = 1.0 - ((distMat[i][j] - min_val) / (max_val - min_val));
        similarity = pow(similarity, 3);

        QColor color = ExploreProcess::qtJetColor(similarity);
        auto line = scene()->addLine(linef, QPen(color, 2));

        line->setParentItem(this);
        line->setFlag(QGraphicsItem::ItemNegativeZStacksBehindParent);
        line->setZValue(-1);

        lines[qMakePair(i,j)] = line;
    }
}

void Explore::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == Qt::Key_F)
    {
        QStringList svg;

        int width = this->bounds.width();
        int height = this->bounds.height();

        auto svgHeader = QString("<svg version='1.1' width='%1' height='%2'"
                                 " xmlns='http://www.w3.org/2000/svg'"
                                 " xmlns:xlink='http://www.w3.org/1999/xlink'>").arg(width).arg(height);
        auto svgFooter = QString("</svg>");

        svg << svgHeader;

        // Build SVG
        {
            // Solid background
            {
                svg << QString("\t<rect width=\"100%\" height=\"100%\" fill=\"%1\" />")
                       .arg(QColor(50,50,50).name());
            }

            // output edges
            for(auto l : lines)
            {
                auto line = l->line();

                QString colorName = QString("rgba(%1,%2,%3,%4)")
                        .arg(l->pen().color().red())
                        .arg(l->pen().color().green())
                        .arg(l->pen().color().blue())
                        .arg(l->pen().color().alphaF());

                svg << QString("\t<line x1='%1' y1='%2' x2='%3' y2='%4' stroke='%5' stroke-width='2'/>")
                        .arg(line.p1().x()).arg(line.p1().y())
                        .arg(line.p2().x()).arg(line.p2().y())
                        .arg(colorName);
            }

            // output thumbnails
            int width = 0;
            for(auto t : thumbs)
            {
                QGraphicsDropShadowEffect *e = new QGraphicsDropShadowEffect;
                e->setColor(QColor(40,40,40,180));
                e->setOffset(0,10);
                e->setBlurRadius(40);

                auto img = t->applyEffectToImage(e);

                auto filename = "images/" + t->data["shape"].toString() + ".png";
                img.save(filename);

                auto x = t->pos().x(), y = t->pos().y();
                width = t->boundingRect().width() * t->scale(),
                        height = t->boundingRect().height() * t->scale();

                svg << QString("\t<image xlink:href='%5' x='%1' y='%2' height='%3px' width='%4px'/>")
                       .arg(x).arg(y)
                       .arg(width).arg(height)
                       .arg(filename);
            }

            // output live preview if ready
            if(liveView && liveView->isReady)
            {
                auto filename = "images/liveView.png";

                int dpm = 300 / 0.0254; // ~300 DPI
                liveView->cachedImage.setDotsPerMeterX(dpm);
                liveView->cachedImage.setDotsPerMeterY(dpm);
                liveView->cachedImage.save(filename);

                auto x = liveView->pos().x(), y = liveView->pos().y();

                svg << QString("\t<radialGradient id='gradient'>"
                        "<stop offset='0%'   style='stop-color: rgba(0,0,0,0.75)' />"
                        "<stop offset='75%'   style='stop-color: rgba(0,0,0,0.75)' />"
                        "<stop offset='100%' style='stop-color: transparent' />"
                        "</radialGradient>"
                        "<circle cx='%1' cy='%2' r='%3' style='fill:url(#gradient)' />"
                        "<circle cx='%1' cy='%2' r='%3' style='fill:transparent' stroke='rgba(255,255,255,0.25)' stroke-width='1' />")
                        .arg(x).arg(y).arg(150 * 0.5);

                svg << QString("\t<image xlink:href='%5' x='%1' y='%2' height='%3px' width='%4px'/>")
                       .arg(x - (150 * 0.5)).arg(y - (150 * 0.5))
                       .arg(150).arg(150)
                       .arg(filename);
            }
        }

        svg << svgFooter;

        // Save snapshot as SVG
        QFile file("snapshot.svg");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&file);
        out << svg.join("\n");
    }
}
